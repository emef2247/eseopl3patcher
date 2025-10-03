# Gate auto-estimation for YM2413 (OPLL)

This tool estimates per-pattern/channel Gate values by simulating the YM2413 envelope (AR/DR/SL/RR, KSR) and KO transitions, then optimizing a Gate parameter to minimize overlap/gap and residual amplitude at note boundaries.

## Features

- **dB-domain scoring**: Residual amplitude is measured in decibels for better perceptual accuracy
- **Wider gate search range**: Searches from 0.50 to 0.98 (previously 0.60-0.98)
- **Shorter provisional release**: Uses 0.7× IOI for sustain evaluation (previously 0.8×)
- **CSV/IR loader**: Can directly load durations from `tests/equiv/outputs/ir/*.csv`
- **Parametric EG model**: Simulates YM2413 envelope behavior with configurable rate tables
- **Grid search optimization**: Finds optimal gate value for each pattern/channel

## Goals

- Model-based: follow public YM2413 emulator behavior (ymfm, MAME YM2413, EMU2413).
- Pluggable rate tables: start with a parametric approximation; later swap in exact rate tables extracted from upstream emulator code.
- Non-destructive: produce CSV recommendations; external `-g` overrides still respected.

## Inputs

### CSV Mode (Recommended)
Load durations CSV files directly from IR extraction:
- Uses files from `tests/equiv/outputs/ir/*_durations.csv`
- Automatically applies YM2413 ROM patch parameters (15 presets)
- Extracts note timing, pitch (fnum/blk), and inter-onset intervals

### JSON Mode (Legacy)
Load from a manifest JSON (patterns.json):
- Patch EG parameters (AR/DR/SL/RR, KSR flag)
- Note list with `t_on` (sec), `ioi` (sec), `fnum`, `blk`

## Outputs

CSV with recommended Gate per pattern/channel and summary metrics:
- `pattern`: Pattern/file name
- `channel`: Channel number (0-8)
- `gate`: Recommended gate ratio (0.50-0.98)
- `avg_residual_db`: Average residual amplitude at next onset (dB)
- `overlap_events`: Count of notes with residual above threshold
- `avg_sustain_loss`: Average sustain shortfall during KO=1 phase
- `score`: Combined optimization score

## How it works

1) Build a per-note timeline: onset t_on, inter-onset interval IOI, candidate KO_off = t_on + gate × IOI
2) Simulate envelope states (A→D→S while KO=1; R when KO=0) with KSR-adjusted rates
3) Score Gate by:
   - **Residual amplitude** (dB) just before next onset (smaller is better)
   - **Overlap/gap penalties** (overlap if residual > -40dB threshold; gap if sustain too short)
   - **Stability constraints** (avoid zero/negative effective duration)
4) Grid-search Gate in [0.50, 0.98] with 0.01 steps and pick the best

## Usage

### Load from CSV directory (recommended)
```bash
python -m tools.gate_estimator.run_estimation \
  --csv-dir tests/equiv/outputs/ir \
  --csv-pattern "*_durations.csv" \
  --inst 2 \
  --out results/gate_estimates.csv
```

### Load specific CSV file
```bash
python -m tools.gate_estimator.run_estimation \
  --csv-dir tests/equiv/outputs/ir \
  --csv-pattern "ym2413_scale_chromatic_durations.csv" \
  --inst 1 \
  --out results/violin_gates.csv
```

### Legacy JSON mode
```bash
python -m tools.gate_estimator.run_estimation \
  --ir-root tools/gate_estimator \
  --out results/gate_estimates.csv
```

## Command-line Options

- `--csv-dir DIR`: Directory containing durations CSV files
- `--csv-pattern PATTERN`: Glob pattern for CSV files (default: `*_durations.csv`)
- `--inst N`: YM2413 instrument/patch number (0-15, default: 2=Guitar)
  - 0: User-defined
  - 1: Violin
  - 2: Guitar
  - 3: Piano
  - 4: Flute
  - 5: Clarinet
  - 6: Oboe
  - 7: Trumpet
  - 8: Organ
  - 9: Horn
  - 10: Synthesizer
  - 11: Harpsichord
  - 12: Vibraphone
  - 13: Synth Bass
  - 14: Acoustic Bass
  - 15: Electric Guitar
- `--ir-root DIR`: Root folder with patterns.json manifest (legacy mode)
- `--out PATH`: Output CSV path for gate estimates (required)

## Configuration

Default parameters can be adjusted in `model.py`:
- `gate_min`, `gate_max`, `gate_step`: Gate search range and resolution
- `residual_threshold_db`: Overlap detection threshold (default: -40dB)
- `overlap_weight`, `gap_weight`: Scoring weights
- Envelope model parameters: `base_attack_ms`, `base_decay_ms`, `base_release_ms`, `exp_shape`, etc.

## Caveats and TODOs

- Initial rate-time mapping is an approximation; replace with exact rate tables later
- KSR mapping is simplified; provide a config hook to calibrate
- For legato (KO stays high), note boundaries are inferred from pitch changes (≥ semitone) and vol=0 events
- Rhythm mode channels not yet fully supported

## References (for future alignment)

- ymfm (Aaron Giles)
- MAME YM2413 core
- EMU2413 (Okazaki)