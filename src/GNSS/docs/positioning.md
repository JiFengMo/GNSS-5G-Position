# 北斗 B1C 离线单点定位

本阶段从已校验的电文和连续跟踪结果，接通 MEO/IGSO 卫星位置、卫星钟差、统一接收采样历元的码伪距，以及联合估计接收机位置与钟差的单点定位（SPP）。主要算法沿用用户提供的 CU-SDR MATLAB，真实记录对照见 [验收记录](validation_position_2026-10-21.md)。

## 编译与运行

在工程根目录、Linux/WSL 执行：

```bash
cmake --preset gnss-offline
cmake --build --preset gnss-offline --parallel 2
ctest --preset gnss-offline

# 已依次按 tracking.md / navigation.md 完成同一 IQ 文件的跟踪和电文处理。
./build/gnss-offline/src/GNSS/bds-b1c-position \
  --tracking-dir log/gnss/tracking-01 \
  --navigation-dir log/gnss/navigation-01 \
  --sample-rate 18000000 --output-dir log/gnss/position-01
```

输出目录必须为新目录，程序创建父目录并拒绝覆盖已有结果。本机已有的真实记录结果可直接复用：

```bash
./build/gnss-offline/src/GNSS/bds-b1c-position \
  --tracking-dir build/gnss-tracking/automatic_60 \
  --navigation-dir build/gnss-navigation/verified_automatic_60 \
  --sample-rate 18000000 --output-dir log/gnss/position-example
```

该例预期得到 278/278 个有效解。程序读取 CSV，不重算 IQ，也不要求安装 MATLAB。Windows 上主要用于查阅源文件和结果，实际 C 构建运行平台为 Linux/WSL。GNSS 仍由独立命令行工具调度，不由 5G 启动脚本启动。

| 选项 | 默认值与含义 |
|---|---|
| `--sample-rate` | 必填，与当前 IQ/跟踪相同；检查每个历元采样数与积分时间 |
| `--period-ms` | 200 ms，与 MATLAB 默认一致；必须对应整数采样间隔 |
| `--elevation-mask` | 5°；首次从地心初始化，随后以已有位置筛选，并持续重新评估所有候选星 |
| `--start-offset-ms` | 68.802 ms，沿用参考的首历元本地钟初值；它不是已知传播时延，接收机钟差参与联合解算 |
| `--max-ephemeris-age` | 7200 s，应用层对轨道和钟差数据年龄的双重限制；不是对所有广播数据适用时长的认证 |
| `--no-tropo` | 关闭参考的固定气象对流层模型，默认开启 |

| 输出 | 内容 |
|---|---|
| `solutions.csv` | 每个统一采样历元的 BDT 接收时间估计、ECEF/经纬高、接收机钟差修正、DOP、残差 RMS、用星数和有效性/失败原因 |
| `observations.csv` | 对应每颗候选星的跟踪代次/历元、BDT 发射时间、原始伪距、发射时刻卫星坐标、卫星钟差、钟差修正后伪距、仰角/方位角、对流层改正和残差 |
| `exclusions.csv` | 按历元/PRN 记录失锁、无连续时间锚点、星历过期、健康/完整性不合格、低仰角或模型不支持等原因 |
| `run.json` | 输入目录、采样率、处理参数、模型范围、处理历元/有效解数量及运行完整性 |

`complete=true` 表示输入成功处理完毕，不表示每个历元都能定位；同时检查 `valid_fixes` 和逐行 `valid/status`。卫星不足、几何退化或不收敛时，坐标输出 `nan`，不沿用上次坐标伪装为新解。少于 4 个具备有效帧的通道时正常输出空表；输入损坏或电文与跟踪不匹配时返回非零。

## 时间与伪距

对每个测量历元，所有通道使用完全相同的 `absolute_sample`。在包含该采样点的完整跟踪历元内，用 DLL 码频率插值主码相位：

```text
codePhase = remCodePhase + codeRate/fs * (sample - epochFirstSample)
transmitTime = frameTOW + ((epoch - frameEpoch) + codePhase/10230) * 0.01
rawPseudorange = c * (localReceiveTime - transmitTime)
```

