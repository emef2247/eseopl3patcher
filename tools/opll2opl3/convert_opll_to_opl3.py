from __future__ import annotations
import argparse
import csv
import os
from typing import Dict, Any, Tuple, List

# Reuse existing loader to read YM2413 timeline
from tools.gate_estimator.loader_ir import load_from_timeline_csv
from tools.gate_estimator.model import PatchParams

def b0_value(fnum: int, blk: int, key_on: bool) -> int:
    fhi = (fnum >> 8) & 0x03
    b   = (blk & 0x07) << 2
    ko  = 0x20 if key_on else 0x00
    return (fhi | b | ko) & 0xFF

def a0_value(fnum: int) -> int:
    return fnum & 0xFF

def build_events_for_channel(notes, ch: int, pattern: str, gate: float, port: int) -> List[Dict[str, Any]]:
    evs: List[Dict[str, Any]] = []
    g = max(0.0, min(1.0, gate))
    for n in notes:
        t_on  = float(n.t_on)
        t_off = t_on + g * float(n.ioi)
        fnum  = int(n.fnum)
        blk   = int(n.blk)

        # A0/B0 writes at note-on (set FNUM, BLK, KEYON=1)
        evs.append({
            "time_s": t_on,
            "port": port,
            "addr": 0xA0 + ch,
            "data": a0_value(fnum),
            "pattern": pattern,
            "channel": ch,
            "event": "A0_FNUM_LOW_ON",
        })
        evs.append({
            "time_s": t_on,
            "port": port,
            "addr": 0xB0 + ch,
            "data": b0_value(fnum, blk, True),
            "pattern": pattern,
            "channel": ch,
            "event": "B0_KEYON_ON",
        })

        # KEYOFF at t_off (keep FNUM/BLK, clear KEYON)
        evs.append({
            "time_s": t_off,
            "port": port,
            "addr": 0xB0 + ch,
            "data": b0_value(fnum, blk, False),
            "pattern": pattern,
            "channel": ch,
            "event": "B0_KEYON_OFF",
        })
    return evs

def main():
    ap = argparse.ArgumentParser(description="OPLL (YM2413) timeline -> OPL3 (YMF262) 2-op register CSV (minimal prototype)")
    ap.add_argument("--timeline-csv", required=True, help="Input *_timeline_YM2413.csv")
    ap.add_argument("--out-csv", required=True, help="Output OPL3 CSV path")
    ap.add_argument("--gate-default", type=float, default=0.80, help="Gate fraction to compute note-off (0..1)")
    ap.add_argument("--port", type=int, default=0, help="OPL3 port (0 or 1). Prototype uses single bank.")
    # Patch params only needed to satisfy loader's API
    ap.add_argument("--ar", type=int, default=8)
    ap.add_argument("--dr", type=int, default=6)
    ap.add_argument("--sl", type=int, default=8)
    ap.add_argument("--rr", type=int, default=5)
    ap.add_argument("--ksr", type=int, default=1)
    args = ap.parse_args()

    patch = PatchParams(ar=args.ar, dr=args.dr, sl=args.sl, rr=args.rr, ksr=bool(args.ksr))
    ds = load_from_timeline_csv(args.timeline_csv, patch)

    all_events: List[Dict[str, Any]] = []
    for (pattern, ch), payload in ds.items():
        # pattern名は CSV のベース名に合わせる（loaderが提供するpatternでもOK）
        pat = pattern or os.path.splitext(os.path.basename(args.timeline_csv))[0]
        notes = payload.get("notes", [])
        if not notes:
            continue
        all_events.extend(build_events_for_channel(notes, ch, pat, args.gate_default, args.port))

    # 時刻で安定ソート
    all_events.sort(key=lambda e: (e["time_s"], e["port"], e["addr"]))

    os.makedirs(os.path.dirname(args.out_csv) or ".", exist_ok=True)
    with open(args.out_csv, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=[
            "time_s", "port", "addr", "data", "pattern", "channel", "event"
        ])
        w.writeheader()
        for ev in all_events:
            w.writerow(ev)

    print(f"Wrote {len(all_events)} events to {args.out_csv}")

if __name__ == "__main__":
    main()