#!/usr/bin/env bash
# Sweep INST=1 (Violin) timbre parameters around fixed g/pre/offon
# Usage: timbre_sweep_inst1.sh -i input.vgm -g gate -p pre -o offon \\
#        [--mod-tl-steps range] [--fb-set values] [--ws-set values] \\
#        [--simplify] [--mute-mod] [--output-dir dir]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Defaults
INPUT_VGM=""
GATE_SAMPLES="8192"
PRE_WAIT="16"
OFFON_WAIT="16"
MOD_TL_STEPS="0:63:8"  # Sweep modulator TL
FB_SET="0 1 2 3 4 5 6 7"  # All FB values
WS_SET="0 1 2 3 4 5 6 7"  # All WS values
SIMPLIFY_FLAG=""
MUTE_MOD_FLAG=""
OUTPUT_DIR="timbre_inst1_output"
FREQSEQ="${ESEOPL3_FREQSEQ:-AB}"
DETUNE="${DETUNE:-1.0}"

usage() {
    cat >&2 << EOF
Usage: $0 -i input.vgm [-g gate] [-p pre] [-o offon] \\
          [--mod-tl-steps range] [--fb-set values] [--ws-set values] \\
          [--simplify] [--mute-mod] [--output-dir dir]

Options:
  -i <input.vgm>         Input YM2413 VGM file (must use INST=1)
  -g <gate_samples>      Gate duration (default: 8192)
  -p <pre_wait>          Pre-keyon wait (default: 16)
  -o <offon_wait>        Off-to-on wait (default: 16)
  --mod-tl-steps <range> Modulator TL sweep range start:end:step (default: 0:63:8)
  --fb-set <values>      FB values to test (default: 0 1 2 3 4 5 6 7)
  --ws-set <values>      WS values to test (default: 0 1 2 3 4 5 6 7)
  --simplify             Enable --voice-simplify-sine flag
  --mute-mod             Enable --voice-debug-mute-mod flag
  --output-dir <dir>     Output directory (default: timbre_inst1_output)

Environment:
  ESEOPL3_FREQSEQ        Frequency sequence mode (default: AB)
  DETUNE                 Detune factor (default: 1.0)
EOF
    exit 1
}

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        -i) INPUT_VGM="$2"; shift 2 ;;
        -g) GATE_SAMPLES="$2"; shift 2 ;;
        -p) PRE_WAIT="$2"; shift 2 ;;
        -o) OFFON_WAIT="$2"; shift 2 ;;
        --mod-tl-steps) MOD_TL_STEPS="$2"; shift 2 ;;
        --fb-set) FB_SET="$2"; shift 2 ;;
        --ws-set) WS_SET="$2"; shift 2 ;;
        --simplify) SIMPLIFY_FLAG="--voice-simplify-sine"; shift ;;
        --mute-mod) MUTE_MOD_FLAG="--voice-debug-mute-mod"; shift ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "[ERROR] Unknown option: $1" >&2; usage ;;
    esac
done

if [ -z "$INPUT_VGM" ]; then
    echo "[ERROR] Missing required argument: -i input.vgm" >&2
    usage
fi

if [ ! -f "$INPUT_VGM" ]; then
    echo "[ERROR] Input VGM not found: $INPUT_VGM" >&2
    exit 1
fi

# Setup output directory
mkdir -p "$OUTPUT_DIR"
CSV_FILE="$OUTPUT_DIR/timbre_sweep.csv"

# Write CSV header
echo "mod_tl,fb,ws,simplify,mute_mod,vgm_path,wav_path,notes" > "$CSV_FILE"

echo "[INFO] INST=1 Timbre sweep starting"
echo "[INFO] Gate=$GATE_SAMPLES Pre=$PRE_WAIT Offon=$OFFON_WAIT"
echo "[INFO] Simplify=$SIMPLIFY_FLAG MuteMod=$MUTE_MOD_FLAG"
echo "[INFO] FREQSEQ=$FREQSEQ DETUNE=$DETUNE"

# Check for patcher
PATCHER="${REPO_ROOT}/build/eseopl3patcher"
if [ ! -x "$PATCHER" ]; then
    echo "[ERROR] Patcher not found or not executable: $PATCHER" >&2
    exit 1
fi

# Parse TL range
IFS=':' read -r TL_START TL_END TL_STEP <<< "$MOD_TL_STEPS"

# Sweep loop
for TL in $(seq "$TL_START" "$TL_STEP" "$TL_END"); do
    for FB in $FB_SET; do
        for WS in $WS_SET; do
            LABEL="tl${TL}_fb${FB}_ws${WS}"
            [ -n "$SIMPLIFY_FLAG" ] && LABEL="${LABEL}_simp"
            [ -n "$MUTE_MOD_FLAG" ] && LABEL="${LABEL}_mute"
            
            VGM_OUT="${OUTPUT_DIR}/${LABEL}.vgm"
            WAV_OUT="${OUTPUT_DIR}/${LABEL}.wav"
            
            # Build patcher command
            CMD=(
                "$PATCHER" "$INPUT_VGM" "$DETUNE"
                --convert-ym2413
                --min-gate-samples "$GATE_SAMPLES"
                --pre-keyon-wait "$PRE_WAIT"
                --min-off-on-wait "$OFFON_WAIT"
                --inst1-tl-override "$TL"
                --inst1-fb-override "$FB"
                --inst1-ws-override "$WS"
            )
            [ -n "$SIMPLIFY_FLAG" ] && CMD+=("$SIMPLIFY_FLAG")
            [ -n "$MUTE_MOD_FLAG" ] && CMD+=("$MUTE_MOD_FLAG")
            CMD+=(-o "$VGM_OUT")
            
            # Run patcher
            ESEOPL3_FREQSEQ="$FREQSEQ" "${CMD[@]}" &>/dev/null || {
                echo "[WARN] Patcher failed for $LABEL"
                continue
            }
            
            # Render to WAV
            if command -v vgm2wav &>/dev/null; then
                vgm2wav "$VGM_OUT" "$WAV_OUT" &>/dev/null || continue
            elif command -v vgmplay &>/dev/null; then
                vgmplay -o "$WAV_OUT" -l 1 "$VGM_OUT" &>/dev/null || continue
            elif command -v VGMPlay &>/dev/null; then
                VGMPlay -o "$WAV_OUT" -l 1 "$VGM_OUT" &>/dev/null || continue
            else
                echo "[ERROR] No VGM player found" >&2
                exit 1
            fi
            
            # Record to CSV
            NOTES="g=$GATE_SAMPLES,p=$PRE_WAIT,o=$OFFON_WAIT"
            echo "${TL},${FB},${WS},${SIMPLIFY_FLAG:+yes},${MUTE_MOD_FLAG:+yes},${VGM_OUT},${WAV_OUT},${NOTES}" >> "$CSV_FILE"
            
            echo "[SWEEP] TL=$TL FB=$FB WS=$WS -> $LABEL"
        done
    done
done

echo ""
echo "[COMPLETE] Timbre sweep finished. Results: $CSV_FILE"
echo "[INFO] Generated $(( $(wc -l < "$CSV_FILE") - 1 )) variations"
