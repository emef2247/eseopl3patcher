# Gate auto-estimation for YM2413 (OPLL)

This tool estimates per-pattern/channel Gate values by simulating the YM2413 envelope (AR/DR/SL/RR, KSR) and KO transitions, then optimizing a Gate parameter to minimize overlap/gap and residual amplitude at note boundaries.

Goals
- Model-based: follow public YM2413 emulator behavior (ymfm, MAME YM2413, EMU2413).
- Pluggable rate tables: start with a parametric approximation; later swap in exact rate tables extracted from upstream emulator code.
- Non-destructive: produce CSV recommendations; external `-g` overrides still respected.

Inputs
- A small manifest JSON (patterns.json) describing notes per pattern/channel:
  - Patch EG parameters (AR/DR/SL/RR, KSR flag)
  - Note list with `t_on` (sec), `ioi` (sec), `fnum`, `blk`
- Next step: add a loader to read your existing IR/CSV under `tests/equiv/outputs/ir` directly.

Outputs
- CSV with recommended Gate per pattern/channel and summary metrics:
  - `residual_at_next_onset` (amplitude of previous note at the next onset)
  - `avg_overlap_gap_score`
  - `chosen_gate`

How it works
1) Build a per-note timeline: onset t_on, inter-onset interval IOI, candidate KO_off = t_on + gate * IOI.
2) Simulate envelope states (A→D→S while KO=1; R when KO=0) with KSR-adjusted rates.
3) Score Gate by:
   - Residual amplitude just before next onset (smaller is better).
   - Overlap/gap penalties (overlap if residual > threshold; gap if sustain is unnaturally short).
   - Stability constraints (avoid zero/negative effective duration).
4) Grid-search Gate in [gate_min, gate_max] and pick the best.

Caveats and TODOs
- Initial rate-time mapping is an approximation; replace with exact rate tables later.
- KSR mapping is simplified; provide a config hook to calibrate.
- For legato (KO stays high), note boundaries are inferred from pitch changes (>= semitone) and vol=0 events.

References (for future alignment)
- ymfm (Aaron Giles)
- MAME YM2413 core
- EMU2413 (Okazaki)

Run
```
python -m tools.gate_estimator.run_estimation \
  --ir-root tools/gate_estimator \
  --out tests/equiv/outputs/gate_estimates.csv
```

Configuration
- Defaults are reasonable; tune via CLI flags or editing constants in model.py and optimizer.py.