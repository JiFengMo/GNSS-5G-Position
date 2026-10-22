# 北斗 B1C 离线基线与捕获

本文记录可复现 MATLAB 基线和 C 端捕获。后续已增加 [连续跟踪](tracking.md)、[导航电文](navigation.md)、[定位](positioning.md) 和 [北斗离线 UI](../../../ui/gnss.md)。独立入口 `sh run_BDS_only.sh` 可调度完整离线链路，`--ui-only` 只查看结果；在线 USRP 尚未接入。

本次真实 IQ 的详细捕获表、误差和测试记录见 [2026-10-20 验收结果](validation_2026-10-20.md)。

## 实现范围与结果定义

- 按 BDS-SIS-ICD-B1C-1.0 表 5-2/5-3/5-4 实现 PRN 1–63 的数据主码、导频主码和导频子码；二进制 0 映射为 +1，1 映射为 −1。主码 10230 码片，子码 1800 码片。
- 捕获采用数据路与导频路 BOC(1,1) 本地副本，按 CU-SDR 的窄带方式联合处理。这不是完整 QMBOC 宽带处理；子码生成已验证，子码同步/擦除留待跟踪阶段。
- 默认 10 ms 相干积分，用 20 ms IQ 做补零 FFT 循环相关，搜索完整码相位，再做 25 Hz 精细频率搜索。多个 PRN 共用 FFT 工作空间，不保留二维搜索矩阵。
- 粗频率步长与 FFT 栅格对齐时复用信号 FFT；不对齐时直接逐频率混频再 FFT。支持 1–10 ms 相干积分，但总输入仍需要 20 ms，供精细搜索截取连续 10 ms。
- 采样副本使用参考 `makeDataTable`/`makePilotTable` 的右端点 `ceil((1/fs)*(n+1)/(1/1.023e6/2))` 约定，包括浮点取整，便于逐样本对照。

| CSV 字段 | 定义 |
|---|---|
| `carrier_hz` | IQ 中的残余载波频率，等于 `IF + doppler_hz`；捕获失败时为 NaN |
| `doppler_hz` | 已扣除配置中频的精细搜索结果，Hz；捕获失败时为 NaN |
| `coarse_doppler_hz` | 粗搜索结果，Hz |
| `code_phase_samples` | 输入窗口内码起点的零起始样本偏移，折叠到一个 10 ms 码周期；仅 `acquired=1` 时有效 |
| `input_start_sample` | `--skip-samples` 指定的窗口起点，单位为复数样本 |
| `peak_metric` | CU-SDR 的联合相关幅度/输入方差归一化统计量；不是功率峰均比，也不是 C/N0 |
| `fine_metric` | 精细搜索最大联合相关幅度，未做载噪比标定 |
| `cn0_db_hz` | 本阶段始终为 NaN，待跟踪阶段实现真实估计 |
| `acquired` | `peak_metric > threshold`，与频率是否恰为 0 Hz 无关 |

粗搜索统计量为 `(sqrt(11)*abs(Rdata)+sqrt(29)*abs(Rpilot))/sqrt(40)`，再除以 `sqrt(var(iq[0:Ncoh])*Ncoh)`；方差采用 N−1 分母。精细搜索沿用参考的 11:29 幅度权重。默认门限 10。

为与参考保持一致，粗捕获搜索补零 FFT 的全部循环相关滞后，获胜滞后折叠到一个主码周期，再在原始连续 IQ 上精搜。这些周期内相位存在整周期模糊，不能直接解释为信号传播时间或距离。

## 编译和专项测试

在工程根目录执行，Linux/WSL、C11、CMake 3.21+、Ninja、pkg-config 和 FFTW3 开发包：

```bash
# 尚未安装依赖时执行；此阶段不需要 UHD。
sudo apt-get install cmake ninja-build pkg-config libfftw3-dev
cmake --preset gnss-offline
cmake --build --preset gnss-offline --parallel 2
ctest --preset gnss-offline
```

输出在 `build/gnss-offline/src/GNSS/`：`bds-b1c-receiver`、`test_b1c_code`、`test_b1c_acquisition`。找到 Python 3.9+ 时还注册 7 项 CLI 测试。代码/波形测试使用提交在 `tests/data/` 的 MATLAB 独立向量摘要，不要求本机安装 MATLAB 或保存真实 IQ。也可用普通 CMake 参数替代 preset，设置 GNSS=ON、5G/RADIO/FUSION=OFF。

FFTW 是 GNSS 专用构建依赖；默认 `offline`/`radio` 预设仍关闭 GNSS。`all-offline` 因启用 GNSS 也需要 FFTW3。

## 直接捕获真实文件

当前参考文件是 int8 交织 `I0,Q0,I1,Q1,...`、`I+jQ`，18 Msps，残余中频 +20 kHz。不要按 cf32 或零中频读取。

```bash
IQ='/mnt/d/My control center/3-实验室/通导一体化/CU-SDR-Collection-main/L1_IF20KHz_FS18MHz.bin'
./build/gnss-offline/src/GNSS/bds-b1c-receiver \
  --input "$IQ" --input-format ci8 --sample-rate 18000000 --if 20000 \
  --prns 1-63 --output build/gnss-offline/acquisition.csv
```

输出 CSV 包括成功和未成功的 PRN。只搜索一颗可用 `--prn 23`；列表如 `--prns 19-30,37,43,46`。使用 `--help` 查看全部选项。

