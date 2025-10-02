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