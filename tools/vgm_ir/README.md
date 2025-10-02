# vgm_ir: OPLL gate extraction (new)

What’s new
- YM2413 (OPLL) Gate extraction from IR
  - Build KeyOn/KeyOff gates per channel (0..8)
  - Inside each gate, build slices split on:
    - f: frequency/block changes (fL/fHBK)
    - i: instrument changes
    - v: volume changes
- CLI: `--opll-gates` writes two CSVs:
  - `<stem>_opll_gates.csv`
  - `<stem>_opll_gate_slices.csv`

CSV formats
- Gates
```
#type,ch,on_tick,on_time_s,off_tick,off_time_s,dur_ticks,dur_samples,
fnum0,block0,inst0,vol0,on_samples,off_samples
```
- Slices inside gates
```
#type,ch,gate_index,start_tick,start_time_s,end_tick,end_time_s,dur_ticks,dur_samples,
fnum,block,inst,vol,start_samples,end_samples
```
Notes
- Timebase aligns with openMSX vgmrecorder:
  - time = samples / 44100.0
  - ticks = ceil(time*60); ticks==1 → 0
- Rhythm (reg 0x0E) is not treated as a gate here. Use `_log.opll_rhythm.csv` for drum hits.

Usage examples
```bash
# Per-write timeline by chip and OPLL gates
python -m tools.vgm_ir.vgm2ir tests/equiv/inputs/your_opll_song.vgm \
  --out analysis/ir --timeline-per-write-split --opll-gates --opll-log
```

Implementation notes
- Gates are opened on KO 0→1 and closed on 1→0 from regs 0x10..0x18.
- Slices start at gate-on; subsequent fL/fHBK/iv writes on the same channel split slices (kind f/i/v/iv).
- If a gate remains open at EOF it is closed at `total_samples`.
- This is a “derived” backend utility; the front-end capture remains objective.

Tuning ideas (future)
- Debounce for very short KO toggles
- Heuristics for re-attack vs portamento while KO=1
- Rhythm hits as separate slice stream