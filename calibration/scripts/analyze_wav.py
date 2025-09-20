#!/usr/bin/env python3
"""
Usage:
  analyze_wav.py F0 original.wav variant1.wav variant2.wav ...

機能:
  - onset (短時間RMS微分 + 閾値)
  - attack90 (onset から 90% peak 到達時間)
  - 200ms / 500ms 付近 RMS
  - 5倍音までの振幅（窓: ハニング, 解析区間: onset+固定）
  - rel_hX (h1 基準 dB 差)

注意:
  - 波形正規化は行わずそのまま
  - 窓長 / FFT サイズは簡易
"""
import sys, math, json, wave, struct
from statistics import mean

def read_wav_mono(path):
    with wave.open(path, 'rb') as w:
        ch = w.getnchannels()
        sr = w.getframerate()
        n = w.getnframes()
        data = w.readframes(n)
        if w.getsampwidth() != 2:
            raise ValueError("16bit PCM only")
        samples = struct.unpack('<' + 'h'*n*ch, data)
        if ch > 1:
            samples = samples[0::ch]
        return sr, [s/32768.0 for s in samples]

def short_rms(arr):
    return math.sqrt(sum(x*x for x in arr)/len(arr))

def detect_onset(samples, sr, win_ms=5, step_ms=2.5, diff_thresh=0.01):
    win = int(sr * win_ms/1000)
    step = int(sr * step_ms/1000)
    if win < 4: return 0
    rms_vals = []
    idxs = []
    for i in range(0, len(samples)-win, step):
        seg = samples[i:i+win]
        rms_vals.append(short_rms(seg))
        idxs.append(i)
    # 差分
    prev = rms_vals[0] if rms_vals else 0
    for i, v in enumerate(rms_vals[1:], start=1):
        dv = v - prev
        if dv > diff_thresh:
            return idxs[i]
        prev = v
    return 0

def attack90(samples, sr, onset_idx):
    # peak レベルを後半 70% 区間から取得
    peak = max(abs(x) for x in samples[onset_idx:])
    if peak <= 1e-9:
        return 0.0
    target = peak * 0.9
    for i in range(onset_idx, len(samples)):
        if abs(samples[i]) >= target:
            return (i - onset_idx) * 1000.0 / sr
    return (len(samples)-onset_idx) * 1000.0 / sr

def window_hanning(n):
    return [0.5 - 0.5*math.cos(2*math.pi*i/(n-1)) for i in range(n)]

def fft_mag(blk):
    # 簡易DFT (短窓想定) → 性能より簡潔さ
    N = len(blk)
    out = []
    for k in range(N//2+1):
        re = 0; im = 0
        for n, x in enumerate(blk):
            ang = -2*math.pi*k*n/N
            re += x*math.cos(ang)
            im += x*math.sin(ang)
        out.append(math.sqrt(re*re + im*im))
    return out

def extract_harmonics(samples, sr, f0, onset_idx, analyze_ms=120):
    # onset 後 analyze_ms の区間で FFT
    length = int(sr * analyze_ms/1000)
    seg = samples[onset_idx:onset_idx+length]
    if len(seg) < length//2:
        seg = samples
    if len(seg) < 128:
        return {}
    win = window_hanning(len(seg))
    blk = [a*b for a,b in zip(seg, win)]
    mags = fft_mag(blk)
    df = sr / len(seg)
    harmonics = {}
    for h in range(1,6):
        freq = f0 * h
        idx = int(round(freq/df))
        if 0 <= idx < len(mags):
            harmonics[f"h{h}"] = 20*math.log10(mags[idx]+1e-12)
    # relative
    h1 = harmonics.get("h1",-120.0)
    rel = {}
    for k,v in harmonics.items():
        if k == "h1": continue
        rel[f"rel_{k}"] = v - h1
    return harmonics, rel

def rms_at(samples, sr, ms_point=200, win_ms=30):
    center = int(sr * ms_point/1000)
    half = int(sr * win_ms/1000 / 2)
    a = max(0, center-half)
    b = min(len(samples), center+half)
    if b - a < 8:
        return -120.0
    return 20*math.log10(short_rms(samples[a:b])+1e-12)

def process_one(f0, path):
    sr, smp = read_wav_mono(path)
    onset = detect_onset(smp, sr)
    atk = attack90(smp, sr, onset)
    r200 = rms_at(smp, sr, 200)
    r500 = rms_at(smp, sr, 500)
    harm, rel = extract_harmonics(smp, sr, f0, onset)
    # rel key rename rel_h2 etc
    harm_rel = {}
    for k,v in rel.items():
        harm_rel[k.replace("rel_h","rel_h")] = v
    return {
        "file": path,
        "f0_used_hz": f0,
        "onset_sample": onset,
        "attack90_ms": atk,
        "rms200_db": r200,
        "rms500_db": r500,
        "harm": harm,
        "harm_rel": {
            f"rel_h{i}": harm.get(f"h{i}", -120.0) - harm.get("h1",-120.0)
            for i in range(2,6)
        }
    }

def main():
    if len(sys.argv) < 3:
        print("Usage: analyze_wav.py F0 orig.wav variant1.wav ...")
        sys.exit(1)
    f0 = float(sys.argv[1])
    files = sys.argv[2:]
    results = [ process_one(f0, f) for f in files ]
    print(json.dumps(results, indent=2))

if __name__ == "__main__":
    main()