from __future__ import annotations
import argparse
from typing import List
from .optimizer import GateEstimator, GateEstimate
from .model import PatchParams
from .loader_ir import load_from_timeline_csv

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ir-root", help="Root folder of IR manifest (patterns.json). If given, uses manifest mode.")
    ap.add_argument("--timeline-csv", help="YM2413 timeline CSV (e.g., tests/equiv/inputs/ir/..._timeline_YM2413.csv)")
    ap.add_argument("--pattern", default="", help="Pattern name override (optional).")
    ap.add_argument("--out", required=True, help="Output CSV path for gate estimates")

    # Patch params (fallback defaults if not specified)
    ap.add_argument("--ar", type=int, default=8)
    ap.add_argument("--dr", type=int, default=6)
    ap.add_argument("--sl", type=int, default=8)
    ap.add_argument("--rr", type=int, default=5)
    ap.add_argument("--ksr", type=int, default=1, help="1: enabled, 0: disabled")

    args = ap.parse_args()
    est = GateEstimator()

    rows: List[GateEstimate] = []

    if args.timeline_csv:
        patch = PatchParams(ar=args.ar, dr=args.dr, sl=args.sl, rr=args.rr, ksr=bool(args.ksr))
        dataset = load_from_timeline_csv(args.timeline_csv, patch)
    elif args.ir_root:
        dataset = est.load_pattern_data(args.ir_root)  # uses patterns.json loader
    else:
        raise SystemExit("Either --timeline-csv or --ir-root must be provided")

    for (pattern, ch), payload in dataset.items():
        patt_name = args.pattern or pattern
        gate, metrics = est.estimate_for_sequence(payload["patch"], payload["notes"])  # type: ignore[index]
        rows.append(GateEstimate(
            pattern=patt_name, channel=ch, gate=gate,
            avg_residual=metrics["avg_residual"],
            overlap_events=metrics.get("overlap_events", 0),
            avg_sustain_loss=metrics.get("avg_sustain_loss", 0.0),
            score=metrics["score"],
        ))

    est.write_csv(args.out, rows)
    print(f"Wrote {len(rows)} gate estimates to {args.out}")

if __name__ == "__main__":
    main()