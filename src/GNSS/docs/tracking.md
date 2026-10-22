# 北斗 B1C 连续跟踪

本阶段实现从捕获结果到连续 IQ 跟踪的完整离线链路：多通道相关器、DLL/PLL、持续样本计数、质量判定和失锁重捕获。使用原始 18 Msps IQ 验证，没有增加降采样。真实信号结果见 [2026-10-21 验收记录](validation_tracking_2026-10-21.md)。

## 编译与运行

在工程根目录、Linux/WSL 环境执行，依赖与上一阶段相同（FFTW3 开发包、CMake、Ninja、pkg-config）：

```bash
cmake --preset gnss-offline
cmake --build --preset gnss-offline --parallel 2
ctest --preset gnss-offline
```

输出 `build/gnss-offline/src/GNSS/bds-b1c-track`。原 `bds-b1c-receiver` 继续负责捕获。

首先对同一文件捕获；已有与文件匹配的捕获 CSV 时可以直接进入跟踪：

```bash
IQ='/mnt/d/My control center/3-实验室/通导一体化/CU-SDR-Collection-main/L1_IF20KHz_FS18MHz.bin'
./build/gnss-offline/src/GNSS/bds-b1c-receiver \
  --input "$IQ" --input-format ci8 --sample-rate 18000000 --if 20000 \
  --prns 1-63 --output build/gnss-offline/acquisition.csv

./build/gnss-offline/src/GNSS/bds-b1c-track \
  --input "$IQ" --input-format ci8 --sample-rate 18000000 --if 20000 \
  --acquisition-csv build/gnss-offline/acquisition.csv \
  --seconds 60 --output-dir log/gnss/tracking-01
```

捕获 CSV 必须来自同一 IQ 文件、采样率和中频；工具检查载波/多普勒关系并使用 CSV 的 `input_start_sample`，但纯数字 CSV 本身不包含文件身份校验。严格基线验收应使用下文的完整 IQ 哈希比较工具。不要把另一份记录的捕获结果用于当前文件。

常用选项：

| 选项 | 用途 |
|---|---|
| `--prns 23,37` | 只跟踪所列的已捕获卫星；跟踪工具的列表为逗号分隔整数 |
| `--input-format ci8/cf32` | int8 或小端 float32 交织 I/Q；默认 cf32 |
| `--seconds 60` | 每通道 6000 个完整主码周期，须为 10 ms 的整数倍 |
| `--chunk-samples 65536` | 共享文件读取块大小，与跟踪历元边界无关 |
| `--dll-bandwidth 1` | 二阶 DLL 带宽，Hz |
| `--pll-bandwidth 18` | 三阶 PLL 带宽，范围 0–18 Hz，不含 0 |
| `--early-late .06` | E/P、P/L 各自偏移，单位为主码码片 |
| `--quality-epochs 50` | 每 50 个完整历元更新一次质量，约 0.5 s |
| `--min-cn0 25` / `--min-lock .6` | 载噪比和锁定指示器门限 |
| `--max-residual-frequency 25` | 半历元频差检查门限，Hz |
| `--max-reacquisitions 3` | 每通道重捕获尝试上限；至少间隔约 1 s |
| `--no-reacquire` | 禁止重捕获，保留连续环路轨迹，用于与 MATLAB 一对一比较 |

自动重捕获发生在已完成的主码周期边界，搜索当前频率附近 ±1000 Hz。捕获重新过门限后创建新批次并重新收敛；`reacquire_ok` 仅代表捕获成功和状态重置，不能代替后续锁定判据。达到尝试上限后继续记录失锁通道，`valid=0`。

`--seconds` 与参考一致，表示完整码周期数量对应的名义时长。实际积分时长随码率略变，重捕获还会跳过部分样本，因此应使用输出的实际样本索引和积分时长，不应把记录行号直接乘以 10 ms 当作精确时间。

## 输出

| 文件 | 内容 |
|---|---|
| `prn_XX.csv` | 每个完整主码周期一行：E/P/L、载波/码率、NCO 相位、鉴别器、质量和有效性 |
| `events.csv` | 初始化、锁定、失锁、重捕获成功/失败；记录样本位置和批次 |
| `summary.csv` | 每通道完成历元数、有效/失锁计数、首末样本、重捕获次数和 EOF 部分样本数 |
| `run.json` | 输入路径、格式、采样率/中频、环路参数、处理状态和是否完成 |

