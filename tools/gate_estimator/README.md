# YM2413 Gate Estimation Tools

Python tools for estimating optimal gate (key-on duration) values for YM2413 (OPLL) to OPL3 conversion.

## Overview

These tools analyze YM2413 envelope behavior and estimate per-patch/per-channel gate durations to ensure proper note triggering and decay in the OPL3 conversion process.

## Components

### 1. Envelope Models

#### `ym2413_exact.py`
Table-driven "Exact" EG model using ymfm/MAME/EMU2413 envelope timing tables.

**Features:**
- Attack/Decay/Release time tables (16 values each)
- Key Scale Rate (KSR) support per FNUM block
- Residual envelope computation at gate-off time
- Grid search for optimal gate selection
- Robustness: handles sequences with no note transitions

**Usage:**
```python
from ym2413_exact import YM2413EnvelopeExact

# Load model
model = YM2413EnvelopeExact('ym2413_eg_tables_ymfm.json')

# Compute residual at gate end
residual = model.residual_at(
    gate_samples=8192,
    sample_rate=44100.0,
    ar=15, dr=4, rr=7, sl=4,
    block=4, ksr=False
)

# Choose optimal gate from candidates
transitions = [(0, 8000, 8000), (10000, 18000, 8000)]
best_gate, metrics = model.choose_gate_grid(
    transitions=transitions,
    sample_rate=44100.0,
    ar=15, dr=4, rr=7, sl=4,
    gate_candidates=[4096, 8192, 12288, 16384]
)
```

### 2. Estimation Tools

#### `run_estimation.py`
Main entry point for running gate estimation on VGM files.

**Usage:**
```bash
# Using exact model with default tables
python run_estimation.py input.vgm --eg-model exact --label myrun

# Using custom tables
python run_estimation.py input.vgm \
  --eg-model exact \
  --eg-tables custom_tables.json \
  --label myrun

# Custom gate grid
python run_estimation.py input.vgm \
  --eg-model exact \
  --gate-grid "4096,8192,12288,16384" \
  --output-dir analysis/gates
```

**Outputs:**
- `<label>_results.csv`: Estimation results per patch/channel
- `<label>_gates.csv`: Gates in C-loadable format

#### `sweep.py`
Runs gate estimation sweeps across multiple scenarios.

**Usage:**
```bash
python sweep.py \
  --scenarios scenarios.json \
  --eg-model exact \
  --eg-tables ym2413_eg_tables_ymfm.json \
  --output results.csv
```

**Scenario JSON format:**
```json
{
  "scenarios": [
    {
      "name": "scale_chromatic_patch1",
      "sequence_path": "sequences/scale.json",
      "patch_params": {
        "patch": 1,
        "ar": 15, "dr": 4, "rr": 7, "sl": 4,
        "block": 4, "ksr": false
      },
      "channel": 0
    }
  ]
}
```

#### `aggregate.py`
Aggregates results from multiple sweep runs.

**Usage:**
```bash
python aggregate.py \
  --input-dir analysis/sweeps \
  --pattern "*_results.csv" \
  --output-best combined_best.csv \
  --output-summary combined_summary.csv
```

**Outputs:**
- `combined_best.csv`: Best gate per scenario/patch/channel
- `combined_summary.csv`: Summary statistics (min/max/mean gates, scores)

### 3. Timing Tables

#### `ym2413_eg_tables_ymfm.json`
Provisional envelope timing tables based on ymfm/MAME/EMU2413.

**Structure:**
```json
{
  "eg_times_ms": {
    "attack": [16 values in milliseconds],
    "decay": [16 values in milliseconds],
    "release": [16 values in milliseconds]
  },
  "ksr": {
    "per_blk_shift": [8 shift values for blocks 0-7]
  }
}
```

## Workflow

### Basic Flow

1. **Estimate gates** for a VGM file:
   ```bash
   python run_estimation.py input.vgm --label test1
   ```

2. **Review results**:
   ```bash
   cat analysis/gate_estimation/test1_gates.csv
   ```

3. **Load in C runtime**:
   ```c
   #include "gate_loader.h"
   
   gate_loader_init("analysis/gate_estimation/test1_gates.csv");
   
   uint16_t gate;
   if (gate_loader_lookup(patch, channel, &gate) == 0) {
       // Use gate value
   } else {
       gate = gate_loader_get_default();
   }
   ```

### Advanced: Coarse-to-Fine Sweeps

1. **Coarse sweep** (wide grid):
   ```bash
   python run_estimation.py input.vgm \
     --gate-grid "2048,8192,14336" \
     --label coarse
   ```

2. **Fine sweep** (narrow grid around best coarse result):
   ```bash
   python run_estimation.py input.vgm \
     --gate-grid "7168,8192,9216,10240" \
     --label fine
   ```

3. **Aggregate**:
   ```bash
   python aggregate.py \
     --input-dir analysis/gate_estimation \
     --output-best best.csv
   ```

## C Runtime Integration

### Loading Gates

Include `gate_loader.h` and link `gate_loader.c`:

```c
#include "gate_loader.h"

// Initialize at startup
if (gate_loader_init("gates.csv") != 0) {
    fprintf(stderr, "Warning: Could not load gates.csv\n");
}

// Lookup during conversion
uint16_t gate;
if (gate_loader_lookup(patch, channel, &gate) == 0) {
    // Use estimated gate
} else {
    // Fall back to default
    gate = gate_loader_get_default();
}

// Cleanup at exit
gate_loader_cleanup();
```

### Gates CSV Format

```csv
patch,channel,gate_samples
1,0,8192
1,1,10240
2,0,12288
...
```

## Robustness

The tools handle edge cases gracefully:

- **No transitions**: When a sequence has no note-to-note transitions, `choose_gate_grid` returns a default gate with `score=0` and a note in metrics
- **Invalid data**: Missing or malformed sequence files are logged and skipped
- **Empty results**: Aggregator handles empty result sets without crashing

## Model Comparison

| Feature | Parametric Model | Exact Model |
|---------|------------------|-------------|
| Speed | Fast | Moderate |
| Accuracy | Approximate | Table-driven (more accurate) |
| Tables | None (formula-based) | Requires JSON tables |
| KSR | Simplified | Per-block shifts |
| Status | Not yet implemented | ✅ Implemented |

## Future Work

- Implement parametric model as fallback
- Parse VGM files directly to extract note events
- Add visualization tools for envelope curves
- Support OPLL→OPL3 4-op mapping
- Integrate strict ymfm tables (replace provisional values)

## References

- ymfm (Yamaha FM sound chip emulator): https://github.com/aaronsgiles/ymfm
- MAME (Multiple Arcade Machine Emulator)
- EMU2413 (YM2413 emulator)
