#!/usr/bin/env python3
from __future__ import annotations
import argparse
import csv
import json
import os
from typing import List, Dict, Any

def read_best_json(path: str) -> Dict[str, Any] | None:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None

def read_summary_csv(path: str) -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []
    try:
        with open(path, "r", encoding="utf-8") as f:
            r = csv.DictReader(f)
            for row in r:
                rows.append(row)
    except Exception:
        pass
    return rows

def write_csv(path: str, rows: List[Dict[str, Any]], fieldnames: List[str]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow(r)

def main():
    ap = argparse.ArgumentParser(description="Aggregate sweep results across scenarios")
    ap.add_argument("--root", required=True, help="Root dir that contains per-scenario subfolders (each with best_params.json, summary.csv)")
    ap.add_argument("--out", default=None, help="Output dir for combined CSVs (default: <root>/combined)")
    args = ap.parse_args()

    root = args.root
    out_dir = args.out or os.path.join(root, "combined")
    os.makedirs(out_dir, exist_ok=True)

    combined_best: List[Dict[str, Any]] = []
    combined_summary: List[Dict[str, Any]] = []

    # Enumerate subdirs under root
    for name in sorted(os.listdir(root)):
        scen_dir = os.path.join(root, name)
        if not os.path.isdir(scen_dir):
            continue
        best_path = os.path.join(scen_dir, "best_params.json")
        summ_path = os.path.join(scen_dir, "summary.csv")

        best = read_best_json(best_path)
        if best:
            row = {"scenario": name}
            row.update(best)
            combined_best.append(row)

        summary_rows = read_summary_csv(summ_path)
        for srow in summary_rows:
            srow_out = {"scenario": name}
            srow_out.update(srow)
            combined_summary.append(srow_out)

    # Write combined CSVs
    if combined_best:
        # Ensure stable columns
        keys = ["scenario"] + [k for k in combined_best[0].keys() if k != "scenario"]
        write_csv(os.path.join(out_dir, "combined_best.csv"), combined_best, fieldnames=keys)
        print(f"Wrote {len(combined_best)} scenarios to {os.path.join(out_dir, 'combined_best.csv')}")
    else:
        print("No best_params.json found under root")

    if combined_summary:
        keys = ["scenario"] + [k for k in combined_summary[0].keys() if k != "scenario"]
        write_csv(os.path.join(out_dir, "combined_summary.csv"), combined_summary, fieldnames=keys)
        print(f"Wrote {len(combined_summary)} rows to {os.path.join(out_dir, 'combined_summary.csv')}")
    else:
        print("No summary.csv found under root")

if __name__ == "__main__":
    main()