# 北斗离线 UI 验收记录 · 2026-10-22

开发与人工查阅目录为 `D:\My control center\3-实验室\ToA_5G`，WSL 使用同一目录的 `/mnt/d/` 挂载。此次增加结果导入、只读接口和北斗三页展示，未修改捕获、跟踪、电文或定位计算逻辑。5G 的 C 配置读取器只增加对网页专用 `gnss_record_dir` 的识别，并加入对应兼容性测试。

## 使用的真实离线结果

| 输入 | 本机路径 | 规模 |
|---|---|---|
| 捕获 | `build/gnss-baseline/final/c_acquisition.csv` | 63 个搜索 PRN，7 颗捕获 |
| 跟踪 | `build/gnss-tracking/automatic_60/` | 7 通道、42000 个 10 ms 历元 |
| 电文 | `build/gnss-navigation/verified_automatic_60/` | 21 个有效帧，每星 3 帧 |
| 定位 | `build/gnss-position/verified_automatic_60/` | 278 个有效定位历元，200 ms 周期 |
| UI 导入 | `log/gnss-ui/b1c-reference-60s/` | `run.json` 和 `data.sqlite`，完整源文件 SHA256 写入元数据 |

本记录沿用此前与 MATLAB 对照过的离线结果，不声称是本次 USRP 实时采样。输入和导入产物位于被 Git 忽略的 `build/`、`log/`；其他电脑需复制导入目录或重新导入，具体命令见 [使用说明](gnss.md)。

默认游标为 59.74994127777778 秒，Summary 显示捕获 7、有效跟踪 7、电文就绪 7、定位用星 6。C25 跟踪为代次 1，排除原因为 `below_elevation_mask`，与底层结果一致。当前纬度约 40.00997492°、经度约 −105.24433657°、椭球高约 1617.078 m，页面不将此当作天线真值。

## 已通过的检查

2026-10-22 重新执行：

```bash
cmake --build --preset all-offline --parallel 2
ctest --preset all-offline
python3 -m unittest discover -s ui/tests -v
node --check ui/web/app.js
node --check ui/web/gnss.js
git diff --check
```

WSL Ubuntu/GCC 的 19 项 CTest 全部通过，包含 GNSS 代码、捕获、跟踪、电文、定位、CLI 对照和原 5G 测试。21 个 Python 用例全部通过，其中 8 个为北斗接口测试、7 个为既有 5G 服务测试、6 个为独立北斗启动器测试；CTest 的 `monitor_service` 也会执行同一组 Python 测试，数量不重复累加。Python 检查覆盖：

- 仓库已有压缩 MATLAB 参考生成的 7 通道、7 帧和 90 个定位历元导入，记录不可覆盖、来源哈希及只读 SQLite。
- 电文必须收齐后才可展示，10 秒处导航就绪为 0，但离线回溯定位有结果并携带更晚的依赖收齐时刻。
- 跟踪间断/代次断线、分桶极值、响应大小限制、非有限值处理，以及超过 JavaScript 精确整数范围的采样计数。
- 只导入跟踪、未完成阶段、错误采样率/帧跨度、非法 PRN/路径/参数和 HTTP 只读行为。
- 独立任务参数、UI-only 不要求 IQ 可用、阶段顺序与本次捕获传递、失败停止后续阶段、打断时停止本次子进程、5G 脚本依赖的 Python 包导入方式及北斗默认页面标记。

Windows 本机 Chrome 加 plain Playwright 检查，测试服务地址为 `http://127.0.0.1:18765`，5G 演示数据隔离在 `build/ui-qa/monitor/`，北斗读取上述真实记录：

```bash
node ui/tests/browser_smoke.cjs http://127.0.0.1:18765
node ui/tests/gnss_browser_smoke.cjs http://127.0.0.1:18765
```

两个检查均输出 `PASS`，北斗覆盖概览、C25 全记录重捕获曲线、缩放、定位/ENU/已知参考点、逐星观测与排除、已收齐帧和星历、早期帧不可提前展示、播放、冻结、无解时不填零、模式切换保留游标、无导入实验提示、800/390 px 布局与无页面脚本错误。用于验证“无导入实验”的空列表只在浏览器请求中模拟，没有删除或修改用户记录。

截图位于本机 `build/playwright/gnss/`：`summary.png`、`trace-c25.png`、`position.png`、`observations.png`、`navigation.png`、`compact.png`、`mobile.png`、`empty.png`。已人工查看概览、单星跟踪、定位、手机布局及空记录提示。

Playwright CLI 的 npm 下载因域名解析失败不可用，本次使用机器已有 Playwright 包与已安装 Chrome 完成相同浏览器操作，不额外安装依赖。以上开发依赖不是 UI 运行依赖。临时 QA 服务在验收结束后关闭。

## 独立启动入口复核

新增 `run_BDS_only.sh` 和 `config/GNSS/offline.f`。默认入口增量构建 GNSS 后依次调用现有捕获、跟踪、电文、定位工具，导入独立实验再启动北斗服务；`--ui-only` 直接打开记录，`--check` 只验证任务配置和 IQ。两个脚本的 `sh -n` 语法检查、北斗配置检查和帮助命令通过。

实际执行 `sh run_5G_only.sh --ui-only` 与 `sh run_BDS_only.sh --ui-only --config build/bds-launcher-ui-qa.f`，两个 WSL 服务同时启动。QA 配置只将北斗端口从 8766 改为 18766，不修改正式配置。Chrome 验证两个无 hash 的根地址分别进入 5G 和北斗，北斗载入真实记录，未出现脚本错误。结束后停止了这两个临时进程。

北斗脚本处理和网页退出只停止本次子进程，未新增设备控制。修复了 `ui.server` 作为包导入时的 GNSS 模块路径，使原 5G 脚本配置检查与直接运行 Python 服务两种方式均可使用。

此外实际运行默认 `sh run_BDS_only.sh`，从原 18 Msps IQ 重新捕获和跟踪 60 秒，再解码、定位和导入，生成 `log/gnss/runs/bds-20261022-124944-341/` 及对应 UI 索引。得到 7 通道、21 帧和 278 个有效定位解。首次算法处理完成后网页绑定 8766 报占用；启动器的端口检查已从“临时绑定并释放”改为“连接探测已有服务”，避免在 WSL 中提前触发端口转发保留，并增加回归用例。随后通过 `sh run_BDS_only.sh --ui-only` 在默认 8766 成功打开本轮新结果，Chrome 验证默认北斗页面及全部计数。关闭脚本后再次检查，8766 没有遗留监听进程。此轮没有在修改端口探测后重新计算相同 IQ。

## 当前边界

北斗是离线处理结果回放，未接入 USRP 在线输入、网页参数修改或设备控制。ENU 默认相对首个有效解，仅用于观察散布；无天线真值，不能据此声称绝对精度。没有录入完整捕获曲面和原始 IQ 时不补造波形。UI 不增加 LDPC 纠错、电离层改正或 GEO 支持。目标 Linux/USRP 环境的连续接收和 UI 算力开销仍待实机验收。
