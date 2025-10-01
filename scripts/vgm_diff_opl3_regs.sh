#!/usr/bin/env bash
# Compare OPL3 TL/FB/WS register writes between two VGMs using vgm2txt
# Usage: vgm_diff_opl3_regs.sh <vgm1> <vgm2>

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <vgm1> <vgm2>" >&2
    exit 1
fi

VGM1="$1"
VGM2="$2"

if [ ! -f "$VGM1" ]; then
    echo "[ERROR] VGM1 not found: $VGM1" >&2
    exit 1
fi

if [ ! -f "$VGM2" ]; then
    echo "[ERROR] VGM2 not found: $VGM2" >&2
    exit 1
fi

if ! command -v vgm2txt &>/dev/null; then
    echo "[ERROR] vgm2txt not found in PATH" >&2
    echo "[INFO] This tool requires vgm2txt from VGMPlay suite" >&2
    exit 1
fi

TMPDIR=$(mktemp -d -t vgm_diff.XXXXXX)
trap "rm -rf $TMPDIR" EXIT

echo "[INFO] Dumping VGM1..."
vgm2txt "$VGM1" "$TMPDIR/vgm1.txt" &>/dev/null

echo "[INFO] Dumping VGM2..."
vgm2txt "$VGM2" "$TMPDIR/vgm2.txt" &>/dev/null

# Extract OPL3 register writes (0x5E = port0, 0x5F = port1)
# Focus on TL (0x40-0x55), FB/CNT (0xC0-0xC8), WS (0xE0-0xF5)
echo "[INFO] Extracting OPL3 TL/FB/WS register writes..."

grep -E "^(0x5E|0x5F) (0x[4-5][0-9A-Fa-f]|0xC[0-8]|0x[EF][0-9A-Fa-f])" "$TMPDIR/vgm1.txt" > "$TMPDIR/vgm1_opl3.txt" || true
grep -E "^(0x5E|0x5F) (0x[4-5][0-9A-Fa-f]|0xC[0-8]|0x[EF][0-9A-Fa-f])" "$TMPDIR/vgm2.txt" > "$TMPDIR/vgm2_opl3.txt" || true

echo ""
echo "=== OPL3 Register Diff (TL/FB/WS) ==="
echo ""

if command -v diff &>/dev/null; then
    diff -u "$TMPDIR/vgm1_opl3.txt" "$TMPDIR/vgm2_opl3.txt" || true
else
    echo "[WARN] diff command not found, showing raw extracts:"
    echo ""
    echo "--- VGM1 ---"
    head -20 "$TMPDIR/vgm1_opl3.txt"
    echo ""
    echo "--- VGM2 ---"
    head -20 "$TMPDIR/vgm2_opl3.txt"
fi

echo ""
echo "[INFO] Full dumps available at:"
echo "  $TMPDIR/vgm1.txt"
echo "  $TMPDIR/vgm2.txt"
