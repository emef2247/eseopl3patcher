# Gate Compensation Implementation Notes

## Summary
This implementation adds a gate-compensation system to the OPLL→OPL3 conversion process that keeps song length consistent with the input VGM file while inserting necessary waits for proper note triggering.

## Problem Statement
The OPLL emulation requires inserting waits in certain situations:
1. Before KeyOn (pre-KeyOn wait) to allow parameters to latch
2. Before KeyOn (off→on minimum) to prevent KeyOff→KeyOn collision
3. Before KeyOff (min-gate) to ensure notes play long enough

Without compensation, these inserted waits would cause the output VGM to be longer than the input, making songs play slower than intended.

## Solution Design

### 1. Global Debt Tracking
- Added `g_gate_comp_debt_samples` variable (uint32_t) in opll_to_opl3_wrapper.c
- Tracks accumulated "debt" from all inserted waits
- Initialized to 0 in `opll_init()`

### 2. Debt Accumulation
When OPLL-specific logic inserts a wait, it:
1. Calls `vgm_wait_samples()` to emit the wait
2. Adds the wait amount to `g_gate_comp_debt_samples`
3. Logs `[GATE WAIT]` with details (if --debug-verbose)

Wait insertion points updated:
- **Pre-KeyOn wait**: Line ~937 in flush_channel_ch()
- **Off→On minimum**: Line ~815 in acc_maybe_flush_triple()
- **Min-gate (3 locations)**:
  - Line ~788 in acc_maybe_flush_triple() (triple path)
  - Line ~977 in flush_channel_ch() (note-off edge)
  - Line ~1213 in opll_write_register() (direct B0 write)

### 3. Debt Compensation
The `duplicate_write_opl3()` function compensates debt from input-derived waits:

```c
// At function tail:
uint32_t *p_debt = opll_get_gate_comp_debt_ptr();

// Add any opl3_keyon_wait to debt
if (keyon_wait_inserted > 0) {
    *p_debt += keyon_wait_inserted;
}

// Compensate from next_wait_samples
if (next_wait_samples > 0 && *p_debt > 0) {
    uint16_t compensation = min(next_wait_samples, *p_debt);
    uint16_t adjusted = next_wait_samples - compensation;
    *p_debt -= compensation;
    
    if (adjusted > 0) {
        vgm_wait_samples(p_music_data, p_vstat, adjusted);
    }
}
```

Key aspects:
- Only compensates when there's both debt and a wait to compensate
- Cannot over-compensate (min ensures we don't subtract more than available)
- Carries over remaining debt if compensation > next_wait
- Never emits zero-length waits (vgm_wait_samples already handles this)

### 4. Function Signature Changes

**duplicate_write_opl3()** now accepts `next_wait_samples`:
```c
int duplicate_write_opl3(
    VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t reg, uint8_t val,
    const CommandOptions *opts,
    uint16_t next_wait_samples  // NEW
);
```

**Call Site Updates:**
- Internal calls (within opll_to_opl3_wrapper.c): pass 0
- External calls (main.c for YM3812/YM3526/Y8950): pass 0
- OPLL calls: pass next_wait_samples from VGM stream when available

The reason most calls pass 0 is that compensation should only happen at the "final" wait emission point - the VGM stream level. Internal operations shouldn't trigger compensation.

## Code Flow Example

### Without Compensation (Before)
```
Input VGM: Note On → Wait 1000 → Note Off → Wait 1000 → ...
           ↓
Processing: Note On → Insert Pre-KeyOn Wait 100 → Wait 1000 → Note Off → ...
           ↓
Output VGM: Total waits = 100 + 1000 + ... = 1100+ samples (LONGER than input)
```

### With Compensation (After)
```
Input VGM: Note On → Wait 1000 → Note Off → Wait 1000 → ...
           ↓
Processing: Note On → Insert Pre-KeyOn Wait 100 (debt += 100) → 
            Compensate Wait (1000 - 100 = 900, debt = 0) → Note Off → ...
           ↓
Output VGM: Total waits = 100 + 900 + ... = 1000 samples (SAME as input)
```

## Debug Output

With `--debug-verbose` enabled:

```
[GATE WAIT] ch=0 pre=256         # Pre-KeyOn wait inserted, debt += 256
[GATE COMP] next_wait=1000 -> adjusted=744, debt_left=0  # Compensated 256 from 1000
[GATE WAIT] ch=1 min_gate=128    # Min-gate wait inserted, debt += 128
[GATE COMP] next_wait=500 -> adjusted=372, debt_left=0   # Compensated 128 from 500
```

## Minimal Change Philosophy

This implementation follows the "surgical changes" principle:
- No new files added (except documentation)
- Only modified existing code at specific injection points
- Preserved all existing logs and behavior
- Used existing infrastructure (vgm_wait_samples, etc.)
- No changes to non-OPLL code paths
- All changes are additive (no deletions except log text changes)

## Testing Recommendations

1. **Parameter Sweep Test**
   ```bash
   ./eseopl3patcher input.vgm output.vgm \
       --convert-ym2413 --audible-sanity \
       --min-gate 8192 --pre-keyon-wait 256 --min-off-on-wait 128 \
       --debug-verbose
   ```
   - Check debug output for [GATE WAIT] and [GATE COMP] logs
   - Verify output VGM total_samples ≈ input total_samples
   - Try different gate values and verify compensation adjusts

2. **Audibility Test**
   ```bash
   # Low gate values (should be choppy)
   ./eseopl3patcher input.vgm low_gate.vgm \
       --convert-ym2413 --audible-sanity --min-gate 32

   # High gate values (should be more legato)
   ./eseopl3patcher input.vgm high_gate.vgm \
       --convert-ym2413 --audible-sanity --min-gate 16384
   ```
   - Convert to WAV and verify note gating differences
   - Verify both files have similar total length

3. **Header Verification**
   ```bash
   # Check total_samples in VGM header
   xxd input.vgm | grep "0000010:"
   xxd output.vgm | grep "0000010:"
   ```
   - Bytes 0x18-0x1B contain total_samples (little-endian)
   - Output should be close to input (within a few hundred samples)

## Known Limitations

1. **Global Debt**: Debt is shared across all channels. In theory, per-channel debt would be more precise, but global debt is simpler and works well in practice.

2. **End-of-File Debt**: If debt remains at EOF (no more input waits to compensate), the output will be slightly shorter. This is acceptable as it's typically a small amount.

3. **Compensation Granularity**: Compensation happens at wait command boundaries in the VGM stream. Very short waits between commands may accumulate small rounding errors.

## Related Issues

- Issue #15: Gate handling and note triggering
- Issue #16: VGM length consistency
- Issue #17: Duplicate write wait compensation

## Future Enhancements

Possible improvements (not implemented):
- Per-channel debt tracking
- End-of-file debt emission as final wait
- Compensation statistics (total debt accumulated, total compensated)
- Optional compensation disable flag for testing
