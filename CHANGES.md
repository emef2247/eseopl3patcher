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

## Gate Duration Tuning and Timbre Debugging Features

### 7. Timbre Debugging CLI Flags

**Added** new command-line flags for timbre analysis and debugging:

#### Voice Simplification
- `--voice-simplify-sine`: Forces both operators to sine wave (WS=0) and FB=0
  - Preserves modulator TL to allow hearing modulation effects
  - Useful for isolating harmonic content from waveform effects

#### Modulator Muting
- `--voice-debug-mute-mod`: Forces modulator TL=63 (complete mute)
  - Applied after all other mappings
  - Allows listening to carrier-only sound

#### INST=1 (Violin) Specific Overrides
- `--inst1-fb-override <0-7>`: Override feedback for INST=1
- `--inst1-tl-override <0-63>`: Override modulator TL for INST=1
- `--inst1-ws-override <0-7>`: Override wave select for both operators on INST=1

#### Mid-Note Parameter Changes
- `--mid-note-param-wait <samples>`: Insert wait before mid-note parameter changes
  - Helps reduce clicks during parameter updates
  - Default: 0 (disabled)

**Implementation:** Added fields to `DebugOpts` struct in `vgm_helpers.h` and implemented application logic in `ym2413_patch_convert.c`.

**Why:** Enables systematic timbre analysis and debugging, particularly for INST=1 (Violin) which is commonly used in YM2413 music.

### 8. Gate Duration Tuning Scripts

**Added** comprehensive bash scripts for gate duration tuning and analysis:

#### Core Tuning Scripts
- `scripts/tune_gate_duration.sh`: Single-file gate duration tuning
  - Sweeps gate values to match baseline duration
  - Outputs CSV: gate,seconds,delta_s,abs_delta_s,tempo_ratio
  - Default FREQSEQ=AB
  
- `scripts/tune_gate_batch.sh`: Batch gate duration tuning
  - Processes multiple VGMs from manifest
  - Generates summary.csv with best gate per file
  - Uses Python helper to prevent one-line CSV breakage
  - Selects best result by minimal abs_delta_s

- `scripts/tune_gate_preoffon.sh`: Stage-2 tuner
  - Sweeps pre-keyon and off-to-on wait parameters with fixed gate
  - For files where gate alone can't meet target duration
  - Outputs: pre,offon,seconds,delta_s,abs_delta_s,tempo_ratio

#### Timbre Analysis
- `scripts/timbre_sweep_inst1.sh`: INST=1 parameter sweep
  - Sweeps modulator TL, FB, and WS values
  - Supports --simplify and --mute-mod flags
  - Records all combinations to CSV for comparison

#### Helper Scripts
- `scripts/make_baseline_wav.sh`: Generate baseline WAV from VGM
  - Tries vgm2wav, vgmplay, or VGMPlay
  - Shows duration via ffprobe

- `scripts/make_ab_compare.sh`: A/B listening helper
  - Creates alternating segments from two WAVs
  - Uses ffmpeg concat demuxer (reliable on ffmpeg 4.4)
  - Forces s16/stereo/44.1kHz for consistency

#### Visualization
- `scripts/spectrogram_one.sh`: Generate single spectrogram
- `scripts/spectrogram_compare.sh`: Side-by-side spectrogram comparison
- Both use ffmpeg showspectrumpic, 0-8kHz range

#### Configuration
- `scripts/manifests/ym2413_problematic.txt`: Files for stage-2 tuning
  - Lists files where -g alone previously undershot target
  - Used by tune_gate_preoffon.sh
  
- `scripts/presets_from_summary.sh`: Generate gate_presets.csv
  - Merges batch summary CSV files
  - Creates unified preset configuration

#### Diagnostics
- `scripts/vgm_diff_opl3_regs.sh`: Compare OPL3 register writes
  - Uses vgm2txt to dump and diff TL/FB/WS registers
  - Confirms override flags are applied correctly

**Default Behavior:** All tuning scripts default to FREQSEQ=AB, consistent with hardware-verified frequency sequence.

**CSV Format:** All tuning scripts output CSV with headers including `abs_delta_s` and `tempo_ratio` for consistent analysis.

**Why:** Provides systematic workflow for tuning gate duration parameters and analyzing timbre variations, addressing the challenge of matching YM2413 timing behavior in OPL3 conversion.