返回码：0 表示完成请求数量的完整历元，**不代表每颗卫星均已锁定**；2 参数或种子错误；3 IO/短文件；6 跟踪输入或连续性错误；7 没有可选的已捕获通道。短文件保留完整历元与 `complete=false`，最后不足一个周期的数据不会伪装成完整历元。

可独立检查完整输出的样本连续性、NCO 相位、批次边界和有效标志：

```bash
python3 src/GNSS/tools/check_b1c_tracking.py log/gnss/tracking-01 \
  --report log/gnss/tracking-01/check.json
```

输出检查报告及每通道有效历元、首次有效时间和重捕获次数。校验通过表示记录自洽，实际锁定情况仍查看通道摘要；电文有效性另行判断。

关键字段：

- `absolute_sample`、`sample_count`：从整个 IQ 文件起点计算的零起始复数样本位置与本历元样本数。同一批次内必须满足“上一行起点+样本数=下一行起点”。
- `sample_time_s`：`absolute_sample/fs`，仅为记录内时间，不是 BDT、UTC 或 UHD 硬件时间。
- `carrier_hz`、`code_rate_hz`：本历元实际使用的 NCO 频率，记录在环路更新之前；`doppler_hz=carrier_hz-IF`。
- `rem_code_phase`：本历元起点的残余主码相位，单位为码片。捕获相位的样本数用于定位文件读取起点，不再直接当作码片数。
- `rem_carrier_phase`、`carrier_cycles`：载波 NCO 残余弧度和本批次累计圈数。它们尚不是完成模糊度处理的 RTK 载波相位观测。
- `epoch`：批次内历元号；`generation`：每次重捕获递增；`record_index`：通道输出的连续行号。重捕获后相位和批次内历元号重新初始化，不能跨批次强行接成连续相位。
- `data_*`、`pilot_*`：双精度累积的相关值；`code_nco_hz`、`carrier_nco_hz` 为本次更新得到的滤波校正。
- `data_cn0_db_hz`、`pilot_cn0_db_hz`、`cn0_db_hz`：矩方法估计的窄带相关 C/N0。开始积累质量窗口前为 NaN；零噪声极限可为正无穷。它们没有进行射频总功率标定。
- `quality_updated`：本行是否产生新质量窗口；其他行保留上次质量。`state` 和 `valid` 表示跟踪质量，不表示电文、星历或绝对测距有效。

## 环路与有效性

数据路与导频路采用 BOC(1,1) 窄带联合相关，沿用 CU-SDR 的 11:29 鉴别器权重。默认 DLL 1 Hz、阻尼 0.7、单边 E/L 偏移 0.06 码片；PLL 使用 18 Hz 三阶环路。初始码率由捕获多普勒和 1575.42 MHz 射频载波频率辅助设置。

PLL 使用对符号翻转不敏感的 Costas 相位鉴别器，导频按相对于数据路的正交关系处理。数据位和导频子码翻转不会被误当作 180° 载波误差。副本索引延续参考的 `ceil` 约定，并保存跨周期的残余码相位。

质量判定与参考原有的固定 `T` 状态分离：

1. 使用各路 Prompt 功率的一阶/二阶矩估计信号和噪声；非物理解不取绝对值伪装为有效 C/N0。
2. 数据路按 I 轴、导频路按 Q 轴计算符号翻转不敏感的 `(sum I²-sum Q²)/(sum I²+sum Q²)`，按估计信号功率组合锁定指示器。
3. 将每个历元分成前后两半，用 Prompt 复相关的相位差估计残余频率，抑制 10 ms Costas 环可能出现的约 50 Hz 假锁分支。
4. 默认连续两个良好质量窗口后从 `pull_in` 转为 `locked`；连续两个坏窗口转为 `lost`。质量窗口须同时通过 C/N0、锁定指示器、DLL 误差和残余频率检查。
5. `valid` 还要求当前半历元频差合格。`lost` 为当前批次的终止状态，必须经显式重捕获/重置重新进入 `pull_in`，不会仅因载噪比回升就重新标为有效。

