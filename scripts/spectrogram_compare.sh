#!/usr/bin/env bash
# Generate side-by-side spectrogram comparison from two WAVs
# Usage: spectrogram_compare.sh <A.wav> <B.wav> [output.png]

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <A.wav> <B.wav> [output.png]" >&2
    exit 1
fi

A_WAV="$1"
B_WAV="$2"
OUTPUT_PNG="${3:-spectrogram_compare.png}"

if [ ! -f "$A_WAV" ]; then
    echo "[ERROR] Input A not found: $A_WAV" >&2
    exit 1
fi

if [ ! -f "$B_WAV" ]; then
    echo "[ERROR] Input B not found: $B_WAV" >&2
    exit 1
fi

if ! command -v ffmpeg &>/dev/null; then
    echo "[ERROR] ffmpeg not found in PATH" >&2
    exit 1
fi

# Create output directory if needed
OUT_DIR=$(dirname "$OUTPUT_PNG")
mkdir -p "$OUT_DIR"

# Generate side-by-side spectrogram comparison
# Use hstack to place them horizontally
ffmpeg -y -i "$A_WAV" -i "$B_WAV" \
    -filter_complex \
    "[0:a]showspectrumpic=s=960x1080:mode=combined:color=intensity:scale=log:fscale=log:win_func=hann:stop=8000[a]; \
     [1:a]showspectrumpic=s=960x1080:mode=combined:color=intensity:scale=log:fscale=log:win_func=hann:stop=8000[b]; \
     [a][b]hstack=inputs=2[out]" \
    -map "[out]" "$OUTPUT_PNG" &>/dev/null

if [ ! -f "$OUTPUT_PNG" ]; then
    echo "[ERROR] Failed to create comparison spectrogram: $OUTPUT_PNG" >&2
    exit 1
fi

echo "[OK] Created side-by-side spectrogram: $OUTPUT_PNG"
