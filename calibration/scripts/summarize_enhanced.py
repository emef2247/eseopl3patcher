#!/usr/bin/env python3
import json, glob, argparse, math, yaml, os, sys
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
CONF = BASE_DIR / "config.yaml"

def load_config():
    return yaml.safe_load(CONF.read_text(encoding="utf-8"))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pass", dest="pass_id", default="pass1")
    parser.add_argument("--warn", action="store_true")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    cfg = load_config()
    max_attack = cfg["analysis"]["max_attack_ms"]
    rel_h2_lo, rel_h2_hi = cfg["analysis"]["rel_h2_target_range"]
    rms_delta_max = cfg["analysis"]["rms200_500_delta_max_db"]

    report_glob = f"report_*_{args.pass_id}.json"
    REPORT_DIR = BASE_DIR / "reports"
    files = sorted(REPORT_DIR.glob(report_glob))

    rows = []
    warns = []
    for f in files:
        try:
            data = json.loads(f.read_text(encoding="utf-8"))
            for e in data:
                file = e["file"]
                a = e.get("attack90_ms")
                r200 = e.get("rms200_db")
                r500 = e.get("rms500_db")
                rel_h2 = e.get("harm_rel",{}).get("rel_h2")
                rel_h3 = e.get("harm_rel",{}).get("rel_h3")
                rows.append((f.name, file, a, r200, r500, rel_h2, rel_h3))

                if args.warn:
                    if a and a > max_attack:
                        warns.append(f"[ATTACK] {file} {a:.1f}ms > {max_attack}ms")
                    if rel_h2 is not None and (rel_h2 < rel_h2_lo or rel_h2 > rel_h2_hi):
                        warns.append(f"[H2] {file} rel_h2={rel_h2:.1f}dB outside [{rel_h2_lo},{rel_h2_hi}]")
                    if r200 is not None and r500 is not None and abs(r200-r500) > rms_delta_max:
                        warns.append(f"[RMSΔ] {file} Δ={abs(r200-r500):.1f}dB > {rms_delta_max}dB")
        except Exception as ex:
            print(f"[ERR] {f}: {ex}", file=sys.stderr)

    csv_lines = ["report,file,attack90_ms,rms200_db,rms500_db,rel_h2,rel_h3"]
    for r in rows:
        csv_lines.append(",".join(str(x) for x in r))

    out_txt = "\n".join(csv_lines)
    if args.out:
        Path(args.out).write_text(out_txt, encoding="utf-8")
    else:
        print(out_txt)

    if args.warn:
        print("\n# WARNINGS")
        if warns:
            for w in warns: print(w)
        else:
            print("None")

if __name__ == "__main__":
    main()