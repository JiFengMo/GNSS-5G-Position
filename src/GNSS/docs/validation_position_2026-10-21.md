# 北斗 B1C 单点定位验收（2026-10-21）

本次接通并验证：已校验的 B-CNAV1 电文与连续码跟踪 → 统一采样历元的发射时间与伪距 → 卫星位置/钟差 → 接收机 ECEF 位置及钟差 → 经纬高和质量诊断。全部开发在 `D:\My control center\3-实验室\ToA_5G` 完成，C 在 WSL Ubuntu/GCC 13.3.0 构建运行，参考为 MATLAB R2024b。

## 数据和参考来源

仍使用用户提供的 CU-SDR 目录中 `L1_IF20KHz_FS18MHz.bin`，复数 int8、18 Msps、20 kHz 中频，处理 60 秒。完整原 IQ SHA-256 为 `6e16f2e6f37d72d22da1700f0e2139f54ce0f4a946238054ed984da806defe9b`，其捕获/跟踪/电文来源链见 [捕获](validation_2026-10-20.md)、[跟踪](validation_tracking_2026-10-21.md) 和 [电文](validation_navigation_2026-10-21.md) 验收记录。

本阶段直接读取前两阶段输出，没有重新计算 IQ：

- 原参考连续环路：`build/gnss-tracking/final_reference` + `build/gnss-navigation/verified_reference`。
- 自动重捕获：`build/gnss-tracking/automatic_60` + `build/gnss-navigation/verified_automatic_60`。
- 对照脚本对原 MATLAB 的 `postNavigation.m`、`calculatePseudoranges.m`、B1C `satpos.m`、`leastSquarePos.m`、`tropo.m`、`e_r_corr.m` 和坐标函数进行新运行，不修改原参考源文件。

两份 MATLAB 基线位于 `build/gnss-position/matlab/` 与 `matlab_automatic/`；输入跟踪数据和参考源文件的 SHA-256、采样率、200 ms 测量周期、5° 仰角门限、对流层设置与初始时钟偏置在各自 `manifest.json` 中。

## 真实记录结果

| 模式 | 有效定位历元 | 首历元用星数 | 后续用星数 | 总观测数 |
|---|---:|---:|---:|---:|
| 原连续环路（禁用重捕获） | 278 / 278 | 6 | 6 | 1668 |
| 默认自动重捕获 | 278 / 278 | 7 | 6 | 1669 |

自动重捕获中 PRN25 已在 generation 1 得到完整电文，首个定位历元参与地心初始化；其解算后仰角约 0.893°，后续按默认 5° 门限排除。其余 PRN23、27、28、37、43、46 始终参与。PRN25 的排除依据是低仰角，不是本阶段再次失锁。

自动模式首统一采样点为 **78,298,943**，测量步长 **3,600,000 样本 / 200 ms**，末统一采样点为 1,075,498,943。输出为回溯处理：电文在完整帧接收后解码，然后对帧首之后的早期历元进行离线解算，不表示在线首定位延迟只有约 4.35 秒。

自动模式坐标中位数：纬度 **40.009973328°**、经度 **−105.244333287°**、椭球高 **1616.116 m**。记录没有已知天线坐标，该数值是当前参考 IQ 的解算结果，不能据此计算绝对定位误差。

相对于坐标中位数的短期 ENU 散布（样本标准差）：

| 统计范围 | E / m | N / m | U / m |
|---|---:|---:|---:|
| 全部 278 个解，保留初始化历元 | 0.522 | 0.518 | 1.020 |
| 后续 277 个解 | 0.465 | 0.506 | 0.849 |

初始化历元因额外使用极低仰角 PRN25，散布更大，图表保留该点而没有隐藏。后验码伪距残差 RMS 范围约 0.016–2.165 m；这些统计只说明短期内部一致性，不能替代天线真值。

## 与 MATLAB 的逐层对照

