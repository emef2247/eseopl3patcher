#!/usr/bin/env python3
import argparse, os, glob, numpy as np, csv, warnings
from scipy.io import wavfile
import librosa

FRAME_LEN=2048; HOP=256

def to_float_mono(sr, y):
    if y.ndim == 2: y = y.mean(axis=1)
    if y.dtype == np.int16:  y = y.astype(np.float32)/32768.0
    elif y.dtype == np.int32: y = y.astype(np.float32)/2147483648.0
    elif y.dtype == np.uint8: y = (y.astype(np.float32)-128.0)/128.0
    else: y = y.astype(np.float32)
    return sr, y

def expand_one(path):
    m = sorted(glob.glob(path))
    if not m:
        raise FileNotFoundError(f'No files match: {path}')
    if len(m) > 1:
        # pick the first for determinism; tell the user
        warnings.warn(f'Multiple files match "{path}", using "{m[0]}"')
    return m[0]

def load_bounds(bounds_csv):
    rows=[]
    with open(bounds_csv, 'r', encoding='utf-8') as f:
        for i,line in enumerate(f):
            if i==0 and line.strip().startswith('idx'): continue
            parts=line.strip().split(',')
            if len(parts)>=3:
                rows.append((float(parts[1]), float(parts[2])))
    if not rows:
        raise ValueError(f'No bounds found in {bounds_csv}')
    return rows

def segment_from_bounds(y, sr, start_s, bounds):
    base = int(start_s*sr); n=len(y); out=[]
    for (t0,t1) in bounds:
        s0 = base + int(t0*sr)
        s1 = base + int(t1*sr)
        s0 = max(0, min(n-1, s0))
        s1 = max(s0+1, min(n, s1))
        out.append(y[s0:s1])
    return out

def yin_pitch(y, sr, fmin, fmax):
    fmin_lim = 2.0*sr/FRAME_LEN
    fmin_eff = max(fmin, fmin_lim)
    if fmin_eff > fmin:
        warnings.warn(f'fmin raised to {fmin_eff:.3f} to satisfy 2 cycles (sr={sr})')
    f0 = librosa.yin(y, fmin=fmin_eff, fmax=fmax, sr=sr, frame_length=FRAME_LEN, hop_length=HOP)
    hz = float(np.median(f0)) if f0 is not None and f0.size>0 else 0.0
    return hz if np.isfinite(hz) and hz>0 else 0.0

def midi_from_hz(hz): return 69.0 + 12.0*np.log2(hz/440.0) if hz>0 else np.nan

def analyze(path, start, bounds, fmin, fmax):
    path = expand_one(path)
    sr, y = wavfile.read(path); sr, y = to_float_mono(sr, y)
    segs = segment_from_bounds(y, sr, start, bounds)
    hz = np.array([yin_pitch(s, sr, fmin, fmax) for s in segs], dtype=float)
    midi = np.array([midi_from_hz(h) for h in hz], dtype=float)
    base = midi[0] if np.isfinite(midi[0]) else np.nanmedian(midi[np.isfinite(midi)])
    delta = midi - base
    return hz, midi, delta

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav_base')
    ap.add_argument('wav_test')
    ap.add_argument('--bounds', required=True, help='CSV from onsets_to_csv.py')
    ap.add_argument('--out', required=True)
    ap.add_argument('--start', type=float, default=0.9)
    ap.add_argument('--fmin', type=float, default=40.0)
    ap.add_argument('--fmax', type=float, default=1200.0)
    args = ap.parse_args()

    try:
        bounds = load_bounds(args.bounds)
        hz0, midi0, d0 = analyze(args.wav_base, args.start, bounds, args.fmin, args.fmax)
        hz1, midi1, d1 = analyze(args.wav_test, args.start, bounds, args.fmin, args.fmax)
    except Exception as e:
        print(f'[ERROR] {e}')
        raise

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['idx','base_hz','test_hz','base_midi','test_midi','base_delta_semitone','test_delta_semitone','delta_diff_semitone'])
        for i in range(len(bounds)):
            b = d0[i] if i<len(d0) else np.nan
            t = d1[i] if i<len(d1) else np.nan
            w.writerow([i,
                        f'{hz0[i]:.6f}' if i<len(hz0) else '',
                        f'{hz1[i]:.6f}' if i<len(hz1) else '',
                        f'{midi0[i]:.3f}' if i<len(midi0) else '',
                        f'{midi1[i]:.3f}' if i<len(midi1) else '',
                        f'{b:.3f}' if np.isfinite(b) else '',
                        f'{t:.3f}' if np.isfinite(t) else '',
                        f'{(t-b):.3f}' if np.isfinite(b) and np.isfinite(t) else ''])
    print(f'[OK] saved: {args.out}')

if __name__ == '__main__':
    main()