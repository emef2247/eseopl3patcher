from __future__ import annotations
import argparse
from typing import List
from .optimizer import GateEstimator, GateEstimate

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ir-root", required=True, help="Root folder of IR manifest (e.g., tools/gate_estimator)")
    ap.add_argument("--out", required=True, help="Output CSV path for gate estimates")
    args = ap.parse_args()

    est = GateEstimator()
    dataset = est.load_pattern_data(args.ir_root)

    results: List[GateEstimate] = []
    for (pattern, ch), payload in dataset.items():
        gate, metrics = est.estimate_for_sequence(payload["patch"], payload["notes"])
        results.append(GateEstimate(
            pattern=pattern, channel=ch, gate=gate,
            avg_residual=metrics["avg_residual"],
            overlap_events=metrics["overlap_events"],
            avg_sustain_loss=metrics["avg_sustain_loss"],
            score=metrics["score"],
        ))

    est.write_csv(args.out, results)
    print(f"Wrote {len(results)} gate estimates to {args.out}")

if __name__ == "__main__":
    main()