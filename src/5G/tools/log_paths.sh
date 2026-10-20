#!/usr/bin/env bash
# Source this file from launch helpers; all output paths are rooted at log/.
NR_LOG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/log"
mkdir -p "$NR_LOG_ROOT"
nr_log_path() {
  local value="$1" part
  case "$value" in
    "$NR_LOG_ROOT"/*) value="${value#"$NR_LOG_ROOT"/}" ;;
    /*) echo "log output must stay inside $NR_LOG_ROOT: $value" >&2; return 1 ;;
  esac
  [[ -n "$value" ]] || return 1
  local -a parts
  IFS='/' read -r -a parts <<< "$value"
  for part in "${parts[@]}"; do
    [[ -n "$part" && "$part" != '.' && "$part" != '..' ]] || {
      echo "invalid log path: $value" >&2; return 1;
    }
  done
  printf '%s/%s\n' "$NR_LOG_ROOT" "$value"
}
