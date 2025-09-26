#!/usr/bin/env python3
"""
Audacity「周波数解析」風のスペクトルカーブを描画
- 複数WAVの重ね描き（ワイルドカード対応）
- 時間範囲の切り出し (--start/--end/--dur)
- STFT/Welch、窓、FFTサイズ、集計(中央値/平均/最大)
- dBFS表示/対数周波数軸/スムージング
- 正規化（ピーク揃え or 任意周波数で揃え）
- 帯域別レベル集計とCSV出力
"""
import argparse, os, glob
import numpy as np, matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy.signal import get_window, stft, welch

def read_wav_mono(path):
    sr, y = wavfile.read(path)
    if y.dtype == np.int16:
        y = y.astype(np.float32)/32768.0
    elif y.dtype == np.int32:
        y = y.astype(np.float32)/2147483648.0
    elif y.dtype == np.uint8:
        y = (y.astype(np.float32)-128.0)/128.0
    else:
        y = y.astype(np.float32)
    if y.ndim == 2:
        y = y.mean(axis=1)
    return sr, y

def slice_range(y, sr, start=None, end=None, dur=None):
    n = len(y)
    t0 = 0.0 if start is None else float(start)
    if dur is not None: t1 = t0 + float(dur)
    else:               t1 = (n / sr) if end is None else float(end)
    i0 = max(0, int(round(t0 * sr)))
    i1 = min(n, int(round(t1 * sr)))
    if i1 <= i0: i1 = min(n, i0 + 1)
    return y[i0:i1]

