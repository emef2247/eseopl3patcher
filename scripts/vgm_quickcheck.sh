#!/usr/bin/env bash
# Quick heuristic check for YMF262 (OPL3) activity in a VGM
set -euo pipefail
if [ $# -lt 1 ]; then
  echo "Usage: $0 <file.vgm>"
  exit 1
fi
f="$1"
if ! command -v vgm2txt >/dev/null 2>&1; then
  echo "[WARN] vgm2txt not found in PATH."
fi

echo "# File: $f"
if command -v vgm2txt >/dev/null 2>&1; then
  total_lines=$(vgm2txt "$f" | wc -l)
  ymf_lines=$(vgm2txt "$f" | grep -c 'YMF262')
  keyon_lines=$(vgm2txt "$f" | grep -E 'YMF262:.*Reg 0xB[0-8].*Data.*' | wc -l)
  tl_lines=$(vgm2txt "$f" | grep -E 'YMF262:.*Reg 0x4[0-9A-F]' | wc -l)
  muted=$(vgm2txt "$f" | grep -E 'YMF262:.*Reg 0x4[0-9A-F].*Data 0x3F' | wc -l)

  echo "Total lines:      $total_lines"
  echo "YMF262 lines:     $ymf_lines"
  echo "KeyOn regs:       $keyon_lines"
  echo "TL regs:          $tl_lines"
  echo "TL=0x3F (muted):  $muted"
  if [ "$keyon_lines" -eq 0 ]; then
    echo "[SUSPECT] No KeyOn register writes detected."
  fi
  if [ "$tl_lines" -gt 0 ] && [ "$tl_lines" -eq "$muted" ]; then
    echo "[SUSPECT] All TL writes are 0x3F (full attenuation) => likely silent."
  fi
else
  echo "vgm2txt not installed; limited check only."
fi

# Rough size check
size=$(stat -c%s "$f" 2>/dev/null || echo 0)
echo "File size bytes: $size"

