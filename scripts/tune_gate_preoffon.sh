#!/usr/bin/env bash
# Stage-2 tuner: sweep pre/offon ranges with fixed gate to approach target duration
# Usage: tune_gate_preoffon.sh -i input.vgm -b baseline.wav -g gate_samples -t target_secs \
#        --pre-range start:end:step --offon-range start:end:step [-o output_dir]
#
# Writes CSV: pre,offon,seconds,delta_s,abs_delta_s,tempo_ratio,analysis_dir,wav_path
# Prints best combo at end

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Defaults
INPUT_VGM=""
BASELINE_WAV=""
GATE_SAMPLES=""
TARGET_SECS=""
PRE_RANGE="0:128:16"
OFFON_RANGE="0:128:16"
OUTPUT_DIR="tune_preoffon_output"
FREQSEQ="${ESEOPL3_FREQSEQ:-AB}"
DETUNE="${DETUNE:-1.0}"

usage() {
    cat >&2 << EOF
Usage: $0 -i input.vgm -b baseline.wav -g gate_samples -t target_secs \\
          [--pre-range start:end:step] [--offon-range start:end:step] [-o output_dir]

Options:
  -i <input.vgm>       Input YM2413 VGM file
  -b <baseline.wav>    Baseline WAV for duration reference
  -g <gate_samples>    Fixed gate duration (e.g., 8192)
  -t <target_secs>     Target duration in seconds
  --pre-range <range>  Pre-keyon wait range (default: 0:128:16)
  --offon-range <range> Off-to-on wait range (default: 0:128:16)
  -o <output_dir>      Output directory (default: tune_preoffon_output)

Environment:
  ESEOPL3_FREQSEQ      Frequency sequence mode (default: AB)
  DETUNE               Detune factor (default: 1.0)
EOF
    exit 1
}

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        -i) INPUT_VGM="$2"; shift 2 ;;
        -b) BASELINE_WAV="$2"; shift 2 ;;
        -g) GATE_SAMPLES="$2"; shift 2 ;;
        -t) TARGET_SECS="$2"; shift 2 ;;
        --pre-range) PRE_RANGE="$2"; shift 2 ;;
        --offon-range) OFFON_RANGE="$2"; shift 2 ;;
        -o) OUTPUT_DIR="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "[ERROR] Unknown option: $1" >&2; usage ;;
    esac
done

# Validate required arguments
if [ -z "$INPUT_VGM" ] || [ -z "$BASELINE_WAV" ] || [ -z "$GATE_SAMPLES" ] || [ -z "$TARGET_SECS" ]; then
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

# Parse ranges
IFS=':' read -r PRE_START PRE_END PRE_STEP <<< "$PRE_RANGE"
IFS=':' read -r OFFON_START OFFON_END OFFON_STEP <<< "$OFFON_RANGE"

# Setup output directory
mkdir -p "$OUTPUT_DIR"
CSV_FILE="$OUTPUT_DIR/preoffon_sweep.csv"

# Write CSV header
echo "pre,offon,seconds,delta_s,abs_delta_s,tempo_ratio,analysis_dir,wav_path" > "$CSV_FILE"

echo "[INFO] Starting pre/offon sweep with fixed gate=$GATE_SAMPLES"
echo "[INFO] Target: ${TARGET_SECS}s, Pre: $PRE_RANGE, Offon: $OFFON_RANGE"
echo "[INFO] FREQSEQ=$FREQSEQ, DETUNE=$DETUNE"

# Check for required tools
PATCHER="${REPO_ROOT}/build/eseopl3patcher"
if [ ! -x "$PATCHER" ]; then
    echo "[ERROR] Patcher not found or not executable: $PATCHER" >&2
    exit 1
fi

# Helper to get WAV duration
get_wav_duration() {
    local wav="$1"
    if command -v ffprobe &>/dev/null; then
        ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$wav" 2>/dev/null || echo "0"
    else
        echo "0"
    fi
}

# Sweep loop
BEST_PRE=""
BEST_OFFON=""
BEST_ABS_DELTA=999999

for ((PRE=PRE_START; PRE<=PRE_END; PRE+=PRE_STEP)); do
    for ((OFFON=OFFON_START; OFFON<=OFFON_END; OFFON+=OFFON_STEP)); do
        ANALYSIS_DIR="${OUTPUT_DIR}/pre${PRE}_offon${OFFON}"
        mkdir -p "$ANALYSIS_DIR"
        
        VGM_OUT="${ANALYSIS_DIR}/converted.vgm"
        WAV_OUT="${ANALYSIS_DIR}/converted.wav"
        
        # Run patcher with fixed gate and variable pre/offon
        ESEOPL3_FREQSEQ="$FREQSEQ" "$PATCHER" "$INPUT_VGM" "$DETUNE" \
            --convert-ym2413 \
            --min-gate-samples "$GATE_SAMPLES" \
            --pre-keyon-wait "$PRE" \
            --min-off-on-wait "$OFFON" \
            -o "$VGM_OUT" &>/dev/null || continue
        
        # Render to WAV (try multiple players)
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
            echo "[WARN] Could not determine duration for pre=$PRE offon=$OFFON"
            continue
        fi
        
        # Calculate metrics
        DELTA_S=$(echo "$SECS - $TARGET_SECS" | bc -l)
        ABS_DELTA=$(echo "scale=6; if ($DELTA_S < 0) -($DELTA_S) else $DELTA_S" | bc -l)
        TEMPO_RATIO=$(echo "scale=6; $SECS / $TARGET_SECS" | bc -l)
        
        # Write to CSV
        echo "${PRE},${OFFON},${SECS},${DELTA_S},${ABS_DELTA},${TEMPO_RATIO},${ANALYSIS_DIR},${WAV_OUT}" >> "$CSV_FILE"
        
        # Track best
        BETTER=$(echo "$ABS_DELTA < $BEST_ABS_DELTA" | bc -l)
        if [ "$BETTER" = "1" ]; then
            BEST_PRE="$PRE"
            BEST_OFFON="$OFFON"
            BEST_ABS_DELTA="$ABS_DELTA"
        fi
        
        echo "[SWEEP] pre=$PRE offon=$OFFON -> ${SECS}s (delta=${DELTA_S}s, abs=${ABS_DELTA})"
    done
done

echo ""
echo "[COMPLETE] Sweep finished. Results: $CSV_FILE"
echo ""
echo "Best combination:"
echo "  pre=$BEST_PRE offon=$BEST_OFFON (abs_delta=${BEST_ABS_DELTA}s)"
