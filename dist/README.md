# Gate Configuration

This directory contains gate timing configuration for YM2413 (OPLL) to OPL3 conversion.

## gates.csv

The `gates.csv` file provides per-patch/channel gate duration values in samples. These values control the minimum duration between KeyOn and KeyOff events during OPLL→OPL3 conversion.

### Format

```csv
patch,channel,gate_samples
0,0,8192
1,1,10240
...
```

**Fields:**
- `patch`: YM2413 instrument/patch number (0-15 for melodic, 16-20 for rhythm)
- `channel`: YM2413 channel number (0-8)
- `gate_samples`: Gate duration in samples (at 44100 Hz sample rate)

### Usage

The converter will automatically load gate values from:

1. **Environment variable**: `ESEOPL3_GATES_CSV` (if set)
2. **Default location**: `dist/gates.csv` (if present)

If no CSV is found, the converter falls back to default gate values from command-line options or compile-time defaults.

**Example:**

```bash
# Use default location (dist/gates.csv)
./build/eseopl3patcher input.vgm 0 -o output.vgm --convert-ym2413

# Use custom gates.csv location
ESEOPL3_GATES_CSV=custom/gates.csv ./build/eseopl3patcher input.vgm 0 -o output.vgm --convert-ym2413
```

### Generating Gates

Gate values can be estimated using the Python tools in `tools/gate_estimator/`. See `tools/gate_estimator/README.md` for details.

### Default Values

The included `gates.csv` provides example values:
- Most patches/channels: 8192 samples (~185ms at 44100 Hz)
- Some variations with longer gates (10240-12288 samples) for specific patch/channel combinations

These values should be tuned based on analysis of specific VGM files for optimal note timing and envelope behavior.
