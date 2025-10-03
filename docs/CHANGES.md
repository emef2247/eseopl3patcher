# OPL3 Patcher Changes

## Overview
This document describes the changes made to fix OPL3 VGM conversion issues that prevented playback on real hardware.

## Root Cause
The YMF262 (OPL3) clock field in the VGM header was being written as 0, which caused silent playback on real hardware. Additionally, the frequency write sequence and initialization needed optimization based on real hardware verification.

## Changes Implemented

### 1. VGM Header Post-Processing (`vgm_header.c/h`)

**Added Function:** `vgm_header_postprocess()`

This function ensures proper clock values in the VGM header after conversion:
- **Always sets YMF262 clock** to either the override value or default `OPL3_CLOCK` (14318182 Hz)
- **Zeroes source chip clocks** (YM3812/YM3526/Y8950) to indicate they're no longer used
- Provides optional `strip_unused` parameter for future enhancement

**Why:** Fixes the root cause of silent playback - YMF262 clock must never be 0.

### 2. Main Flow Update (`main.c`)

**Changed:** Replaced manual clock setting with `vgm_header_postprocess()` call

Before:
```c
set_ymf262_clock(p_header_buf, OPL3_CLOCK);
if (chip_flags.has_ym3812) {
    set_ym3812_clock(p_header_buf, 0);
}
// ... repeated for other chips
```

After:
```c
vgm_header_postprocess(p_header_buf, &chip_flags, 0, false);
```

**Why:** Cleaner, more maintainable code with centralized header logic.

### 3. Frequency Sequence Mode (`opl3_convert.c`)

**Added:**
- `FreqSeqMode` enum: `FREQSEQ_AB` and `FREQSEQ_BAB`
- Static variable `g_freqseq_mode` defaulting to `FREQSEQ_AB`
- `init_freqseq_mode()` function to initialize from environment variable
- Environment variable `ESEOPL3_FREQSEQ` support:
  - Set to 'A' or 'AB' for AB mode (default)
  - Set to 'B' or 'BAB' for BAB mode

**Why:** AB sequence (A→B) was verified to work correctly on real hardware, making it the optimal default.

### 4. Frequency Write Pattern (`opl3_convert.c`)

**Updated:** `duplicate_write_opl3()` function for B0-B8 register writes

Now uses AB pattern for both ports:
- **Port0:** A(lsb) → B(msb)
- **Port1:** A(lsb detuned) → B(msb detuned)

Skips B write for rhythm-mode channels (6-8, 15-17) on port1.

**Added debug logging:**
```
[SEQ0] ch=0 mode=AB A=9F B=02 (rhythm=0) port0: A(9F)->B(02)
[SEQ1] ch=0 mode=AB A=9F B=02 (rhythm=0) port1: A(9F)->B(02)
```

**Why:** Hardware-verified sequence ensures proper frequency updates.

### 5. Zero-Length Wait Skipping (`vgm_helpers.c`)

**Updated:** `vgm_wait_samples()` function

Now skips writing wait commands when `samples == 0`:
```c
if (samples == 0) {
    return;
}
```

**Why:** Zero-length waits are unnecessary and verified to be skippable on real hardware.

### 6. OPL3 Initialization Sequence (`opl3_convert.c`)

**Updated:** `opl3_init()` function

Emits proper initialization sequence:
1. Port1: 0x105=0x01 (OPL3 enable)
2. Port1: 0x104=0x00 (4-OP mode off at start)
3. Port0: 0x001=0x00 (LSI TEST)
4. Port0: 0x008=0x00 (CSM off, note select off)
5. Port1: 0x101=0x00 (LSI TEST on port1 as well)
6. Channel panning defaults (C0-C8 on both ports)
7. Waveform select registers (E0-EF, F0-F5) initialized to 0

**Added:** Call to `init_freqseq_mode()` at initialization
**Added:** Debug print showing selected FREQSEQ mode

**Why:** Ensures proper OPL3 chip initialization for hardware compatibility.

## Environment Variables

### ESEOPL3_FREQSEQ
Controls the frequency write sequence mode:
- **Unset or 'A'/'AB':** Use AB mode (A→B) - Default and recommended
- **'B'/'BAB':** Use BAB mode (B→A→B) - Alternative mode

Example:
```bash
ESEOPL3_FREQSEQ=AB ./eseopl3patcher input.vgm 1.0
```

## Debug Output

When running with `-verbose` flag, you'll see:
```
[FREQSEQ] selected=AB (ESEOPL3_FREQSEQ=(unset))
[SEQ0] ch=0 mode=AB A=81 B=31 (rhythm=0) port0: A(81)->B(31)
[SEQ1] ch=0 mode=AB A=85 B=31 (rhythm=0) port1: A(85)->B(31)
```

## Backward Compatibility

All changes maintain backward compatibility:
- Existing command-line arguments unchanged
- Default behavior improved (AB mode, proper clocks)
- No breaking changes to file formats or APIs

## Testing

All changes have been tested with:
- Header postprocessing verification
- FREQSEQ mode selection
- Zero-length wait skipping
- OPL3 initialization sequence
- AB frequency write pattern
- Actual VGM file conversion

See test results in commit messages and PR description.

## References

- VGM file format specification
- YMF262 (OPL3) hardware documentation
- Real hardware verification results
