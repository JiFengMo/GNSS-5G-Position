#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BASE_CFG="$ROOT_DIR/config/5G/ue.toa.ssb.usrpb210.n1_422910_pci84.precise.f"
BIN="${NR_TOA_BIN:-$ROOT_DIR/build/radio/src/5G/nr-toa-uesoftmodem}"
TRIAL_SECS="${TRIAL_SECS:-30}"
GAINS=("$@")
if [[ ${#GAINS[@]} -eq 0 ]]; then GAINS=(58 61 64); fi
OUT_DIR="${OUT_DIR:-$ROOT_DIR/build/precise_trials_$(date +%Y%m%d_%H%M%S)}"
[[ -x "$BIN" ]] || { echo "missing executable: $BIN"; exit 1; }
[[ -f "$BASE_CFG" ]] || { echo "missing base config: $BASE_CFG"; exit 1; }
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

for gain in "${GAINS[@]}"; do
  [[ "$gain" =~ ^[0-9]+([.][0-9]+)?$ ]] || { echo "invalid gain: $gain"; exit 1; }
  cfg="$OUT_DIR/pci84_gain${gain}.f"
  main="$OUT_DIR/main_gain${gain}.f"
  log="$OUT_DIR/pci84_gain${gain}.log"
  iq_dir="$OUT_DIR/pci84_gain${gain}_iq"
  mkdir -p "$iq_dir"
  cp "$ROOT_DIR/config/5G/anchors.csv" "$OUT_DIR/anchors.csv"
  awk -v gain="$gain" '
    /^rx_gain_db[[:space:]]*=/ { print "rx_gain_db = " gain; next }
    /^gain_sweep_enable[[:space:]]*=/ { print "gain_sweep_enable = 0"; next }
    { print }
  ' "$BASE_CFG" > "$cfg"
  cat > "$main" <<EOF
# 是否启用 5G，1 启用。
# 填写示例：enable_5g = 1
enable_5g = 1
# 本次试验子配置，相对于本文件。
# 填写示例：nr_config = pci84_gain58.f
nr_config = pci84_gain${gain}.f
EOF
  echo "gain=$gain main=$main log=$log iq=$iq_dir"
  status=0
  timeout "${TRIAL_SECS}s" env NR_TOA_IQ_DUMP_DIR="$iq_dir" \
    "$BIN" "$main" | tee "$log" || status=$?
  if [[ $status -ne 0 && $status -ne 124 ]]; then
    echo "trial failed with status $status" >&2
    exit "$status"
  fi
done
