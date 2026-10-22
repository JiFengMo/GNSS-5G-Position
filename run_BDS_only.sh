#!/bin/sh
# Linux 北斗独立入口：sh run_BDS_only.sh；只回看：sh run_BDS_only.sh --ui-only。
# 输出：build/gnss-offline/、log/gnss/runs/、log/gnss-ui/ 和 log/launcher/。
# 下一步：打开终端打印的北斗网页地址；Ctrl+C 停止本次任务。
set -eu
cd -- "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
command -v python3 >/dev/null 2>&1 || { echo '缺少 Python 3.9+。' >&2; exit 1; }
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else "需要 Python 3.9+")'
exec python3 -u ui/launch_bds.py "$@"
