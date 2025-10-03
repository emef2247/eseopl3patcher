from __future__ import annotations
import argparse
from typing import List
from .optimizer import GateEstimator, GateEstimate
from .model import PatchParams
from .loader_ir import load_from_timeline_csv

# Optional exact model
try:
    from .ym2413_exact import YM2413EnvelopeExact, load_tables_json, PatchParams as PatchParamsExact  # type: ignore
    HAS_EXACT = True
except Exception:
    HAS_EXACT = False
    YM2413EnvelopeExact = None  # type: ignore

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ir-root", help="Root folder of IR manifest (patterns.json). If given, uses manifest mode.")
    ap.add_argument("--timeline-csv", help="YM2413 timeline CSV (e.g., tests/equiv/inputs/ir/..._timeline_YM2413.csv)")
    ap.add_argument("--pattern", default="", help="Pattern name override (optional).")
    ap.add_argument("--out", required=True, help="Output CSV path for gate estimates")

    # Patch params (used when --timeline-csv)
    ap.add_argument("--ar", type=int, default=8)
    ap.add_argument("--dr", type=int, default=6)
    ap.add_argument("--sl", type=int, default=8)
    ap.add_argument("--rr", type=int, default=5)
    ap.add_argument("--ksr", type=int, default=1, help="1: enabled, 0: disabled")

    # Model selection
    ap.add_argument("--eg-model", choices=["param", "exact"], default="param",
                    help="Use parametric model or exact (table-driven) model")
    ap.add_argument("--eg-tables", help="JSON path for exact EG tables (required when --eg-model exact)")

    args = ap.parse_args()
    est = GateEstimator()

    # Load dataset
    if args.timeline_csv:
        patch = PatchParams(ar=args.ar, dr=args.dr, sl=args.sl, rr=args.rr, ksr=bool(args.ksr))
        dataset = load_from_timeline_csv(args.timeline_csv, patch)
    elif args.ir_root:
        dataset = est.load_pattern_data(args.ir_root)
    else:
        raise SystemExit("Either --timeline-csv or --ir-root must be provided")

    # Choose model
    model = None
    if args.eg_model == "exact":
        if not HAS_EXACT:
            raise SystemExit("Exact model not available (missing ym2413_exact.py)")
        if not args.eg_tables:
            raise SystemExit("--eg-tables is required for --eg-model exact")
        tables = load_tables_json(args.eg_tables)
        model = YM2413EnvelopeExact(tables)

    rows: List[GateEstimate] = []
    for (pattern, ch), payload in dataset.items():
        patt_name = args.pattern or pattern
        if model is not None:
            gate, metrics = model.choose_gate_grid(payload["patch"], payload["notes"])  # type: ignore[index]
        else:
            gate, metrics = est.estimate_for_sequence(payload["patch"], payload["notes"])  # type: ignore[index]
        rows.append(GateEstimate(
            pattern=patt_name, channel=ch, gate=gate,
            avg_residual=metrics.get("avg_residual", 0.0),
            overlap_events=metrics.get("overlap_events", 0),
            avg_sustain_loss=metrics.get("avg_sustain_loss", 0.0),
            score=metrics.get("score", 0.0),
        ))

    est.write_csv(args.out, rows)
    print(f"Wrote {len(rows)} gate estimates to {args.out}")

if __name__ == "__main__":
    main()