from __future__ import annotations
import argparse
import csv
import json
import math
import os
import time
import itertools
import concurrent.futures as cf
from typing import Dict, List, Tuple, Any, Optional

from .model import YM2413EnvelopeModel, PatchParams, NoteContext
from .loader_ir import load_from_timeline_csv
from .optimizer import GateEstimator

# Optional exact (table-driven) model
try:
    from .ym2413_exact import YM2413EnvelopeExact, load_tables_json  # type: ignore
    HAS_EXACT = True
except Exception:
    HAS_EXACT = False
    YM2413EnvelopeExact = None  # type: ignore
    load_tables_json = None     # type: ignore


def _parse_range(s, name: str) -> Tuple[float, float]:
    if isinstance(s, (tuple, list)) and len(s) == 2:
        try:
            lo, hi = float(s[0]), float(s[1])
        except Exception:
            raise argparse.ArgumentTypeError(f"{name} must contain numeric values")
        if lo > hi:
            lo, hi = hi, lo
        return lo, hi
    if isinstance(s, str):
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
    raise argparse.ArgumentTypeError(f"{name} must be a tuple/list of two numbers or 'lo,hi' string")


def _linpoints(lo: float, hi: float, n: int) -> List[float]:
    if n <= 1 or math.isclose(lo, hi, rel_tol=0, abs_tol=1e-12):
        return [lo]
    step = (hi - lo) / (n - 1)
    return [round(lo + i * step, 8) for i in range(n)]


def _shrink_around(best: float, lo: float, hi: float, factor: float,
                   lo_bound: float, hi_bound: float) -> Tuple[float, float]:
    width = hi - lo
    if width <= 0:
        return lo, hi
    new_w = width * factor
    new_lo = best - new_w / 2
    new_hi = best + new_w / 2
    new_lo = max(lo_bound, new_lo)
    new_hi = min(hi_bound, new_hi)
    if new_hi - new_lo < 1e-6:
        mid = (new_lo + new_hi) / 2
        new_lo = max(lo_bound, mid - 5e-7)
        new_hi = min(hi_bound, mid + 5e-7)
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


