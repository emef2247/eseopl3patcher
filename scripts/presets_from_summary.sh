#!/usr/bin/env bash
# Generate configs/gate_presets.csv from batch summary CSV files
# Usage: presets_from_summary.sh <summary1.csv> [summary2.csv ...] [-o output.csv]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

OUTPUT_CSV="${REPO_ROOT}/configs/gate_presets.csv"
SUMMARY_FILES=()

usage() {
    cat >&2 << EOF
Usage: $0 <summary1.csv> [summary2.csv ...] [-o output.csv]

Reads batch summary CSV files and generates a gate presets configuration.
Default output: configs/gate_presets.csv
EOF
    exit 1
}

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        -o) OUTPUT_CSV="$2"; shift 2 ;;
        -h|--help) usage ;;
        *.csv) SUMMARY_FILES+=("$1"); shift ;;
        *) echo "[ERROR] Unknown argument: $1" >&2; usage ;;
    esac
done

if [ ${#SUMMARY_FILES[@]} -eq 0 ]; then
    echo "[ERROR] No summary CSV files provided" >&2
    usage
fi

# Check input files exist
for f in "${SUMMARY_FILES[@]}"; do
    if [ ! -f "$f" ]; then
        echo "[ERROR] Summary file not found: $f" >&2
        exit 1
    fi
done

# Create output directory
mkdir -p "$(dirname "$OUTPUT_CSV")"

# Temporary file for merging
TMPFILE=$(mktemp)
trap "rm -f $TMPFILE" EXIT

# Write header
echo "file,gate,pre,offon,target_seconds,best_seconds,delta_s,tempo_ratio" > "$OUTPUT_CSV"

# Process each summary file
for SUMMARY in "${SUMMARY_FILES[@]}"; do
    echo "[INFO] Processing: $SUMMARY"
    
    # Skip header and process data lines
    tail -n +2 "$SUMMARY" | while IFS=',' read -r file gate best_secs delta_s abs_delta tempo_ratio rest; do
        # Extract pre/offon if available (may be in future format)
        # For now, use defaults since batch summary may not have these fields
        PRE="16"
        OFFON="16"
        
        # Use best_secs as target (approximation)
        TARGET_SECS="$best_secs"
        
        echo "${file},${gate},${PRE},${OFFON},${TARGET_SECS},${best_secs},${delta_s},${tempo_ratio}" >> "$TMPFILE"
    done
done

# Sort by file, then append to output
sort -t',' -k1,1 "$TMPFILE" >> "$OUTPUT_CSV"

LINE_COUNT=$(( $(wc -l < "$OUTPUT_CSV") - 1 ))
echo ""
echo "[OK] Generated: $OUTPUT_CSV"
echo "[INFO] $LINE_COUNT preset entries"
