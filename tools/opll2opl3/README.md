# OPLL (YM2413) → OPL3 (YMF262) 2-operator mapper

This module converts an OPLL timeline (register CSV) to an OPL3 2-operator register CSV, preserving musical intent and allowing optional “luxury” enhancements (pan, waveform choices, layering).

Status
- v0: Melodic 2-op mapping (mod→car), rough rate/TL maps, basic rhythm presets.
- Next: Fit rate maps using ymfm tables; refine TL/KSL/SL in level domain; pan and layering options.

Usage
```
python -m tools.opll2opl3.convert_opll_to_opl3 \
  --timeline-csv tests/equiv/inputs/ir/ym2413_scale_chromatic_timeline_YM2413.csv \
  --map-json tools/opll2opl3/tables/opl3_mapping_ymfm.json \
  --out-csv tests/equiv/outputs/opl3/scale_chromatic_opl3.csv
```

Design
- 2-op only (no 4-op).
- Fixed algorithm (modulator → carrier).
- OPLL waveforms mapped to OPL3 wave0 (sine) / wave1 (half-sine), customizable.
- Rhythm: map to OPL3 rhythm mode (0xBD) routing (BD=ch6 2ops; SD/HH=ch7; TOM/TC=ch8), with per-instrument presets.

Future work
- Replace static rate_map with fitted tables derived from ymfm timings to better match OPLL envelope times across BLK/KSR.
- Introduce precise TL/KSL/SL conversion in level (dB) domain with LUTs.
- Add pan/layer controls (config flags).