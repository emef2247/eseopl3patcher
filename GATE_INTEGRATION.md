# Gate CSV Integration Implementation

This document describes the implementation of Python-derived gate results integration into the C runtime for OPLL→OPL3 conversion.

## Overview

The gate loader system enables per-patch/channel gate timing control based on values exported from Python gate estimation tools. Gate values control the minimum duration between KeyOn and KeyOff events during YM2413 (OPLL) to OPL3 conversion.

## Components

### 1. Gate Loader (`src/gate_loader.h`, `src/gate_loader.c`)

Core CSV loading and lookup API:

```c
// Initialize from CSV file
int gate_loader_init(const char *csv_path);

// Lookup gate for specific patch/channel
int gate_loader_lookup(int patch, int channel, uint16_t *out_gate);

// Get default gate value
uint16_t gate_loader_get_default(void);

// Cleanup resources
void gate_loader_cleanup(void);

// Get number of loaded entries
int gate_loader_count(void);
```

**CSV Format:**
```csv
patch,channel,gate_samples
0,0,8192
1,1,10240
...
```

### 2. Integration in OPLL Wrapper (`src/opll/opll_to_opl3_wrapper.c`)

#### Initialization

The gate loader is initialized in `opll_init()`:

1. Checks `ESEOPL3_GATES_CSV` environment variable
2. Falls back to `dist/gates.csv` if not set
3. Loads CSV and reports status to stderr
4. Non-fatal if CSV is missing (uses default gate values)

Example output:
```
[GATES] Loaded gates from: dist/gates.csv (12 entries)
```

#### Gate Lookup

New functions for per-channel gate lookup:

```c
// Get current patch for a channel from YM2413 registers
static inline int get_current_patch_for_ch(int ch);

// Get minimum gate for specific channel/patch
static inline uint16_t get_min_gate_for_ch(int ch, int patch, const CommandOptions* o);

// Legacy fallback (uses default from CSV or options)
static inline uint16_t get_min_gate(const CommandOptions* o);
```

#### Integration Points

Gate lookup is integrated at all key KEYOFF decision points:

1. **Delayed KeyOff flush** (`opll_tick_pending_on_elapsed`)
   - Checks if pending KeyOff can be emitted based on gate elapsed time
   - Uses per-channel lookup: `get_min_gate_for_ch(ch, patch, p_opts)`

2. **Triple accumulator KeyOff** (`acc_maybe_flush_triple`)
   - Pre-KeyOff wait injection before retriggering notes
   - Uses per-channel gate for wait calculation

3. **Standard KeyOff handling** (`flush_channel_ch`)
   - Note-off edge detection and gate enforcement
   - Delayed KeyOff arming when gate threshold not met

4. **Direct B0 register writes** (`opll_write_register`)
   - KeyOff events from direct register writes
   - Gate enforcement at hardware register level

### 3. Public API (`src/opll/opll_to_opl3_wrapper.h`)

Exported functions for external use:

```c
// Check if gates CSV was loaded
int opll_gates_loaded(void);

// Lookup gate for specific patch/channel
int opll_gate_lookup(int patch, int channel, uint16_t *out_gate);
```

## Usage

### Loading Gates

**Method 1: Default location**
```bash
./build/eseopl3patcher input.vgm 0 -o output.vgm --convert-ym2413
# Automatically loads dist/gates.csv if present
```

**Method 2: Custom location via environment variable**
```bash
ESEOPL3_GATES_CSV=custom/gates.csv ./build/eseopl3patcher input.vgm 0 -o output.vgm --convert-ym2413
```

### Generating Gates

Use Python tools in `tools/gate_estimator/`:

```bash
python tools/gate_estimator/run_estimation.py input.vgm --label myrun
# Produces myrun_gates.csv in C-loadable format
```

See `tools/gate_estimator/README.md` for details.

## Gate Value Resolution

The system uses a fallback chain for determining gate values:

1. **Per-patch/channel CSV lookup** (if CSV loaded and entry exists)
2. **CSV default value** (if CSV loaded but no specific entry)
3. **Command-line option** (`--min-gate-samples`)
4. **Compile-time default** (`OPLL_MIN_GATE_SAMPLES`)

This allows flexible configuration while maintaining backward compatibility.

## Debugging

Gate-related messages are logged to stderr with `[GATES]` or `[GATE WAIT]` prefixes:

```
[GATES] Loaded gates from: dist/gates.csv (12 entries)
[GATE WAIT] ch=3 patch=1 min_gate=256 (elapsed=128, min=10240)
[DELAY_KEYOFF_ARM] ch=3 patch=1 elapsed=8192/10240 val=25
```

Use `--debug-verbose` flag for detailed gate operation traces.

## Implementation Notes

### Patch Detection

The current patch for each channel is extracted from YM2413 register state:
- Register `0x30-0x38` contains instrument/volume for channels 0-8
- Upper nibble `(reg3n >> 4) & 0x0F` is the patch number (0-15)

This allows automatic gate lookup without requiring explicit pattern/timeline tracking.

### Gate Timing

Gate values are in samples at the VGM sample rate (typically 44100 Hz):
- 8192 samples ≈ 185ms @ 44100 Hz
- 10240 samples ≈ 232ms @ 44100 Hz

The gate elapsed counter (`g_gate_elapsed[]`) tracks KeyOn duration per channel and is reset on each new KeyOn event.

### Backward Compatibility

- If no CSV is loaded, the system falls back to existing behavior
- All gate-related code is non-invasive to existing logic
- The `get_min_gate()` function maintains the original interface

## Testing

Run integration tests:

```bash
bash /tmp/test_gates_integration.sh
```

Verify gate loading:
```bash
./build/eseopl3patcher 2>&1 | grep GATES
```

## Files Modified/Added

### Added:
- `src/gate_loader.h` - Gate loader API
- `src/gate_loader.c` - Gate loader implementation
- `dist/gates.csv` - Sample gate configuration
- `dist/README.md` - Gate CSV documentation
- `GATE_INTEGRATION.md` - This document

### Modified:
- `src/opll/opll_to_opl3_wrapper.c` - Integration implementation
- `src/opll/opll_to_opl3_wrapper.h` - Public API additions

### Build System:
- `Makefile` - No changes needed (auto-detects `src/*.c`)

## Future Enhancements

Potential improvements:

1. **Pattern-based gates**: Support for per-timeline/pattern gate profiles
2. **Dynamic gate adjustment**: Runtime gate tuning based on envelope analysis
3. **Hot reload**: Reload gates.csv without restarting
4. **Performance optimization**: Hash table for large CSV files
5. **Gate interpolation**: Smooth transitions between different gate values

## References

- Python gate estimation tools: `tools/gate_estimator/`
- YM2413 specification: See project documentation
- OPL3 conversion notes: `IMPLEMENTATION_NOTES.md`
