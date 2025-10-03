# Analysis Pipeline (VGM → WAV → Spectrogram → Compare)

This pipeline automates:
- converting VGM with multiple min-gate values (audible-sanity)
- rendering WAV via vgmplay (or vgm2wav fallback)
- generating spectrogram images (basic and advanced)
- comparing each against a baseline WAV and stacking all results

## Directory Layout

```
analysis/<timestamp>_<label>/
  logs/
  vgm/
  wav/
  spec/
    single/    # per-WAV basic spectrograms
    adv/       # per-WAV advanced spectrograms (onset/envelope optional)
    compare/   # baseline vs. generated, side-by-side
  spec/compare_all.png   # stacked comparison of all generated WAVs
```

## Quick Start

1) Build your converter:
```
make clean && make
```

2) Run a gate sweep on an input VGM:
```
make gate-sweep IN=tests/equiv/inputs/ym2413_scale_chromatic.vgm \
                BASE=tests/equiv/inputs/ym2413_scale_chromatic.wav \
                LABEL=scale_chromatic
```

3) Or specify gate values inline:
```
make gate-sweep-gates IN=tests/equiv/inputs/ym2413_scale_chromatic.vgm \
                      GATES="8192 10240 12288" \
                      BASE=tests/equiv/inputs/ym2413_scale_chromatic.wav \
                      LABEL=scale_chromatic
```

Results are placed under `analysis/`.

## Script Options

You can call the script directly:
```
scripts/auto_analyze_vgm.sh -i <input.vgm> \
  -b <baseline.wav> \
  -l <label> \
  -g "8192 10240 12288" \
  --pre 16 --offon 16 --boost 8 --clamp 16 \
  --opl3-clock 14318180 \
  --adv-onset --adv-env
```

- `-i, --input`: input VGM path (required)
- `-b, --baseline`: baseline WAV to compare against (optional)
- `-l, --label`: label for the output folder (default: input basename)
- `-g, --gates`: space-separated min-gate sample values; if omitted, uses presets
- `--pre`: pre-keyon wait samples (default 16)
- `--offon`: min off→on wait samples (default 16)
- `--boost`: emergency boost steps (default 8)
- `--clamp`: carrier TL clamp (default 16)
- `--opl3-clock`: override YMF262 clock (e.g., 14318180)
- `--adv-onset`: overlay onset detection (requires librosa)
- `--adv-env`: overlay RMS envelope
- `--vgmplay`, `--vgm2wav`: paths to the renderers
- `--patcher`: path to eseopl3patcher (default: ./build/eseopl3patcher)
- `--no-strip`: do not strip unused chip clocks from the header

## Notes

- The converter must support runtime flags:
  - `--min-gate-samples`, `--pre-keyon-wait`, `--min-off-on-wait`, `--strip-unused-chips`, `--opl3-clock`.
  If not available, update the converter accordingly.
- `vgmplay` should support `-o <wav>` (WAV dump). If not present, the script falls back to `vgm2wav`.
- Fix spectrogram dynamic range via `--vmin/--vmax` if needed in the Python scripts.