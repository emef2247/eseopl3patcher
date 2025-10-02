#!/usr/bin/env python3
import argparse
from pathlib import Path
import pandas as pd
import numpy as np

def summarize_file(path: Path) -> dict:
    df = pd.read_csv(path)
    n = len(df)
    if n == 0:
        return {"file": str(path), "notes": 0}

    ko_off = (df.get("close_reason", "") == "ko_off").sum()
    ratio = pd.to_numeric(df.get("dur_to_next_on_ratio", pd.Series(dtype=float)), errors="coerce")
    ratio_valid = ratio.replace([np.inf, -np.inf], np.nan).dropna()

    def bucketize(x):
        if x < 0.3: return "0-0.3"
        if x < 0.6: return "0.3-0.6"
        if x < 0.9: return "0.6-0.9"
        if x < 1.1: return "0.9-1.1"
        return ">1.1"

    buckets = ratio_valid.apply(bucketize).value_counts().to_dict()

    a0_before = int(df.get("a0_before_on_in_window", pd.Series([False]*n)).sum())
    a0_after = int(df.get("a0_after_on_in_window", pd.Series([False]*n)).sum())
    iv_before = int(df.get("iv_before_on_in_window", pd.Series([False]*n)).sum())
    iv_after = int(df.get("iv_after_on_in_window", pd.Series([False]*n)).sum())

    return {
        "file": str(path),
        "notes": n,
        "ko_off_count": int(ko_off),
        "ko_off_ratio": round(ko_off / n, 4) if n else 0.0,
        "ratio_mean": round(ratio_valid.mean(), 6) if len(ratio_valid) else np.nan,
        "ratio_median": round(ratio_valid.median(), 6) if len(ratio_valid) else np.nan,
        "ratio_0-0.3": int(buckets.get("0-0.3", 0)),
        "ratio_0.3-0.6": int(buckets.get("0.3-0.6", 0)),
        "ratio_0.6-0.9": int(buckets.get("0.6-0.9", 0)),
        "ratio_0.9-1.1": int(buckets.get("0.9-1.1", 0)),
        "ratio_>1.1": int(buckets.get(">1.1", 0)),
        "a0_before_in_win": a0_before,
        "a0_after_in_win": a0_after,
        "iv_before_in_win": iv_before,
        "iv_after_in_win": iv_after,
    }

def main():
    ap = argparse.ArgumentParser(description="Summarize YM2413 *_durations.csv metrics")
    ap.add_argument("--glob", default="tests/equiv/outputs/ir/*_durations.csv", help="Glob pattern to find durations CSVs")
    ap.add_argument("--out", default="tests/equiv/outputs/ir/_summary/ym2413_durations_summary.csv", help="Output CSV path")
    args = ap.parse_args()

    files = sorted(Path().glob(args.glob))
    if not files:
        print(f"No files matched: {args.glob}")
        return

    rows = [summarize_file(p) for p in files]
    out_df = pd.DataFrame(rows)
    out_df.sort_values("file", inplace=True)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_df.to_csv(out_path, index=False)
    print(f"Wrote summary: {out_path} ({len(out_df)} files)")

if __name__ == "__main__":
    main()