from __future__ import annotations
import argparse
from typing import Tuple
from .loader_ir import load_from_timeline_csv
from .model import PatchParams as ParamPatch
from .optimizer import GateEstimator
from .ym2413_exact import YM2413EnvelopeExact, load_tables_json, PatchParams as ExactPatch

def run_once(timeline_csv: str, eg_tables: str, ar=8, dr=6, sl=8, rr=5, ksr=1) -> Tuple[float, float]:
    # Load dataset
    patch_param = ParamPatch(ar=ar, dr=dr, sl=sl, rr=rr, ksr=bool(ksr))
    dataset = load_from_timeline_csv(timeline_csv, patch_param)

    # Param result
    est = GateEstimator()
    (k, v), = dataset.items()
    gate_param, _ = est.estimate_for_sequence(v["patch"], v["notes"])

    # Exact result
    tables = load_tables_json(eg_tables)
    model = YM2413EnvelopeExact(tables)
    gate_exact, _ = model.choose_gate_grid(v["patch"], v["notes"])
    return gate_param, gate_exact

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeline-csv", required=True)
    ap.add_argument("--eg-tables", required=True)
    ap.add_argument("--ar", type=int, default=8)
    ap.add_argument("--dr", type=int, default=6)
    ap.add_argument("--sl", type=int, default=8)
    ap.add_argument("--rr", type=int, default=5)
    ap.add_argument("--ksr", type=int, default=1)
    args = ap.parse_args()

    gp, ge = run_once(args.timeline_csv, args.eg_tables, args.ar, args.dr, args.sl, args.rr, args.ksr)
    print(f"Gate (param) = {gp:.3f}, Gate (exact) = {ge:.3f}")

if __name__ == "__main__":
    main()