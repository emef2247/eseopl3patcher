from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from .ir_types import IR, Event
from .opll_helpers import (
    channel_from_reg,
    derive_fm_params,
    derive_inst_vol,
)


TICKS_PER_SECOND = 60.0
SAMPLE_RATE_FIXED = 44100.0  # openMSX vgmrecorder と同じ規約に合わせる


def _time_from_samples(samples: int, sample_rate: float) -> float:
    # サンプル→秒。IR.header が sample_rate を持っていれば優先、なければ固定
    sr = sample_rate or SAMPLE_RATE_FIXED
    return samples / sr


def _ticks_from_time(time_s: float) -> int:
    t = int(math.ceil(time_s * TICKS_PER_SECOND))
    return 0 if t == 1 else t


@dataclass
class Gate:
    ch: int
    on_samples: int
    off_samples: int
    # 開始時のスナップショット（便宜）
    fnum0: int
    block0: int
    inst0: int
    vol0: int


@dataclass
class Slice:
    ch: int
    gate_index: int  # 0-based index in list of gates for that ch
    kind: str        # "f" | "i" | "v"
    start_samples: int
    end_samples: int
    fnum: int
    block: int
    inst: int
    vol: int


def build_opll_gates(ir: IR) -> Tuple[List[Gate], List[Slice]]:
    """
    IR(events)から YM2413 メロディ9chの KeyOn/Off Gate と、Gate中の f/i/v スライスを抽出。
    Rhythmは対象外（別CSVで記録済み）。
    """
    sample_rate = float(ir.header.get("sample_rate", SAMPLE_RATE_FIXED))

    regs = [0] * 0x40
    gates: List[Gate] = []
    slices: List[Slice] = []

    # chごとの作業状態
    ko_state = [0] * 9
    open_gate_start: Dict[int, int] = {}  # ch -> samples
    gate_count_by_ch: Dict[int, int] = {}  # ch -> n (for slice.gate_index)
    # 現在の slice（Gate中のみ運用）
    cur_slice_start: Dict[int, int] = {}
    cur_slice_kind: Dict[int, str] = {}
    cur_slice_params: Dict[int, Tuple[int, int, int, int]] = {}  # (fnum,block,inst,vol)

    def close_slice_if_any(ch: int, end_samples: int):
        if ch in cur_slice_start:
            start = cur_slice_start.pop(ch)
            kind = cur_slice_kind.pop(ch)
            fnum, block, inst, vol = cur_slice_params.pop(ch)
            gi = max(0, gate_count_by_ch.get(ch, 1) - 1)
            slices.append(Slice(ch=ch, gate_index=gi, kind=kind,
                                start_samples=start, end_samples=end_samples,
                                fnum=fnum, block=block, inst=inst, vol=vol))

    def start_slice(ch: int, kind: str, start_samples: int):
        # 現在のパラメータで開始
        ko, blk, fnum, fnumL = derive_fm_params(regs, ch)
        inst, vol = derive_inst_vol(regs, ch)
        cur_slice_start[ch] = start_samples
        cur_slice_kind[ch] = kind
        cur_slice_params[ch] = (fnum, blk, inst, vol)

    for e in ir.events:
        if e.chip != "YM2413" or e.kind != "reg-write":
            continue

        reg = int(e.extras.get("reg", -1))
        if reg < 0 or reg >= 0x40:
            continue
        dd = int(e.value or 0) & 0xFF

        # 反映（スナップショットは書込後）
        regs[reg] = dd
        ch = channel_from_reg(reg)

        # KO変化の検出（対象chのみ）
        if 0 <= ch <= 8 and 0x10 <= reg <= 0x18:
            ko, blk, fnum, _ = derive_fm_params(regs, ch)
            prev = ko_state[ch]
            if prev == 0 and ko == 1:
                # Gate start
                open_gate_start[ch] = e.samples
                gate_count_by_ch[ch] = gate_count_by_ch.get(ch, 0) + 1
                # 開始時点のパラメータ
                inst, vol = derive_inst_vol(regs, ch)
                g = Gate(ch=ch, on_samples=e.samples, off_samples=e.samples,
                         fnum0=fnum, block0=blk, inst0=inst, vol0=vol)
                gates.append(g)
                # スライス開始（最初はfとしておく）
                start_slice(ch, "f", e.samples)
            elif prev == 1 and ko == 0:
                # Gate end
                if ch in open_gate_start:
                    # ゲート終端で現在のスライスを閉じる
                    close_slice_if_any(ch, e.samples)
                    # Gate本体の off を更新
                    # 同chの直近Gateを探す（最後に追加されたはず）
                    for i in range(len(gates) - 1, -1, -1):
                        if gates[i].ch == ch and gates[i].off_samples <= gates[i].on_samples:
                            gates[i].off_samples = e.samples
                            break
                    open_gate_start.pop(ch, None)
            ko_state[ch] = ko

        # Gate中のスライス切断（f/i/v）
        if 0 <= ch <= 8 and ch in open_gate_start:
            if 0x20 <= reg <= 0x28 or 0x10 <= reg <= 0x18:
                # 周波数・ブロック変化（同一ch）
                close_slice_if_any(ch, e.samples)
                start_slice(ch, "f", e.samples)
            elif 0x30 <= reg <= 0x38:
                # INST/VOL いずれか（または両方）変化
                # 変化の種類を判定
                inst, vol = derive_inst_vol(regs, ch)
                prev_inst_vol = cur_slice_params.get(ch)
                if prev_inst_vol is not None:
                    pf, pb, pi, pv = prev_inst_vol
                    changed_i = (inst != pi)
                    changed_v = (vol != pv)
                    if changed_i or changed_v:
                        close_slice_if_any(ch, e.samples)
                        kind = "i" if changed_i and not changed_v else ("v" if changed_v and not changed_i else "iv")
                        # kind は "iv" を許容（CSVでは kind 列にそのまま出す）
                        start_slice(ch, kind, e.samples)

    # EOFで開いているものを閉じる
    last_samples = int(ir.header.get("total_samples", 0))
    for ch in list(open_gate_start.keys()):
        close_slice_if_any(ch, last_samples)
        for i in range(len(gates) - 1, -1, -1):
            if gates[i].ch == ch and gates[i].off_samples <= gates[i].on_samples:
                gates[i].off_samples = last_samples
                break
        open_gate_start.pop(ch, None)

    return gates, slices


