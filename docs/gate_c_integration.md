# Gate estimation: C integration guide

Purpose
- Use Python-estimated Gate values in C runtime without re-running the estimator.

Artifacts
- gates.csv (required): list of recommended Gate per pattern/channel
- model_params.json (optional): sweep/estimation parameters for traceability

CSV schema (gates.csv)
- Columns:
  - pattern: string (must match your runtime pattern identifier)
  - channel: integer (0-based channel index)
  - gate: float in [0.0, 1.0]
  - notes: integer count used in estimation (optional)
  - score: float (optional)
- Example:
  pattern,channel,gate,notes,score
  ym2413_scale_chromatic_timeline_YM2413,0,0.82,64,0.0123
  ym2413_short_pulses_timeline_YM2413,0,0.75,40,0.0345

How to export (Python)
- From a list of timeline CSVs, produce gates.csv:
```
python -m tools.gate_estimator.export_for_c \
  --eg-model exact \
  --eg-tables tools/gate_estimator/ym2413_eg_tables_ymfm.json \
  --out-csv dist/gates.csv \
  tests/equiv/inputs/ir/ym2413_scale_chromatic_timeline_YM2413.csv \
  tests/equiv/inputs/ir/ym2413_short_pulses_timeline_YM2413.csv
```
- Or via a file list:
```
python -m tools.gate_estimator.export_for_c \
  --list tests/equiv/inputs/ir/list_ym2413.txt \
  --eg-model exact \
  --eg-tables tools/gate_estimator/ym2413_eg_tables_ymfm.json \
  --out-csv dist/gates.csv
```

Using in C
- Load at startup:
  - gatecfg_load("dist/gates.csv", &cfg);
- Lookup per voice:
  - float g = gatecfg_lookup(&cfg, pattern_name, ch, default_gate);
  - float t_off = gate_compute_off_time(t_on, t_next_on, g);
- Cleanup:
  - gatecfg_free(&cfg);

Notes
- Ensure the pattern identifier used at runtime matches the "pattern" written in gates.csv (the default is the timeline CSV basename without extension).
- If a (pattern,channel) is missing, the C API returns `default_gate`.