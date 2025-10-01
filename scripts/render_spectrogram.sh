#!/usr/bin/env bash
# render_spectrogram.sh
# Render VGM to WAV and generate spectrogram
# Uses vgm2wav if available, falls back to vgmplay with -o support detection

set -e

usage() {
    cat <<EOF
Usage: $0 [OPTIONS] <input.vgm>

Options:
  -o, --output DIR      Output directory (default: analysis)
  -w, --wav FILE        Output WAV filename (default: derived from VGM)
  -s, --spec FILE       Output spectrogram PNG filename (default: derived from WAV)
  --prefer-vgmplay      Prefer vgmplay over vgm2wav if both available
  -h, --help            Show this help

Environment:
  PREFER_VGMPLAY=1      Same as --prefer-vgmplay flag
  VGMPLAY               Path to vgmplay binary (default: search in PATH and tools/bin)
  VGM2WAV               Path to vgm2wav binary (default: search in PATH and tools/bin)
EOF
}

# Default values
OUT_DIR="analysis"
WAV_FILE=""
SPEC_FILE=""
PREFER_VGMPLAY="${PREFER_VGMPLAY:-0}"
INPUT_VGM=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o|--output)
            OUT_DIR="$2"
            shift 2
            ;;
        -w|--wav)
            WAV_FILE="$2"
            shift 2
            ;;
        -s|--spec)
            SPEC_FILE="$2"
            shift 2
            ;;
        --prefer-vgmplay)
            PREFER_VGMPLAY=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
        *)
            if [[ -z "$INPUT_VGM" ]]; then
                INPUT_VGM="$1"
            else
                echo "Multiple input files not supported" >&2
                exit 1
            fi
            shift
            ;;
    esac
done

if [[ -z "$INPUT_VGM" ]]; then
    echo "Error: No input VGM file specified" >&2
    usage >&2
    exit 1
fi

if [[ ! -f "$INPUT_VGM" ]]; then
    echo "Error: Input file not found: $INPUT_VGM" >&2
    exit 1
fi

# Create output directory
mkdir -p "$OUT_DIR"

# Derive output filenames if not specified
BASENAME=$(basename "$INPUT_VGM" .vgm)
if [[ -z "$WAV_FILE" ]]; then
    WAV_FILE="$OUT_DIR/${BASENAME}.wav"
fi
if [[ -z "$SPEC_FILE" ]]; then
    SPEC_FILE="$OUT_DIR/${BASENAME}_spec.png"
fi

# Detect available tools
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS_BIN="$PROJECT_ROOT/tools/bin"

# Search for vgm2wav
VGM2WAV_BIN=""
if [[ -n "$VGM2WAV" && -x "$VGM2WAV" ]]; then
    VGM2WAV_BIN="$VGM2WAV"
elif command -v vgm2wav >/dev/null 2>&1; then
    VGM2WAV_BIN="$(command -v vgm2wav)"
elif [[ -x "$TOOLS_BIN/vgm2wav" ]]; then
    VGM2WAV_BIN="$TOOLS_BIN/vgm2wav"
fi

# Search for vgmplay
VGMPLAY_BIN=""
if [[ -n "$VGMPLAY" && -x "$VGMPLAY" ]]; then
    VGMPLAY_BIN="$VGMPLAY"
elif command -v VGMPlay >/dev/null 2>&1; then
    VGMPLAY_BIN="$(command -v VGMPlay)"
elif command -v vgmplay >/dev/null 2>&1; then
    VGMPLAY_BIN="$(command -v vgmplay)"
elif [[ -x "$TOOLS_BIN/VGMPlay" ]]; then
    VGMPLAY_BIN="$TOOLS_BIN/VGMPlay"
elif [[ -x "$TOOLS_BIN/vgmplay" ]]; then
    VGMPLAY_BIN="$TOOLS_BIN/vgmplay"
fi

# Detect vgmplay -o support
VGMPLAY_HAS_O=0
if [[ -n "$VGMPLAY_BIN" ]]; then
    if "$VGMPLAY_BIN" -? 2>&1 | grep -qE '\-o( |,|=)'; then
        VGMPLAY_HAS_O=1
    elif "$VGMPLAY_BIN" --help 2>&1 | grep -qE '\-o( |,|=)'; then
        VGMPLAY_HAS_O=1
    fi
fi

# Choose renderer
RENDERER=""
if [[ "$PREFER_VGMPLAY" == "1" ]]; then
    # User prefers vgmplay
    if [[ -n "$VGMPLAY_BIN" && "$VGMPLAY_HAS_O" == "1" ]]; then
        RENDERER="vgmplay"
    elif [[ -n "$VGM2WAV_BIN" ]]; then
        RENDERER="vgm2wav"
    fi
else
    # Default: prefer vgm2wav
    if [[ -n "$VGM2WAV_BIN" ]]; then
        RENDERER="vgm2wav"
    elif [[ -n "$VGMPLAY_BIN" && "$VGMPLAY_HAS_O" == "1" ]]; then
        RENDERER="vgmplay"
    fi
fi

if [[ -z "$RENDERER" ]]; then
    echo "Error: No suitable VGM renderer found" >&2
    echo "  vgm2wav: ${VGM2WAV_BIN:-not found}" >&2
    echo "  vgmplay: ${VGMPLAY_BIN:-not found}${VGMPLAY_HAS_O:+ (no -o support)}" >&2
    exit 1
fi

# Render VGM to WAV
echo "[render_spectrogram] Rendering $INPUT_VGM -> $WAV_FILE using $RENDERER"
if [[ "$RENDERER" == "vgm2wav" ]]; then
    "$VGM2WAV_BIN" "$INPUT_VGM" "$WAV_FILE" > "$OUT_DIR/${BASENAME}_render.log" 2>&1
elif [[ "$RENDERER" == "vgmplay" ]]; then
    "$VGMPLAY_BIN" -o "$WAV_FILE" -l 1 "$INPUT_VGM" > "$OUT_DIR/${BASENAME}_render.log" 2>&1
fi

if [[ ! -f "$WAV_FILE" ]]; then
    echo "Error: Failed to generate WAV file" >&2
    exit 1
fi

# Generate spectrogram
echo "[render_spectrogram] Generating spectrogram $WAV_FILE -> $SPEC_FILE"
SPECTRO_SCRIPT="$SCRIPT_DIR/wav_spectrogram.py"
if [[ ! -f "$SPECTRO_SCRIPT" ]]; then
    echo "Warning: Spectrogram script not found: $SPECTRO_SCRIPT" >&2
    echo "Skipping spectrogram generation" >&2
    exit 0
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "Warning: python3 not found, skipping spectrogram generation" >&2
    exit 0
fi

python3 "$SPECTRO_SCRIPT" "$WAV_FILE" --out "$SPEC_FILE" --max_freq 8000 --n_fft 4096 --hop 512 > "$OUT_DIR/${BASENAME}_spectro.log" 2>&1 || {
    echo "Warning: Spectrogram generation failed (check $OUT_DIR/${BASENAME}_spectro.log)" >&2
}

echo "[render_spectrogram] Done. Output: $WAV_FILE, $SPEC_FILE"
