#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<EOF
Usage: $0 -c <config.yaml> [--resume] [--dry-run]

Options:
  -c FILE    config file path (required; relative or absolute)
  --resume   processed.txt を参照して未処理のみ実行
  --dry-run  実行せず処理対象ファイル一覧を表示
EOF
}

CONFIG=""
RESUME=0
DRYRUN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -c) CONFIG="$2"; shift 2;;
    --resume) RESUME=1; shift;;
    --dry-run) DRYRUN=1; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown arg: $1"; usage; exit 1;;
  esac
done
[[ -z "$CONFIG" ]] && { echo "[ERROR] -c <config.yaml> が必要"; usage; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ ! -f "$CONFIG" ]]; then
  if [[ -f "$PROJECT_ROOT/$CONFIG" ]]; then
    CONFIG="$PROJECT_ROOT/$CONFIG"
  else
    echo "[ERROR] config not found: $CONFIG"
    exit 1
  fi
fi
CONFIG_ABS=$(realpath "$CONFIG")
CONFIG_DIR=$(dirname "$CONFIG_ABS")

cfg() { python3 "$SCRIPT_DIR/config_get.py" "$CONFIG_ABS" "$1"; }

CORPUS_LIST_RAW=$(cfg corpus_list)
BINARY_RAW=$(cfg binary)
OUT_ROOT_RAW=$(cfg out_root)
DETUNE=$(cfg detune)
WAITVAL=$(cfg wait)
JOBS=$(cfg jobs)
TIMEOUT=$(cfg timeout_sec_per_file)

to_abs() {
  local p="$1"
  if [[ "$p" == /* ]]; then
    realpath "$p"
  else
    realpath "$CONFIG_DIR/$p"
  fi
}

CORPUS_LIST=$(to_abs "$CORPUS_LIST_RAW")
BINARY=$(to_abs "$BINARY_RAW")
OUT_ROOT=$(to_abs "$OUT_ROOT_RAW")

echo "[INFO] SCRIPT_DIR : $SCRIPT_DIR"
echo "[INFO] CONFIG     : $CONFIG_ABS"
echo "[INFO] CORPUS_LIST: $CORPUS_LIST"
echo "[INFO] BINARY     : $BINARY"
echo "[INFO] OUT_ROOT   : $OUT_ROOT"
echo "[INFO] DETUNE     : $DETUNE  WAIT: $WAITVAL  JOBS: $JOBS  TIMEOUT: $TIMEOUT"

[[ -f "$CORPUS_LIST" ]] || { echo "[ERROR] corpus list missing: $CORPUS_LIST"; exit 1; }
[[ -x "$BINARY" ]]      || { echo "[ERROR] binary not executable: $BINARY"; exit 1; }

RAW_LOG_DIR="$OUT_ROOT/raw_logs"
STAT_DIR="$OUT_ROOT/stats_lines"
JSON_DIR="$OUT_ROOT/json"
AGGR_DIR="$OUT_ROOT/aggregate"
META_DIR="$OUT_ROOT/meta"
mkdir -p "$RAW_LOG_DIR" "$STAT_DIR" "$JSON_DIR" "$AGGR_DIR" "$META_DIR"

PROCESSED_LIST="$OUT_ROOT/processed.txt"
FAILED_LIST="$OUT_ROOT/failed.txt"
: > "$FAILED_LIST"
if [[ $RESUME -eq 0 ]]; then : > "$PROCESSED_LIST"; fi

RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
{
  echo "run_id: $RUN_ID"
  echo "config: $CONFIG_ABS"
  echo "binary: $BINARY"
  echo "corpus_list: $CORPUS_LIST"
  echo "detune: $DETUNE"
  echo "wait: $WAITVAL"
  echo "jobs: $JOBS"
  echo "timeout: $TIMEOUT"
  echo "git_commit: $(git rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "resume: $RESUME"
} > "$META_DIR/run_${RUN_ID}.info"

declare -A DONE
if [[ $RESUME -eq 1 && -f "$PROCESSED_LIST" ]]; then
  while IFS= read -r f; do [[ -n "$f" ]] && DONE["$f"]=1; done < "$PROCESSED_LIST"
  echo "[INFO] RESUME: already processed = ${#DONE[@]}"
fi

mapfile -t RAW_LINES < <(grep -v '^[[:space:]]*$' "$CORPUS_LIST" | grep -v '^[[:space:]]*#')

QUEUE=()
for line in "${RAW_LINES[@]}"; do
  f="$(echo "$line" | xargs)"
  if [[ "$f" != /* ]]; then
    f="$(realpath "$(dirname "$CORPUS_LIST")/$f")"
  fi
  if [[ ! -f "$f" ]]; then
    echo "[MISS] $f"
    continue
  fi
  [[ -n "${DONE[$f]:-}" ]] && continue
  QUEUE+=("$f")
done

echo "[INFO] TARGET total=${#RAW_LINES[@]} new=${#QUEUE[@]}"
if [[ $DRYRUN -eq 1 ]]; then
  printf '%s\n' "${QUEUE[@]}"
  echo "[INFO] dry-run done."
  exit 0
fi

[[ ${#QUEUE[@]} -eq 0 ]] && { echo "[INFO] nothing to do"; exit 0; }

process_one() {
  local vgm="$1"
  local base="$(basename "$vgm" .vgm)"
  local stdout_f="$RAW_LOG_DIR/${base}.stdout"
  local stderr_f="$RAW_LOG_DIR/${base}.stderr"
  local stat_f="$STAT_DIR/${base}.stats"
  if timeout "$TIMEOUT" "$BINARY" "$vgm" "$DETUNE" "$WAITVAL" >"$stdout_f" 2>"$stderr_f"; then
    grep -E 'RATEMAP|SHAPEFIX|KeyOnDbg|Apply OPL3 VoiceParam' "$stderr_f" > "$stat_f" || true
    echo "$vgm" >> "$PROCESSED_LIST"
    echo "[OK] $vgm"
  else
    echo "$vgm" >> "$FAILED_LIST"
    echo "[FAIL] $vgm" >&2
  fi
}

export -f process_one
export RAW_LOG_DIR STAT_DIR PROCESSED_LIST FAILED_LIST BINARY DETUNE WAITVAL TIMEOUT

printf "%s\n" "${QUEUE[@]}" | xargs -I{} -P "$JOBS" bash -c 'process_one "$@"' _ {}

python3 "$SCRIPT_DIR/extract_stats.py" "$STAT_DIR" "$OUT_ROOT/json" "$AGGR_DIR/aggregate_stats.json" || {
  echo "[WARN] extract_stats failed"
}

echo "[DONE] OUT_ROOT=$OUT_ROOT"