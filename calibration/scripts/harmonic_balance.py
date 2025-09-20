#!/usr/bin/env python3
"""
PASS1 の report_*_pass1.json を集約して rel_h2 を評価し、
Mod/Car TL の微調整と FB 変更提案を overrides/overrides_pass2.json に出力。

前提:
 - patcher が overrides JSON を読んで patch単位で TL/FB を差分適用できる拡張を将来実装
 - overrides JSON フォーマット（提案）:
   {
     "patch_overrides": {
        "1": {"mod_tl_delta": +4, "car_tl_delta": -2, "fb_delta": -1},
        ...
     },
     "meta": { "source": "harmonic_balance.py", "profile": "auto-v1" }
   }
"""
import json, glob, re, sys, math, os
from pathlib import Path
import yaml

BASE_DIR = Path(__file__).resolve().parent.parent
CONF = BASE_DIR / "config.yaml"
REPORT_DIR = BASE_DIR / "reports"
OUT_DIR = BASE_DIR / "overrides"
OUT_DIR.mkdir(exist_ok=True)
OUT_FILE = OUT_DIR / "overrides_pass2.json"

if not CONF.exists():
    print("[ERR] config.yaml missing", file=sys.stderr)
    sys.exit(1)

cfg = yaml.safe_load(CONF.read_text(encoding="utf-8"))

rel_h2_target_center = cfg["harmonic_balance"]["rel_h2_target_center_db"]
target_range = cfg["analysis"]["rel_h2_target_range"]
clip = cfg["harmonic_balance"]["rel_h2_clip_db"]
gain_factor = cfg["harmonic_balance"]["rel_h2_gain_factor"]
max_mod = cfg["harmonic_balance"]["max_mod_tl_delta"]
max_car = cfg["harmonic_balance"]["max_car_tl_delta"]
fb_cfg = cfg["harmonic_balance"]["feedback_adjust"]

# patch 番号推定: ファイル名の中に "patchXX" が無ければ仮に 1 を適用するなど。
# 現状は patch 単位情報が WAV に無いので「全体共通補正」または variant 毎補正に留める。
# ここでは variant 毎の rel_h2 平均から一括補正係数を提案する方式 (patcher 側で variant→patch map 適用を想定)
# 拡張余地: 解析スクリプトで per-patch WAV / JSON を出す。

# 簡易: ファイル名 p<base>_<variant>_pass1.wav → variant をキーに集計
variant_stats = {}  # variant -> list(rel_h2)
pattern = re.compile(r"p[0-9]{2}o4_(?P<variant>[a-z0-9]+)_pass1\.wav$")

for rep_json in sorted(REPORT_DIR.glob("report_*_pass1.json")):
    data = json.loads(rep_json.read_text(encoding="utf-8"))
    for entry in data:
        fname = entry.get("file","")
        m = pattern.match(fname)
        if not m:
            continue
        variant = m.group("variant")
        rel_h2 = entry.get("harm_rel", {}).get("rel_h2")
        if rel_h2 is None:
            continue
        variant_stats.setdefault(variant, []).append(rel_h2)

overrides = {"patch_overrides": {}, "meta": {
    "source": "harmonic_balance.py",
    "variant_mode": True,
    "note": "Per variant aggregate TL deltas"
}}

def clamp(v, lo, hi): return max(lo, min(hi, v))

for variant, values in variant_stats.items():
    avg_rel_h2 = sum(values)/len(values)
    # 目標中心との差分
    diff = avg_rel_h2 - rel_h2_target_center

    # 範囲内なら補正不要
    if target_range[0] <= avg_rel_h2 <= target_range[1]:
        continue

    # 過大な 2倍音 → Mod TL を上げる(=音量下げる) / Car TL を少し下げる（音量上げる）で深度軽減
    # diff > 0 (2倍音強すぎ)
    if diff > 0:
        # TL delta 計算 (丸め)
        mod_delta = clamp(int(math.ceil(diff * gain_factor)), 1, max_mod)
        car_delta = clamp(int(math.floor(-diff * gain_factor * 0.3)), -max_car, -1)  # Car TL 下げ（音量↑）は負
        fb_delta = 0
        if fb_cfg["enable"] and avg_rel_h2 > fb_cfg["threshold_db"]:
            fb_delta = -fb_cfg["reduce_step"]
        overrides["patch_overrides"][variant] = {
            "rel_h2_avg": round(avg_rel_h2,2),
            "action": "reduce_2nd_harm",
            "mod_tl_delta": mod_delta,
            "car_tl_delta": car_delta,
            "fb_delta": fb_delta
        }
    else:
        # diff < 0 (2倍音弱すぎ) → Mod TL を下げる(=音量上げる) or Car TL 上げる
        mod_delta = clamp(int(math.floor(diff * gain_factor)), -max_mod, -1)  # 負方向
        car_delta = clamp(int(math.ceil(-diff * gain_factor * 0.3)), 1, max_car)
        overrides["patch_overrides"][variant] = {
            "rel_h2_avg": round(avg_rel_h2,2),
            "action": "increase_2nd_harm",
            "mod_tl_delta": mod_delta,
            "car_tl_delta": car_delta,
            "fb_delta": 0
        }

OUT_FILE.write_text(json.dumps(overrides, indent=2), encoding="utf-8")
print(f"[OK] wrote {OUT_FILE} (variants processed: {len(variant_stats)})")