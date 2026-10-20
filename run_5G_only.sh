#!/bin/sh
# Linux 一键启动：sh run_5G_only.sh；仅查看历史记录：sh run_5G_only.sh --ui-only。
# 输出：build/radio/、log/launcher/<时间>-<进程号>/ 和配置指定的监看记录。
set -eu

case "${1:-}" in
    '') mode=radio ;;
    --ui-only) mode=ui ;;
    -h|--help)
        echo '用法：sh run_5G_only.sh [--ui-only]'
        echo '默认：增量编译、检查配置、启动接收机和浏览器服务。'
        echo '--ui-only：只启动浏览器服务，不编译、不打开接收设备。'
        echo '参数均在 config/config.f 及其选择的子配置中修改。'
        exit 0 ;;
    *) echo "未知参数：$1；使用 sh run_5G_only.sh --help 查看帮助。" >&2; exit 2 ;;
esac
if [ "$#" -gt 1 ]; then
    echo '参数过多；使用 sh run_5G_only.sh --help 查看帮助。' >&2
    exit 2
fi

# 相对于脚本定位工程；即使路径包含空格，也不依赖当前终端所在目录。
cd -- "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
command -v python3 >/dev/null 2>&1 || { echo '缺少 Python 3.9+。' >&2; exit 1; }
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else "需要 Python 3.9+")'
python3 -c 'from pathlib import Path; from ui.server import load_settings; s = load_settings(Path("config/config.f")); print("配置检查通过；浏览器地址：http://127.0.0.1:" + str(s["port"]))'

if [ "$mode" = radio ]; then
    command -v cmake >/dev/null 2>&1 || { echo '缺少 CMake 3.21+；在线构建还需要 Ninja、编译器及 UHD 开发库。' >&2; exit 1; }
    cmake --preset radio
    cmake --build --preset radio --parallel 2
    ./build/radio/src/5G/nr-config-check config/config.f receiver
fi

if [ "$mode" = ui ]; then
    # exec 保持 Ctrl+C 的正常行为；不启动任何无线设备。
    exec python3 -u ui/server.py --config config/config.f
fi

run_log="log/launcher/$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$run_log"
ui_pid=
receiver_pid=
cleanup() {
    trap '' INT TERM
    # 只停止本次脚本启动的两个子进程；先请求正常退出，最多等待 10 秒。
    for pid in $receiver_pid $ui_pid; do kill -TERM "$pid" 2>/dev/null || :; done
    remaining=10
    while [ "$remaining" -gt 0 ]; do
        running=0
        for pid in $receiver_pid $ui_pid; do
            if kill -0 "$pid" 2>/dev/null; then running=1; fi
        done
        [ "$running" -eq 1 ] || break
        sleep 1
        remaining=$((remaining - 1))
    done
    for pid in $receiver_pid $ui_pid; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "子进程 $pid 未及时退出，强制停止。" >&2
            kill -KILL "$pid" 2>/dev/null || :
        fi
        wait "$pid" 2>/dev/null || :
    done
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM

python3 -u ui/server.py --config config/config.f > "$run_log/ui.log" 2>&1 &
ui_pid=$!
./build/radio/src/5G/nr-toa-uesoftmodem -O config/config.f > "$run_log/receiver.log" 2>&1 &
receiver_pid=$!
echo "接收机与 UI 已启动；运行日志：$run_log/"
echo "实时查看接收日志：tail -f '$run_log/receiver.log'"
echo '请在本机浏览器打开上方地址；按 Ctrl+C 停止本次接收机和 UI。'
echo '锚点发射程序仍按实验流程单独启动。'

# 任一进程退出时停止另一进程，避免留下后台接收或过期的网页服务。
while :; do
    for pid in "$ui_pid" "$receiver_pid"; do
        if ! kill -0 "$pid" 2>/dev/null; then
            status=0
            wait "$pid" || status=$?
            if [ "$pid" = "$ui_pid" ]; then
                ui_pid=
                echo 'UI 服务已退出。'
                tail -n 30 "$run_log/ui.log"
            else
                receiver_pid=
                echo '接收机已退出。'
                tail -n 30 "$run_log/receiver.log"
            fi
            exit "$status"
        fi
    done
    sleep 1
done
