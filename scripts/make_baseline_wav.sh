#!/usr/bin/env bash
# Make baseline WAV from input VGM using vgm2wav or vgmplay
# Usage: make_baseline_wav.sh <input.vgm> [output.wav]

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <input.vgm> [output.wav]" >&2
    exit 1
fi

INPUT_VGM="$1"
OUTPUT_WAV="${2:-${INPUT_VGM%.vgm}.wav}"

if [ ! -f "$INPUT_VGM" ]; then
    echo "[ERROR] Input VGM not found: $INPUT_VGM" >&2
    exit 1
fi

# Try vgm2wav first, fallback to vgmplay
if command -v vgm2wav &>/dev/null; then
    echo "[INFO] Using vgm2wav to render baseline WAV..."
    vgm2wav "$INPUT_VGM" "$OUTPUT_WAV"
elif command -v vgmplay &>/dev/null; then
    echo "[INFO] Using vgmplay to render baseline WAV..."
    vgmplay -o "$OUTPUT_WAV" -l 1 "$INPUT_VGM"
elif command -v VGMPlay &>/dev/null; then
    echo "[INFO] Using VGMPlay to render baseline WAV..."
    VGMPlay -o "$OUTPUT_WAV" -l 1 "$INPUT_VGM"
else
    echo "[ERROR] No VGM player found (vgm2wav, vgmplay, or VGMPlay)" >&2
    exit 1
fi

if [ ! -f "$OUTPUT_WAV" ]; then
    echo "[ERROR] Failed to create WAV file: $OUTPUT_WAV" >&2
    exit 1
fi

# Show duration using ffprobe if available
if command -v ffprobe &>/dev/null; then
    DURATION=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$OUTPUT_WAV" 2>/dev/null || echo "unknown")
    echo "[OK] Created: $OUTPUT_WAV (duration: ${DURATION}s)"
else
    echo "[OK] Created: $OUTPUT_WAV"
fi
