#!/usr/bin/env bash
# auto_analyze_vgm.sh
# Automated VGM analysis with gate parameter sweeps

set -e

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Options:
  -i, --input FILE      Input VGM file (required)
  -b, --baseline FILE   Baseline WAV file for comparison (optional)
  -l, --label LABEL     Label for output files (default: derived from input)
  -o, --output DIR      Output directory (default: analysis)
  -g, --gates VALUES    Gate values to sweep (space-separated, default: auto)
  --adv-onset           Generate advanced onset analysis
  --adv-env             Generate advanced envelope analysis
  -h, --help            Show this help

Environment:
  ESEOPL3PATCHER        Path to eseopl3patcher binary
  PREFER_VGMPLAY=1      Prefer vgmplay over vgm2wav for rendering
EOF
}

# Default values
INPUT_VGM=""
BASELINE_WAV=""
LABEL=""
OUT_DIR="analysis"
GATES=""
ADV_ONSET=0
ADV_ENV=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -i|--input)
            INPUT_VGM="$2"
            shift 2
            ;;
        -b|--baseline)
            BASELINE_WAV="$2"
            shift 2
            ;;
        -l|--label)
            LABEL="$2"
            shift 2
            ;;
        -o|--output)
            OUT_DIR="$2"
            shift 2
            ;;
        -g|--gates)
            GATES="$2"
            shift 2
            ;;
        --adv-onset)
            ADV_ONSET=1
            shift
            ;;
        --adv-env)
            ADV_ENV=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z "$INPUT_VGM" ]]; then
    echo "Error: No input VGM file specified (use -i/--input)" >&2
    usage >&2
    exit 1
fi

if [[ ! -f "$INPUT_VGM" ]]; then
    echo "Error: Input file not found: $INPUT_VGM" >&2
    exit 1
fi

# Find eseopl3patcher binary
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ -n "$ESEOPL3PATCHER" && -x "$ESEOPL3PATCHER" ]]; then
    PATCHER="$ESEOPL3PATCHER"
elif [[ -x "$PROJECT_ROOT/build/eseopl3patcher" ]]; then
    PATCHER="$PROJECT_ROOT/build/eseopl3patcher"
else
    echo "Error: eseopl3patcher binary not found" >&2
    echo "  Build it with 'make' or set ESEOPL3PATCHER environment variable" >&2
    exit 1
fi

# Derive label if not specified
if [[ -z "$LABEL" ]]; then
    LABEL=$(basename "$INPUT_VGM" .vgm)
fi

# Create output directory
mkdir -p "$OUT_DIR"

# Default gate sweep values if not specified
if [[ -z "$GATES" ]]; then
    GATES="0 2048 4096 8192 16384 32768"
fi

echo "[auto_analyze_vgm] Input: $INPUT_VGM"
echo "[auto_analyze_vgm] Label: $LABEL"
echo "[auto_analyze_vgm] Output: $OUT_DIR"
echo "[auto_analyze_vgm] Gate sweep: $GATES"

# Find rendering script
RENDER_SCRIPT="$SCRIPT_DIR/render_spectrogram.sh"
if [[ ! -x "$RENDER_SCRIPT" ]]; then
    echo "Warning: render_spectrogram.sh not found or not executable" >&2
    RENDER_SCRIPT=""
fi

# Perform gate sweep
for gate in $GATES; do
    echo ""
    echo "[auto_analyze_vgm] === Testing min_gate=$gate ==="
    
    # Convert VGM with gate parameter
    OUT_VGM="$OUT_DIR/${LABEL}_gate${gate}.vgm"
    echo "[auto_analyze_vgm] Converting: $INPUT_VGM -> $OUT_VGM"
    
    "$PATCHER" \
        --convert-ym2413 \
        --audible-sanity \
        --min-gate "$gate" \
        --verbose \
        "$INPUT_VGM" \
        "$OUT_VGM" \
        > "$OUT_DIR/${LABEL}_gate${gate}.log" 2>&1
    
    if [[ ! -f "$OUT_VGM" ]]; then
        echo "Warning: Failed to generate $OUT_VGM" >&2
        continue
    fi
    
    # Check VGM header for total_samples
    if command -v xxd >/dev/null 2>&1; then
        TOTAL_SAMPLES=$(xxd -s 0x18 -l 4 -e -g 4 "$OUT_VGM" | awk '{print $2}')
        echo "[auto_analyze_vgm] VGM total_samples: 0x$TOTAL_SAMPLES"
    fi
    
    # Render to WAV and generate spectrogram
    if [[ -n "$RENDER_SCRIPT" ]]; then
        echo "[auto_analyze_vgm] Rendering spectrogram..."
        "$RENDER_SCRIPT" -o "$OUT_DIR" "$OUT_VGM" || {
            echo "Warning: Spectrogram generation failed" >&2
        }
    fi
done

# Generate comparison if baseline provided
if [[ -n "$BASELINE_WAV" && -f "$BASELINE_WAV" ]]; then
    echo ""
    echo "[auto_analyze_vgm] Generating comparison with baseline: $BASELINE_WAV"
    
    # Find the best gate WAV (last one in sweep)
    LAST_GATE=$(echo "$GATES" | awk '{print $NF}')
    COMPARE_WAV="$OUT_DIR/${LABEL}_gate${LAST_GATE}.wav"
    
    if [[ -f "$COMPARE_WAV" && -f "$SCRIPT_DIR/wav_spectrogram.py" ]]; then
        python3 "$SCRIPT_DIR/wav_spectrogram.py" \
            "$BASELINE_WAV" \
            --compare "$COMPARE_WAV" \
            --out "$OUT_DIR/${LABEL}_compare.png" \
            --title "Baseline" \
            --title2 "gate=$LAST_GATE" \
            || echo "Warning: Comparison failed" >&2
    fi
fi

echo ""
echo "[auto_analyze_vgm] Analysis complete. Results in: $OUT_DIR"
