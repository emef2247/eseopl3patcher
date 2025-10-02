from __future__ import annotations
import argparse
import csv
import json
import math
import os
from dataclasses import asdict
from typing import Dict, List, Tuple, Any, Optional

from .model import YM2413EnvelopeModel, PatchParams, NoteContext
from .loader_ir import load_from_timeline_csv
from .optimizer import GateEstimator

def _parse_range(s: str, name: str) -> Tuple[float, float]:
    try:
        parts = [float(x.strip()) for x in s.split(",")]
        if len(parts) != 2:
            raise ValueError
        lo, hi = parts
        if lo > hi:
            lo, hi = hi, lo
        return lo, hi
    except Exception:
        raise argparse.ArgumentTypeError(f"{name} must be 'lo,hi'")

def _linpoints(lo: float, hi: float, n: int) -> List[float]:
    if n <= 1 or math.isclose(lo, hi, rel_tol=0, abs_tol=1e-12):
        return [lo]
    step = (hi - lo) / (n - 1)
    return [round(lo + i * step, 8) for i in range(n)]

def _shrink_around(best: float, lo: float, hi: float, factor: float = 0.5) -> Tuple[float, float]:
    width = hi - lo
    if width <= 0:
        return lo, hi
    new_w = width * factor
    new_lo = best - new_w / 2
    new_hi = best + new_w / 2
    if new_hi - new_lo < 1e-6:
        mid = (new_lo + new_hi) / 2
        new_lo = mid - 5e-7
        new_hi = mid + 5e-7
    return new_lo, new_hi

def _default_patch(args: argparse.Namespace) -> PatchParams:
    return PatchParams(ar=args.ar, dr=args.dr, sl=args.sl, rr=args.rr, ksr=bool(args.ksr))

def _load_dataset(args: argparse.Namespace) -> Dict[Tuple[str, int], Dict[str, Any]]:
    if args.timeline_csv:
        patch = _default_patch(args)
        return load_from_timeline_csv(args.timeline_csv, patch)
    if args.ir_root:
        est = GateEstimator()
        return est.load_pattern_data(args.ir_root)
    raise SystemExit("Either --timeline-csv or --ir-root must be provided")

def _eval_params_on_dataset(
    dataset: Dict[Tuple[str, int], Dict[str, Any]],
    base_release_ms: float,
    exp_shape: float,
    ksr_scale_per_blk: float,
    sl_floor: float,
    sl_curve: float,
    gate_min: float,
    gate_max: float,
    gate_step: float,
    residual_threshold_db: float,
    overlap_weight: float,
    gap_weight: float,
    db_weight: float,
) -> Dict[str, Any]:
    model = YM2413EnvelopeModel(
        base_attack_ms=0.50,
        base_decay_ms=1.00,
        base_release_ms=base_release_ms,
        exp_shape=exp_shape,
        ksr_scale_per_blk=ksr_scale_per_blk,
        sl_floor=sl_floor,
        sl_curve=sl_curve,
    )

    total_score = 0.0
    total_residual = 0.0
    total_db_norm = 0.0
    total_sustain_loss = 0.0
    total_overlap_events = 0
    seq_count = 0
    gates: List[Tuple[str, int, float]] = []

    for (pattern, ch), payload in dataset.items():
        gate, metrics = model.choose_gate_grid(
            payload["patch"],
            payload["notes"],
            gate_min=gate_min,
            gate_max=gate_max,
            gate_step=gate_step,
            residual_threshold_db=residual_threshold_db,
            overlap_weight=overlap_weight,
            gap_weight=gap_weight,
            db_weight=db_weight,
        )
        total_score += metrics["score"]
        total_residual += metrics["avg_residual"]
        total_db_norm += metrics.get("avg_residual_db_norm", 0.0)
        total_sustain_loss += metrics.get("avg_sustain_loss", 0.0)
        total_overlap_events += metrics.get("overlap_events", 0)
        seq_count += 1
        gates.append((pattern, ch, gate))

    avg_score = total_score / max(1, seq_count)
    avg_residual = total_residual / max(1, seq_count)
    avg_db_norm = total_db_norm / max(1, seq_count)
    avg_sustain_loss = total_sustain_loss / max(1, seq_count)

    return {
        "avg_score": avg_score,
        "avg_residual": avg_residual,
        "avg_db_norm": avg_db_norm,
        "avg_sustain_loss": avg_sustain_loss,
        "total_overlap_events": total_overlap_events,
        "sequences": seq_count,
        "gates": gates,
    }

