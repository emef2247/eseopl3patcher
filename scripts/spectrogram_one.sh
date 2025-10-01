#!/usr/bin/env bash
# Generate spectrogram PNG from WAV using ffmpeg showspectrumpic
# Usage: spectrogram_one.sh <input.wav> [output.png]

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <input.wav> [output.png]" >&2
    exit 1
fi

INPUT_WAV="$1"
OUTPUT_PNG="${2:-${INPUT_WAV%.wav}_spec.png}"

if [ ! -f "$INPUT_WAV" ]; then
    echo "[ERROR] Input WAV not found: $INPUT_WAV" >&2
    exit 1
fi

if ! command -v ffmpeg &>/dev/null; then
    echo "[ERROR] ffmpeg not found in PATH" >&2
    exit 1
fi

# Create output directory if needed
OUT_DIR=$(dirname "$OUTPUT_PNG")
mkdir -p "$OUT_DIR"

# Generate spectrogram using showspectrumpic
# Parameters tuned for music analysis: 0-8kHz range
ffmpeg -y -i "$INPUT_WAV" \
    -lavfi "showspectrumpic=s=1920x1080:mode=combined:color=intensity:scale=log:fscale=log:win_func=hann:stop=8000" \
    "$OUTPUT_PNG" &>/dev/null

if [ ! -f "$OUTPUT_PNG" ]; then
    echo "[ERROR] Failed to create spectrogram: $OUTPUT_PNG" >&2
    exit 1
fi

echo "[OK] Created spectrogram: $OUTPUT_PNG"
