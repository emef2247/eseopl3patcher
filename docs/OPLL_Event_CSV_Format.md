# OPLL Event CSV Format (Phase 1)

## Overview

The OPLL (YM2413) event timeline CSV logger records register writes that affect tonal parameters, providing a human-readable timeline for analysis, debugging, and format conversion (e.g., bridging OPLL VGM to OPL3, aiding MML/MIDI conversion).

This document describes **Phase 1** implementation, which logs register write events. Future phases will add channel enable/disable detection, operator-level snapshots, and key duration aggregation.

## Rationale

- **Bridging OPLL VGM to OPL3**: Track frequency, block, instrument, and volume changes across channels to facilitate accurate conversion.
- **MML/MIDI Conversion**: Event timeline provides structured data for music notation tools.
- **Active vs Disabled Channels**: Future phases will distinguish truly active channels from silent/disabled ones using envelope and output energy heuristics.

## Phase 1: CSV Columns

The event CSV has the following columns:

```
time_s,sample,chip,addr,data,#type,ch,ko,blk,fnum,fnumL,inst,vol
```

### Column Definitions

| Column | Type | Description |
|--------|------|-------------|
| `time_s` | float | Time in seconds (computed as `sample / sample_rate`) |
| `sample` | uint64 | Absolute sample counter (incremented each generated sample) |
| `chip` | string | Chip identifier (always "YM2413" for OPLL) |
| `addr` | int | Register address (0x10-0x18, 0x20-0x28, or 0x30-0x38) |
| `data` | int | Data value written to the register |
| `#type` | string | Event type: `fL`, `fHBK`, or `iv` (see below) |
| `ch` | int | Channel number (0-8) derived from register address |
| `ko` | int | Key-On bit (0 or 1), extracted from register 0x2n |
| `blk` | int | Block/octave (0-7), extracted from register 0x2n bits [3:1] |
| `fnum` | int | Full F-number (0-511), computed as `((reg2n & 1) << 8) | reg1n` |
| `fnumL` | int | Low byte of F-number (register 0x1n value) |
| `inst` | int | Instrument/patch number (0-15), from register 0x3n upper nibble |
| `vol` | int | Volume/attenuation (0-15), from register 0x3n lower nibble |

### Event Types

- **`fL`**: F-number low byte write (addresses 0x10-0x18)
  - Only `ch` and `fnumL` are populated; other derived fields are empty.
  
- **`fHBK`**: F-number high + block + key-on write (addresses 0x20-0x28)
  - Populates `ch`, `ko`, `blk`, `fnum`, and `fnumL` (from current reg1n state).
  
- **`iv`**: Instrument + volume write (addresses 0x30-0x38)
  - Populates `ch`, `inst`, and `vol`.

**Note**: Some columns may be empty depending on event type. Empty fields appear as blank (no value) between commas in the CSV.

### Example Row

```csv
time_s,sample,chip,addr,data,#type,ch,ko,blk,fnum,fnumL,inst,vol
13.816666666666666,687268,YM2413,32,22,fHBK,0,1,3,172,172,,
```

This row indicates:
- At time 13.82 seconds (sample 687268)
- A write to register 0x20 (channel 0 frequency high + block + key-on)
- Data value 22 (0x16)
- Key-on bit is 1 (note on)
- Block is 3
- Full F-number is 172
- Low byte is 172

## Time Base

- **Sample counter**: Incremented by 1 for each sample generated in `opll_base::generate()`.
- **Sample rate**: Currently hardcoded to 49716.0 Hz (default OPLL sample rate). Future integration may provide runtime configuration.
- **Time calculation**: `time_s = sample / sample_rate`
- **Precision**: Retain `sample` integer to permit recomputation with different sample rates if needed.

## Compile-Time and Runtime Controls

### Macros and Environment Variables

| Name | Type | Description |
|------|------|-------------|
| `ESEOPL3_OPLL_CSV` | Compile-time macro | Enable CSV logging code. Add `-DESEOPL3_OPLL_CSV` to `USER_DEFINES` when building. |
| `ESEOPL3_OPLL_VCD` | Compile-time macro | Enable VCD logging (separate feature, unaffected by CSV logging). |
| `ESEOPL3_OPLL_TRACE` | Compile-time macro | Enable TRACE logging (separate feature, unaffected by CSV logging). |
| `OPLL_CSV_EVENTS` | Environment variable | Runtime control for CSV logging. Set to `0` to disable CSV output at runtime. Default is ON if macro is enabled. |

