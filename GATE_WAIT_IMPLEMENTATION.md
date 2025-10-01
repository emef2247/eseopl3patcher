# Gate Wait Implementation

This document describes the gate wait implementation that addresses issues #15 and #16.

## Problem Statement

The original implementation had gate waits in a wrapper pre-hook, but they were not being applied in the "triple path" (acc_maybe_flush_triple) which is the active code path. This resulted in:
- `[GATE WAIT]` logs not appearing
- VGM header `total_samples` not changing with parameter sweeps
- Near-silent output until very large min_gate values (>=96)

## Solution

Gate waits are now wired directly into the triple/flush controller path, specifically:
- Before KeyOn commits
- Before KeyOff commits
- At the exact control points: `[KEYON_ARM]`, `[DELAY_KEYOFF_*]`, `[TRIPLE_FLUSH]`, `[FLUSH]`

## Implementation Details

### 1. Gate Tracker Structure

Added `OpllNoteGateTracker` to track per-channel gate timing:

```c
typedef struct {
    uint32_t last_keyon;          // Sample timestamp of last KeyOn
    uint32_t last_keyoff;         // Sample timestamp of last KeyOff
    uint8_t  key_is_on;           // Current key state
    // Statistics
    uint32_t total_pre_wait;
    uint32_t total_off_on_wait;
    uint32_t total_min_gate_wait;
    uint32_t count_pre;
    uint32_t count_off_on;
    uint32_t count_min_gate;
} OpllNoteGateTracker;
```

### 2. Gate Wait Insertion Points

#### In `acc_maybe_flush_triple()` (Triple Path)

**Before KeyOff:**
- Calculate `min_gate_need = max(0, min_gate_samples - held_time)`
- If `min_gate_need > 0`, insert wait and log `[GATE WAIT] ch=%d min_gate=%u (held=%u)`
- Update `last_keyoff` timestamp

**Before KeyOn:**
- Calculate `off_on_need = max(0, min_off_on_wait - time_since_keyoff)`
- Calculate `pre_keyon_need = pre_keyon_wait_samples`
- Insert waits (off_on first, then pre_keyon)
- Log `[GATE WAIT] ch=%d pre=%u off_on=%u` when any wait is inserted
- Update `last_keyon` timestamp after KeyOn commit

#### In `flush_channel_ch()` (Regular Path)

- Updates gate tracker at KeyOn/KeyOff commit points
- Maintains consistency between triple and regular paths

#### In DELAY_KEYOFF_FLUSH

- Updates `last_keyoff` timestamp when delayed KeyOff is flushed

### 3. Timestamp-Based Calculation

Uses `VGMStatus.total_samples` (absolute sample count) instead of elapsed counters:
- More reliable across different code paths
- Automatically accounts for wait commands
- Simpler logic without manual increment/saturation

### 4. Logging

**During Conversion (--verbose):**
- `[GATE WAIT] ch=%d min_gate=%u (held=%u)` - before KeyOff
- `[GATE WAIT] ch=%d pre=%u off_on=%u` - before KeyOn
- Only logged when waits are actually inserted

**End-of-Run Summary:**
```
[GATE WAIT SUMMARY]
  ch=0: pre=512 samples (4 times), off_on=256 samples (4 times), min_gate=1024 samples (4 times)
  ch=1: pre=256 samples (2 times), off_on=0 samples (0 times), min_gate=512 samples (2 times)
```

### 5. Scripts

#### scripts/render_spectrogram.sh
- Detects vgm2wav and vgmplay availability
- Checks vgmplay -o support via `vgmplay -?` output
- Prefers vgm2wav unless `PREFER_VGMPLAY=1`
- Robust against different vgmplay implementations
- Generates spectrograms using wav_spectrogram.py

#### scripts/auto_analyze_vgm.sh
- Performs gate parameter sweeps
- Converts VGM with different gate values
- Renders WAVs and spectrograms for each
- Compares against baseline if provided

## Usage

### Basic Conversion

```bash
./build/eseopl3patcher input.vgm 0 0 "" \
  -o output.vgm \
  --convert-ym2413 \
  --audible-sanity \
  --min-gate 8192 \
  --pre-keyon-wait 128 \
  --min-off-on-wait 64 \
  --verbose
```

### Parameter Sweep

```bash
scripts/auto_analyze_vgm.sh \
  -i input.vgm \
  -l test_label \
  -o analysis \
  -g "0 2048 4096 8192 16384"
```

### Spectrogram Rendering

```bash
scripts/render_spectrogram.sh -o analysis input.vgm
```

## Testing

### Acceptance Criteria

1. ✓ Gate waits inserted in triple path (acc_maybe_flush_triple)
2. ✓ `[GATE WAIT]` logs appear when waits are inserted
3. ✓ VGM header `total_samples` varies with gate parameters
4. ✓ End-of-run summary shows per-channel statistics
5. ✓ Scripts detect vgm2wav/vgmplay capabilities
6. ✓ Code compiles without errors

### Manual Verification

1. Check VGM total_samples changes:
```bash
# Without gate waits
./build/eseopl3patcher input.vgm 0 0 "" -o out1.vgm --convert-ym2413
xxd -s 0x18 -l 4 -e -g 4 out1.vgm

# With gate waits
./build/eseopl3patcher input.vgm 0 0 "" -o out2.vgm --convert-ym2413 --audible-sanity --min-gate 8192
xxd -s 0x18 -l 4 -e -g 4 out2.vgm
```

The second file should have larger total_samples.

2. Check for `[GATE WAIT]` logs:
```bash
./build/eseopl3patcher input.vgm 0 0 "" -o output.vgm \
  --convert-ym2413 --audible-sanity --min-gate 4096 --verbose 2>&1 | grep "GATE WAIT"
```

Should show gate wait logs and summary.

## Files Modified

- `src/opll/opll_to_opl3_wrapper.c` - Main implementation
- `src/opll/opll_to_opl3_wrapper.h` - Added `opll_print_gate_summary()`
- `src/main.c` - Call summary function at end
- `scripts/render_spectrogram.sh` - New
- `scripts/auto_analyze_vgm.sh` - New

## References

- Issue #15: Gate wait tracking issues
- Issue #16: render_spectrogram.sh robustness
