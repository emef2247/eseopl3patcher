#!/usr/bin/env python3
import csv, os, re, sys, glob

def pick_first(keys, row_or_headers):
    if isinstance(row_or_headers, dict):
        for k in keys:
            if k in row_or_headers and str(row_or_headers[k]).strip() != "":
                return k
        return None
    else:
        headers = row_or_headers or []
        for k in keys:
            if k in headers:
                return k
        return None

def normalize_gate(g, max_samples):
    try:
        g = float(g)
    except Exception:
        return None
    if 0.0 <= g <= 1.0:
        return g
    # サンプル数や固定グリッドの整数を 0..1 に正規化
    if g > 1.0:
        if g <= 8192.0:
            return min(1.0, max(0.0, g / 8192.0))
        if g <= 16384.0:
            return min(1.0, max(0.0, g / 16384.0))
        return min(1.0, max(0.0, g / float(max_samples)))
    return None

def latest_trials_csv(pattern_dir):
    files = glob.glob(os.path.join(pattern_dir, "iter_*_trials.csv"))
    best = None; best_idx = -1
    for f in files:
        m = re.search(r"iter_(\d+)_trials\.csv$", f)
        if not m: continue
        idx = int(m.group(1))
        if idx > best_idx:
            best_idx = idx; best = f
    return best

def main():
    import argparse
    ap = argparse.ArgumentParser(description="Extract per-channel best gates from iter_*_trials.csv to gates.csv")
    ap.add_argument("--root", required=True, help="Sweep root (contains pattern dirs and combined/combined_best.csv)")
    ap.add_argument("--combined", default=None, help="Path to combined_best.csv (if omitted, tries root/combined/combined_best.csv)")
    ap.add_argument("--out", default="dist/gates.csv", help="Output gates.csv")
    ap.add_argument("--max-gate-samples", type=int, default=16384, help="Max samples used to normalize integer gates")
    args = ap.parse_args()

    root = args.root
    combined = args.combined or os.path.join(root, "combined", "combined_best.csv")
    if not os.path.exists(combined):
        print(f"[ERROR] combined_best.csv not found: {combined}", file=sys.stderr)
        sys.exit(1)

    # scenario 列からパターン名一覧を取得
    patterns = []
    with open(combined, newline="", encoding="utf-8") as f:
        r = csv.DictReader(f)
        headers = r.fieldnames or []
        if "scenario" not in headers:
            print(f"[ERROR] 'scenario' column not found in {combined} headers={headers}", file=sys.stderr)
            sys.exit(1)
        for row in r:
            s = (row.get("scenario") or "").strip()
            if s:
                patterns.append(s)
    if not patterns:
        print("[WARN] No scenarios found in combined_best.csv")
        sys.exit(0)

    rows_out = []
    for pat in patterns:
        pat_dir = os.path.join(root, pat)
        trials = latest_trials_csv(pat_dir)
        if not trials or not os.path.exists(trials):
            print(f"[WARN] No trials CSV found for pattern: {pat} under {pat_dir}")
            continue

        with open(trials, newline="", encoding="utf-8") as f:
            r = csv.DictReader(f)
            headers = r.fieldnames or []
            # 候補列の自動検出
            ch_col = pick_first(["channel","ch","track","voice"], headers)
            gate_col = pick_first(["gate_frac","gate_norm","gate","best_gate","gate_samples"], headers)
            score_col = pick_first(["score","avg_score","fitness","objective"], headers)
            if not ch_col or not gate_col:
                print(f"[WARN] Missing required cols in {trials}: ch_col={ch_col}, gate_col={gate_col}, headers={headers}")
                continue

            best_per_ch = {}  # ch -> (score, gate_norm)
            for row in r:
                ch_str = str(row.get(ch_col, "")).strip()
                if ch_str == "": continue
                try:
                    ch = int(ch_str)
                except ValueError:
                    continue

                g_norm = normalize_gate(row.get(gate_col, ""), args.max_gate_samples)
                if g_norm is None: 
                    continue

                score = 0.0
                if score_col and str(row.get(score_col, "")).strip() != "":
                    try:
                        score = float(row.get(score_col))
                    except Exception:
                        score = 0.0

                prev = best_per_ch.get(ch)
                if prev is None or score > prev[0]:
                    best_per_ch[ch] = (score, g_norm)

            # 出力行
            for ch, (score, g) in sorted(best_per_ch.items()):
                rows_out.append({
                    "pattern": pat,
                    "channel": ch,
                    "gate": max(0.0, min(1.0, g)),
                    "notes": 0,
                    "score": score,
                })
        print(f"[OK] extracted {sum(1 for x in rows_out if x['pattern']==pat)} rows from {pat}")

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["pattern","channel","gate","notes","score"])
        w.writeheader()
        w.writerows(rows_out)

    print(f"[DONE] wrote {args.out} with {len(rows_out)} rows")

if __name__ == "__main__":
    main()
