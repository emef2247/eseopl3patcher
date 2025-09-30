#!/usr/bin/env python3
import argparse
import os
import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile

# optional librosa
try:
    import librosa
    import librosa.display  # type: ignore
    HAVE_LIBROSA = True
except Exception:
    HAVE_LIBROSA = False

def read_wav_mono(path):
    sr, y = wavfile.read(path)
    # int16/24/32 -> float
    if y.dtype == np.int16:
        y = y.astype(np.float32) / 32768.0
    elif y.dtype == np.int32:
        y = y.astype(np.float32) / 2147483648.0
    elif y.dtype == np.uint8:
        y = (y.astype(np.float32) - 128.0) / 128.0
    else:
        y = y.astype(np.float32)
    # mono化
    if y.ndim == 2:
        y = y.mean(axis=1)
    return sr, y

def stft_spectrogram(sr, y, n_fft=4096, hop_length=512, max_freq=None):
    # Hann window
    window = np.hanning(n_fft).astype(np.float32)
    # フレーム化
    n_frames = 1 + (len(y) - n_fft) // hop_length if len(y) >= n_fft else 0
    if n_frames <= 0:
        # パディングして最低1フレーム出す
        pad = n_fft - len(y) if len(y) < n_fft else 0
        y = np.pad(y, (0, pad), mode='constant')
        n_frames = 1 + (len(y) - n_fft) // hop_length
    frames = np.lib.stride_tricks.sliding_window_view(y, n_fft)[::hop_length]
    frames = frames * window
    # FFT
    spec = np.fft.rfft(frames, n=n_fft, axis=1)
    mag = np.abs(spec).T  # [freq_bins, time]
    # dB表示
    eps = 1e-10
    mag_db = 20.0 * np.log10(mag + eps)
    # 周波数軸制限
    freqs = np.linspace(0, sr/2, mag_db.shape[0])
    if max_freq is not None and max_freq < sr/2:
        idx = freqs <= max_freq
        mag_db = mag_db[idx, :]
        freqs = freqs[idx]
    times = np.arange(mag_db.shape[1]) * (hop_length / sr)
    return times, freqs, mag_db

def plot_single(ax, sr, y, title, n_fft, hop, max_freq, cmap, vmin, vmax, use_cqt):
    times, freqs, mag_db = stft_spectrogram(sr, y, n_fft=n_fft, hop_length=hop, max_freq=max_freq)
    im = ax.imshow(mag_db, origin='lower', aspect='auto',
                   extent=[times[0], times[-1] if len(times)>1 else 0, freqs[0], freqs[-1]],
                   cmap=cmap, vmin=vmin, vmax=vmax)
    ax.set_ylabel('Freq [Hz]')
    ax.set_title(title)
    ax.set_xlabel('Time [s]')
    plt.colorbar(im, ax=ax, fraction=0.046, pad=0.02, label='dB')

    if use_cqt and HAVE_LIBROSA:
        # CQTを右側に重ねず、セカンダリ軸で簡易表示（任意）
        ax_cqt = ax.twinx()
        y_lib = y.astype(np.float32)
        C = librosa.cqt(y_lib, sr=sr, hop_length=hop, fmin=librosa.note_to_hz('C2'),
                        n_bins=72, bins_per_octave=12)
        C_db = librosa.amplitude_to_db(np.abs(C), ref=np.max)
        # 横軸合わせのためタイムを近似
        t2 = np.arange(C_db.shape[1]) * (hop / sr)
        ax_cqt.imshow(C_db, origin='lower', aspect='auto',
                      extent=[t2[0], t2[-1] if len(t2)>1 else 0, 0, C_db.shape[0]],
                      cmap='viridis', alpha=0.25)
        ax_cqt.set_yticks([])
        ax_cqt.set_ylabel('CQT bins', color='gray')

def main():
    ap = argparse.ArgumentParser(description='WAV spectrogram (STFT/CQT) renderer')
    ap.add_argument('wav', help='Input WAV path')
    ap.add_argument('--out', default=None, help='Output PNG path')
    ap.add_argument('--compare', default=None, help='Second WAV to compare (2-row figure)')
    ap.add_argument('--title', default=None, help='Title for the first plot')
    ap.add_argument('--title2', default=None, help='Title for the second plot')
    ap.add_argument('--n_fft', type=int, default=4096)
    ap.add_argument('--hop', type=int, default=512)
    ap.add_argument('--max_freq', type=float, default=8000.0, help='Max freq to display (Hz)')
    ap.add_argument('--cmap', default='magma')
    ap.add_argument('--vmin', type=float, default=-100.0)
    ap.add_argument('--vmax', type=float, default=-20.0)
    ap.add_argument('--dpi', type=int, default=150)
    ap.add_argument('--cqt', action='store_true', help='Overlay CQT (requires librosa)')
    args = ap.parse_args()

    sr1, y1 = read_wav_mono(args.wav)
    t1 = args.title or f'{os.path.basename(args.wav)} (sr={sr1})'

    if args.compare:
        sr2, y2 = read_wav_mono(args.compare)
        t2 = args.title2 or f'{os.path.basename(args.compare)} (sr={sr2})'
        fig, axes = plt.subplots(2, 1, figsize=(12, 8), constrained_layout=True)
        plot_single(axes[0], sr1, y1, t1, args.n_fft, args.hop, args.max_freq, args.cmap, args.vmin, args.vmax, args.cqt)
        plot_single(axes[1], sr2, y2, t2, args.n_fft, args.hop, args.max_freq, args.cmap, args.vmin, args.vmax, args.cqt)
    else:
        fig, ax = plt.subplots(1, 1, figsize=(12, 4), constrained_layout=True)
        plot_single(ax, sr1, y1, t1, args.n_fft, args.hop, args.max_freq, args.cmap, args.vmin, args.vmax, args.cqt)

    out_path = args.out or (os.path.splitext(args.wav)[0] + '_spec.png')
    plt.savefig(out_path, dpi=args.dpi)
    print(f'[OK] saved: {out_path}')

if __name__ == '__main__':
    main()