def write_opll_gates_csv(path: Path, ir: IR, gates: List[Gate]) -> None:
    sample_rate = float(ir.header.get("sample_rate", SAMPLE_RATE_FIXED))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        f.write("#type,ch,on_tick,on_time_s,off_tick,off_time_s,dur_ticks,dur_samples,fnum0,block0,inst0,vol0,on_samples,off_samples\n")
        for g in gates:
            on_t = _time_from_samples(g.on_samples, sample_rate)
            off_t = _time_from_samples(g.off_samples, sample_rate)
            on_tick = _ticks_from_time(on_t)
            off_tick = _ticks_from_time(off_t)
            dur_samples = max(0, g.off_samples - g.on_samples)
            dur_ticks = _ticks_from_time(_time_from_samples(dur_samples, sample_rate))
            f.write(",".join([
                "gate",
                str(g.ch),
                str(on_tick),
                f"{on_t:.9f}",
                str(off_tick),
                f"{off_t:.9f}",
                str(dur_ticks),
                str(dur_samples),
                str(g.fnum0),
                str(g.block0),
                str(g.inst0),
                str(g.vol0),
                str(g.on_samples),
                str(g.off_samples),
            ]) + "\n")


def write_opll_gate_slices_csv(path: Path, ir: IR, slices: List[Slice]) -> None:
    sample_rate = float(ir.header.get("sample_rate", SAMPLE_RATE_FIXED))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        f.write("#type,ch,gate_index,start_tick,start_time_s,end_tick,end_time_s,dur_ticks,dur_samples,fnum,block,inst,vol,start_samples,end_samples\n")
        for s in slices:
            st = _time_from_samples(s.start_samples, sample_rate)
            et = _time_from_samples(s.end_samples, sample_rate)
            st_tick = _ticks_from_time(st)
            et_tick = _ticks_from_time(et)
            dur_samples = max(0, s.end_samples - s.start_samples)
            dur_ticks = _ticks_from_time(_time_from_samples(dur_samples, sample_rate))
            f.write(",".join([
                s.kind,
                str(s.ch),
                str(s.gate_index),
                str(st_tick),
                f"{st:.9f}",
                str(et_tick),
                f"{et:.9f}",
                str(dur_ticks),
                str(dur_samples),
                str(s.fnum),
                str(s.block),
                str(s.inst),
                str(s.vol),
                str(s.start_samples),
                str(s.end_samples),
            ]) + "\n")