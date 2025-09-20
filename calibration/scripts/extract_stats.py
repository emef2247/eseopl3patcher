#!/usr/bin/env python3
import sys, os, re, json, statistics, math

if len(sys.argv) < 4:
    print("Usage: extract_stats.py <STAT_DIR> <PER_FILE_JSON_DIR> <OUT_JSON>", file=sys.stderr)
    sys.exit(1)

STAT_DIR, PER_FILE_DIR, OUT_JSON = sys.argv[1:4]
os.makedirs(PER_FILE_DIR, exist_ok=True)

re_ratemap = re.compile(r'RATEMAP.*raw=([0-9]+).*?mapped=([0-9]+)')
re_shape   = re.compile(r'SHAPEFIX.*gap=([0-9]+)')
re_keyon   = re.compile(
    r'KeyOnDbg.*?fnum=([0-9]+)\s+block=([0-9]+).*?modTLraw=([0-9]+).*?carTLraw=([0-9]+).*?FB=([0-9]+)\s+CNT=([0-9]+)'
)
re_hz      = re.compile(r'hz=([0-9]+(?:\.[0-9]+)?)')

def inc(d, k, v=1):
    d[k] = d.get(k, 0) + v

global_rate_pairs = {}
global_shape_gaps = []
global_pitch       = {}
global_mod_tl      = {}
global_car_tl      = {}
global_fb          = {}
global_cnt         = {}
global_hz_hist     = {}
global_hz_bucket   = {}
HZ_BUCKET_SIZE = 25.0  # 25Hz ステップ例

per_file = {}

for fname in os.listdir(STAT_DIR):
    if not fname.endswith(".stats"):
        continue
    path = os.path.join(STAT_DIR, fname)
    base = fname[:-6]

    file_rate = {}
    file_shape_gaps = []
    file_pitch = {}
    file_mod_tl = {}
    file_car_tl = {}
    file_fb = {}
    file_cnt = {}
    file_hz = {}
    file_hz_bucket = {}
    keyon_count = 0

    with open(path, "r", errors="ignore") as f:
        for line in f:
            m_r = re_ratemap.search(line)
            if m_r:
                raw, mapped = m_r.group(1), m_r.group(2)
                inc(file_rate, f"{raw}->{mapped}")
            m_s = re_shape.search(line)
            if m_s:
                gap = int(m_s.group(1))
                file_shape_gaps.append(gap)
            m_k = re_keyon.search(line)
            if m_k:
                fnum = int(m_k.group(1))
                block = int(m_k.group(2))
                mod_tl = int(m_k.group(3))
                car_tl = int(m_k.group(4))
                fb = int(m_k.group(5))
                cnt = int(m_k.group(6))
                pitch_code = (block << 10) | fnum
                inc(file_pitch, pitch_code)
                inc(file_mod_tl, mod_tl)
                inc(file_car_tl, car_tl)
                inc(file_fb, fb)
                inc(file_cnt, cnt)
                keyon_count += 1
            m_hz = re_hz.search(line)
            if m_hz:
                hz = float(m_hz.group(1))
                hz_key = f"{hz:.3f}"
                inc(file_hz, hz_key)
                bucket = int(math.floor(hz / HZ_BUCKET_SIZE) * HZ_BUCKET_SIZE)
                bucket_key = f"{bucket:.0f}-{bucket+HZ_BUCKET_SIZE:.0f}"
                inc(file_hz_bucket, bucket_key)

    # 集約
    for k,v in file_rate.items(): inc(global_rate_pairs,k,v)
    for g in file_shape_gaps: global_shape_gaps.append(g)
    for src,dst in [
        (file_pitch, global_pitch),
        (file_mod_tl, global_mod_tl),
        (file_car_tl, global_car_tl),
        (file_fb, global_fb),
        (file_cnt, global_cnt),
        (file_hz, global_hz_hist),
        (file_hz_bucket, global_hz_bucket),
    ]:
        for k,v in src.items(): inc(dst,k,v)

    per_file[base] = {
        "rate_pairs": file_rate,
        "shape_gap_count": len(file_shape_gaps),
        "keyon_count": keyon_count,
        "pitch_code_hist": file_pitch,
        "mod_tl_hist": file_mod_tl,
        "car_tl_hist": file_car_tl,
        "fb_hist": file_fb,
        "cnt_hist": file_cnt,
        "hz_hist": file_hz,
        "hz_bucket_hist": file_hz_bucket
    }

    with open(os.path.join(PER_FILE_DIR, base + ".json"), "w") as pf:
        json.dump(per_file[base], pf, indent=2)

shape_stats = {}
if global_shape_gaps:
    import statistics
    shape_stats = {
        "count": len(global_shape_gaps),
        "min": min(global_shape_gaps),
        "max": max(global_shape_gaps),
        "mean": statistics.mean(global_shape_gaps),
        "median": statistics.median(global_shape_gaps),
    }

out = {
    "global": {
        "rate_pairs": global_rate_pairs,
        "shape": shape_stats,
        "pitch_code_hist": global_pitch,
        "mod_tl_hist": global_mod_tl,
        "car_tl_hist": global_car_tl,
        "fb_hist": global_fb,
        "cnt_hist": global_cnt,
        "hz_hist": global_hz_hist,
        "hz_bucket_hist": global_hz_bucket
    },
    "per_file": per_file
}

with open(OUT_JSON, "w") as w:
    json.dump(out, w, indent=2)

print(f"[extract_stats] files={len(per_file)} keyon_total={sum(p['keyon_count'] for p in per_file.values())}")