- `--input-format cf32` 为小端 float32 交织 IQ，也是兼容旧入口的默认格式。
- 每次只读指定起点的 20 ms，不把约 67 秒的完整文件载入内存，也不需要预先转换全文件。
- `--skip-samples` 的单位是复数样本，不是字节；采用 64 位文件偏移。
- 采样率范围 2.046–100 Msps，10 ms 内样本数必须为整数；这是本阶段离线副本的边界。
- 粗搜索上下界差值须为步长整数倍；细步长须整除粗步长。精搜范围为获胜粗频点 ± 一个粗步长，可能稍超出粗搜索上下界。
- 返回码：0 有捕获；7 完成但没有捕获；2 参数错误；3 文件/内存错误；6 算法参数或输入无效。NaN/Inf IQ 会拒绝处理。

`config/GNSS/bds_b1c.f` 仍是未来在线设备配置预留，C 工具使用命令行参数。`run_BDS_only.sh` 读取独立 `config/GNSS/offline.f` 并传递文件与采样参数；`run_5G_only.sh` 不启动北斗。

## 从原始参考重新建立 MATLAB 基线

已验证 MATLAB R2024b。下面的封装只调用参考捕获和码生成函数，不启动参考的交互入口，不改变参考目录里的任何文件。输出参数应指向本工程 `build/`。

在工程根目录启动 MATLAB 后执行：

```matlab
addpath('src/GNSS/tools');
referenceRoot = 'D:/My control center/3-实验室/通导一体化/CU-SDR-Collection-main';
iqPath = fullfile(referenceRoot, 'L1_IF20KHz_FS18MHz.bin');
make_b1c_baseline(referenceRoot, iqPath, fullfile(pwd, 'build/gnss-baseline/matlab'));
```

实际对照参数固定为：首个样本索引 0、前 360000 个复数样本（20 ms）、PRN 1–63、±5000 Hz、粗步长 50 Hz、细步长 25 Hz、积分 10 ms、数据/导频联合捕获。未降采样。

输出：

| 文件 | 内容 |
|---|---|
| `manifest.json` | IQ 文件字节数、参与计算的 20 ms 原始字节 SHA-256、采样率/中频/起点、MATLAB 版本、参考源文件摘要及耗时 |
| `acquisition.csv` | MATLAB 重新计算的所有 PRN 捕获结果；成功码相位已从 MATLAB 的 1 起始改为 0 起始 |
| `baseline.mat` | 本次设置、清单和捕获结果；不是原目录里已有结果的复制 |
| `data_primary.i8`、`pilot_primary.i8`、`pilot_secondary.i8` | PRN 顺序 1–63、每颗依次存放完整双极性 int8 码序列 |
| `sampled_4092000.i8`、`sampled_18000000.i8` | PRN 1、19、23、63，每颗数据路后接导频路，供逐字节验证 |

清单记录的是实际使用窗口的 SHA-256；没有把未参与本次计算的其余约 67 秒全部哈希。基线脚本会检查参考关键参数，参数或参考源码改变后需要重新生成并评估结果。

## 自动比较 C 与 MATLAB

在 Linux/WSL 的工程根目录执行，Python 仅使用标准库：

```bash
python3 src/GNSS/tools/compare_b1c_baseline.py \
  --baseline build/gnss-baseline/matlab \
  --receiver build/gnss-offline/src/GNSS/bds-b1c-receiver \
  --code-test build/gnss-offline/src/GNSS/test_b1c_code \
  --iq "$IQ" \
  --reference-root '/mnt/d/My control center/3-实验室/通导一体化/CU-SDR-Collection-main' \
  --output build/gnss-baseline/comparison
```

先验证 IQ 窗口与参考源码摘要，再对 189 组码序列、16 组采样副本逐字节比较，最后重跑 C 捕获。验收要求：所有 PRN 检测判决一致，成功 PRN 频率差 0 Hz、码相位差 0 个样本，统计量相对差不超过 1e−7。

输出 `comparison.json`、`c_acquisition.csv`、`receiver.log`、`code_vectors.log`。不通过时脚本返回非零。`comparison.json` 还记录实际运行二进制的 SHA-256，方便确认结果对应的程序。

比较工具还将同一个 20 ms 窗口转换为 `window.cf32`，复核一颗卫星的 float32 读取路径，输出 `cf32_acquisition.csv` 和 `cf32_receiver.log`；不会转换完整记录。

## 来源及下一步

- [北斗官方 B1C ICD](https://en.beidou.gov.cn/SYSTEMS/ICD/201806/P020180608519640359959.pdf)：码参数和信号定义。
- CU-SDR `BDS/B1C`，Li、Shivaramaiah、Akos：*Design and implementation of an open-source BDS-3 B1C/B2a SDR receiver*，GPS Solutions 23:60 (2019)，[论文](https://doi.org/10.1007/s10291-019-0853-z)。捕获 C 适配文件保留作者归属及 GPL-2.0-or-later 标识；GPL 正文已随仓库 [MATLAB 参考](../reference/GNSS_SDR/license.txt) 保存。
- 固定测试摘要的 MATLAB 版本和参考文件校验和见 `tests/data/provenance.json`。这些摘要来自 MATLAB 输出，没有使用 C 输出生成期望值。

连续 IQ 跟踪现已实现，见 [跟踪说明和验收](tracking.md)。导航电文与星历占位框架已替换为真实帧同步和双 CRC 校验，见 [导航电文说明](navigation.md)；统一接收历元伪距和单点定位见 [定位说明](positioning.md)。捕获结果本身仍不能直接用于生成绝对距离。