### Building with CSV Support

```bash
make clean
make USE_YMFM=1 USER_DEFINES="-DESEOPL3_OPLL_CSV" -j4
```

### Runtime Usage

By default (if compiled with `-DESEOPL3_OPLL_CSV`), the logger will create `opll_events.csv` in the current directory.

To disable CSV logging at runtime:
```bash
export OPLL_CSV_EVENTS=0
./build/bin/eseopl3patcher input.vgm ...
```

To enable (default when macro is defined):
```bash
# OPLL_CSV_EVENTS is not set or set to any value other than "0"
./build/bin/eseopl3patcher input.vgm ...
```

## Phase 2 and Beyond (Planned)

Future enhancements will include:

### Channel Active/Disabled Detection
- **`active` flag**: Boolean column indicating whether the channel is truly producing sound.
- **Heuristic**: Based on operator envelope state + output energy threshold.
- **Goal**: Distinguish silent/disabled channels from active ones for accurate conversion.

### Key Duration Aggregation
- **Duration tracking**: Compute time between key-on and key-off events.
- **Export format**: Separate CSV or additional columns with duration metrics.

### Operator-Level Snapshot Columns
Additional columns per event (or separate snapshot CSV):
- `opX_phase`: Operator phase accumulator
- `opX_env`: Envelope level
- `opX_eg_state`: Envelope generator state (attack, decay, sustain, release)
- `opX_multiple`: Frequency multiplier
- `opX_total_level`: Total level (attenuation)
- `opX_phase_step`: Phase increment per sample

Where X is the operator index (0 = modulator, 1 = carrier for 2-op FM).

### Sample Snapshot CSV
- **Separate file**: `opll_samples.csv`
- **Contents**: Per-sample output values (L/R channels), LFO state, global envelope counters.
- **Decimation**: Environment variable `OPLL_CSV_SAMPLE_DECIMATE` to control sample rate (e.g., log every Nth sample).

### Additional Environment Variables (Future)
- `OPLL_CSV_SAMPLES`: Enable/disable sample snapshot logging.
- `OPLL_CSV_SAMPLE_DECIMATE`: Decimation factor for sample logging (e.g., `10` = log every 10th sample).

## Testing Guidance

### Build and Enable
```bash
make USE_YMFM=1 USER_DEFINES="-DESEOPL3_OPLL_CSV" -j4
```

### Test with Existing OPLL VGM Files
Run the converter on short OPLL-only VGM inputs:
```bash
./build/bin/eseopl3patcher input_opll.vgm 0 --convert-ym2413 -o output.vgm
```

### Verify CSV Output
- Check that `opll_events.csv` is created in the current directory.
- Verify column headers match the format above.
- Compare against reference timeline CSV (e.g., `tests/equiv/inputs/ir/ym2413_3ch_test_timeline_YM2413.csv`).
- Accept minor floating-point rounding differences in `time_s`.
- Ensure `sample` integer is monotonically increasing for each write event.

### Disable CSV Logging
```bash
export OPLL_CSV_EVENTS=0
./build/bin/eseopl3patcher input_opll.vgm 0 --convert-ym2413 -o output.vgm
# opll_events.csv should NOT be created
```

## Notes

- **VCD/TRACE Compatibility**: CSV logging is independent of existing VCD and TRACE features. All three can be enabled simultaneously without interference.
- **Sample Rate TODO**: The sample rate is currently hardcoded as `49716.0`. Future work will integrate dynamic sample rate configuration via the YMFM interface.
- **Performance**: CSV logging is minimal overhead; writes are buffered by the C standard library and flushed periodically.

## References

- **Test Reference**: `tests/equiv/inputs/ir/ym2413_3ch_test_timeline_YM2413.csv`
- **YM2413 Register Map**: See OPLL datasheet or MAME/YMFM documentation.
