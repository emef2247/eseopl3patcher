# OPLL Event Timeline CSV Format (Phase 1)

## Overview

This document describes the CSV event logging format for YM2413/OPLL register writes. The event timeline captures register writes that affect tonal parameters (frequency, block, key on/off, instrument, volume) for conversion analysis and debugging.

## Rationale

The OPLL event CSV logger supports:
- **OPLL→OPL3 conversion analysis**: Track how OPLL register writes map to OPL3 parameters
- **MML/MIDI generation**: Provide structured event data for music notation conversion
- **Debugging**: Identify timing issues, missing parameters, and channel state transitions
- **Bridging**: Connect VGM playback data with analysis tools expecting tabular formats

This is complementary to existing VCD trace output (which captures waveform-level data) and provides a higher-level view of musical events.

## Phase 1: Register Write Events

### CSV Columns

```
time_s,sample,chip,addr,data,#type,ch,ko,blk,fnum,fnumL,inst,vol
```

| Column | Type | Description |
|--------|------|-------------|
| `time_s` | float | Event time in seconds (sample / sample_rate) |
| `sample` | uint64 | Sample index (monotonically increasing) |
| `chip` | string | Chip identifier (fixed to "YM2413" for compatibility) |
| `addr` | hex | Register address (0x10-0x38 for Phase 1) |
| `data` | hex | Register data value |
| `#type` | string | Event type: `fL` (freq low), `fHBK` (freq high+block+key), `iv` (instrument+volume) |
| `ch` | int | Channel number (0-8 for OPLL's 9 channels) |
| `ko` | int | Key-on bit (0=off, 1=on) |
| `blk` | int | Block/octave value (0-7) |
| `fnum` | int | Full F-number (9-bit, 0-511) |
| `fnumL` | int | Lower 8 bits of F-number |
| `inst` | int | Instrument/patch number (0-15) |
| `vol` | int | Volume level (0-15, where 0=loudest) |

### Event Types (#type)

- **`fL`** (frequency Low): Register 0x10-0x18 writes (F-number lower 8 bits)
- **`fHBK`** (frequency High + Block + Key): Register 0x20-0x28 writes
  - Bits 0: F-number MSB (bit 8)
  - Bits 1-3: Block (octave)
  - Bit 4: Key-on (sustain/trigger)
  - Bits 5-7: Unused
- **`iv`** (Instrument + Volume): Register 0x30-0x38 writes
  - Bits 0-3: Volume (attenuation)
  - Bits 4-7: Instrument number

### Example CSV Output

```csv
time_s,sample,chip,addr,data,#type,ch,ko,blk,fnum,fnumL,inst,vol
0.000000,0,YM2413,0x30,0x10,iv,0,0,0,0,0,1,0
0.000020,1,YM2413,0x10,0x81,fL,0,0,0,129,129,1,0
0.000040,2,YM2413,0x20,0x0E,fHBK,0,1,3,129,129,1,0
0.100000,4972,YM2413,0x20,0x0C,fHBK,0,0,3,129,129,1,0
```

### Register Address Ranges (Phase 1)

| Range | Purpose | Event Type |
|-------|---------|------------|
| 0x10-0x18 | F-number low 8 bits | fL |
| 0x20-0x28 | F-number MSB + Block + Key-on | fHBK |
| 0x30-0x38 | Instrument + Volume | iv |

## Configuration

### Compile-Time Macro

Enable CSV logging at compile time:

```bash
gcc ... -DESEOPL3_OPLL_CSV ...
```

### Runtime Environment Variables

| Variable | Values | Default | Description |
|----------|--------|---------|-------------|
| `OPLL_CSV_EVENTS` | `0` or `1` | `1` (if macro enabled) | Enable/disable CSV logging at runtime |

When `ESEOPL3_OPLL_CSV` is defined:
- CSV logging is **enabled by default**
- Set `OPLL_CSV_EVENTS=0` to disable without recompiling
- CSV file: `opll_events.csv` (created in current working directory)

### Sample Rate

Phase 1 uses a static configurable sample rate variable (default: **49716.0 Hz**).

This is a transitional approach until proper integration with VGM playback sample rate is implemented in a future phase.

## Phase 2 Extensions (Planned)

The following features are **documented but not yet implemented**:

### 1. Active Channel Detection

**Goal**: Emit an `active` column indicating whether the channel is audibly producing sound.

**Approach** (future):
- Track operator envelope states (attack, decay, sustain, release, off)
- Monitor output energy/amplitude
- Mark channel as `active=1` when:
  - Key is on AND operator is in attack/decay/sustain
  - Output energy exceeds threshold
- Mark as `active=0` when:
  - Key is off AND envelope is in release/off
  - Output energy below threshold

**Column addition**:
```
time_s,sample,chip,addr,data,#type,ch,ko,blk,fnum,fnumL,inst,vol,active
```

### 2. Operator-Level Parameters

**Goal**: Capture detailed operator state for advanced analysis.

**New columns**:
- `op{0,1}_phase`: Operator phase accumulator value
- `op{0,1}_env`: Operator envelope level
- `op{0,1}_eg_state`: Envelope generator state (attack, decay, sustain, release, off)
- `op{0,1}_multiple`: Frequency multiplier
- `op{0,1}_total_level`: Operator total level (TL)
- `op{0,1}_phase_step`: Phase increment per sample

**Example**:
```csv
...,op0_env,op0_eg_state,op1_env,op1_eg_state
...,32,attack,15,attack
```

### 3. Sample-Level Snapshots

**Goal**: Periodic full-state dumps for detailed time-series analysis.

**Format**: Separate CSV with all channel states at regular intervals (e.g., every 1000 samples).

**Use case**: Reconstruct full chip state at any point in time.

### 4. Key Duration Aggregation

**Goal**: Summary statistics for note timing analysis.

**Format**: Separate CSV with per-note duration metrics:
```csv
ch,note_start_sample,note_end_sample,duration_samples,duration_s,fnum,blk,inst
```

**Use case**:
- Identify note length distributions
- Detect timing quantization issues
- Validate gate timing adjustments

## Implementation Notes

### Static Sample Counter

The sample counter is incremented in the sample generation loop. For Phase 1, this is implemented as a static counter in the OPLL wrapper module, incremented each time a sample is produced.

### CSV Writer Initialization

The CSV writer is initialized on the first relevant register write (0x10-0x38 range) when `ESEOPL3_OPLL_CSV` is enabled and `OPLL_CSV_EVENTS` is not set to "0".

### Memory Management

The CSV writer uses minimal memory:
- Single `FILE*` pointer
- Static sample rate (double)
- No dynamic allocation beyond standard `fopen()`

### Thread Safety

Phase 1 assumes single-threaded access. Multi-threaded playback scenarios are not yet supported.

## Testing

### Build with CSV Logging

```bash
make clean
make USER_DEFINES="-DESEOPL3_OPLL_CSV"
```

### Run with CSV Logging

```bash
./build/eseopl3patcher input.vgm 0 --convert-ym2413 -o output.vgm
```

Expected output: `opll_events.csv` in the current directory.

### Disable at Runtime

```bash
OPLL_CSV_EVENTS=0 ./build/eseopl3patcher input.vgm 0 --convert-ym2413 -o output.vgm
```

No CSV file should be created.

### Validation

Compare generated CSV against reference:
```bash
# Check columns match
head -1 opll_events.csv
# Should output: time_s,sample,chip,addr,data,#type,ch,ko,blk,fnum,fnumL,inst,vol

# Verify sample monotonicity
awk -F, 'NR>1 {print $2}' opll_events.csv | awk 'NR>1 && $1 < prev {print "ERROR: non-monotonic sample"; exit 1} {prev=$1}'
```

## Future Enhancements

Beyond Phase 2, potential extensions include:
- **Percussion mode support**: Track rhythm channel states (BD, SD, TOM, HH, CYM)
- **Custom instrument tracking**: Log user-defined patch (address 0x00-0x07) changes
- **Multi-chip support**: Handle dual YM2413 configurations
- **JSON export**: Alternative format for structured data interchange
- **VCD integration**: Cross-reference CSV events with VCD trace timestamps

## References

- YM2413 Application Manual: [link to datasheet]
- VGM Format Specification: https://vgmrips.net/wiki/VGM_Specification
- OPLL to OPL3 Conversion Notes: (internal implementation docs)

---

**Document Version**: 1.0 (Phase 1)  
**Last Updated**: 2024-10-04
