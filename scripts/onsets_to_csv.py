#!/usr/bin/env python3
import argparse, os, numpy as np
from scipy.io import wavfile
import librosa

def to_float_mono(sr, y):
    if y.ndim == 2: y = y.mean(axis=1)
    if y.dtype == np.int16:  y = y.astype(np.float32)/32768.0
    elif y.dtype == np.int32: y = y.astype(np.float32)/2147483648.0
    elif y.dtype == np.uint8: y = (y.astype(np.float32)-128.0)/128.0
    else: y = y.astype(np.float32)
    return sr, y

def detect_onsets(y, sr, start, dur, max_notes=None, min_seg_ms=30):
    y = y[int(start*sr): int((start+dur)*sr)]
    hop = 256
    oenv = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    onsets = librosa.onset.onset_detect(onset_envelope=oenv, sr=sr, hop_length=hop, backtrack=True, units='time')
    if len(onsets) == 0:
        return [(0.0, len(y)/sr)]
    bounds = list(onsets)
    if bounds[0] > 0.02: bounds = [0.0] + bounds
    if bounds[-1] < len(y)/sr - 0.02: bounds = bounds + [len(y)/sr]
    segs=[]
    for i in range(len(bounds)-1):
        t0, t1 = float(bounds[i]), float(bounds[i+1])
        if (t1 - t0) * 1000.0 >= min_seg_ms:
            segs.append((t0, t1))
        if max_notes and len(segs) >= max_notes:
            break
    if not segs:
        segs=[(0.0, len(y)/sr)]
    return segs

def force_to_notes(segs, target):
    # 長いセグメントから二分割していき、ちょうど target に合わせる
    if len(segs) >= target: return segs[:target]
    import heapq
    # (長さ, -index) 最大長優先（安定化のため -index を加味）
    heap = [(-(t1-t0), -i, (t0,t1)) for i,(t0,t1) in enumerate(segs)]
    heapq.heapify(heap)
    while len(heap) < target:
        L, ni, (t0,t1) = heapq.heappop(heap)
        L = -L
        mid = (t0+t1)/2.0
        left=(t0, mid); right=(mid, t1)
        heapq.heappush(heap, (-(left[1]-left[0]), ni-0.1, left))
        heapq.heappush(heap, (-(right[1]-right[0]), ni-0.2, right))
    # 取り出して時刻順に
    res=[item[2] for item in heap]
    res.sort(key=lambda x: x[0])
    return res[:target]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav')
    ap.add_argument('--out', required=True)
    ap.add_argument('--start', type=float, default=0.9)
    ap.add_argument('--dur', type=float, default=2.2)
    ap.add_argument('--notes', type=int, default=14)
    ap.add_argument('--min-ms', type=float, default=30.0, help='minimum segment length [ms]')
    ap.add_argument('--force-notes', action='store_true', help='force output to exactly --notes by splitting longest segments')
    args = ap.parse_args()

    sr, y = wavfile.read(args.wav)
    sr, y = to_float_mono(sr, y)
    segs = detect_onsets(y, sr, args.start, args.dur, max_notes=args.notes, min_seg_ms=args.min_ms)
    if args.force_notes:
        segs = force_to_notes(segs, args.notes)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'w', encoding='utf-8') as f:
        f.write('idx,t0_sec,t1_sec\n')
        for i,(t0,t1) in enumerate(segs):
            f.write(f'{i},{t0:.6f},{t1:.6f}\n')
    print(f'[OK] saved: {args.out} ({len(segs)} segments)')

if __name__ == '__main__':
    main()