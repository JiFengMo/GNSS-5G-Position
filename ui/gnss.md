# 北斗 B1C 实采与离线结果 UI

北斗和 5G 共用 Python 网页代码与三页框架，通过独立脚本分别启动，默认使用不同端口。`run_BDS_only.sh` 默认启动 B210 实采接收机及网页；`--offline` 使用 IQ 文件验证算法；`--ui-only` 只查看已导入的离线结果。网页服务只读，接收与算法在独立程序中运行。

## 独立启动北斗实采

在 Linux/WSL 的工程根目录执行：

```bash
sh run_BDS_only.sh
```

脚本读取 `config/config.f` 的 `gnss_config`，默认是 `config/GNSS/bds_b1c.f`；检查配置与端口，按 `gnss-radio` 增量构建，再启动只接收的 B210 程序与北斗实时页面。依赖 Python 3.9+、CMake 3.21+、Ninja、C/C++17 编译器、pkg-config、FFTW3 与 UHD 开发库；无需 MATLAB。打开 <http://127.0.0.1:8766/>，直接显示北斗页面。Ctrl+C 停止本次接收机与 UI；任一程序退出会清理另一个。

配置中 `serial =` 留空时，使用 UHD 默认发现列表中的第一台设备；填入序列号时只选择该设备，找不到就报错，不回退。序列号只在这个独立配置项填写，不写入 `device_args`。两台 B210 可以分别在各自脚本配置中固定选择。当前按已确认接线使用外部 10 MHz 和 PPS，RX 默认接 `RX2`；若实际接 `TX/RX`，相应修改。B1C 默认中心频率 1575.42 MHz、4.092 Msps、零中频，不能照搬离线文件的 18 Msps / +20 kHz 参数。主配置 `enable_gnss` 仍是统一调度预留标记，不影响独立北斗脚本。

实采数据通过有界 IQ 队列直接进入原有捕获、连续跟踪、电文与定位核心，不先录制文件再回放。失锁通道重捕获；采样时间戳跳变、UHD 溢出或队列丢弃会断开连续段并重新捕获。设备报告未锁定外部参考、没有 PPS、持续接收超时等情况会报错停止。没有 `ref_locked` 传感器时显示“无传感器报告”，不会虚报锁定。外部 PPS 只建立硬件秒边界，不自动赋予 UTC/BDT 日期；定位仍联合估计接收机钟差。

`log/gnss-live/bds-<时间>-<进程号>/` 保存本次监看 JSONL，单段 32 MiB，最多保留本次最近 8 段；`log/launcher/<同名目录>/` 保存构建、接收机和 UI 日志。默认不保存原始 IQ。页面每约 0.5 秒更新，显示处理延迟、丢弃计数和参考/PPS 状态；处理延迟超过 2 秒、记录超过 3 秒未更新或已停止时，当前锁定和当前坐标留空。冻结只暂停页面，不停止采样。

只检查配置，不构建、不打开设备：

```bash
sh run_BDS_only.sh --check
```

可用 `--config <文件>` 选择另一份设备配置，用 `--main-config <文件>` 选择公共主配置入口。北斗端口由设备配置的 `ui_port` 指定，默认 8766；5G 使用原 `run_5G_only.sh`，默认 8765。Linux/B210 实机验收仍待完成，详见 [接收链路与验收](../src/GNSS/docs/live_receiver.md)。初次捕获全 PRN、稳定跟踪的算力与积压情况需要实测。

## 离线算法验证

```bash
sh run_BDS_only.sh --offline --check
sh run_BDS_only.sh --offline
```

离线模式读取 `config/GNSS/offline.f`，检查 IQ 文件并按 `gnss-offline` 构建，然后依次执行捕获 → 跟踪 → 电文 → 定位 → 导入 → 网页。先把 `input_file`、格式、采样率、中频、时长、PRN 改为文件真实值；相对路径以工程根目录为基准，路径带空格无需引号。离线模式无需 UHD，不会打开 USRP，也不在实采失败时自动启用。

