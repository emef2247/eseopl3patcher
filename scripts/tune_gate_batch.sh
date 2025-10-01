#!/usr/bin/env bash
# Batch tune gate duration for multiple VGM files
# Usage: tune_gate_batch.sh -m manifest.txt -d vgm_dir -b baseline_dir [-g "gates"] [-o output_dir]
#
# Default FREQSEQ=AB
# Writes summary.csv: file,gate,best_seconds,delta_s,abs_delta_s,tempo_ratio
# Fixed: summary one-line issue (Python helper with no trailing newline, trim CR/LF)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Defaults
MANIFEST=""
VGM_DIR=""
BASELINE_DIR=""
GATES="${GATES:-512 1024 1536 2048 4096 8192 10240 12288 16384}"
OUTPUT_DIR="tune_gate_batch_output"
FREQSEQ="${ESEOPL3_FREQSEQ:-AB}"
DETUNE="${DETUNE:-1.0}"

usage() {
    cat >&2 << EOF
Usage: $0 -m manifest.txt -d vgm_dir -b baseline_dir [-g "gates"] [-o output_dir]

Options:
  -m <manifest.txt>  Manifest file (one VGM basename per line, no extension)
  -d <vgm_dir>       Directory containing input VGM files
  -b <baseline_dir>  Directory containing baseline WAV files
  -g <gates>         Space-separated gate values (default: 512 1024 ... 16384)
  -o <output_dir>    Output directory (default: tune_gate_batch_output)

Environment:
  ESEOPL3_FREQSEQ    Frequency sequence mode (default: AB)
  DETUNE             Detune factor (default: 1.0)
  GATES              Alternative way to specify gate values

Output:
  summary.csv with columns: file,gate,best_seconds,delta_s,abs_delta_s,tempo_ratio
  Picks best row by minimal abs_delta_s
EOF
    exit 1
}

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        -m) MANIFEST="$2"; shift 2 ;;
        -d) VGM_DIR="$2"; shift 2 ;;
        -b) BASELINE_DIR="$2"; shift 2 ;;
        -g) GATES="$2"; shift 2 ;;
        -o) OUTPUT_DIR="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "[ERROR] Unknown option: $1" >&2; usage ;;
    esac
done

if [ -z "$MANIFEST" ] || [ -z "$VGM_DIR" ] || [ -z "$BASELINE_DIR" ]; then
    echo "[ERROR] Missing required arguments" >&2
    usage
fi

if [ ! -f "$MANIFEST" ]; then
    echo "[ERROR] Manifest not found: $MANIFEST" >&2
    exit 1
fi

if [ ! -d "$VGM_DIR" ]; then
    echo "[ERROR] VGM directory not found: $VGM_DIR" >&2
    exit 1
fi

if [ ! -d "$BASELINE_DIR" ]; then
    echo "[ERROR] Baseline directory not found: $BASELINE_DIR" >&2
    exit 1
fi

# Setup output
mkdir -p "$OUTPUT_DIR"
SUMMARY_CSV="$OUTPUT_DIR/summary.csv"

echo "[INFO] Batch tuning gate duration"
echo "[INFO] FREQSEQ=$FREQSEQ, DETUNE=$DETUNE"
echo "[INFO] Gates: $GATES"

# Check patcher
PATCHER="${REPO_ROOT}/build/eseopl3patcher"
if [ ! -x "$PATCHER" ]; then
    echo "[ERROR] Patcher not found or not executable: $PATCHER" >&2
    exit 1
fi

# Python helper to find best row (prints without trailing newline, handles edge cases)
python3 - <<'PYTHON_HELPER' > "$OUTPUT_DIR/best_finder.py"
import sys, csv
def find_best(csv_file):
    """Find row with minimal abs_delta_s, return one line"""
    try:
        with open(csv_file, 'r') as f:
            reader = csv.DictReader(f)
            rows = list(reader)
            if not rows:
                return None
            # Find row with minimum abs_delta_s
            best = min(rows, key=lambda r: float(r.get('abs_delta_s', 999999)))
            return best
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return None

if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(1)
    best = find_best(sys.argv[1])
    if best:
        # Print without trailing newline
        sys.stdout.write(f"{best['gate']},{best['seconds']},{best['delta_s']},{best['abs_delta_s']},{best['tempo_ratio']}")
PYTHON_HELPER

# Write summary header
echo "file,gate,best_seconds,delta_s,abs_delta_s,tempo_ratio" > "$SUMMARY_CSV"

# Process each file in manifest
while IFS= read -r BASENAME || [ -n "$BASENAME" ]; do
    # Skip comments and empty lines
    [[ "$BASENAME" =~ ^#.*$ ]] && continue
    [[ -z "$BASENAME" ]] && continue
    
    VGM_FILE="${VGM_DIR}/${BASENAME}.vgm"
    WAV_FILE="${BASELINE_DIR}/${BASENAME}.wav"
    
    if [ ! -f "$VGM_FILE" ]; then
        echo "[WARN] VGM not found: $VGM_FILE"
        continue
    fi
    
    if [ ! -f "$WAV_FILE" ]; then
        echo "[WARN] Baseline WAV not found: $WAV_FILE"
        continue
    fi
    
    echo ""
    echo "[FILE] $BASENAME"
    
    FILE_OUTPUT="${OUTPUT_DIR}/${BASENAME}"
    mkdir -p "$FILE_OUTPUT"
    
    # Run tune_gate_duration.sh for this file
    "$SCRIPT_DIR/tune_gate_duration.sh" \
        -i "$VGM_FILE" \
        -b "$WAV_FILE" \
        -g "$GATES" \
        -o "$FILE_OUTPUT" || {
        echo "[ERROR] Failed to tune $BASENAME"
        continue
    }
    
    # Find best result using Python helper
    GATES_CSV="${FILE_OUTPUT}/${BASENAME}_gates.csv"
    if [ ! -f "$GATES_CSV" ]; then
        echo "[ERROR] Gates CSV not found: $GATES_CSV"
        continue
    fi
    
    # Get best line (no trailing newline from Python)
    BEST_LINE=$(python3 "$OUTPUT_DIR/best_finder.py" "$GATES_CSV" 2>/dev/null)
    
    # Trim any CR/LF just in case
    BEST_LINE=$(echo "$BEST_LINE" | tr -d '\r\n')
    
    if [ -n "$BEST_LINE" ]; then
        # Append to summary: file,gate,best_seconds,delta_s,abs_delta_s,tempo_ratio
        echo "${BASENAME},${BEST_LINE}" >> "$SUMMARY_CSV"
        echo "[BEST] $BEST_LINE"
    else
        echo "[WARN] Could not find best result for $BASENAME"
    fi
    
done < "$MANIFEST"

echo ""
echo "[COMPLETE] Batch tuning finished"
echo "[SUMMARY] $SUMMARY_CSV"
echo ""
echo "Top 5 results by abs_delta_s:"
if command -v sort &>/dev/null; then
    tail -n +2 "$SUMMARY_CSV" | sort -t',' -k5,5n | head -5 | while IFS=',' read -r file gate secs delta abs ratio; do
        printf "  %-30s gate=%-6s abs_delta=%ss\n" "$file" "$gate" "$abs"
    done
fi
