#!/usr/bin/env bash
# Tune gate duration for a single VGM file against baseline WAV
# Usage: tune_gate_duration.sh -i input.vgm -b baseline.wav [-g "gates"] [-o output_dir]
#
# Default FREQSEQ=AB, writes CSV with headers: gate,seconds,delta_s,abs_delta_s,tempo_ratio

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Defaults
INPUT_VGM=""
BASELINE_WAV=""
GATES="${GATES:-512 1024 1536 2048 4096 8192 10240 12288 16384}"
OUTPUT_DIR="tune_gate_output"
FREQSEQ="${ESEOPL3_FREQSEQ:-AB}"
DETUNE="${DETUNE:-1.0}"

usage() {
    cat >&2 << EOF
Usage: $0 -i input.vgm -b baseline.wav [-g "gates"] [-o output_dir]

Options:
  -i <input.vgm>     Input YM2413 VGM file
  -b <baseline.wav>  Baseline WAV for duration reference
  -g <gates>         Space-separated gate values (default: 512 1024 ... 16384)
  -o <output_dir>    Output directory (default: tune_gate_output)

Environment:
  ESEOPL3_FREQSEQ    Frequency sequence mode (default: AB)
  DETUNE             Detune factor (default: 1.0)
  GATES              Alternative way to specify gate values

CSV Output: gate,seconds,delta_s,abs_delta_s,tempo_ratio
EOF
    exit 1
}

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        -i) INPUT_VGM="$2"; shift 2 ;;
        -b) BASELINE_WAV="$2"; shift 2 ;;
        -g) GATES="$2"; shift 2 ;;
        -o) OUTPUT_DIR="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "[ERROR] Unknown option: $1" >&2; usage ;;
    esac
done

if [ -z "$INPUT_VGM" ] || [ -z "$BASELINE_WAV" ]; then
    echo "[ERROR] Missing required arguments" >&2
    usage
fi

if [ ! -f "$INPUT_VGM" ]; then
    echo "[ERROR] Input VGM not found: $INPUT_VGM" >&2
    exit 1
fi

if [ ! -f "$BASELINE_WAV" ]; then
    echo "[ERROR] Baseline WAV not found: $BASELINE_WAV" >&2
    exit 1
fi

# Setup output directory
mkdir -p "$OUTPUT_DIR"
BASENAME=$(basename "$INPUT_VGM" .vgm)
CSV_FILE="$OUTPUT_DIR/${BASENAME}_gates.csv"

# Get baseline duration
if command -v ffprobe &>/dev/null; then
    TARGET_SECS=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$BASELINE_WAV" 2>/dev/null)
else
    echo "[ERROR] ffprobe not found - cannot determine baseline duration" >&2
    exit 1
fi

if [ -z "$TARGET_SECS" ] || [ "$TARGET_SECS" = "N/A" ]; then
    echo "[ERROR] Could not determine baseline duration" >&2
    exit 1
fi

echo "[INFO] Tuning gate duration for: $INPUT_VGM"
echo "[INFO] Target duration: ${TARGET_SECS}s"
echo "[INFO] FREQSEQ=$FREQSEQ, DETUNE=$DETUNE"
echo "[INFO] Testing gates: $GATES"

# Check patcher
PATCHER="${REPO_ROOT}/build/eseopl3patcher"
if [ ! -x "$PATCHER" ]; then
    echo "[ERROR] Patcher not found or not executable: $PATCHER" >&2
    exit 1
fi

# Write CSV header
echo "gate,seconds,delta_s,abs_delta_s,tempo_ratio" > "$CSV_FILE"

# Helper to get WAV duration
get_wav_duration() {
    local wav="$1"
    ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$wav" 2>/dev/null || echo "0"
}

# Sweep gates
for GATE in $GATES; do
    GATE_DIR="${OUTPUT_DIR}/gate_${GATE}"
    mkdir -p "$GATE_DIR"
    
    VGM_OUT="${GATE_DIR}/converted.vgm"
    WAV_OUT="${GATE_DIR}/converted.wav"
    
    # Run patcher
    ESEOPL3_FREQSEQ="$FREQSEQ" "$PATCHER" "$INPUT_VGM" "$DETUNE" \
        --convert-ym2413 \
        --min-gate-samples "$GATE" \
        -o "$VGM_OUT" &>/dev/null || {
        echo "[WARN] Patcher failed for gate=$GATE"
        continue
    }
    
    # Render to WAV
    if command -v vgm2wav &>/dev/null; then
        vgm2wav "$VGM_OUT" "$WAV_OUT" &>/dev/null || continue
    elif command -v vgmplay &>/dev/null; then
        vgmplay -o "$WAV_OUT" -l 1 "$VGM_OUT" &>/dev/null || continue
    elif command -v VGMPlay &>/dev/null; then
        VGMPlay -o "$WAV_OUT" -l 1 "$VGM_OUT" &>/dev/null || continue
    else
        echo "[ERROR] No VGM player found" >&2
        exit 1
    fi
    
    # Get duration
    SECS=$(get_wav_duration "$WAV_OUT")
    if [ "$SECS" = "0" ]; then
        echo "[WARN] Could not determine duration for gate=$GATE"
        continue
    fi
    
    # Calculate metrics
    DELTA_S=$(echo "$SECS - $TARGET_SECS" | bc -l)
    ABS_DELTA=$(echo "scale=6; if ($DELTA_S < 0) -($DELTA_S) else $DELTA_S" | bc -l)
    TEMPO_RATIO=$(echo "scale=6; $SECS / $TARGET_SECS" | bc -l)
    
    # Write to CSV
    echo "${GATE},${SECS},${DELTA_S},${ABS_DELTA},${TEMPO_RATIO}" >> "$CSV_FILE"
    
    echo "[GATE $GATE] -> ${SECS}s (delta=${DELTA_S}s, ratio=${TEMPO_RATIO})"
done

echo ""
echo "[COMPLETE] Results: $CSV_FILE"

# Find and display best gate
if command -v sort &>/dev/null && [ -f "$CSV_FILE" ]; then
    BEST_LINE=$(tail -n +2 "$CSV_FILE" | sort -t',' -k4,4n | head -1)
    if [ -n "$BEST_LINE" ]; then
        BEST_GATE=$(echo "$BEST_LINE" | cut -d',' -f1)
        BEST_ABS=$(echo "$BEST_LINE" | cut -d',' -f4)
        echo "[BEST] gate=$BEST_GATE (abs_delta=${BEST_ABS}s)"
    fi
fi