def _write_csv(path: str, rows: List[Dict[str, Any]], fieldnames: List[str]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow(r)

def main():
    ap = argparse.ArgumentParser(description="Coarse-to-fine parameter sweep for YM2413 gate estimator")
    ap.add_argument("--timeline-csv", help="YM2413 timeline CSV (e.g., tests/equiv/inputs/ir/..._timeline_YM2413.csv)")
    ap.add_argument("--ir-root", help="Root folder containing patterns.json")
    ap.add_argument("--pattern", default="", help="Optional pattern name override (not used in sweep scoring)")
    ap.add_argument("--ar", type=int, default=8)
    ap.add_argument("--dr", type=int, default=6)
    ap.add_argument("--sl", type=int, default=8)
    ap.add_argument("--rr", type=int, default=5)
    ap.add_argument("--ksr", type=int, default=1)

    ap.add_argument("--gate-min", type=float, default=0.45)
    ap.add_argument("--gate-max", type=float, default=0.98)
    ap.add_argument("--gate-step", type=float, default=0.005)

    ap.add_argument("--brms-range", type=lambda s: _parse_range(s, "brms-range"), default="0.4,1.0",
                    help="base_release_ms range in seconds, e.g. '0.4,1.0'")
    ap.add_argument("--resh-db-range", type=lambda s: _parse_range(s, "resh-db-range"), default="-60,-30",
                    help="residual_threshold_db range in dB, e.g. '-60,-30'")
    ap.add_argument("--dbw-range", type=lambda s: _parse_range(s, "dbw-range"), default="0.10,0.50",
                    help="db_weight range, e.g. '0.10,0.50'")
    ap.add_argument("--gapw-range", type=lambda s: _parse_range(s, "gapw-range"), default="0.10,0.50",
                    help="gap_weight range, e.g. '0.10,0.50'")
    ap.add_argument("--exp-range", type=lambda s: _parse_range(s, "exp-range"), default="0.7,1.1",
                    help="exp_shape range, e.g. '0.7,1.1'")
    ap.add_argument("--ksr-scale-range", type=lambda s: _parse_range(s, "ksr-scale-range"), default="0.10,0.30",
                    help="ksr_scale_per_blk range, e.g. '0.10,0.30'")
    ap.add_argument("--sl-floor-range", type=lambda s: _parse_range(s, "sl-floor-range"), default="0.03,0.10",
                    help="sl_floor range, e.g. '0.03,0.10'")
    ap.add_argument("--sl-curve-range", type=lambda s: _parse_range(s, "sl-curve-range"), default="0.8,1.5",
                    help="sl_curve range, e.g. '0.8,1.5'")

    ap.add_argument("--points", type=int, default=3, help="Points per dimension (e.g., 3 -> low/mid/high)")
    ap.add_argument("--iters", type=int, default=3, help="Number of zoom iterations")
    ap.add_argument("--shrink", type=float, default=0.5, help="Per-iteration window shrink factor (0.5 halves the window)")
    ap.add_argument("--out-dir", required=True, help="Output directory for sweep logs")

    args = ap.parse_args()

    dataset = _load_dataset(args)
    out_dir = args.out_dir
    os.makedirs(out_dir, exist_ok=True)

    (br_lo, br_hi) = args.brms_range if isinstance(args.brms_range, tuple) else _parse_range(args.brms_range, "brms-range")
    (resh_lo, resh_hi) = args.resh_db_range if isinstance(args.resh_db_range, tuple) else _parse_range(args.resh_db_range, "resh-db-range")
    (dbw_lo, dbw_hi) = args.dbw_range if isinstance(args.dbw_range, tuple) else _parse_range(args.dbw_range, "dbw-range")
    (gapw_lo, gapw_hi) = args.gapw_range if isinstance(args.gapw_range, tuple) else _parse_range(args.gapw_range, "gapw-range")
    (exp_lo, exp_hi) = args.exp_range if isinstance(args.exp_range, tuple) else _parse_range(args.exp_range, "exp-range")
    (ksr_lo, ksr_hi) = args.ksr_scale_range if isinstance(args.ksr_scale_range, tuple) else _parse_range(args.ksr_scale_range, "ksr-scale-range")
    (slf_lo, slf_hi) = args.sl_floor_range if isinstance(args.sl_floor_range, tuple) else _parse_range(args.sl_floor_range, "sl-floor-range")
    (slc_lo, slc_hi) = args.sl_curve_range if isinstance(args.sl_curve_range, tuple) else _parse_range(args.sl_curve_range, "sl-curve-range")

    summary_rows: List[Dict[str, Any]] = []
    best_overall: Optional[Dict[str, Any]] = None

    for it in range(args.iters):
        br_points   = _linpoints(br_lo,   br_hi,   args.points)
        resh_points = _linpoints(resh_lo, resh_hi, args.points)
        dbw_points  = _linpoints(dbw_lo,  dbw_hi,  args.points)
        gapw_points = _linpoints(gapw_lo, gapw_hi, args.points)
        exp_points  = _linpoints(exp_lo,  exp_hi,  args.points)
        ksr_points  = _linpoints(ksr_lo,  ksr_hi,  args.points)
        slf_points  = _linpoints(slf_lo,  slf_hi,  args.points)
        slc_points  = _linpoints(slc_lo,  slc_hi,  args.points)

        trials: List[Dict[str, Any]] = []
        best_iter: Optional[Dict[str, Any]] = None

        for br in br_points:
            for resh in resh_points:
                for dbw in dbw_points:
                    for gapw in gapw_points:
                        for exp in exp_points:
                            for ksr in ksr_points:
                                for slf in slf_points:
                                    for slc in slc_points:
                                        metrics = _eval_params_on_dataset(
                                            dataset=dataset,
                                            base_release_ms=br,
                                            exp_shape=exp,
                                            ksr_scale_per_blk=ksr,
                                            sl_floor=slf,
                                            sl_curve=slc,
                                            gate_min=args.gate_min,
                                            gate_max=args.gate_max,
                                            gate_step=args.gate_step,
                                            residual_threshold_db=resh,
                                            overlap_weight=1.0,
                                            gap_weight=gapw,
                                            db_weight=dbw,
                                        )
                                        row = {
                                            "iter": it,
                                            "base_release_ms": br,
                                            "exp_shape": exp,
                                            "ksr_scale_per_blk": ksr,
                                            "sl_floor": slf,
                                            "sl_curve": slc,
                                            "residual_threshold_db": resh,
                                            "db_weight": dbw,
                                            "gap_weight": gapw,
                                            "avg_score": metrics["avg_score"],
                                            "avg_residual": metrics["avg_residual"],
                                            "avg_db_norm": metrics["avg_db_norm"],
                                            "avg_sustain_loss": metrics["avg_sustain_loss"],
                                            "total_overlap_events": metrics["total_overlap_events"],
                                            "sequences": metrics["sequences"],
                                        }
                                        trials.append(row)
                                        if (best_iter is None) or (row["avg_score"] < best_iter["avg_score"] - 1e-12):
                                            best_iter = row

        trial_path = os.path.join(out_dir, f"iter_{it}_trials.csv")
        _write_csv(trial_path, trials, fieldnames=list(trials[0].keys()) if trials else [])

        if best_iter:
            summary_rows.append(best_iter)
            if (best_overall is None) or (best_iter["avg_score"] < best_overall["avg_score"] - 1e-12):
                best_overall = best_iter

            br_lo, br_hi = _shrink_around(best_iter["base_release_ms"], br_lo, br_hi, args.shrink)
            resh_lo, resh_hi = _shrink_around(best_iter["residual_threshold_db"], resh_lo, resh_hi, args.shrink)
            dbw_lo, dbw_hi = _shrink_around(best_iter["db_weight"], dbw_lo, dbw_hi, args.shrink)
            gapw_lo, gapw_hi = _shrink_around(best_iter["gap_weight"], gapw_lo, gapw_hi, args.shrink)
            exp_lo, exp_hi = _shrink_around(best_iter["exp_shape"], exp_lo, exp_hi, args.shrink)
            ksr_lo, ksr_hi = _shrink_around(best_iter["ksr_scale_per_blk"], ksr_lo, ksr_hi, args.shrink)
            slf_lo, slf_hi = _shrink_around(best_iter["sl_floor"], slf_lo, slf_hi, args.shrink)
            slc_lo, slc_hi = _shrink_around(best_iter["sl_curve"], slc_lo, slc_hi, args.shrink)

    if summary_rows:
        summary_path = os.path.join(out_dir, "summary.csv")
        _write_csv(summary_path, summary_rows, fieldnames=list(summary_rows[0].keys()))
    if best_overall:
        best_path = os.path.join(out_dir, "best_params.json")
        with open(best_path, "w", encoding="utf-8") as f:
            json.dump(best_overall, f, indent=2)
        print(f"Best params written to: {best_path}")
    print(f"Sweep completed. Logs at {out_dir}")

if __name__ == "__main__":
    main()