| 检查项 | 原连续环路最大差 | 自动重捕获最大差 |
|---|---:|---:|
| 公共采样点、用星集合 | 0 | 0 |
| 各星发射时刻 | 0 s | 0 s |
| 原始码伪距 | 0 m | 0 m |
| 卫星钟差 | 0 s | 0 s |
| 卫星 ECEF 坐标分量 | 1.50×10⁻⁸ m 以内 | 1.59×10⁻⁸ m 以内 |
| 接收机 ECEF 坐标分量 | 1.26×10⁻⁸ m 以内 | 1.22×10⁻⁸ m 以内 |
| 接收机钟差修正 | 1.32×10⁻⁸ m 以内 | 1.28×10⁻⁸ m 以内 |
| GDOP / PDOP / TDOP | 8×10⁻¹⁵ 以内 | 7×10⁻¹⁵ 以内 |

每个历元的本地接收时间估计也与原 MATLAB 一致。数值差为双精度数学运算及 QR 实现的舍入量级，**不代表纳米级实际定位精度**。

未照搬原参考两个显示问题：首历元保留实际求得的钟差修正，不强制写零；HDOP/VDOP 从协方差转换到本地 ENU 后计算，原参考的 ECEF XY/Z 项只作为其旧输出，不当作水平/垂直 DOP。仰角筛选还会持续复查所有候选星，避免参考中排除后不再加入的问题；本记录没有跨门限星，因此用星集合与原参考一致。

## 回归与失效行为

全离线工程 CTest **19/19 组通过**，包含原 5G、融合、UI 服务和全部 GNSS 测试；ASan/UBSan 加内存泄漏检查的 GNSS 测试 **9/9 组通过**。

核心测试含 112 个独立原 MATLAB 卫星向量，覆盖 7 颗卫星的 MEO 路径、构造的 IGSO 路径、负/正轨道时间差和两个周交界方向，最大卫星坐标分量差约 1.49×10⁻⁸ m。并验证完整周过期拒绝、非法/不支持星历、开普勒不收敛、64 位采样点、共同钟差可解性、恰好 4 星、少于 4 星、重复 PRN、退化几何、非有限输入及赤道/极点坐标转换。

命令行 7 项测试使用每星一个完整帧的实际跟踪数据和 90 个原 MATLAB 参考定位历元，另验证：

- 初始本地钟偏置由 68.802 ms 改为 120 ms，变化被接收机钟差吸收，坐标保持一致。
- 失锁后及重捕获的新代次，未得到新帧锚点时输出无效解，不跨段继承旧时间。
- 过期星历和卫星不足时不输出虚构坐标。
- CRC 有效但 HS 不健康的帧仍被排除。
- 电文 CRC 错误、实际跟踪符号与电文不一致时拒绝运行。
- 对流层开关、非法参数、采样率不匹配和已有输出保护。

## 结果文件

- `build/gnss-position/verified_automatic_60/solutions.csv`：278 个有效解。
- 同目录 `observations.csv`、`exclusions.csv`、`run.json`：逐星观测、PRN25 的低仰角排除及元数据。
- `build/gnss-position/verified_reference/`：原连续环路的 278 个解。
- `build/gnss-position/comparison_automatic.json`、`comparison_reference.json`：上述逐层差异、输入/输出哈希和已测可执行文件哈希。
- `build/gnss-position/position_overview.png`：全部有效解的水平散布、ENU、残差 RMS、用星数和 PDOP，已进行目视检查。
- `build/gnss-audit/position_matlab.log`、`position_all_offline_tests.log`、`position_sanitize_tests.log`：参考与测试日志。

日常回归的小型输入和独立期望值保存在 `src/GNSS/tests/data/b1c_satellite_matlab.csv` 和 `b1c_position_matlab.json.gz`。不包含原始 IQ，正常 CTest 无需 MATLAB。

## 当前边界与后续

已经获得可运行的北斗 B1C 离线 SPP，支持当前窄带联合跟踪、CRC 通过的 MEO/IGSO 数据、公共采样时间、卫星/接收机钟差、地球旋转和参考对流层改正。MEO 有真实记录验收；IGSO 仅有构造参数对照；GEO 暂不支持。

LDPC 纠错仍未加入；电离层改正、真实气象和站高、完整性监测、硬件延迟和绝对精度、用户自己的在线 USRP 输入尚需后续验证。下一步应先拿用户实测记录及天线参考坐标复核，再决定在线接入与性能优化方案。运行命令和 API 见 [定位说明](positioning.md)。
