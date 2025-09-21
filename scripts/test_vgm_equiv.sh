#!/usr/bin/env bash
# OPLL -> OPL3 equivalence test harness
#
# Usage:
#   scripts/test_vgm_equiv.sh <converter_binary> [--init-baseline|--update-baseline]
#
# 環境変数:
#   DETUNE=0                  # ← デフォルト 0 に変更 (以前 1.0)
#   EXTRA_ARGS="--convert-ym2413 --strip-non-opl"
#
# Exit codes:
#   0: 正常 (差分なし)
#   1: 差分あり
#   2: セットアップ/引数エラー
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "$REPO_ROOT"

if [ $# -lt 1 ]; then
  echo "Usage: $0 <converter_binary> [--init-baseline|--update-baseline]" >&2
  exit 2
fi

CONV="$1"; shift || true
MODE="normal"
case "${1:-}" in
  --init-baseline) MODE="init"; shift ;;
  --update-baseline) MODE="update"; shift ;;
  *) ;;
esac

if [ ! -x "$CONV" ]; then
  echo "[ERROR] Converter not found or not executable: $CONV" >&2
  exit 2
fi

# デフォルト detune を 0 に
DETUNE="${DETUNE:-0}"
EXTRA_ARGS="${EXTRA_ARGS:-}"

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

mapfile -t FILES < <(grep -v '^[[:space:]]*#' "$MANIFEST" | sed '/^[[:space:]]*$/d')
if [ ${#FILES[@]} -eq 0 ]; then
  echo "[ERROR] manifest has no entries" >&2
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

run_convert () {
  local in_rel="$1"
  local out_dir="$2"
  local mode_label="$3"
  local in_path="$INPUT_DIR/$in_rel"
  local stem="${in_rel%.vgm}"
  local out_file="${stem}OPL3.vgm"
  local tmp_out="${out_dir}/${stem}.tmp.vgm"
  local final_out="${out_dir}/${out_file}"
  local log="$LOG_DIR/${stem}_${mode_label}.log"

  if [ "$mode_label" = "baseline" ] && [ "$MODE" = "init" ] && [ -f "$final_out" ]; then
    echo "[SKIP] baseline exists: $out_file"
    return 0
  fi

  rm -f "$tmp_out" "$final_out"

  if ! "$CONV" "$in_path" "$DETUNE" $EXTRA_ARGS -o "$tmp_out" >"$log" 2>&1; then
    echo "[ERROR] Converter failed for $in_rel (see $log)" >&2
    tail -n 40 "$log" || true
    return 2
  fi

  if [ ! -f "$tmp_out" ]; then
    echo "[ERROR] Expected output not found: $tmp_out" >&2
    tail -n 40 "$log" || true
    return 2
  fi

  mv -f "$tmp_out" "$final_out"
  echo "[OK] Generated ${mode_label}: $out_file"
}

generate_set () {
  local label="$1"
  local target_dir
  if [ "$label" = "baseline" ]; then
    target_dir="$BASELINE_DIR"
  else
    target_dir="$NEW_DIR"
  fi
  for f in "${FILES[@]}"; do
    run_convert "$f" "$target_dir" "$label" || exit $?
  done
}

if [ "$MODE" = "update" ] || [ "$MODE" = "init" ] || [ $need_baseline_gen -eq 1 ]; then
  echo "[INFO] Generating baseline artifacts (DETUNE=$DETUNE EXTRA_ARGS='$EXTRA_ARGS')..."
  generate_set "baseline"
  echo "[INFO] Baseline generation done."
fi

echo "[INFO] Generating new outputs (DETUNE=$DETUNE EXTRA_ARGS='$EXTRA_ARGS')..."
generate_set "new"

echo "[INFO] Comparing..."
diff_found=0
have_vgm2txt=0
if command -v vgm2txt >/dev/null 2>&1; then
  have_vgm2txt=1
else
  echo "[WARN] vgm2txt not found; textual diffs disabled."
fi

normalize_txt () { sed -E 's/File Version:.*//g'; }

for f in "${FILES[@]}"; do
  base="${f%.vgm}OPL3.vgm"
  ref="$BASELINE_DIR/$base"
  new="$NEW_DIR/$base"
  if [ ! -f "$ref" ]; then
    echo "[WARN] Missing baseline for $base"
    diff_found=1
    continue
  fi
  if ! cmp -s "$ref" "$new"; then
    echo "[DIFF] $f"
    diff_found=1
    if [ $have_vgm2txt -eq 1 ]; then
      ref_txt="$TXT_DIR/${f%.vgm}_ref.txt"
      new_txt="$TXT_DIR/${f%.vgm}_new.txt"
      vgm2txt "$ref" > "$ref_txt" 2>/dev/null || true
      vgm2txt "$new" > "$new_txt" 2>/dev/null || true
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
  echo "         (Intentional? Re-run with --update-baseline)"
  exit 1
fi