def spectrum_stft(sr, y, n_fft=1024, hop=None, window='hann', stat='max', ref=1.0):
    if hop is None: hop = n_fft // 4
    win = get_window(window, n_fft, fftbins=True).astype(np.float32)
    if len(y) < n_fft: y = np.pad(y, (0, n_fft-len(y)))
    f, tt, Zxx = stft(y, fs=sr, window=win, nperseg=n_fft, noverlap=n_fft-hop,
                      nfft=n_fft, boundary=None, padded=False)
    mag = np.abs(Zxx)
    if mag.size == 0:
        f = np.linspace(0, sr/2, n_fft//2+1)
        mag = np.zeros((len(f), 1), dtype=np.float32)
    if     stat == 'mean':   m = mag.mean(axis=1)
    elif   stat == 'median': m = np.median(mag, axis=1)
    else:                    m = mag.max(axis=1)
    m = np.maximum(m, 1e-12)
    db = 20.0 * np.log10(m / ref)
    return f, db

def spectrum_welch(sr, y, n_fft=1024, window='hann', ref=1.0, as_power=False):
    win = get_window(window, n_fft, fftbins=True).astype(np.float32)
    noverlap = n_fft // 2
    f, Pxx = welch(y, fs=sr, window=win, nperseg=n_fft, noverlap=noverlap, scaling='spectrum')
    Pxx = np.maximum(Pxx, 1e-24)
    if as_power: db = 10.0 * np.log10(Pxx)
    else:
        amp = np.sqrt(Pxx); amp = np.maximum(amp, 1e-12)
        db = 20.0 * np.log10(amp / ref)
    return f, db

def smooth_curve(y, bins=11):
    if bins <= 1: return y
    k = np.ones(bins, dtype=np.float32) / float(bins)
    return np.convolve(y, k, mode='same')

def format_db_axis(ax, vmin=-100, vmax=-20):
    ax.set_ylim(vmin, vmax)
    ax.set_ylabel('Level [dBFS]')
    ax.grid(True, which='both', axis='both', alpha=0.25)

def plot_curve(ax, f, db, label=None, color=None, fill=True,
               min_freq=20.0, max_freq=None, logx=True, linewidth=1.8, alpha=0.95):
    sel = f >= min_freq
    if max_freq is not None: sel &= (f <= max_freq)
    f2, db2 = f[sel], db[sel]
    line, = ax.plot(f2, db2, label=label, color=color, lw=linewidth, alpha=alpha)
    if fill and color is not None:
        ax.fill_between(f2, db2, ax.get_ylim()[0], color=color, alpha=0.15)
    if logx:
        ax.set_xscale('log')
        ax.set_xlim(max(min_freq, 20.0), max_freq if max_freq else (f2.max() if f2.size else 20000.0))
        ax.set_xlabel('Frequency [Hz] (log)')
    else:
        ax.set_xlabel('Frequency [Hz]')
    return line

def save_csv(path, f, db):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as w:
        w.write('freq_hz,level_db\n')
        for fi, di in zip(f, db):
            w.write(f'{fi:.6f},{di:.6f}\n')

def band_integrals(f, db, bands):
    pow_lin = 10.0 ** (db / 20.0)
    res = []
    for (lo, hi) in bands:
        sel = (f >= lo) & (f <= hi)
        if not np.any(sel):
            res.append((lo, hi, None)); continue
        integ = np.trapz(pow_lin[sel], f[sel])
        db_equiv = 20.0 * np.log10(max(integ, 1e-12))
        res.append((lo, hi, db_equiv))
    return res

def parse_bands(bands_str):
    bands = []
    for token in bands_str.split(','):
        token = token.strip()
        if not token: continue
        lo, hi = token.split('-')
        bands.append((float(lo), float(hi)))
    return bands

def find_db_at_freq(f, db, target_hz, width_frac=1/12):
    if target_hz is None or target_hz <= 0: return None
    lo = target_hz / (2 ** (width_frac / 2))
    hi = target_hz * (2 ** (width_frac / 2))
    sel = (f >= lo) & (f <= hi)
    if not np.any(sel): return None
    return float(np.max(db[sel]))

def main():
    ap = argparse.ArgumentParser(description='Render Audacity-like spectrum curve(s) from WAV')
    ap.add_argument('wavs', nargs='+', help='Input WAV(s); supports wildcards like analysis/*/wav/foo.wav')
    ap.add_argument('--out', required=True, help='Output PNG path')
    ap.add_argument('--start', type=float, default=None, help='Start time [s]')
    ap.add_argument('--end', type=float, default=None, help='End time [s]')
    ap.add_argument('--dur', type=float, default=None, help='Duration [s] (overrides end if set)')
    ap.add_argument('--mode', choices=['stft','welch'], default='stft')
    ap.add_argument('--stat', choices=['median','mean','max'], default='max', help='Aggregation across time (stft)')
    ap.add_argument('--n_fft', type=int, default=1024)
    ap.add_argument('--hop', type=int, default=None, help='Hop size (stft); default n_fft//4')
    ap.add_argument('--window', default='hann', help='Window name for scipy.signal.get_window')
    ap.add_argument('--power', action='store_true', help='Welch: 10*log10(power) instead of amplitude dBFS')
    ap.add_argument('--smooth', type=int, default=11, help='Smoothing bins along frequency')
    ap.add_argument('--min_freq', type=float, default=20.0)
    ap.add_argument('--max_freq', type=float, default=20000.0)
    ap.add_argument('--vmin', type=float, default=-100.0)
    ap.add_argument('--vmax', type=float, default=-20.0)
    ap.add_argument('--logx', action='store_true', help='Log frequency axis')
    ap.add_argument('--labels', default=None, help='Comma-separated labels for legend')
    ap.add_argument('--colors', default=None, help='Comma-separated colors (e.g., "#7b68ee,#e74c3c")')
    ap.add_argument('--no-fill', dest='no_fill', action='store_true', help='Disable area fill under curves')
    ap.add_argument('--dpi', type=int, default=150)
    ap.add_argument('--csv-dir', default=None, help='If set, write CSV per input')
    # 正規化
    ap.add_argument('--normalize', choices=['none','peak','at'], default='none',
                    help='Normalize curves: peak -> max=0dB, at -> use --norm-at Hz')
    ap.add_argument('--norm-at', dest='norm_at', type=float, default=None,
                    help='Frequency [Hz] to align when --normalize at')
    # 帯域サマリ
    ap.add_argument('--bands', default=None, help='Comma-separated bands, e.g. "20-200,200-1000,1000-5000,5000-20000"')
    ap.add_argument('--bands-csv', dest='bands_csv', default=None, help='Write band summaries to CSV')
    args = ap.parse_args()

    # Expand globs in positional wavs
    expanded_paths = []
    for p in args.wavs:
        m = sorted(glob.glob(p))
        if len(m) == 0:
            raise FileNotFoundError(f'No files match: {p}\nHint: check the timestamped analysis folder, or use a wildcard like analysis/*<label>*/wav/<file>.wav')
        expanded_paths.extend(m)

    labels = [s.strip() for s in args.labels.split(',')] if args.labels else None
    colors = [s.strip() for s in args.colors.split(',')] if args.colors else None

    curves = []
    for path in expanded_paths:
        sr, y = read_wav_mono(path)
        seg = slice_range(y, sr, start=args.start, end=args.end, dur=args.dur)
        if args.mode == 'welch':
            f, db = spectrum_welch(sr, seg, n_fft=args.n_fft, window=args.window, as_power=args.power)
        else:
            f, db = spectrum_stft(sr, seg, n_fft=args.n_fft, hop=args.hop, window=args.window, stat=args.stat)
        if args.smooth and args.smooth > 1:
            db = smooth_curve(db, bins=args.smooth)
        curves.append((path, f, db))

    # Normalization
    if args.normalize != 'none' and curves:
        if args.normalize == 'peak':
            base_peak = np.max(curves[0][2])
            curves = [(p, f, db - (np.max(db) - base_peak)) for (p, f, db) in curves]
        elif args.normalize == 'at':
            if args.norm_at is None:
                print('[WARN] --normalize at requires --norm-at <Hz>; skipping normalization.')
            else:
                base_val = None
                vals = []
                for (p, f, db) in curves:
                    v = find_db_at_freq(f, db, args.norm_at)
                    vals.append(v)
                    if base_val is None and v is not None:
                        base_val = v
                if base_val is not None:
                    curves = [(p, f, db if v is None else db - (v - base_val))
                              for (p, f, db), v in zip(curves, vals)]

    fig, ax = plt.subplots(1, 1, figsize=(10, 6), constrained_layout=True)
    format_db_axis(ax, vmin=args.vmin, vmax=args.vmax)

    for idx, (path, f, db) in enumerate(curves):
        label = labels[idx] if labels and idx < len(labels) else os.path.basename(path)
        color = colors[idx] if colors and idx < len(colors) else None
        plot_curve(ax, f, db, label=label, color=color,
                   fill=(not args.no_fill),
                   min_freq=args.min_freq, max_freq=args.max_freq, logx=args.logx)
        if args.csv_dir:
            base = os.path.splitext(os.path.basename(path))[0]
            save_csv(os.path.join(args.csv_dir, f'{base}_spectrum.csv'), f, db)

    if labels or len(curves) > 1:
        ax.legend(loc='best', framealpha=0.9)
    ax.set_title('Spectrum (Audacity-like)')

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    plt.savefig(args.out, dpi=args.dpi)
    print(f'[OK] saved: {args.out}')

    if args.bands and args.bands_csv:
        bands = parse_bands(args.bands)
        with open(args.bands_csv, 'w', encoding='utf-8') as w:
            w.write('file,band_low_hz,band_high_hz,level_db_equiv\n')
            for (path, f, db) in curves:
                for lo, hi, val in band_integrals(f, db, bands):
                    if val is not None:
                        w.write(f'{os.path.basename(path)},{lo},{hi},{val:.4f}\n')

if __name__ == '__main__':
    main()