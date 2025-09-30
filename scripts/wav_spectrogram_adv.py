#!/usr/bin/env python3
import argparse, os, numpy as np, matplotlib.pyplot as plt
from scipy.io import wavfile
try:
    import librosa, librosa.display  # type: ignore
    HAVE_LIBROSA = True
except Exception:
    HAVE_LIBROSA = False

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

def stft_mag_db(sr, y, n_fft=2048, hop=256):
    win = np.hanning(n_fft).astype(np.float32)
    if len(y) < n_fft:
        y = np.pad(y, (0, n_fft - len(y)))
    frames = np.lib.stride_tricks.sliding_window_view(y, n_fft)[::hop]
    frames = frames * win
    spec = np.fft.rfft(frames, n=n_fft, axis=1)
    mag = np.abs(spec).T
    eps = 1e-10
    mag_db = 20.0 * np.log10(mag + eps)
    freqs = np.linspace(0, sr/2, mag_db.shape[0])
    times = np.arange(mag_db.shape[1]) * (hop / sr)
    return times, freqs, mag_db

def plot_spec(ax, times, freqs, mag_db, title, max_freq=None, cmap='magma', vmin=-100, vmax=-20):
    if max_freq is not None:
        sel = freqs <= max_freq
        freqs = freqs[sel]
        mag_db = mag_db[sel, :]
    im = ax.imshow(mag_db, origin='lower', aspect='auto',
                   extent=[times[0], times[-1] if len(times)>1 else 0, freqs[0], freqs[-1]],
                   cmap=cmap, vmin=vmin, vmax=vmax)
    ax.set_title(title)
    ax.set_ylabel('Freq [Hz]')
    ax.set_xlabel('Time [s]')
    cb = plt.colorbar(im, ax=ax, fraction=0.046, pad=0.02, label='dB')
    return im

def overlay_onsets(ax, y, sr, hop, color='cyan'):
    if not HAVE_LIBROSA:
        ax.text(0.02, 0.95, 'librosa not installed (no onsets)', transform=ax.transAxes,
                fontsize=9, color='orange', bbox=dict(facecolor='black', alpha=0.2))
        return
    oenv = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    onsets = librosa.onset.onset_detect(onset_envelope=oenv, sr=sr, hop_length=hop, backtrack=True)
    times = librosa.frames_to_time(onsets, sr=sr, hop_length=hop)
    for t in times:
        ax.axvline(t, color=color, alpha=0.7, lw=1.2)
    ax.text(0.02, 0.9, f'onsets: {len(times)}', transform=ax.transAxes, fontsize=9,
            color=color, bbox=dict(facecolor='black', alpha=0.2))

def overlay_env(ax, y, sr, color='white', alpha=0.6):
    t = np.arange(len(y))/sr
    # 簡易RMS
    win = max(1, int(0.002 * sr))  # 2ms
    kern = np.ones(win)/win
    rms = np.sqrt(np.convolve(y**2, kern, mode='same'))
    # 右側に第2縦軸で重ねる
    ax2 = ax.twinx()
    ax2.plot(t, (rms / (rms.max()+1e-9)) * (ax.get_ylim()[1]), color=color, alpha=alpha, lw=1.0)
    ax2.set_yticks([])
    ax2.set_ylabel('RMS (norm)', color=color)

def main():
    ap = argparse.ArgumentParser(description='Advanced WAV spectrogram with onset/envelope overlays')
    ap.add_argument('wav', help='Input WAV path')
    ap.add_argument('--out', default=None, help='Output PNG path')
    ap.add_argument('--n_fft', type=int, default=2048)
    ap.add_argument('--hop', type=int, default=256)
    ap.add_argument('--max_freq', type=float, default=8000.0)
    ap.add_argument('--vmin', type=float, default=-100.0)
    ap.add_argument('--vmax', type=float, default=-20.0)
    ap.add_argument('--cmap', default='magma')
    ap.add_argument('--onset', action='store_true', help='Overlay onset detection (librosa)')
    ap.add_argument('--env', action='store_true', help='Overlay RMS envelope')
    ap.add_argument('--dpi', type=int, default=150)
    args = ap.parse_args()

    sr, y = read_wav_mono(args.wav)
    times, freqs, mag_db = stft_mag_db(sr, y, n_fft=args.n_fft, hop=args.hop)

    fig, ax = plt.subplots(1, 1, figsize=(12, 4), constrained_layout=True)
    plot_spec(ax, times, freqs, mag_db, f'{os.path.basename(args.wav)} (sr={sr})',
              max_freq=args.max_freq, cmap=args.cmap, vmin=args.vmin, vmax=args.vmax)
    if args.env:
        overlay_env(ax, y, sr)
    if args.onset:
        overlay_onsets(ax, y, sr, hop=args.hop)

    out = args.out or (os.path.splitext(args.wav)[0] + '_spec_adv.png')
    plt.savefig(out, dpi=args.dpi)
    print(f'[OK] saved: {out}')

if __name__ == '__main__':
    main()