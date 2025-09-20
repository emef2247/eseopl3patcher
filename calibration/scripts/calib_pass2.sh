#!/bin/sh
# overrides/overrides_pass2.json を利用して再生成 → 分析
set -eu

CONF_DIR="$(dirname "$0")/.."
YAML="$CONF_DIR/config.yaml"
OVR="$CONF_DIR/overrides/overrides_pass2.json"

if [ ! -f "$OVR" ]; then
  echo "[ERR] overrides file not found: $OVR"
  exit 1
fi

parse_yaml_list() {
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

echo "[INFO] PASS2 start (using overrides_pass2.json)"

for base in $BASES; do
  src_vgm="${base}t03a.vgm"
  src_wav="${base}t03a.wav"
  report_json="$REPORT_DIR/report_${base}_pass2.json"
  log_file="$LOG_DIR/${base}_pass2.log"

  echo "[INFO] base=$base"
  wav_list=""

  for v in $VARIANTS; do
    bin="${PATCHER_PREFIX}${v}"
    out_vgm="p${base}_${v}_pass2.vgm"
    out_wav="p${base}_${v}_pass2.wav"

    if [ ! -x "$bin" ]; then
      echo "[WARN] missing binary: $bin" | tee -a "$log_file"
      continue
    fi

    # 将来 patcher に --override 実装 (例)
    echo "[RUN] $bin $src_vgm 0 -o $out_vgm --override $OVR" | tee -a "$log_file"
    "$bin" "$src_vgm" 0 -o "$out_vgm" --override "$OVR" >>"$log_file" 2>&1 || echo "[WARN] patcher failed" | tee -a "$log_file"

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

echo "[INFO] PASS2 end"