各阶段结果位于 `log/gnss/runs/<实验>/`，导入记录位于 `log/gnss-ui/<实验>/`，终端日志位于 `log/launcher/<实验>/`。每次使用本次捕获结果；阶段失败停止后续执行。离线计算完成后网页才启动，60 秒记录可能计算数分钟。`--offline --config <文件>` 可选择另一份离线任务配置。

## 查看已有参考记录

在工程根目录执行：

```bash
sh run_BDS_only.sh --ui-only
```

打开 <http://127.0.0.1:8766/>。本机已导入 `log/gnss-ui/b1c-reference-60s/`，名称为“B1C 真实参考记录 · 60 秒”。预期默认时刻显示 7 颗捕获、7 颗有效跟踪、7 颗电文就绪、6 颗定位用星；整份记录含 21 个有效帧和 278 个有效定位历元。C25 在前期重捕获，后期因低于仰角门限而未参与定位。

`--ui-only` 不编译、不读取 IQ、不重新计算，只查看已有导入记录。Windows 在工程目录执行 `python ui/launch_bds.py --ui-only`。按 Ctrl+C 停止本次服务；若端口已有服务，重启原服务以加载新增 Python 接口，再刷新浏览器。

`log/` 不上传 Git；将工程复制到其他电脑时，需一并复制所需的 `log/gnss-ui/<实验>/`，或从原始结果重新导入。界面无需原 IQ、MATLAB、UHD、npm、pip 或外部地图服务。

## 导入新实验

先按 [跟踪](../src/GNSS/docs/tracking.md)、[电文](../src/GNSS/docs/navigation.md) 和 [定位](../src/GNSS/docs/positioning.md) 说明完成同一次采样的离线处理，然后执行：

```bash
python3 ui/import_gnss.py \
  --tracking-dir build/gnss-tracking/automatic_60 \
  --navigation-dir build/gnss-navigation/verified_automatic_60 \
  --position-dir build/gnss-position/verified_automatic_60 \
  --acquisition-csv build/gnss-baseline/final/c_acquisition.csv \
  --output-dir log/gnss-ui/experiment-02 \
  --name 'B1C 实验 02'
```

输出为新目录中的 `data.sqlite` 和 `run.json`。原 CSV、JSON 和 IQ 不被修改，记录中保存源文件路径、大小、时间戳与 SHA256。目录名称限字母、数字、短横线和下划线，必须尚不存在；重新导入请使用新名称。导入完成后点击“刷新实验”，选择新实验。

只有跟踪结果时也能导入：

```bash
python3 ui/import_gnss.py \
  --tracking-dir log/gnss/tracking-01 \
  --output-dir log/gnss-ui/tracking-only-01
```

`--navigation-dir`、`--position-dir`、`--acquisition-csv` 均可省略，但导入定位必须同时提供对应电文目录。缺少捕获记录时，捕获数量显示不可用；没有电文或定位数据时，相应区域为空。导入工具检查采样率、采样跨度、代次/历元连续性、有效帧标志和定位用星的一致性；仍需操作者选择同一实验的输入，不能仅凭相同文件名认定实验一致。请在处理程序停止写入后导入，目录索引不会自动跟随源 CSV 更新。

监看配置中的 `gnss_record_dir = gnss-ui` 相对于工程 `log/` 解析，可修改后重启服务。它独立于 5G 的 `record_dir`。

## 三个页面

| 页面 | 内容与操作 |
|---|---|
| Summary | 四种独立数量：捕获、当前跟踪有效、完整电文就绪、当前定位用星；C/N₀ 柱图、各星状态、排除原因、处理参数与来源摘要。点击卫星进入单星跟踪。 |
| Signal Trace | 选择 PRN，查看 C/N₀、多普勒、DLL/PLL、数据/导频 Prompt I/Q、当前三个 E/P/L 抽头和残余频率诊断；标出失锁、重捕获和间断事件。 |
| Results → 定位结果 | WGS84 经纬度、椭球高、ECEF、接收机钟差、用星数、残差 RMS、DOP，ENU 散布与时间序列、天空图。 |
| Results → 逐星观测 | 原始码伪距、卫星钟差改正后观测、接收机钟差改正后观测、后验残差、仰角、参与情况与排除原因。 |
| Results → 电文与星历 | 当前 PRN 已收齐帧的分页列表、BCH/CRC 结果、帧首与收齐时间、页号，点击帧查看星历与完整解码信息。 |

