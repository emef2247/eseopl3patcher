# Gate Compensation Design - Changelog

## Overview
This update implements a gate-compensation system to keep VGM song length consistent with the input file while inserting necessary gate waits for proper note triggering in OPLL→OPL3 conversion.

## Key Changes (refs #15, #16, #17)

### 1. Gate Compensation Debt Tracking
- Added global `g_gate_comp_debt_samples` variable to track accumulated compensation debt
- Debt is increased when waits are inserted (pre-KeyOn, min-gate, off→on)
- Debt is decreased when input-derived waits are compensated

### 2. duplicate_write_opl3() Tail Compensation
- Updated signature to accept `uint16_t next_wait_samples` parameter
- Added tail compensation logic:
  - Tracks waits inserted by `opl3_keyon_wait` and adds to debt
  - Subtracts debt from `next_wait_samples` before emitting wait
  - Carries over remaining debt if compensation exceeds next wait
  - Emits `[GATE COMP]` debug logs when verbose mode enabled
- Updated all call sites to pass next_wait_samples (0 for internal operations)

### 3. OPLL-Specific Gate Wait Insertion
- Updated existing wait insertion points to track debt:
  - Pre-KeyOn wait (`get_pre_keyon_wait()`)
  - Off→On minimum wait (`get_min_off_on_wait()`)
  - Min-gate wait before KeyOff (`get_min_gate()`)
- Changed log prefix from `[KEYOFF_INJECT_WAIT]`, `[PRE_KEYON_WAIT]`, `[OFF_TO_ON_WAIT]` to unified `[GATE WAIT]`
- Logs now show: `[GATE WAIT] ch=N pre=X`, `[GATE WAIT] ch=N off_on=X`, `[GATE WAIT] ch=N min_gate=X`

### 4. Zero-Length Wait Safety
- `vgm_wait_samples()` already skips zero-length waits
- Compensation logic ensures adjusted waits are never negative

## Technical Details

### Compensation Flow
1. When a gate wait is inserted (e.g., pre-KeyOn, min-gate):
   - `vgm_wait_samples()` is called to insert the wait
   - `g_gate_comp_debt_samples += inserted_wait` tracks the debt
   - Debug log emitted under `--debug-verbose`

2. When an input-derived wait arrives at `duplicate_write_opl3()`:
   - Function receives `next_wait_samples` from the VGM stream
   - Calculates: `compensation = min(next_wait_samples, g_gate_comp_debt_samples)`
   - Emits adjusted wait: `adjusted = next_wait_samples - compensation`
   - Updates debt: `g_gate_comp_debt_samples -= compensation`
   - Debug log shows: `[GATE COMP] next_wait=N -> adjusted=A, debt_left=D`

### Preserved Behavior
- AB/BAB sequencing unchanged
- Existing logs preserved (except unified gate wait naming)
- Non-OPLL chip paths unaffected
- All existing functionality maintained

## Usage

The compensation system is automatic when using `--audible-sanity` mode with gate parameters:

```bash
# Example with gate compensation
./eseopl3patcher input.vgm output.vgm \
    --convert-ym2413 \
    --audible-sanity \
    --min-gate 8192 \
    --pre-keyon-wait 256 \
    --min-off-on-wait 128 \
    --debug-verbose
```

Debug output will show:
- `[GATE WAIT] ch=X ...` when waits are inserted
- `[GATE COMP] next_wait=N -> adjusted=A, debt_left=D` when compensating

## Expected Results
- VGM header `total_samples` should remain close to the original (small drift only if debt survives to end)
- Parameter sweeps of gate values should vary note gating audibly
- Songs should not slow down due to inserted waits

## Implementation Notes
- Gate compensation is global across all channels (not per-channel)
- Debt accumulates across the entire conversion process
- Only waits inserted by OPLL-specific gate logic contribute to debt
- Internal `duplicate_write_opl3()` calls pass `next_wait_samples=0` (no compensation)
- Final wait compensation happens at VGM stream level via `duplicate_write_opl3()`
