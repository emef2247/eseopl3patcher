#!/usr/bin/env python3
import sys, math, json, pathlib
import numpy as np
import soundfile as sf

def load_wav(path):
    data, sr = sf.read(path)
    if data.ndim > 1:
        data = data.mean(axis=1)
    return data, sr

def attack_time_ms(x, sr, thresh=0.9):
    peak = np.max(np.abs(x))
    if peak <= 1e-9: return None
    tgt = peak * thresh
    idx = np.argmax(np.abs(x) >= tgt)
    return (idx / sr) * 1000.0

def rms_db(segment):
    if len(segment)==0: return -999.0
    rms = math.sqrt(np.mean(segment**2))
    return 20*math.log10(rms+1e-12)

def window_segment(x, sr, center_ms, width_ms=40):
    half = int(width_ms/1000*sr/2)
    c    = int(center_ms/1000*sr)
    s    = max(0, c-half)
    e    = min(len(x), c+half)
    return x[s:e]

def harmonic_levels(x, sr, f0, max_h=5):
    # 簡易: 短い窓でFFT -> 近傍最大
    win = x[: min(len(x), int(sr*0.25))]
    N = len(win)
    if N < 32: return {}
    fft = np.fft.rfft(win * np.hanning(N))
    freqs = np.fft.rfftfreq(N, 1/sr)
    out = {}
    for h in range(1, max_h+1):
        target = f0*h
        if target > freqs[-1]: break
        # ±2% 幅
        band = np.where((freqs >= target*0.98) & (freqs <= target*1.02))[0]
        if len(band)==0: continue
        mag = np.abs(fft[band])
        val = mag.max()
        out[f"h{h}"] = 20*math.log10(val + 1e-12)
    return out

def main():
    if len(sys.argv)<3:
        print("Usage: analyze_wav.py <f0_hz> file1.wav [file2.wav ...]")
        return
    f0 = float(sys.argv[1])
    results=[]
    for f in sys.argv[2:]:
        x,sr = load_wav(f)
        at   = attack_time_ms(x, sr)
        s200 = rms_db(window_segment(x,sr,200))
        s500 = rms_db(window_segment(x,sr,500))
        harms= harmonic_levels(x,sr,f0)
        results.append({
            "file": f,
            "attack90_ms": at,
            "rms200_db": s200,
            "rms500_db": s500,
            "harm": harms
        })
    print(json.dumps(results, indent=2))

if __name__=="__main__":
    main()