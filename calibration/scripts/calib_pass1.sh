#!/bin/sh
# 初回変換 & 分析
set -eu

CONF_DIR="$(dirname "$0")/.."
YAML="$CONF_DIR/config.yaml"

parse_yaml_list() {
  # $1: key
  awk -v key="$1" '
    $1==key":" {inlist=1; next}
    inlist && /^  -/ {gsub(/^  -[[:space:]]*/,""); print; next}
    inlist && NF==0 {inlist=0}
  ' "$YAML"
}

BASES="$(parse_yaml_list bases)"
VARIANTS="$(parse_yaml_list variants)"

FREQ_DEFAULT=$(awk '/frequency_default:/ {print $2}' "$YAML")

REPORT_DIR="$CONF_DIR/reports"
LOG_DIR="$CONF_DIR/logs"
mkdir -p "$REPORT_DIR" "$LOG_DIR"

ANALYZE="$CONF_DIR/scripts/analyze_wav.py"

PATCHER_PREFIX="./eseopl3patcher_"

echo "[INFO] PASS1 start"

for base in $BASES; do
  src_vgm="${base}t03a.vgm"
  src_wav="${base}t03a.wav"
  report_json="$REPORT_DIR/report_${base}_pass1.json"
  log_file="$LOG_DIR/${base}_pass1.log"

  echo "[INFO] base=$base"
  wav_list=""

  for v in $VARIANTS; do
    bin="${PATCHER_PREFIX}${v}"
    out_vgm="p${base}_${v}_pass1.vgm"
    out_wav="p${base}_${v}_pass1.wav"

    if [ ! -x "$bin" ]; then
      echo "[WARN] missing binary: $bin" | tee -a "$log_file"
      continue
    fi
    echo "[RUN] $bin $src_vgm 0 -o $out_vgm" | tee -a "$log_file"
    "$bin" "$src_vgm" 0 -o "$out_vgm" >>"$log_file" 2>&1 || echo "[WARN] patcher failed" | tee -a "$log_file"

    # vgmplay 自動 WAV 生成 (VGMPlay.ini LogSound=2)
    echo "[RUN] vgmplay $out_vgm" | tee -a "$log_file"
    vgmplay "$out_vgm" >>"$log_file" 2>&1 || echo "[WARN] vgmplay failed" | tee -a "$log_file"

    if [ -f "$out_wav" ]; then
      wav_list="$wav_list $out_wav"
    else
      echo "[WARN] not found wav: $out_wav" | tee -a "$log_file"
    fi
  done

  if [ -f "$src_wav" ] && [ -n "$wav_list" ]; then
    echo "[ANALYZE] $ANALYZE $FREQ_DEFAULT $src_wav $wav_list" | tee -a "$log_file"
    python3 "$ANALYZE" "$FREQ_DEFAULT" "$src_wav" $wav_list > "$report_json" 2>>"$log_file" || echo "[ERR] analyze failed" | tee -a "$log_file"
  else
    echo "[SKIP] analysis for $base" | tee -a "$log_file"
  fi
done

echo "[INFO] PASS1 end"