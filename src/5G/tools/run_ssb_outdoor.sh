#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
source "$ROOT_DIR/src/5G/tools/log_paths.sh"
BIN="${NR_TOA_BIN:-$ROOT_DIR/build/radio/src/5G/nr-toa-uesoftmodem}"
DEFAULT_CFG="$ROOT_DIR/config/config.f"

CFG="${1:-$DEFAULT_CFG}"
RUN_DIR="outdoor_$(date +%Y%m%d_%H%M%S)"
LOG_PATH="$(nr_log_path "${2:-$RUN_DIR/receiver.log}")"
NR_TOA_IQ_DUMP_DIR="$(nr_log_path "${NR_TOA_IQ_DUMP_DIR:-$RUN_DIR/iq}")"
NR_TOA_DELAY_CSV="$(nr_log_path "${NR_TOA_DELAY_CSV:-$RUN_DIR/relative_delay.csv}")"
export NR_TOA_IQ_DUMP_DIR NR_TOA_DELAY_CSV

if [[ ! -x "$BIN" ]]; then
  echo "missing executable: $BIN"
  echo "build first: cmake --build $ROOT_DIR/build/radio --parallel"
  exit 1
fi

if [[ ! -f "$CFG" ]]; then
  echo "missing config: $CFG"
  exit 1
fi

mkdir -p "$(dirname "$LOG_PATH")" "$NR_TOA_IQ_DUMP_DIR" "$(dirname "$NR_TOA_DELAY_CSV")"

echo "Outdoor SSB test"
echo "bin: $BIN"
echo "cfg: $CFG"
echo "log: $LOG_PATH"
echo "iq_dump_dir: $NR_TOA_IQ_DUMP_DIR"
echo "delay_csv: $NR_TOA_DELAY_CSV"
echo
echo "Live success markers:"
echo "  SSB candidate seen:   pss_hits:"
echo "  PBCH tried:           pbch_cand: / pbch_bch:"
echo "  PBCH+MIB success:     SSB_LOCK_EVENT:"
echo "  Final per-cycle line: sync_result:"
echo "  Relative delay CSV:   relative_delay:"
echo
echo "Stop with Ctrl+C."
echo

stdbuf -oL -eL "$BIN" "$CFG" 2>&1 | tee "$LOG_PATH"
