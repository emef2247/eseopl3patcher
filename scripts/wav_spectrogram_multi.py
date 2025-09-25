#!/usr/bin/env python3
import argparse, os, numpy as np, matplotlib.pyplot as plt
from scipy.io import wavfile

def read_wav_mono(path):
    sr, y = wavfile.read(path)
    if y.dtype == np.int16:
        y = y.astype(np.float32) / 32768.0
    elif y.dtype == np.int32:
        y = y.astype(np.float32) / 2147483648.0
    elif y.dtype == np.uint8:
        y = (y.astype(np.float32) - 128.0) / 128.0
    else:
        y = y.astype(np.float32)
    if y.ndim == 2:
        y = y.mean(axis=1)
    return sr, y

def stft(sr, y, n_fft=2048, hop=256):
    win = np.hanning(n_fft).astype(np.float32)
    if len(y) < n_fft:
        y = np.pad(y, (0, n_fft - len(y)))
    frames = np.lib.stride_tricks.sliding_window_view(y, n_fft)[::hop]
    frames = frames * win
    spec = np.fft.rfft(frames, n=n_fft, axis=1)
    mag = np.abs(spec).T
    mag_db = 20.0 * np.log10(mag + 1e-10)
    freqs = np.linspace(0, sr/2, mag_db.shape[0])
    times = np.arange(mag_db.shape[1]) * (hop / sr)
    return times, freqs, mag_db

def main():
    ap = argparse.ArgumentParser(description='Compare multiple WAV spectrograms in a grid')
    ap.add_argument('wavs', nargs='+', help='Input WAV paths (2-6 files recommended)')
    ap.add_argument('--out', default='compare_spec.png')
    ap.add_argument('--n_fft', type=int, default=2048)
    ap.add_argument('--hop', type=int, default=256)
    ap.add_argument('--max_freq', type=float, default=8000.0)
    ap.add_argument('--vmin', type=float, default=-100.0)
    ap.add_argument('--vmax', type=float, default=-20.0)
    ap.add_argument('--dpi', type=int, default=150)
    args = ap.parse_args()

    rows = len(args.wavs)
    fig, axes = plt.subplots(rows, 1, figsize=(12, 4*rows), constrained_layout=True)
    if rows == 1:
        axes = [axes]

    for ax, path in zip(axes, args.wavs):
        sr, y = read_wav_mono(path)
        times, freqs, mag_db = stft(sr, y, n_fft=args.n_fft, hop=args.hop)
        if args.max_freq is not None:
            sel = freqs <= args.max_freq
            freqs = freqs[sel]; mag_db = mag_db[sel, :]
        im = ax.imshow(mag_db, origin='lower', aspect='auto',
                       extent=[times[0], times[-1] if len(times)>1 else 0, freqs[0], freqs[-1]],
                       cmap='magma', vmin=args.vmin, vmax=args.vmax)
        ax.set_title(f'{os.path.basename(path)} (sr={sr})')
        ax.set_ylabel('Freq [Hz]')
    axes[-1].set_xlabel('Time [s]')
    cb = fig.colorbar(im, ax=axes, fraction=0.02, pad=0.01, label='dB')
    fig.savefig(args.out, dpi=args.dpi)
    print(f'[OK] saved: {args.out}')

if __name__ == '__main__':
    main()