该公式与 MATLAB `calculatePseudoranges.m` 一致。程序保留 64 位采样计数，并使用周号与周内秒分开表示时间，支持周边界。只允许同一连续段、同一代次的已校验帧作为锚点。每个后续帧还要满足电文时间增量与主码周期数一致。

首历元本地时钟按最大有效发射时刻加初始偏置建立；最小二乘解算 `[X,Y,Z,b]`，其中 `b` 是该次本地时钟的米制钟差修正。解后用 `localTime -= b/c` 修正时钟，再按公共采样步长推进。因此首历元 `clock_correction_m` 通常很大，后续主要是相邻更新间的剩余偏差，不能把它当成未经重置的累计硬件钟差。原参考把首历元显示的 dt 强制为零，本工具保留实际求出的初始修正。

`raw_pseudorange_m` 含接收机钟差，不是直接的几何传播距离。`clock_corrected_pseudorange_m = raw + c*satellite_clock_s`，再减接收机钟差，得到 `receiver_corrected_pseudorange_m`；其中仍有大气、码测量噪声和多径等误差。

电文需要接收完整 18 秒帧后才能解码。本工具按 MATLAB 的离线方式回溯到帧首处理，属于 **离线回溯定位**，不能将第一个输出历元当作在线首定位时间。当前也不要求新增设备时钟或 PPS 输入，接收机钟差由 SPP 解算；绝对硬件时间标定另行验证。

## 卫星位置和钟差

`bds_b1c_satellite_state()` 移植 B1C 专用 `satpos.m`：

