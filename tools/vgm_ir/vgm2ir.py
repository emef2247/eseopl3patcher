#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
vgm2ir.py

VGM を最小限にパースして YM2413 関連の出力を行います:
- --timeline-per-write-split: YM2413 の per-write タイムライン CSV（#type 付き）
- --opll-log: YM2413 ログ CSV（durations 抽出器がそのまま読める #type 付き）

拡張:
- Konami SCC/SCC+ (0xD2) を実直に取り込み、<stem>_timeline_SCC.csv に生ログとして出力します
  (b0,b1,b2 と addr16=(b0<<8)|b1, data8=b2 を併記)

安全性重視:
- YM2413 以外のコマンドは、長さが確実にわかるもののみ厳密にスキップ
- 未知コマンド遭遇時はデフォルトで停止（--lenient でのみベストエフォート・スキップ）
"""

from __future__ import annotations
import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import List, Dict, Iterable, Optional, Tuple
import struct
import csv
import sys
import binascii

from .per_write_timeline import (
    WriteEvent,
    write_split_csv,
    write_timeline_per_write_csv,  # 後方互換 alias
    classify_write,
    OPLLState,
)

# ---- VGM パーサ（必要最小限 + 安全スキップ） ----------------------------------

@dataclass
class VGMHeader:
    data_offset: int
    rate: int
    version: int


def _read_u32le(b: bytes, off: int) -> int:
    return struct.unpack_from("<I", b, off)[0]


def _hexdump_slice(b: bytes, off: int, *, radius: int = 32) -> str:
    start = max(0, off - radius)
    end = min(len(b), off + radius)
    slice_bytes = b[start:end]
    hexstr = binascii.hexlify(slice_bytes).decode("ascii")
    spaced = " ".join(hexstr[i:i+2] for i in range(0, len(hexstr), 2))
    marker = " " * ((off - start) * 3) + "^"
    return f"offset 0x{off:X} in slice [0x{start:X}:0x{end:X}):\n{spaced}\n{marker}"


def parse_vgm(path: Path, *, lenient: bool = False, debug_unknown: bool = False) -> Tuple[VGMHeader, List[WriteEvent], List[Tuple[float, int, int, int]]]:
    """
    戻り値: (header, ym2413_events, scc_events)
      - ym2413_events: WriteEvent(time_s, "YM2413", addr, data)
      - scc_events: (time_s, b0, b1, b2)  # 0xD2 の3バイトペイロード（生）
    """
    data = path.read_bytes()
    if len(data) < 0x40 or data[0:4] != b'Vgm ':
        raise SystemExit(f"Not a valid VGM file: {path}")

    # version at 0x08 (e.g., 0x00000171 for v1.71)
    version = _read_u32le(data, 0x08) if len(data) >= 0x0C else 0

    # data offset: at 0x34, relative to 0x34. If 0, default is 0x40.
    rel = _read_u32le(data, 0x34) if len(data) >= 0x38 else 0
    data_offset = (0x34 + rel) if rel != 0 else 0x40

    # sample rate: at 0x24 (0 means 44100 by spec)
    rate = _read_u32le(data, 0x24) if len(data) >= 0x28 else 44100
    if rate == 0:
        rate = 44100

    cur = data_offset
    samples = 0
    ym_events: List[WriteEvent] = []
    scc_events: List[Tuple[float, int, int, int]] = []  # (time_s, b0, b1, b2)

    def commit_wait(n: int):
        nonlocal samples
        samples += n

    def at_end(n: int = 0) -> bool:
        return cur + n > len(data)

    def read_u8() -> int:
        nonlocal cur
        if at_end(1):
            raise SystemExit("Unexpected EOF while reading VGM")
        v = data[cur]
        cur += 1
        return v

    def skip(n: int):
        nonlocal cur
        if at_end(n):
            raise SystemExit("Unexpected EOF while skipping VGM data")
        cur += n

    def warn(msg: str):
        print(f"[vgm2ir][warn] {msg}", file=sys.stderr)

    # 固定長コマンド（コマンド自身の1バイトを除くペイロード長）
    fixed_len = {
        0x4F: 1,  # GG stereo
        0x50: 1,  # SN76489 write

        # YM family and others (addr/data 2B)
        0x52: 2, 0x53: 2,  # YM2612
        0x54: 2,          # YM2151
        0x55: 2, 0x56: 2, 0x57: 2, 0x58: 2, 0x59: 2,
        0x5A: 2, 0x5B: 2, 0x5C: 2, 0x5D: 2, 0x5E: 2, 0x5F: 2,

        0xA0: 2, 0xA1: 2, 0xA2: 2,  # AY/YM2149 family

        # Data stream / 拡張系（ここでは 0xD2 は個別に処理するので固定長テーブルには載せない）
        0xD0: 3, 0xD1: 3,             # 参考まで（必要なら ingest を追加）

        # その他（別処理だが長さは固定）
        0xE0: 4,  # PCM seek
    }

    # parse stream
    nbytes = len(data)
    while cur < nbytes:
        cmd_off = cur
        cmd = read_u8()

        # End of sound data
        if cmd == 0x66:
            break

        # YM2413 write: 0x51 aa dd
        elif cmd == 0x51:
            if at_end(2):
                break
            addr = read_u8()
            val = read_u8()
            t = samples / rate
            ym_events.append(WriteEvent(time_s=t, chip="YM2413", addr=addr, data=val))

        # Konami SCC/SCC+: 0xD2 b0 b1 b2 （生取り込み）
        elif cmd == 0xD2:
            if at_end(3):
                break
            b0 = read_u8()
            b1 = read_u8()
            b2 = read_u8()
            t = samples / rate
            scc_events.append((t, b0, b1, b2))

        # Wait n samples: 0x61 nn nn (LE)
        elif cmd == 0x61:
            if at_end(2):
                break
            n = read_u8() | (read_u8() << 8)
            commit_wait(n)

        # Wait 735 samples (60 Hz)
        elif cmd == 0x62:
            commit_wait(735)

        # Wait 882 samples (50 Hz)
        elif cmd == 0x63:
            commit_wait(882)

        # Short waits 0x70..0x7F (n-0x70+1 samples)
        elif 0x70 <= cmd <= 0x7F:
            n = (cmd & 0x0F) + 1
            commit_wait(n)

        # Data block: 0x67 0x66 tt ll ll ll ll [data...]
        elif cmd == 0x67:
            if at_end(2):
                break
            marker = read_u8()
            if marker != 0x66:
                raise SystemExit(f"Malformed data block (0x67 without 0x66 marker) at 0x{cmd_off:X}")
            if at_end(1 + 4):
                break
            _type = read_u8()
            length = _read_u32le(data, cur)
            skip(4)
            if at_end(length):
                raise SystemExit(f"Unexpected EOF inside data block at 0x{cmd_off:X}")
            skip(length)

        # PCM seek (rare): 0xE0 aa aa aa aa
        elif cmd == 0xE0:
            if at_end(4):
                break
            skip(4)

        # Known fixed-size skip commands
        elif cmd in fixed_len:
            skip(fixed_len[cmd])

        else:
            # 未知コマンド: デフォルトは停止
            msg = f"Unsupported VGM command 0x{cmd:02X} at offset 0x{cmd_off:X}"
            if debug_unknown:
                dump = _hexdump_slice(data, cmd_off)
                msg += "\n" + dump
            if not lenient:
                raise SystemExit(msg + " (use --lenient to try skipping)")
            # 寛容モード: 暫定で 2 バイトをスキップ
            warn(msg + " — lenient: skipping 2 bytes (best-effort)")
            if at_end(2):
                break
            skip(2)

    header = VGMHeader(data_offset=data_offset, rate=rate, version=version)
    return header, ym_events, scc_events


# ---- 出力ユーティリティ -------------------------------------------------------

def write_opll_log(events: Iterable[WriteEvent], out_csv: Path) -> None:
    """
    YM2413 の書き込みを #type 付きでログ出力（durations 抽出器互換）
    """
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    cols = ["time_s", "chip", "addr", "data", "#type", "ch", "ko", "blk", "fnum", "fnumL", "inst", "vol"]

    with out_csv.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()

        opll = OPLLState()
        for ev in events:
            if ev.chip.upper() != "YM2413":
                continue
            row = classify_write(ev, opll)
            if row is None:
                continue
            out = {
                "time_s": row.get("time_s"),
                "chip": row.get("chip", "YM2413"),
                "addr": row.get("addr"),
                "data": row.get("data"),
                "#type": row.get("#type"),
                "ch": row.get("ch"),
                "ko": row.get("ko"),
                "blk": row.get("blk"),
                "fnum": row.get("fnum"),
                "fnumL": row.get("fnumL"),
                "inst": row.get("inst"),
                "vol": row.get("vol"),
            }
            w.writerow(out)


def write_scc_timeline(scc_events: List[Tuple[float, int, int, int]], out_csv: Path) -> None:
    """
    Konami SCC/SCC+ (0xD2) の生ログを CSV 出力する。
    列: time_s, chip, b0, b1, b2, addr16, data8
      - 慣例的な見方: b0=port/addr_hi, b1=addr_lo, b2=data
    """
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    cols = ["time_s", "chip", "b0", "b1", "b2", "addr16", "data8"]
    with out_csv.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for (t, b0, b1, b2) in scc_events:
            addr16 = ((b0 & 0xFF) << 8) | (b1 & 0xFF)
            w.writerow({
                "time_s": t,
                "chip": "SCC",
                "b0": b0,
                "b1": b1,
                "b2": b2,
                "addr16": addr16,
                "data8": b2,
            })


# ---- CLI ---------------------------------------------------------------------

def build_argparser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description="VGM -> IR helpers (subset). Focused on YM2413 per-write outputs.")
    ap.add_argument("input_vgm", help="Input .vgm file path")
    ap.add_argument("--out", default=".", help="Output directory")
    ap.add_argument("--opll-log", action="store_true", help="Emit YM2413 *_log.opll.csv (#type included)")
    ap.add_argument("--timeline-per-write-split", action="store_true", help="Emit per-write timeline CSV(s) with #type")
    ap.add_argument("--lenient", action="store_true", help="Try to skip unknown commands instead of aborting")
    ap.add_argument("--debug-unknown", action="store_true", help="Dump hexdump around unknown commands for diagnosis")
    return ap


def main():
    ap = build_argparser()
    args = ap.parse_args()

    in_path = Path(args.input_vgm)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    header, ym_events, scc_events = parse_vgm(in_path, lenient=args.lenient, debug_unknown=args.debug_unknown)
    base_prefix = in_path.stem  # 例: ym2413_legato_patch_mix

    if args.timeline_per_write_split:
        # YM2413 typed timeline
        try:
            out_map = write_split_csv(ym_events, out_dir, base_prefix=base_prefix)
        except TypeError:
            out_map = write_timeline_per_write_csv(ym_events, out_dir)
        for chip, p in out_map.items():
            print(f"[timeline] {chip}: {p}")

        # SCC raw timeline
        if scc_events:
            scc_csv = out_dir / f"{base_prefix}_timeline_SCC.csv"
            write_scc_timeline(scc_events, scc_csv)
            print(f"[timeline] SCC: {scc_csv}")

    if args.opll_log:
        log_csv = out_dir / f"{base_prefix}_log.opll.csv"
        write_opll_log(ym_events, log_csv)
        print(f"[opll-log] {log_csv}")


if __name__ == "__main__":
    main()