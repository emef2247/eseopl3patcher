# Gate Estimation Example Usage

## Quick Start

### 1. Estimate Gates for a VGM File

```bash
cd tools/gate_estimator

# Run estimation with default settings
python3 run_estimation.py /path/to/song.vgm --label my_song

# Output files created:
# - analysis/gate_estimation/my_song_results.csv (detailed results)
# - analysis/gate_estimation/my_song_gates.csv (C-loadable format)
```

### 2. View Results

```bash
cat analysis/gate_estimation/my_song_gates.csv
```

Output:
```csv
patch,channel,gate_samples
1,0,8192
1,1,10240
2,0,12288
...
```

### 3. Use in C Runtime

Add to your converter code:

```c
#include "gate_loader.h"

int main(int argc, char *argv[]) {
    // ... command line parsing ...
    
    // Load gates if provided
    const char *gates_file = "analysis/gate_estimation/my_song_gates.csv";
    if (gate_loader_init(gates_file) == 0) {
        fprintf(stderr, "[INFO] Loaded gate estimates from %s\n", gates_file);
    } else {
        fprintf(stderr, "[INFO] No gate file loaded, using defaults\n");
    }
    
    // ... VGM processing ...
    
    // When processing a YM2413 note:
    uint16_t gate;
    if (gate_loader_lookup(patch, channel, &gate) == 0) {
        // Use estimated gate
        fprintf(stderr, "[DEBUG] Using gate %u for patch %d, channel %d\n",
                gate, patch, channel);
    } else {
        // Use default
        gate = gate_loader_get_default();
        fprintf(stderr, "[DEBUG] Using default gate %u for patch %d, channel %d\n",
                gate, patch, channel);
    }
    
    // Apply gate duration...
    
    // Cleanup
    gate_loader_cleanup();
    return 0;
}
```

## Advanced Workflows

### Multi-Run Sweep

Run estimation with different gate grids:

```bash
# Coarse grid
python3 run_estimation.py song.vgm \
    --gate-grid "2048,8192,14336" \
    --label coarse

# Fine grid around best coarse result
python3 run_estimation.py song.vgm \
    --gate-grid "7168,8192,9216,10240" \
    --label fine

# Very fine grid
python3 run_estimation.py song.vgm \
    --gate-grid "7680,8192,8704,9216" \
    --label veryfine
```

### Aggregate Results

```bash
python3 aggregate.py \
    --input-dir analysis/gate_estimation \
    --pattern "*_results.csv" \
    --output-best combined_best.csv \
    --output-summary combined_summary.csv
```

View best results:
```bash
cat combined_best.csv
```

View summary statistics:
```bash
cat combined_summary.csv
```

### Custom Scenarios with sweep.py

Create `scenarios.json`:
```json
{
  "scenarios": [
    {
      "name": "scale_patch1_ch0",
      "sequence_path": "sequences/chromatic_scale.json",
      "patch_params": {
        "patch": 1,
        "ar": 15, "dr": 4, "rr": 7, "sl": 4,
        "block": 4, "ksr": false
      },
      "channel": 0
    },
    {
      "name": "melody_patch5_ch1",
      "sequence_path": "sequences/melody.json",
      "patch_params": {
        "patch": 5,
        "ar": 14, "dr": 3, "rr": 6, "sl": 3,
        "block": 5, "ksr": true
      },
      "channel": 1
    }
  ]
}
```

Run sweep:
```bash
python3 sweep.py \
    --scenarios scenarios.json \
    --eg-model exact \
    --eg-tables ym2413_eg_tables_ymfm.json \
    --gate-grid "4096,8192,12288,16384" \
    --output sweep_results.csv
```

## Testing Exact EG Model

Direct test of the envelope model:

```bash
python3 ym2413_exact.py ym2413_eg_tables_ymfm.json
```

Output shows:
- Residual levels at different gate lengths
- Grid search results
- Robustness test (no transitions)

## Integration with Converter

### Option 1: Command-line flag (future enhancement)

```bash
./eseopl3patcher song.vgm 2.5 \
    --convert-ym2413 \
    --gates-file analysis/gate_estimation/my_song_gates.csv
```

### Option 2: Environment variable (future enhancement)

```bash
export ESEOPL3_GATES_FILE="analysis/gate_estimation/my_song_gates.csv"
./eseopl3patcher song.vgm 2.5 --convert-ym2413
```

### Option 3: Direct API usage (current)

See C code example above - call `gate_loader_init()` at startup.

## Troubleshooting

### "No transitions available for estimation"

This means the sequence has no note-to-note transitions to analyze. This can happen when:
- VGM has only single notes with gaps
- Sequence parsing failed
- Wrong channel/patch selected

The tools handle this gracefully by returning default values.

### Empty results

Check that:
1. VGM file is valid YM2413
2. Sequence files exist and are valid JSON
3. Patch/channel combinations are active in the VGM

### C loader fails

Ensure:
1. CSV file exists and is readable
2. CSV format is correct: `patch,channel,gate_samples`
3. Values are valid integers
4. File path is absolute or relative to working directory