- MEO 半长轴参考值 27,906,100 m；IGSO 为 42,162,200 m，加入 `deltaA`、`ADot`、平均角速度及其变化率。
- 与参考一致地解开普勒方程，加入谐波项、地球旋转和轨道倾角变化，输出发射时刻的 ECEF 卫星坐标。
- 钟差为钟多项式、相对论修正和 `-T_GDB1Cp`。当前沿用参考的联合数据/导频跟踪时间模型，不额外减 `ISC_B1Cd`；若改为纯数据测距需重新核对该模型，参见 [官方 B1C ICD 7.5–7.7 节](https://www.beidou.gov.cn/xt/gfxz/201712/P020171226740641381817.pdf)。
- 轨道/钟差年龄结合广播帧的 WN/TOW 解析，避免简单周内时间取模把一周前的数据误判为新数据。

本阶段只支持已对照的 MEO/IGSO 路径，遇到 GEO 明确返回不支持，不输出未经验证的坐标。MEO 已有真实记录对照，IGSO 用由真实参数构造的原 MATLAB 向量验证，尚无本地 IGSO 实测验收。

## 定位计算与质量约束

采用等权、四状态最小二乘，从地心进行与参考相同的 10 次迭代；卫星钟差先改正到伪距，解算中加入信号传播期间的地球旋转改正。线性方程使用 Householder QR，拒绝秩不足或严重病态几何；需要有限、收敛且位于地球附近的结果才输出有效标记。

对流层模型移植参考的 `tropo.m`，参数固定为海平面、1013 mb、293 K、相对湿度 50%，未使用实测气象或实际站高。此版本尚未加入电离层改正、URA/SISMAI 权重、多径处理、RAIM/故障星剔除或载波相位定位。`valid=1` 代表通过当前工程检查并成功求解，不等同于完整性或绝对精度认证。

处理时重新验证电文的 878 位双 CRC，并用对应 1800 个实际跟踪符号重新核对导频子码、BCH、数据比特、代次和采样连续性，防止误用另一批跟踪结果。失锁、采样缺口或重捕获后必须有新连续段的完整帧，旧锚点不会跨段延续。健康状态 HS、DIF、SIF、AIF 任一非零时排除，并记录原因。

本阶段没有照搬参考中的两个显示/筛选问题：HDOP/VDOP 在本地 ENU 坐标系计算，参考用 ECEF XY/Z 分量替代并不正确；低仰角星会在后续历元重新评估，避免参考“排除后不再加入”的已知问题。当前 60 秒记录内候选星没有跨越门限，对照用星集合保持一致。GDOP、PDOP、TDOP 可直接对照参考。

经纬高采用 WGS84 椭球表达，卫星与接收机核心输出为 ECEF。没有测量天线真值，当前的坐标散布、残差和与 MATLAB 的差异只用于验证实现及短期稳定性，不能当作绝对定位误差。

## MATLAB 对照与测试

在 MATLAB 工程根目录运行；已有跟踪 MAT/CSV 时无需重新计算 IQ：

```matlab
addpath('src/GNSS/tools');
referenceRoot = 'D:/My control center/3-实验室/通导一体化/CU-SDR-Collection-main';
make_b1c_position_baseline(referenceRoot, ...
    'build/gnss-tracking/matlab/tracking.mat','build/gnss-position/matlab');
make_b1c_position_baseline(referenceRoot, ...
    'build/gnss-tracking/automatic_60','build/gnss-position/matlab_automatic');
```

脚本执行未改动的原 `postNavigation()`、`calculatePseudoranges()`、`satpos()`、`leastSquarePos()` 和改正函数。输出逐历元 `solutions.csv`、逐星 `observations.csv`、含 MEO/IGSO 和周交界情况的 `satellite_vectors.csv`、`navigation.mat`、原控制台日志及源码/输入哈希清单。

Linux/WSL 对照 C 结果：

```bash
python3 src/GNSS/tools/compare_b1c_position.py \
  --reference build/gnss-position/matlab_automatic \
  --actual log/gnss/position-example \
  --tracking build/gnss-tracking/automatic_60 \
  --decoder build/gnss-offline/src/GNSS/bds-b1c-position \
  --output build/gnss-position/comparison-example.json
```

对照覆盖全部解算历元、用星集合、统一采样点、发射时刻、三类伪距、卫星坐标/钟差、接收机坐标/钟差、经纬高、GDOP/PDOP/TDOP 和仰角/方位角。报告保留输入、C 程序和输出哈希。

日常 CTest 使用小型参考文件，无需 MATLAB 或原 IQ。重新导出：

```bash
python3 src/GNSS/tools/export_b1c_position_fixtures.py \
  --reference build/gnss-position/matlab_automatic \
  --navigation build/gnss-navigation/matlab_automatic \
  --tracking build/gnss-tracking/automatic_60 --output src/GNSS/tests/data
```

生成 `b1c_satellite_matlab.csv`（112 个原 MATLAB 卫星向量）和 `b1c_position_matlab.json.gz`（每星一个完整帧的实际跟踪、90 个参考定位历元），不包含原始 IQ。导出脚本用 MATLAB 输出作为期望值，不由 C 求解器生成期望结果。

可选生成稳定性图：

```matlab
plot_b1c_position('build/gnss-position/verified_automatic_60', ...
    'build/gnss-position/position_overview.png');
```

## API 与下一阶段

核心接口位于 `bds5g/b1c_position.h`，不依赖文件系统。`bds_b1c_transmit_time()` 负责单星当前历元插值，调用者仍需验证锚点到当前历元的连续性；命令行工具已维护连续段。`bds_b1c_satellite_state()` 负责轨道与钟差，`bds_b1c_spp_solve()` 处理同一接收历元的观测。实现保留 CU-SDR/SoftGNSS、Kai Borre、C. Goad 等来源的 GPL 归属，参见已有 [许可正文](../reference/GNSS_SDR/license.txt)。

离线定位结果已接入现有网页，详见 [北斗 UI 使用说明](../../../ui/gnss.md)。页面读取现成结果并标注离线回溯性质，不重新执行 IQ 算法。下一阶段适合先用用户自己的采样记录和已知天线位置复核时间模型、硬件延迟与绝对精度，再讨论 GNSS 在线接收、环路性能和完整纠错。当前 5G 设备接收与监看流程独立运行。