跟踪模块本身只提供记录相对采样时间，电文帧同步和 BDT 时间标签由后续 [导航模块](navigation.md) 提取。BOC 副峰测距偏差的专项验收和硬件延迟标定仍未完成，跟踪有效标记不能直接转换为有效伪距或传播距离。

## 核心 API

头文件：`bds5g/b1c_tracking.h`。核心不依赖文件系统，也不持有原始 IQ，便于后续接入设备接收缓冲：

```c
bds_b1c_tracking_options_t options;
bds_b1c_tracking_defaults(&options);
options.sample_rate_hz = fs;
options.if_hz = residual_if;
bds_b1c_tracker_t *tracker = NULL;
int error = bds_b1c_tracker_create(&options, &acquisition, capture_window_start, &tracker);
/* 成功后，从 next_sample() 开始提供连续复数样本。 */
if (!error) {
    error = bds_b1c_tracker_push(tracker, iq, count,
        bds_b1c_tracker_next_sample(tracker), on_epoch, context);
}
bds_b1c_tracker_destroy(tracker);
```

回调同步发生，不能重入同一个 tracker。任意输入块大小不改变结果；非有限输入在整块校验后拒绝，样本计数不前移。出现样本间断会拒绝继续拼接并要求显式重置，不能把丢采样假装成连续时间。`pending_samples()` 给出未满周期样本数。

旧的“外部给出三个相关值并直接调整频率”的未验收接口已被上述实际 IQ 跟踪接口替换。

## MATLAB 基线与自动对照

MATLAB 从工程根目录运行：

```matlab
addpath('src/GNSS/tools');
referenceRoot = 'D:/My control center/3-实验室/通导一体化/CU-SDR-Collection-main';
make_b1c_tracking_baseline(referenceRoot, ...
    fullfile(referenceRoot,'L1_IF20KHz_FS18MHz.bin'), ...
    'build/gnss-baseline/matlab','build/gnss-tracking/matlab',60);
```

需要先按 [捕获说明](offline_acquisition.md) 生成 `baseline.mat`。脚本在本工程输出目录生成 `NB_tracking_headless.m`，只移除参考进度窗的创建、更新和关闭，保留数值环路；原参考目录不修改。输出完整跟踪 MAT、逐 PRN CSV 和包含完整 IQ SHA-256 的清单。

Linux/WSL 自动运行 C 并对照全部历元：

```bash
python3 src/GNSS/tools/compare_b1c_tracking.py --run \
  --reference build/gnss-tracking/matlab \
  --actual build/gnss-tracking/final_reference \
  --iq "$IQ" \
  --tracker build/gnss-offline/src/GNSS/bds-b1c-track \
  --acquisition-csv build/gnss-offline/acquisition.csv \
  --output build/gnss-tracking/comparison.json
```

不加 `--run` 时比较已有 C 结果。对照模式禁用重捕获，才能与参考的单批次连续环路逐历元比较。锁定质量另外验证，不把参考中固定不变的通道状态当作真值。

可选生成静态图：

```matlab
plot_b1c_tracking('build/gnss-tracking/final_reference', ...
    'build/gnss-tracking/matlab','build/gnss-tracking/tracking_overview.png');
```

## 来源与后续

跟踪数学来自 CU-SDR `NB_tracking.m`、`calcLoopCoef.m` 和 `calcLoopCoefCarr.m`，作者包括 Yafeng Li、Nagaraj C. Shivaramaiah、Dennis M. Akos、Darius Plausinaitis 等，保留 GPL-2.0-or-later 归属。相关 [GPL 正文](../reference/GNSS_SDR/license.txt) 已在工程中。

导频子码/电文同步、校验和星历已在后续阶段接通，运行方法见 [导航电文说明](navigation.md)。卫星位置与钟差、统一接收历元伪距和单点定位也已完成离线对照，见 [定位说明](positioning.md)。在线 USRP 输入、实时性能优化和 GNSS UI 另行接入；本文验证范围为已有文件上的连续跟踪。
