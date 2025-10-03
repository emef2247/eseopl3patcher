#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import csv
import argparse
from collections import defaultdict

def parse_args():
    ap = argparse.ArgumentParser(description="Analyze YMFM trace CSV and estimate per-session gate times.")
    ap.add_argument("csv", help="Path to ymfm_trace.csv")
    ap.add_argument("--silence-db", type=float, default=-105.0,
                    help="RMS dBFS threshold to consider a window 'silent' (default: -105.0)")
    ap.add_argument("--require-nz-zero", action="store_true", default=False,
                    help="Require nz==0 in addition to dB threshold")
    ap.add_argument("--required-silent-windows", type=int, default=1,
                    help="Consecutive silent windows required to declare overall silence (default: 1)")
    ap.add_argument("--print-windows", action="store_true", default=False,
                    help="Print per-window debug lines")
    return ap.parse_args()

def is_silent(row, silence_db, require_nz_zero):
    # Expect columns: session_id,ch,t_samples,t_ms,wait_samples,mean_abs,rms_db,nz,...,event
    try:
        rms_db = float(row.get("rms_db", "0"))
    except ValueError:
        rms_db = 0.0
    try:
        nz = int(row.get("nz", "0"))
    except ValueError:
        nz = 0
    cond_db = (rms_db <= silence_db)
    cond_nz = (nz == 0) if require_nz_zero else True
    return cond_db and cond_nz

def analyze(csv_path, silence_db, require_nz_zero, required_silent_windows, print_windows):
    sessions = defaultdict(list)

    with open(csv_path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            sid = row["session_id"]
            sessions[sid].append(row)

    results = []
    for sid, rows in sessions.items():
        # Consider only WAIT rows (event==WAIT)
        waits = [r for r in rows if r.get("event","") == "WAIT"]
        # Sort by t_samples just in case
        waits.sort(key=lambda r: int(float(r.get("t_samples","0"))))

        silent_streak = 0
        gate_t_ms = None
        for r in waits:
            t_ms = float(r.get("t_ms","0"))
            wait_samples = int(r.get("wait_samples","0"))
            rms_db = float(r.get("rms_db","0"))
            nz = int(r.get("nz","0"))
            silent = is_silent(r, silence_db, require_nz_zero)
            if print_windows:
                print(f"[SID {sid}] t={t_ms:.3f} ms wait={wait_samples} rms_db={rms_db:.2f} nz={nz} silent={silent}")
            if silent:
                silent_streak += 1
                if silent_streak >= required_silent_windows and gate_t_ms is None:
                    gate_t_ms = t_ms
            else:
                silent_streak = 0

        results.append({
            "session_id": sid,
            "gate_t_ms": gate_t_ms if gate_t_ms is not None else -1.0,
            "params": {
                "silence_db": silence_db,
                "require_nz_zero": require_nz_zero,
                "required_silent_windows": required_silent_windows
            }
        })
    return results

def main():
    args = parse_args()
    results = analyze(args.csv, args.silence_db, args.require_nz_zero, args.required_silent_windows, args.print_windows)
    print("session_id,gate_t_ms,silence_db,require_nz_zero,required_silent_windows")
    for r in results:
        p = r["params"]
        print(f"{r['session_id']},{r['gate_t_ms']:.6f},{p['silence_db']:.2f},{int(p['require_nz_zero'])},{p['required_silent_windows']}")

if __name__ == "__main__":
    main()