#!/usr/bin/env python3
import argparse, os, numpy as np, csv, warnings
from scipy.io import wavfile
import librosa, librosa.feature

FRAME_LEN = 2048
HOP = 256

def to_float_mono(sr, y):
    if y.ndim == 2: y = y.mean(axis=1)
    if y.dtype == np.int16:  y = y.astype(np.float32)/32768.0
    elif y.dtype == np.int32: y = y.astype(np.float32)/2147483648.0
    elif y.dtype == np.uint8: y = (y.astype(np.float32)-128.0)/128.0
    else: y = y.astype(np.float32)
    return sr, y

def segment_uniform(y, sr, n, start_s, dur_s):
    seg = y[int(start_s*sr): int((start_s+dur_s)*sr)]
    L = len(seg); step = max(1, L // n)
    out = []
    for i in range(n):
        s0 = i*step
        s1 = (i+1)*step if i < n-1 else L
        out.append(seg[s0:s1])
    return out

def segment_from_bounds(y, sr, start_s, bounds):
    base = int(start_s*sr)
    segs=[]
    n = len(y)
    for (t0,t1) in bounds:
        s0 = base + int(t0*sr)
        s1 = base + int(t1*sr)
        s0 = max(0, min(n-1, s0))
        s1 = max(s0+1, min(n, s1))
        segs.append(y[s0:s1])
    return segs

def load_bounds(bounds_csv):
    rows=[]
    with open(bounds_csv, 'r', encoding='utf-8') as f:
        for i,line in enumerate(f):
            if i==0 and line.strip().startswith('idx'): continue
            parts=line.strip().split(',')
            if len(parts)>=3:
                rows.append((float(parts[1]), float(parts[2])))
    return rows

def detect_pitch(y, sr, method='yin', fmin=100.0, fmax=1200.0):
    # 自動補正: 最低でも2周期がFRAME_LENに入るfminを確保
    fmin_lim = 2.0*sr/FRAME_LEN
    fmin_eff = max(fmin, fmin_lim)
    if fmin_eff > fmin:
        warnings.warn(f'fmin raised to {fmin_eff:.3f} to satisfy 2 cycles in frame (sr={sr}, frame={FRAME_LEN})')
    if method == 'pyin':
        f0, _, _ = librosa.pyin(y, fmin=fmin_eff, fmax=fmax, sr=sr, frame_length=FRAME_LEN, hop_length=HOP)
        hz = float(np.nanmedian(f0)) if f0 is not None else 0.0
    else:
        f0 = librosa.yin(y, fmin=fmin_eff, fmax=fmax, sr=sr, frame_length=FRAME_LEN, hop_length=HOP)
        hz = float(np.median(f0)) if f0 is not None and f0.size>0 else 0.0
    return hz if np.isfinite(hz) and hz>0 else 0.0

def midi_from_hz(hz): return 69.0 + 12.0*np.log2(hz/440.0) if hz>0 else np.nan
def hz_from_midi(m): return 440.0 * (2.0 ** ((m-69.0)/12.0))
def hz_to_cents(hz, ref_hz):
    if hz<=0 or ref_hz<=0: return np.nan
    return 1200.0*np.log2(hz/ref_hz)

def fit_chromatic_start(midi_list):
    idx = np.arange(len(midi_list))
    valid = np.isfinite(midi_list)
    if valid.sum() < 3: return None
    a = np.median(midi_list[valid] - idx[valid])
    return round(a)

def octave_correct(hz, exp_hz):
    if hz<=0 or exp_hz<=0: return hz
    best = hz; best_err = abs(hz_to_cents(hz, exp_hz))
    for k in (-2,-1,1,2):
        cand = hz * (2.0 ** k)
        err = abs(hz_to_cents(cand, exp_hz))
        if err < best_err:
            best_err, best = err, cand
    return best

def rms_band_db(y, sr, lo=5000, hi=20000, n_fft=4096, hop=1024):
    S = np.abs(librosa.stft(y, n_fft=n_fft, hop_length=hop))**2
    freqs = librosa.fft_frequencies(sr=sr, n_fft=n_fft)
    sel = (freqs>=lo) & (freqs<=hi)
    if not np.any(sel): return np.nan
    band_pow = np.mean(S[sel,:])
    return 10*np.log10(max(band_pow, 1e-20))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav', help='WAV path')
    ap.add_argument('--out', required=True, help='CSV out path')
    ap.add_argument('--start', type=float, default=0.9)
    ap.add_argument('--dur', type=float, default=2.2)
    ap.add_argument('--notes', type=int, default=14, help='expected number of notes')
    ap.add_argument('--seg', choices=['uniform','bounds'], default='bounds', help='bounds: use --bounds CSV; uniform: equal split')
    ap.add_argument('--bounds', default=None, help='CSV from onsets_to_csv.py (idx,t0_sec,t1_sec)')
    ap.add_argument('--method', choices=['pyin','yin'], default='yin')
    ap.add_argument('--start-midi', type=int, default=None)
    ap.add_argument('--auto-start', action='store_true')
    ap.add_argument('--fmin', type=float, default=100.0)
    ap.add_argument('--fmax', type=float, default=1200.0)
    ap.add_argument('--octave-correct', action='store_true')
    ap.add_argument('--hf-band', default='5000-20000')
    args = ap.parse_args()

    sr, y = wavfile.read(args.wav); sr, y = to_float_mono(sr, y)

    if args.seg == 'bounds' and args.bounds:
        bounds = load_bounds(args.bounds)
        segs = segment_from_bounds(y, sr, args.start, bounds)
    else:
        segs = segment_uniform(y, sr, args.notes, args.start, args.dur)

    meas_hz = np.array([detect_pitch(s, sr, args.method, args.fmin, args.fmax) for s in segs], dtype=float)
    meas_midi = np.array([midi_from_hz(h) for h in meas_hz], dtype=float)

    if args.start_midi is not None:
        start_midi = args.start_midi
    elif args.auto_start:
        est = fit_chromatic_start(meas_midi)
        start_midi = est if est is not None else 60
    else:
        start_midi = 60

    lo, hi = [float(x) for x in args.hf_band.split('-')]

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['idx','exp_midi','exp_hz','meas_hz','meas_midi','cents_err','hf_db'])
        for i,(s,hz,midi) in enumerate(zip(segs, meas_hz, meas_midi)):
            exp_m = start_midi + i
            exp_hz = hz_from_midi(exp_m)
            if args.octave_correct:
                hz = octave_correct(hz, exp_hz)
                midi = midi_from_hz(hz)
            cents = hz_to_cents(hz, exp_hz)
            hfdb = rms_band_db(s, sr, lo, hi)
            w.writerow([i, exp_m, f'{exp_hz:.6f}', f'{hz:.6f}', f'{midi:.3f}', f'{cents if np.isfinite(cents) else ""}', f'{hfdb if np.isfinite(hfdb) else ""}'])
    print(f'[OK] saved: {args.out} (start_midi={start_midi}, seg={args.seg}, notes={args.notes}, method={args.method})')

if __name__ == '__main__':
    main()