实时和离线页面共用 PRN 选择、冻结和 10/30/120 秒历史窗口；图表可缩放和平移。离线回看另有实验选择、时间轴、秒数输入、步进和 1/2/5 倍播放。实时定位只使用当时已收齐的连续电文，界面标注“在线因果解”；下列离线回溯解释只适用于离线结果。

## 读数含义

- 时间轴是 `absolute_sample / sample_rate_hz`，对应输入文件的采样时间，不是电脑墙钟，也不声称是已标定的硬件时间。64 位采样计数、代次和历元以字符串传到网页，避免 JavaScript 整数精度损失。
- 电文仅在完整帧收齐后进入列表；当前连续跟踪段中已有完整校验帧，才计入“电文就绪”。失锁前旧帧不会使新代次立即就绪。
- 现有定位工具支持用随后收齐的帧回溯构造较早时刻的伪距。因此 UI 明确标注“离线回溯解”和所依赖完整电文的收齐时间。10 秒处可以已有离线定位结果，同时电文就绪数量仍为 0；这不表示接收机在第 10 秒已实时输出该位置。
- 缺失曲线不填零，不跨无效、采样间断或代次变化连线；没有当前有效定位解时，经纬高显示“—”。完整捕获相关曲面和原始 IQ 未被这些结果文件记录，页面不重建它们。
- 默认 ENU 原点取本实验首个有效定位解，只表示相对散布。可在“显示参考点”中填写已知天线 WGS84 纬度、经度和**椭球高**；这只改变浏览器的 ENU 显示，不改算法、源文件或接收机配置。尚无独立真值时，不能用散布或后验残差认定绝对定位精度。
- 天空图只绘制当前定位历元实际保存的仰角/方位角；被提前排除且没有角度记录的卫星不补画。码伪距与两级钟差改正后的观测都不直接等同于无误差几何距离。
- UI 沿用核心算法报告的有效性，不增加 LDPC 纠错、电离层模型或 GEO 支持；算法边界仍见对应 GNSS 文档。

## 索引与验证

一次导入生成 SQLite 时间/PRN 索引，网页请求用只读连接按需查询。跟踪请求最多读 12001 条原始行，各曲线压缩到至多 1200 个显示点并保留分桶极值与断点；定位历史最多约 1604 项，帧列表每页最多 50 项。页面默认使用 20 项帧分页。内存和带宽有界不代表在线接收开销已完成实测。

运行接口测试：

```bash
python3 -m unittest discover -s ui/tests -v
cmake --preset all-offline
cmake --build --preset all-offline --parallel 2
ctest --preset all-offline
```

预期 33 个 Python 用例和 21 项 all-offline CTest 通过。实时 reader 每次最多读取 4 MiB，单行最多 256 KiB，每星最多保留 12000 跟踪历元和 256 帧、定位历史 800 项、事件 240 项；不会把跳过的记录跨段连线。当前实采 JSONL 用于滚动监看与诊断，尚未提供完整历史导入回看或 IQ 重算。多次实验目录总占用需人工管理。

已准备上述完整参考实验并启动 UI 服务时，可按 [UI 检查说明](README.md#检查) 配置开发专用 Playwright 依赖，然后执行：

```bash
node ui/tests/gnss_browser_smoke.cjs http://127.0.0.1:8766
```

预期输出 `PASS`，截图写入 `build/playwright/gnss/`。该浏览器检查针对本节 60 秒参考实验；仅克隆 Git 仓库时不会带有该本机数据。Python 接口测试使用仓库已有的压缩 MATLAB 小型参考，不依赖本机原始 IQ 或 MATLAB。