def _write_csv(path: str, rows: List[Dict[str, Any]], fieldnames: List[str]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow(r)


def _progress_bar(done: int, total: int, start_t: float, it_idx: int, iters: int) -> None:
    now = time.time()
    pct = (done / total) * 100 if total else 100.0
    rate = done / max(1e-6, (now - start_t))
    eta = (total - done) / max(1e-6, rate) if rate > 0 else 0.0
    print(f"[iter {it_idx+1}/{iters}] {done}/{total} ({pct:5.1f}%) ETA {eta:6.1f}s", end="\r", flush=True)


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

    ap.add_argument("--brms-range", type=str, default="0.4,1.0")
    ap.add_argument("--resh-db-range", type=str, default="-60,-30")
    ap.add_argument("--dbw-range", type=str, default="0.10,0.50")
    ap.add_argument("--gapw-range", type=str, default="0.10,0.50")
    ap.add_argument("--exp-range", type=str, default="0.7,1.1")
    ap.add_argument("--ksr-scale-range", type=str, default="0.10,0.30")
    ap.add_argument("--sl-floor-range", type=str, default="0.03,0.10")
    ap.add_argument("--sl-curve-range", type=str, default="0.8,1.5")

    ap.add_argument("--points", type=int, default=3)
    ap.add_argument("--iters", type=int, default=3)
    ap.add_argument("--shrink", type=float, default=0.5)

    ap.add_argument("--workers", type=int, default=1)
    ap.add_argument("--progress", action="store_true")
    ap.add_argument("--nice", type=int, default=None)

    ap.add_argument("--eg-model", choices=["param", "exact"], default="param")
    ap.add_argument("--eg-tables", help="JSON path for exact EG tables (required when --eg-model exact)")

    ap.add_argument("--out-dir", required=True)

    args = ap.parse_args()

    if args.nice is not None:
        try:
            os.nice(args.nice)
        except Exception:
            pass

    dataset = _load_dataset(args)
    out_dir = args.out_dir
    os.makedirs(out_dir, exist_ok=True)

    exact_tables: Optional[Dict[str, Any]] = None
    if args.eg_model == "exact":
        if not HAS_EXACT:
            raise SystemExit("Exact model not available (missing ym2413_exact.py)")
        if not args.eg_tables:
            raise SystemExit("--eg-tables is required for --eg-model exact")
        exact_tables = load_tables_json(args.eg_tables)  # type: ignore

    def build_model(base_release_ms: float,
                    exp_shape: float,
                    ksr_scale_per_blk: float,
                    sl_floor: float,
                    sl_curve: float):
        if args.eg_model == "exact":
            return YM2413EnvelopeExact(exact_tables, exp_shape=exp_shape)  # type: ignore
        else:
            return YM2413EnvelopeModel(
                base_attack_ms=0.50,
                base_decay_ms=1.00,
                base_release_ms=base_release_ms,
                exp_shape=exp_shape,
                ksr_scale_per_blk=ksr_scale_per_blk,
                sl_floor=sl_floor,
                sl_curve=sl_curve,
            )

    br_lo, br_hi       = _parse_range(args.brms_range, "brms-range")
    resh_lo, resh_hi   = _parse_range(args.resh_db_range, "resh-db-range")
    dbw_lo, dbw_hi     = _parse_range(args.dbw_range, "dbw-range")
    gapw_lo, gapw_hi   = _parse_range(args.gapw_range, "gapw-range")
    exp_lo, exp_hi     = _parse_range(args.exp_range, "exp-range")
    ksr_lo, ksr_hi     = _parse_range(args.ksr_scale_range, "ksr-scale-range")
    slf_lo, slf_hi     = _parse_range(args.sl_floor_range, "sl-floor-range")
    slc_lo, slc_hi     = _parse_range(args.sl_curve_range, "sl-curve-range")

    br_bounds   = (br_lo, br_hi)
    resh_bounds = (resh_lo, resh_hi)
    dbw_bounds  = (dbw_lo, dbw_hi)
    gapw_bounds = (gapw_lo, gapw_hi)
    exp_bounds  = (exp_lo, exp_hi)
    ksr_bounds  = (ksr_lo, ksr_hi)
    slf_bounds  = (slf_lo, slf_hi)
    slc_bounds  = (slc_lo, slc_hi)

    summary_rows: List[Dict[str, Any]] = []
    best_overall: Optional[Dict[str, Any]] = None

    for it in range(args.iters):
        br_points   = _linpoints(br_lo,   br_hi,   args.points)
        resh_points = _linpoints(resh_lo, resh_hi, args.points)
        dbw_points  = _linpoints(dbw_lo,  dbw_hi,  args.points)
        gapw_points = _linpoints(gapw_lo, gapw_hi, args.points)
        exp_points  = _linpoints(exp_lo,  exp_hi,  args.points)
        ksr_points  = _linpoints(ksr_lo,  ksr_hi,  args.points)
        slf_points  = _linpoints(slf_lo,  slf_hi, args.points)
        slc_points  = _linpoints(slc_lo,  slc_hi, args.points)

        combos = list(itertools.product(
            br_points, resh_points, dbw_points, gapw_points,
            exp_points, ksr_points, slf_points, slc_points
        ))
        total = len(combos)
        trials: List[Dict[str, Any]] = []
        best_iter: Optional[Dict[str, Any]] = None

        def run_one(combo: Tuple[float, ...]) -> Dict[str, Any]:
            (br, resh, dbw, gapw, exp, ksr, slf, slc) = combo
            model = build_model(br, exp, ksr, slf, slc)
            total_score = 0.0
            total_residual = 0.0
            total_db_norm = 0.0
            total_sustain = 0.0
            total_overlap = 0
            seq_count = 0
            for (_key, payload) in dataset.items():
                gate, metrics = model.choose_gate_grid(
                    payload["patch"],
                    payload["notes"],
                    gate_min=args.gate_min,
                    gate_max=args.gate_max,
                    gate_step=args.gate_step,
                    residual_threshold_db=resh,
                    overlap_weight=1.0,
                    gap_weight=gapw,
                    db_weight=dbw,
                )
                total_score += float(metrics.get("score", 0.0))
                total_residual += float(metrics.get("avg_residual", 0.0))
                total_db_norm += float(metrics.get("avg_residual_db_norm", 0.0))
                total_sustain += float(metrics.get("avg_sustain_loss", 0.0))
                total_overlap += int(metrics.get("overlap_events", 0))
                seq_count += 1
            avg_score = total_score / max(1, seq_count)
            avg_residual = total_residual / max(1, seq_count)
            avg_db_norm = total_db_norm / max(1, seq_count)
            avg_sustain = total_sustain / max(1, seq_count)
            return {
                "iter": it,
                "base_release_ms": br,
                "exp_shape": exp,
                "ksr_scale_per_blk": ksr,
                "sl_floor": slf,
                "sl_curve": slc,
                "residual_threshold_db": resh,
                "db_weight": dbw,
                "gap_weight": gapw,
                "avg_score": avg_score,
                "avg_residual": avg_residual,
                "avg_db_norm": avg_db_norm,
                "avg_sustain_loss": avg_sustain,
                "total_overlap_events": total_overlap,
                "sequences": seq_count,
            }

        start_t = time.time()
        done = 0

        if args.workers and args.workers > 1:
            with cf.ProcessPoolExecutor(max_workers=args.workers) as ex:
                for row in ex.map(run_one, combos, chunksize=1):
                    trials.append(row)
                    done += 1
                    if args.progress and (done % max(1, total // 100) == 0 or done == total):
                        _progress_bar(done, total, start_t, it_idx=it, iters=args.iters)
        else:
            for combo in combos:
                row = run_one(combo)
                trials.append(row)
                done += 1
                if args.progress and (done % max(1, total // 100) == 0 or done == total):
                    _progress_bar(done, total, start_t, it_idx=it, iters=args.iters)

        if args.progress:
            print()

        if trials:
            trial_path = os.path.join(out_dir, f"iter_{it}_trials.csv")
            _write_csv(trial_path, trials, fieldnames=list(trials[0].keys()))

        for row in trials:
            if (best_iter is None) or (row["avg_score"] < best_iter["avg_score"] - 1e-12):
                best_iter = row

        if best_iter:
            summary_rows.append(best_iter)
            if (best_overall is None) or (best_iter["avg_score"] < best_overall["avg_score"] - 1e-12):
                best_overall = best_iter

            br_lo, br_hi     = _shrink_around(best_iter["base_release_ms"],      br_lo,   br_hi,   args.shrink, *br_bounds)
            resh_lo, resh_hi = _shrink_around(best_iter["residual_threshold_db"], resh_lo, resh_hi, args.shrink, *resh_bounds)
            dbw_lo, dbw_hi   = _shrink_around(best_iter["db_weight"],             dbw_lo,  dbw_hi,  args.shrink, *dbw_bounds)
            gapw_lo, gapw_hi = _shrink_around(best_iter["gap_weight"],            gapw_lo, gapw_hi, args.shrink, *gapw_bounds)
            exp_lo, exp_hi   = _shrink_around(best_iter["exp_shape"],             exp_lo,  exp_hi,  args.shrink, *exp_bounds)
            ksr_lo, ksr_hi   = _shrink_around(best_iter["ksr_scale_per_blk"],     ksr_lo,  ksr_hi,  args.shrink, *ksr_bounds)
            slf_lo, slf_hi   = _shrink_around(best_iter["sl_floor"],              slf_lo,  slf_hi,  args.shrink, *slf_bounds)
            slc_lo, slc_hi   = _shrink_around(best_iter["sl_curve"],              slc_lo,  slc_hi,  args.shrink, *slc_bounds)

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