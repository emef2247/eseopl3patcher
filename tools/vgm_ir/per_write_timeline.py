#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
per_write_timeline.py

- Per-write timeline の生成支援（vgm2ir から呼び出される想定のヘルパ）
- YM2413 の A0/B0/C0 を #type=fL/fHBK/iv に分類し、durations 抽出器が期待する列を整える
- すでに出力済みの timeline CSV（*_timeline_YM2413.csv）を後から注釈（annotate）して #type 列を付けるCLIも提供

注意:
- YM2413 (OPLL) の 0x10..0x18 / 0x20..0x28 / 0x30..0x38 をそれぞれ fL/fHBK/iv として扱います
- fHBK のフィールド割り当ては以下の前提に基づきます（OPLL互換）
  - fnumH = bit0
  - blk   = bits[3:1]  (3 bits)
  - ko    = bit4       (Key-On)
- fnum は (fnumH<<8)|fnumL として計算します（fnumL は直近の 0x10+n 書き込み値を保持して使用）

用途:
1) ライブラリとして: write_split_csv(...) を vgm2ir 側から呼んで per-write CSV を吐く
2) CLI として: 既存の *_timeline_YM2413.csv を読み、#type 等を付与して書き直す (--annotate-csv)

"""

from __future__ import annotations
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Tuple, Any
import csv
from pathlib import Path

# 注釈CLIでのみ pandas を使用
try:
    import pandas as pd  # type: ignore
except Exception:
    pd = None  # pandas 未導入環境でもライブラリ関数は利用可（CLI annotate は不可）


# ---- YM2413 (OPLL) デコード ---------------------------------------------------

@dataclass
class OPLLChanState:
    fnumL: Optional[int] = None  # 8-bit
    fnumH: Optional[int] = None  # 1-bit (assumption)
    blk: Optional[int] = None
    ko: Optional[int] = None
    inst: Optional[int] = None
    vol: Optional[int] = None


@dataclass
class OPLLState:
    chans: Dict[int, OPLLChanState] = field(default_factory=lambda: {ch: OPLLChanState() for ch in range(9)})

    def update_fL(self, ch: int, data: int) -> Dict[str, Any]:
        self.chans[ch].fnumL = data & 0xFF
        return {
            "#type": "fL",
            "ch": ch,
            "fnumL": self.chans[ch].fnumL,
        }

    def update_fHBK(self, ch: int, data: int) -> Dict[str, Any]:
        # YM2413 assumption: fnumH=bit0, blk=bits[3:1], ko=bit4
        fnumH = (data & 0x01)
        blk = (data >> 1) & 0x07
        ko = (data >> 4) & 0x01

        st = self.chans[ch]
        st.fnumH = fnumH
        st.blk = blk
        st.ko = ko

        fnumL = st.fnumL if st.fnumL is not None else None
        fnum = ((fnumH << 8) | fnumL) if (fnumL is not None) else None

        return {
            "#type": "fHBK",
            "ch": ch,
            "ko": ko,
            "blk": blk,
            "fnum": fnum,
            "fnumL": fnumL,
        }

    def update_iv(self, ch: int, data: int) -> Dict[str, Any]:
        inst = (data >> 4) & 0x0F
        vol = data & 0x0F
        self.chans[ch].inst = inst
        self.chans[ch].vol = vol
        return {
            "#type": "iv",
            "ch": ch,
            "inst": inst,
            "vol": vol,
        }


# ---- タイムライン分類 ---------------------------------------------------------

@dataclass
class WriteEvent:
    time_s: float
    chip: str
    addr: int
    data: int


def classify_write(evt: WriteEvent, opll: OPLLState) -> Optional[Dict[str, Any]]:
    """
    1イベントを YM2413 なら fL/fHBK/iv のどれかに分類し、付加情報を返す。
    対象外なら None を返す。
    """
    if evt.chip.upper() != "YM2413":
        return None

    a = evt.addr & 0xFF
    if 0x10 <= a <= 0x18:
        ch = a - 0x10
        row = opll.update_fL(ch, evt.data)
    elif 0x20 <= a <= 0x28:
        ch = a - 0x20
        row = opll.update_fHBK(ch, evt.data)
    elif 0x30 <= a <= 0x38:
        ch = a - 0x30
        row = opll.update_iv(ch, evt.data)
    else:
        return None

    row.update({
        "time_s": evt.time_s,
        "addr": a,
        "data": evt.data,
        "chip": "YM2413",
    })
    return row


# ---- 出力（per-write CSV） ----------------------------------------------------

def write_split_csv(events: Iterable[WriteEvent], out_dir: Path, base_prefix: Optional[str] = None) -> Dict[str, Path]:
    """
    Per-chip CSV を生成する。YM2413 については #type を含む列を出力する。
    返り値: chip名 -> 出力パス
    base_prefix が与えられた場合、ファイル名に <base_prefix>_ を付与する。
    """
    out_dir.mkdir(parents=True, exist_ok=True)

    def out_path_for(chip: str) -> Path:
        if base_prefix:
            return out_dir / f"{base_prefix}_timeline_{chip}.csv"
        else:
            return out_dir / f"timeline_{chip}.csv"

    ym2413_path = out_path_for("YM2413")

    with ym2413_path.open("w", newline="", encoding="utf-8") as ym2413_f:
        ym2413_w = csv.DictWriter(
            ym2413_f,
            fieldnames=[
                "time_s", "chip", "addr", "data",
                "#type", "ch",
                "ko", "blk", "fnum", "fnumL",
                "inst", "vol",
            ],
        )
        ym2413_w.writeheader()

        opll = OPLLState()
        for ev in events:
            if ev.chip.upper() == "YM2413":
                row = classify_write(ev, opll)
                if row is not None:
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
                    ym2413_w.writerow(out)

    return {"YM2413": ym2413_path}


# ---- 既存 timeline CSV への注釈（#type 付与）CLI -----------------------------

def annotate_csv(input_csv: Path, output_csv: Path, chip: str = "YM2413") -> None:
    """
    既存の *_timeline_*.csv を読み、対象チップ（YM2413）について #type 等の列を付与して出力する。
    入力CSVに最低限必要な列: time_s, addr, data
    任意列: chip（無い場合は引数 chip を採用）
    """
    if pd is None:
        raise RuntimeError("pandas is required for --annotate-csv. Please `pip install pandas`.")

    df = pd.read_csv(input_csv)

    if "time_s" not in df.columns or "addr" not in df.columns or "data" not in df.columns:
        raise SystemExit('input CSV must have columns: time_s, addr, data')

    if "chip" not in df.columns:
        df["chip"] = chip
    else:
        df["chip"] = df["chip"].fillna(chip)

    df_out_rows: List[Dict[str, Any]] = []
    opll = OPLLState()

    for _, r in df.iterrows():
        evt = WriteEvent(
            time_s=float(r["time_s"]),
            chip=str(r["chip"]),
            addr=int(r["addr"]) & 0xFF,
            data=int(r["data"]) & 0xFF,
        )
        row = classify_write(evt, opll)
        if row is not None:
            df_out_rows.append(row)

    cols = ["time_s", "chip", "addr", "data", "#type", "ch", "ko", "blk", "fnum", "fnumL", "inst", "vol"]
    if not df_out_rows:
        # YM2413 の対象行が無い場合でも schema を持った空CSVを書いておく
        pd.DataFrame(columns=cols).to_csv(output_csv, index=False)
        return

    out_df = pd.DataFrame(df_out_rows)
    for c in cols:
        if c not in out_df.columns:
            out_df[c] = None
    out_df = out_df[cols].sort_values(["time_s", "ch", "#type"], kind="stable")
    out_df.to_csv(output_csv, index=False)


# ---- 後方互換のための alias ---------------------------------------------------
# 既存コードが import する名前（write_timeline_per_write_csv）を残す
def write_timeline_per_write_csv(events: Iterable[WriteEvent], out_dir: Path, base_prefix: Optional[str] = None) -> Dict[str, Path]:
    return write_split_csv(events, out_dir, base_prefix=base_prefix)


# ---- CLI ---------------------------------------------------------------------

def _build_argparser():
    import argparse
    ap = argparse.ArgumentParser(description="Per-write timeline utilities (YM2413 typed timeline generator)")
    sub = ap.add_subparsers(dest="cmd")

    spa = sub.add_parser("annotate", help="Annotate existing per-write timeline CSV to add #type for YM2413")
    spa.add_argument("--input", required=True, help="Input timeline CSV (e.g., *_timeline_YM2413.csv)")
    spa.add_argument("--output", required=True, help="Output typed CSV")
    spa.add_argument("--chip", default="YM2413", help="Chip name for annotation (default: YM2413)")

    return ap


def main():
    ap = _build_argparser()
    args = ap.parse_args()

    if args.cmd == "annotate":
        annotate_csv(Path(args.input), Path(args.output), chip=args.chip)
    else:
        ap.print_help()


if __name__ == "__main__":
    main()