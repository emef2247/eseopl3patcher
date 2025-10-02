from __future__ import annotations
import argparse
from typing import List
from .optimizer import GateEstimator, GateEstimate

def main():
    ap = argparse.ArgumentParser(description="Estimate gate parameters for YM2413/OPLL from IR/CSV data")
    ap.add_argument("--ir-root", help="Root folder with patterns.json manifest (legacy mode)")
    ap.add_argument("--csv-dir", help="Directory containing durations CSV files")
    ap.add_argument("--csv-pattern", default="*_durations.csv", help="Glob pattern for CSV files (default: *_durations.csv)")
    ap.add_argument("--inst", type=int, default=2, help="YM2413 instrument/patch number (0-15, default: 2=Guitar)")
    ap.add_argument("--out", required=True, help="Output CSV path for gate estimates")
    args = ap.parse_args()

    est = GateEstimator()
    
    # Determine load mode
    if args.csv_dir:
        dataset = est.load_csv_directory(args.csv_dir, pattern=args.csv_pattern, inst=args.inst)
    elif args.ir_root:
        dataset = est.load_pattern_data(args.ir_root)
    else:
        ap.error("Either --ir-root or --csv-dir must be specified")

    results: List[GateEstimate] = []
    for (pattern, ch), payload in dataset.items():
        gate, metrics = est.estimate_for_sequence(payload["patch"], payload["notes"])
        results.append(GateEstimate(
            pattern=pattern, channel=ch, gate=gate,
            avg_residual_db=metrics["avg_residual_db"],
            overlap_events=metrics["overlap_events"],
            avg_sustain_loss=metrics["avg_sustain_loss"],
            score=metrics["score"],
        ))

    est.write_csv(args.out, results)
    print(f"Wrote {len(results)} gate estimates to {args.out}")

if __name__ == "__main__":
    main()