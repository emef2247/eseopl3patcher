# Gate auto-estimation for YM2413 (OPLL)

This tool estimates per-pattern/channel Gate values by simulating the YM2413 envelope (AR/DR/SL/RR, KSR) and KO transitions, then optimizing a Gate parameter to minimize overlap/gap and residual amplitude at note boundaries.

Goals
- Model-based: follow public YM2413 emulator behavior (ymfm, MAME YM2413, EMU2413).
- Pluggable rate tables: start with a parametric approximation; later swap in exact rate tables extracted from upstream emulator code.
- Non-destructive: produce CSV recommendations; external `-g` overrides still respected.

Inputs
- A YM2413 timeline CSV with per-write events (e.g., `..._timeline_YM2413.csv`) OR a small manifest JSON (`patterns.json`).
- Patch EG parameters passed via CLI (`--ar/--dr/--sl/--rr/--ksr`) when using timeline CSV.

Outputs
- CSV with recommended Gate per pattern/channel and summary metrics:
  - `residual_at_next_onset` (amplitude of previous note at the next onset)
  - `avg_overlap_gap_score`
  - `chosen_gate`

How it works
1) From timeline CSV, detect KO rising edges (register `0x20..0x28` bit4) to define note onsets.
2) Track FNUM low (`0x10..0x18`) and high+BLK (`0x20..0x28`) to attach pitch to each note.
3) Compute IOI from neighboring onsets and simulate EG (A→D→S while KO=1; R when KO=0).
4) Grid-search Gate in [gate_min, gate_max] and pick the best.

Run (timeline CSV)
```
python -m tools.gate_estimator.run_estimation \
  --timeline-csv tests/equiv/inputs/ir/ym2413_scale_chromatic_timeline_YM2413.csv \
  --pattern ym2413_scale_chromatic \
  --ar 8 --dr 6 --sl 8 --rr 5 --ksr 1 \
  --out tests/equiv/outputs/gate_estimates_scale_chromatic.csv
```

Run (manifest JSON)
```
python -m tools.gate_estimator.run_estimation \
  --ir-root tools/gate_estimator \
  --out tests/equiv/outputs/gate_estimates.csv
```

Caveats and TODOs
- Rate-time mapping is approximate; we will replace with exact rate tables later.
- If your timeline CSV uses different header names, adjust `loader_ir.py` or let us know the header row.
- For legato (KO stays high), boundary inference (pitch/volume) will be added later.

# Gate auto-estimation for YM2413 (OPLL)

This tool estimates per-pattern/channel Gate values by simulating the YM2413 envelope (AR/DR/SL/RR, KSR) and KO transitions, then optimizing a Gate parameter to minimize overlap/gap and residual amplitude at note boundaries.

## Models
- Parametric model (default): analytic time mapping from rate codes.
- Exact (table-driven) model: loads AR/DR/RR time tables and KSR scaling from JSON, for behavior closer to ymfm/MAME/EMU2413.

### Using the exact model
Prepare a JSON file with EG tables (see `ym2413_eg_tables.example.json`):
```json
{
  "eg_times_ms": {
    "attack":  [ /* 16 values (ms) */ ],
    "decay":   [ /* 16 values (ms) */ ],
    "release": [ /* 16 values (ms) */ ]
  },
  "ksr": {
    "per_blk_shift": [ /* 8 integers for blk=0..7 */ ]
  }
}
```
Run:
```
python -m tools.gate_estimator.run_estimation \
  --timeline-csv tests/equiv/inputs/ir/ym2413_scale_chromatic_timeline_YM2413.csv \
  --pattern ym2413_scale_chromatic \
  --ar 8 --dr 6 --sl 8 --rr 5 --ksr 1 \
  --eg-model exact \
  --eg-tables tools/gate_estimator/ym2413_eg_tables.example.json \
  --out tests/equiv/outputs/gate_estimates_scale_chromatic_exact.csv
```

Compare against parametric model:
```
python -m tools.gate_estimator.validate_exact_vs_param \
  --timeline-csv tests/equiv/inputs/ir/ym2413_scale_chromatic_timeline_YM2413.csv \
  --eg-tables tools/gate_estimator/ym2413_eg_tables.example.json
```

Notes
- The example JSON is provisional. Replace with values extracted from your chosen reference (ymfm/MAME/EMU2413).
- KSR handling is modeled as a per-block index shift; adjust if your reference differs.

## Existing CLI (parametric model)
```
python -m tools.gate_estimator.run_estimation \
  --timeline-csv tests/equiv/inputs/ir/ym2413_scale_chromatic_timeline_YM2413.csv \
  --pattern ym2413_scale_chromatic \
  --ar 8 --dr 6 --sl 8 --rr 5 --ksr 1 \
  --out tests/equiv/outputs/gate_estimates_scale_chromatic.csv
```

## Parameter sweep (coarse-to-fine)
See `sweep.py` and `scripts/auto_gate_sweep.sh` for coarse-to-fine sweeping of scoring parameters.
You can extend it to use the exact model if needed by constructing `YM2413EnvelopeExact` with your tables and calling its `choose_gate_grid`.