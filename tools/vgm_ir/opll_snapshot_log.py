from __future__ import annotations

import math
from pathlib import Path
from typing import List, Tuple

from .ir_types import IR
from .opll_helpers import (
    channel_from_reg,
    type_from_reg,
    derive_fm_params,
    derive_inst_vol,
    derive_rhythm,
)

CSV_HEADER = (
    "#type,time,ch,ticks,ko,blk,fnum,fnumL,inst,vol,rhythm_mode,rhythm_bits,reg,dd,"
    + ",".join([f"r{idx:02X}" for idx in range(0x40)])
)


def _time_from_samples(samples: int) -> float:
    return samples / 44100.0


def _ticks_from_time(time_s: float) -> int:
    t = int(math.ceil(time_s * 60.0))
    return 0 if t == 1 else t


def write_opll_snapshot_log_csv(path: Path, ir: IR) -> None:
    """
    YM2413: 1 write = 1 row, with a full 0x00..0x3F register snapshot per row.
    Also writes a companion rhythm event CSV alongside (suffix _rhythm).
    """
    path.parent.mkdir(parents=True, exist_ok=True)

    regs = [0] * 0x40  # 0x00..0x3F
    rhythm_rows: List[List[str]] = []
    prev_rhy_val = regs[0x0E]

    with path.open("w", encoding="utf-8", newline="") as f:
        f.write(CSV_HEADER + "\n")

        for e in ir.events:
            if e.chip != "YM2413" or e.kind != "reg-write":
                continue

            reg = int(e.extras.get("reg", -1))
            if not (0 <= reg < 0x40):
                continue
            dd = int(e.value or 0) & 0xFF

            regs[reg] = dd  # apply first; snapshot is "after write"

            t = _time_from_samples(e.samples)
            ticks = _ticks_from_time(t)

            ch = channel_from_reg(reg)
            ko, blk, fnum, fnumL = derive_fm_params(regs, ch)
            inst, vol = derive_inst_vol(regs, ch)
            rhy_mode, rhy_bits = derive_rhythm(regs)

            row = [
                type_from_reg(reg),
                f"{t:.16g}",
                str(ch),
                str(ticks),
                str(ko),
                str(blk),
                str(fnum),
                str(fnumL),
                str(inst),
                str(vol),
                str(rhy_mode),
                str(rhy_bits),
                f"{reg}",
                f"{dd}",
            ] + [str(regs[i]) for i in range(0x40)]

            f.write(",".join(row) + "\n")

            # Rhythm independent events from reg 0x0E changes
            if reg == 0x0E:
                cur = regs[0x0E] & 0xFF
                old = prev_rhy_val & 0xFF
                if cur != old:
                    names = ["BD", "SD", "TOM", "TC", "HH"]
                    for bit in range(5):
                        o = (old >> bit) & 1
                        n = (cur >> bit) & 1
                        if o != n:
                            rhythm_rows.append([
                                "drum",
                                f"{t:.16g}",
                                str(ticks),
                                names[bit],
                                str(n),
                                str(rhy_mode),
                                str(rhy_bits),
                                f"{reg}",
                                f"{dd}",
                            ])
                prev_rhy_val = regs[0x0E]

    rhy_path = Path(str(path).replace("_log.opll.csv", "_log.opll_rhythm.csv"))
    if rhythm_rows:
        with rhy_path.open("w", encoding="utf-8", newline="") as f:
            f.write("#type,time,ticks,drum,on,rhythm_mode,rhythm_bits,reg,dd\n")
            for r in rhythm_rows:
                f.write(",".join(r) + "\n")