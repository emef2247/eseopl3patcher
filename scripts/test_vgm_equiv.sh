#!/usr/bin/env bash
# Equivalence test for OPLL->OPL3 converter outputs.
# Usage:
#   scripts/test_vgm_equiv.sh <converter_binary>
#   scripts/test_vgm_equiv.sh <converter_binary> --update-baseline
#   scripts/test_vgm_equiv.sh <converter_binary> --init-baseline
#
# Layout (relative to repo root):
#   tests/equiv/
#     manifest.txt
#     inputs/*.vgm             (committed)
#     baseline/*.vgm           (committed baseline; names: <base>OPL3.vgm)
#     out_new/*.vgm            (gitignored)
#     logs/*.log
#     txt/*.norm               (optional textual diff)
#
# Exit codes:
#   0 = all identical
#   1 = differences found
#   2 = setup / argument error

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "$REPO_ROOT"

if [ $# -lt 1 ]; then
  echo "Usage: $0 <converter_binary> [--init-baseline|--update-baseline]"
  exit 2
fi

CONV="$1"; shift || true
MODE="normal"
case "${1:-}" in
  --init-baseline) MODE="init"; shift ;;
  --update-baseline) MODE="update"; shift ;;
  *) ;;
esac

EQUIV_DIR="tests/equiv"
MANIFEST="$EQUIV_DIR/manifest.txt"
INPUT_DIR="$EQUIV_DIR/inputs"
BASELINE_DIR="$EQUIV_DIR/baseline"
NEW_DIR="$EQUIV_DIR/out_new"
LOG_DIR="$EQUIV_DIR/logs"
TXT_DIR="$EQUIV_DIR/txt"

for d in "$BASELINE_DIR" "$NEW_DIR" "$LOG_DIR" "$TXT_DIR"; do
  mkdir -p "$d"
done

if [ ! -f "$MANIFEST" ]; then
  echo "[ERROR] manifest not found: $MANIFEST" >&2
  exit 2
fi

# Collect test files
mapfile -t FILES < <(grep -v '^[[:space:]]*#' "$MANIFEST" | sed '/^[[:space:]]*$/d')
if [ ${#FILES[@]} -eq 0 ]; then
  echo "[ERROR] manifest has no entries"
  exit 2
fi

missing=0
for f in "${FILES[@]}"; do
  if [ ! -f "$INPUT_DIR/$f" ]; then
    echo "[ERROR] Missing input: $INPUT_DIR/$f" >&2
    missing=1
  fi
done
[ $missing -eq 0 ] || exit 2

# Decide baseline generation
if [ "$MODE" = "update" ]; then
  echo "[INFO] Forcing full baseline regeneration."
  rm -f "$BASELINE_DIR"/*.vgm
elif [ "$MODE" = "init" ]; then
  echo "[INFO] Initializing baseline: will only create missing ones."
fi

need_baseline_gen=0
for f in "${FILES[@]}"; do
  base="${f%.vgm}OPL3.vgm"
  if [ ! -f "$BASELINE_DIR/$base" ]; then
    need_baseline_gen=1
    break
  fi
done

if [ "$MODE" = "update" ] || [ "$MODE" = "init" ] || [ $need_baseline_gen -eq 1 ]; then
  echo "[INFO] Generating baseline artifacts..."
  for f in "${FILES[@]}"; do
    in="$INPUT_DIR/$f"
    base_noext="${f%.vgm}"
    out_name="${base_noext}OPL3.vgm"
    log="$LOG_DIR/${base_noext}_baseline.log"
    "$CONV" "$in" >"$log" 2>&1
    if [ ! -f "${out_name}" ]; then
      echo "[ERROR] Converter did not produce expected output '${out_name}' for input $f" >&2
      exit 2
    fi
    mv -f "${out_name}" "$BASELINE_DIR/$out_name"
  done
  echo "[INFO] Baseline generation done."
fi

echo "[INFO] Generating new outputs..."
for f in "${FILES[@]}"; do
  in="$INPUT_DIR/$f"
  base_noext="${f%.vgm}"
  out_name="${base_noext}OPL3.vgm"
  log="$LOG_DIR/${base_noext}_new.log"
  "$CONV" "$in" >"$log" 2>&1
  if [ ! -f "${out_name}" ]; then
    echo "[ERROR] Converter did not produce expected output '${out_name}' for input $f" >&2
    exit 2
  fi
  mv -f "${out_name}" "$NEW_DIR/$out_name"
done

echo "[INFO] Comparing..."
diff_found=0

have_vgm2txt=0
if command -v vgm2txt >/dev/null 2>&1; then
  have_vgm2txt=1
else
  echo "[WARN] vgm2txt not found; textual diffs disabled."
fi

normalize_txt () {
  # Normalize lines that are known to vary (example: File Version). Extend if必要.
  sed -E 's/File Version:.*//g'
}

for f in "${FILES[@]}"; do
  out_base="${f%.vgm}OPL3.vgm"
  ref="$BASELINE_DIR/$out_base"
  new="$NEW_DIR/$out_base"
  if ! cmp -s "$ref" "$new"; then
    echo "[DIFF] $f"
    diff_found=1
    if [ $have_vgm2txt -eq 1 ]; then
      ref_txt="$TXT_DIR/${f%.vgm}_ref.txt"
      new_txt="$TXT_DIR/${f%.vgm}_new.txt"
      vgm2txt "$ref" > "$ref_txt" || true
      vgm2txt "$new" > "$new_txt" || true
      normalize_txt < "$ref_txt" > "$ref_txt.norm"
      normalize_txt < "$new_txt" > "$new_txt.norm"
      echo "----- textual diff (normalized) for $f -----"
      diff -u "$ref_txt.norm" "$new_txt.norm" || true
      echo "-------------------------------------------"
    fi
  else
    echo "[OK]  $f"
  fi
done

if [ $diff_found -eq 0 ]; then
  echo "[RESULT] ✅ All outputs identical."
  exit 0
else
  echo "[RESULT] ❌ Differences detected."
  echo "         (Update baseline if intentional: $0 $CONV --update-baseline)"
  exit 1
fi