#!/usr/bin/env bash
# Build A/B alternating WAV using concat demuxer (reliable on ffmpeg 4.4)
# Usage: make_ab_compare.sh <A.wav> <B.wav> [out.wav] [seg_secs=1.0] [repeats=2]
#
# Steps:
#  1. Extract SEG seconds from each input (force s16/stereo/44.1k)
#  2. Write temp a.wav/b.wav
#  3. Create list.txt with A,B repeated N times
#  4. Concat with -c copy

set -euo pipefail

DEBUG="${DEBUG:-0}"

if [ $# -lt 2 ]; then
    echo "Usage: $0 <A.wav> <B.wav> [out.wav] [seg_secs=1.0] [repeats=2]" >&2
    exit 1
fi

A_WAV="$1"
B_WAV="$2"
OUT_WAV="${3:-ab_compare.wav}"
SEG_SECS="${4:-1.0}"
REPEATS="${5:-2}"

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

# Create temp directory
TMPDIR=$(mktemp -d -t ab_compare.XXXXXX)
trap "rm -rf $TMPDIR" EXIT

[ "$DEBUG" = "1" ] && echo "[DEBUG] Temp dir: $TMPDIR"

# Extract segment A (force 44.1kHz, stereo, s16le)
[ "$DEBUG" = "1" ] && echo "[DEBUG] Extracting ${SEG_SECS}s from A: $A_WAV"
ffmpeg -y -i "$A_WAV" -t "$SEG_SECS" -ar 44100 -ac 2 -sample_fmt s16 "$TMPDIR/a.wav" ${DEBUG:+-loglevel debug} ${DEBUG:+-nostats} &>/dev/null

# Extract segment B
[ "$DEBUG" = "1" ] && echo "[DEBUG] Extracting ${SEG_SECS}s from B: $B_WAV"
ffmpeg -y -i "$B_WAV" -t "$SEG_SECS" -ar 44100 -ac 2 -sample_fmt s16 "$TMPDIR/b.wav" ${DEBUG:+-loglevel debug} ${DEBUG:+-nostats} &>/dev/null

# Create concat list (A, B repeated N times)
LIST_FILE="$TMPDIR/list.txt"
[ "$DEBUG" = "1" ] && echo "[DEBUG] Creating concat list with $REPEATS repeats"
for ((i=0; i<REPEATS; i++)); do
    echo "file 'a.wav'" >> "$LIST_FILE"
    echo "file 'b.wav'" >> "$LIST_FILE"
done

if [ "$DEBUG" = "1" ]; then
    echo "[DEBUG] Concat list:"
    cat "$LIST_FILE"
fi

# Create output directory if needed
OUT_DIR=$(dirname "$OUT_WAV")
mkdir -p "$OUT_DIR"

# Concat with -c copy (no re-encoding)
[ "$DEBUG" = "1" ] && echo "[DEBUG] Concatenating to: $OUT_WAV"
ffmpeg -y -f concat -safe 0 -i "$LIST_FILE" -c copy "$OUT_WAV" ${DEBUG:+-loglevel debug} &>/dev/null

if [ ! -f "$OUT_WAV" ]; then
    echo "[ERROR] Failed to create output: $OUT_WAV" >&2
    exit 1
fi

echo "[OK] Created A/B comparison: $OUT_WAV (${SEG_SECS}s segments, ${REPEATS} repeats)"

# Show duration if ffprobe available
if command -v ffprobe &>/dev/null; then
    DURATION=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$OUT_WAV" 2>/dev/null || echo "unknown")
    echo "[INFO] Total duration: ${DURATION}s"
fi
