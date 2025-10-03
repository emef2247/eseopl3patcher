#!/usr/bin/env python3
import csv, os, sys, argparse

def main():
    ap = argparse.ArgumentParser(description="Create gates.csv from combined_best.csv scenarios (uniform gate per pattern/channel).")
    ap.add_argument("--combined", required=True, help="Path to combined_best.csv")
    ap.add_argument("--out", default="dist/gates.csv", help="Output gates.csv")
    ap.add_argument("--channels", type=int, default=9, help="Number of channels per pattern (default: 9 for OPLL)")
    ap.add_argument("--gate-default", type=float, default=0.80, help="Uniform gate value to assign (0..1)")
    args = ap.parse_args()

    if not os.path.exists(args.combined):
        print(f"[ERROR] not found: {args.combined}", file=sys.stderr)
        sys.exit(1)

    patterns = []
    with open(args.combined, newline="", encoding="utf-8") as f:
        r = csv.DictReader(f)
        headers = r.fieldnames or []
        if "scenario" not in headers:
            print(f"[ERROR] 'scenario' column not found. headers={headers}", file=sys.stderr)
            sys.exit(1)
        for row in r:
            s = (row.get("scenario") or "").strip()
            if s:
                patterns.append(s)

    if not patterns:
        print("[WARN] no scenarios found; nothing to write", file=sys.stderr)
        # それでも空ヘッダだけ書く
        os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
        with open(args.out, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=["pattern","channel","gate","notes","score"])
            w.writeheader()
        print(f"[DONE] wrote {args.out} with 0 rows")
        return

    rows_out = []
    g = max(0.0, min(1.0, float(args.gate_default)))
    for pat in patterns:
        for ch in range(args.channels):
            rows_out.append({
                "pattern": pat,
                "channel": ch,
                "gate": g,
                "notes": 0,
                "score": 0.0
            })

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["pattern","channel","gate","notes","score"])
        w.writeheader()
        w.writerows(rows_out)

    print(f"[DONE] wrote {args.out} with {len(rows_out)} rows (gate_default={g})")

if __name__ == "__main__":
    main()
