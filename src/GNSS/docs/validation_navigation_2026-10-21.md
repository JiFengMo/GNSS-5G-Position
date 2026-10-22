# B1C 导航电文与星历验收（2026-10-21）

本阶段使用上一阶段已经过 MATLAB 数值对照的真实 18 Msps、20 kHz 中频记录，继续验证电文同步、校验、星历和逐帧 BDT 时间。开发目录为 `D:\My control center\3-实验室\ToA_5G`，C 构建与测试在 WSL Ubuntu/GCC 13.3.0 执行，参考程序为 MATLAB R2024b。

## 输入与参考

- 原 IQ：用户提供的 CU-SDR 根目录 `L1_IF20KHz_FS18MHz.bin`，2,412,000,000 字节，复数 int8，I+jQ。
- 原 IQ SHA-256：`6e16f2e6f37d72d22da1700f0e2139f54ce0f4a946238054ed984da806defe9b`。本阶段读取跟踪结果，完整 IQ 身份及跟踪数值已在 [跟踪验收](validation_tracking_2026-10-21.md) 中验证。
- C 连续跟踪输入：`build/gnss-tracking/final_reference`（禁用重捕获）和 `build/gnss-tracking/automatic_60`（默认重捕获），各 7 通道、60 秒、42,000 个历元。
- MATLAB 跟踪输入：`build/gnss-tracking/matlab/tracking.mat`，SHA-256：`0fedb5de17a6f169a126a3818b027e501bb1749dc80d25f1c2f0fbe4fc23d49e`。
- 电文参考源：`BDS/B1C/include` 下的 `BCNAV1decoding.m`、BCH21/51、`generate2ndCode.m`、`ephemeris.m`，保持原文件不变。

`make_b1c_navigation_baseline.m` 对每个完整帧调用原 `BCNAV1decoding()`，由原 `comm.CRCDetector` 检验双 CRC，独立保存该帧原始符号、878 位信息/CRC 和全部星历字段。分别从原 MATLAB 跟踪 MAT 和 C 自动重捕获 CSV 新运行，避免直接以 C 输出作为期望值。原源码与每份跟踪输入的 SHA-256 在基线 `manifest.json` 中。

另对 PRN23 完整 60 秒直接调用未改动的参考函数，得到首帧位置 434（MATLAB 一基）、TOW 270342；输出保留首帧 SF2 并累计后续 SF3，符合其原有接口语义。日志位于 `build/gnss-audit/navigation_original_full.log`。

## 真实电文结果

| PRN | 自动跟踪的有效帧数 | 首帧 generation / epoch（零基） | 首帧绝对采样点 | 子帧 2/3 CRC |
|---|---:|---|---:|---|
| 23 | 3 | 0 / 433 | 78046653 | 全通过 |
| 25 | 3 | 1 / 334 | 78298942 | 全通过 |
| 27 | 3 | 0 / 433 | 78111023 | 全通过 |
| 28 | 3 | 0 / 433 | 77956374 | 全通过 |
| 37 | 3 | 0 / 433 | 77979201 | 全通过 |
| 43 | 3 | 0 / 433 | 78082448 | 全通过 |
| 46 | 3 | 0 / 434 | 78172152 | 全通过 |

自动重捕获模式共得到 **7 颗卫星、21 帧**，没有 BCH、CRC 或字段范围失败。PRN25 的帧全部来自重捕获后的 generation 1，未与失锁前的数据拼接。禁用重捕获时与原 MATLAB 一样，PRN25 无完整子码匹配，另外 6 颗卫星共 18 帧。

所有有效帧都满足连续 1800 个锁定、有效跟踪历元；子码相关为 ±1800。帧起始接收时间约为记录的 4.33、22.33、40.33 秒，各卫星略有不同。所有通道携带 BDT 周号 832，三个帧时间分别为 **270342、270360、270378 秒**，帧间按 18 秒递增。电文卫星类型均为 MEO，HS/DIF/SIF/AIF 均为 0，SISMAI 为 15；这些字段原样保留，不据此宣称已经得到可用定位解。

本记录三个扩展页面为 60、1、2。Page 1 电离层/BDT-UTC 参数可与 MATLAB 实值对照；Page 3 BGTO 未在本记录出现，以合成边界测试覆盖，不声称已完成该页面的真实信号验收。

## 逐项对照

| 检查项 | MATLAB 原跟踪 → C 电文 | C 自动跟踪 → MATLAB / C 电文 |
|---|---:|---:|
| 有效帧数 | 18 | 21 |
| 878 位信息/CRC 不一致数 | 0 | 0 |
| 帧起始采样点最大差 | 0 | 0 |
| 极性与子码相关不一致数 | 0 | 0 |
| 已解析星历字段最大绝对差（IODC 更正后） | 0 | 0 |

IODC 单独修正：原 MATLAB 读 11 位，参考值为 38；[官方 B1C ICD 图 6-6 和 7.4.2 节](https://www.beidou.gov.cn/xt/gfxz/201712/P020171226740641381817.pdf) 明确为 10 位，本实现读出 19，IODE 为 19。没有修改其他数学标度或用容差掩盖此差异。逐帧解析刷新 SOH/TOW，原参考首帧时间标签另行保留在基线里。

最终 C 输出：

- `build/gnss-navigation/verified_automatic_60/`：21 帧、逐帧星历及通道摘要。
- `build/gnss-navigation/verified_reference/`：18 帧、对应原 MATLAB 连续环路。
- `build/gnss-navigation/comparison_automatic.json`、`comparison_reference.json`：通过结果、全部输入/输出哈希和 C 可执行文件哈希。
- `build/gnss-navigation/matlab/`、`matlab_automatic/`：本次新运行的参考结果。
- `build/gnss-audit/navigation_matlab.log`：MATLAB 基线运行日志。

## 回归与异常验证

全部离线工程 CTest **17/17 组通过**，覆盖 5G、融合、GNSS 和 UI 服务。ASan/UBSan 加内存泄漏检查的 GNSS 测试 **7/7 组通过**；日志保存到 `build/gnss-audit/navigation_all_offline_tests.log` 和 `navigation_sanitize_tests.log`。

新增核心测试包括 CRC24Q 标准校验向量、全部 64/256 个 BCH 码字及逐位单错拒绝、两种数据极性、PRN 不匹配、双 CRC 分别失败、33 位有符号边界、IODC/IODE 位边界、Page 1/3 参数、保留页字段空值、失锁/采样间断/代次变化/历元缺失、单个子码错误、非有限输入、部分帧与 64 位采样点。

新增 CLI 的 6 项测试使用 7 个真实 MATLAB 电文小型参考，检查全部字段、跨通道处理、两种极性、失败诊断不输出星历、缺采样不拼帧、参数与 CSV 拒绝以及不覆盖已有输出。另明确测试 LDPC 校验部分未使用的当前范围，防止把系统信息位路径误称为完整 FEC 译码。

## 当前范围

已经跑通的链路为：IQ 捕获 → 多通道连续跟踪/重捕获 → 子码与电文同步 → BCH 检查与解交织 → 系统信息位直取和双 CRC → BDT 帧时间/真实星历。沿用 MATLAB 的 **无 LDPC 纠错** 路径，弱信号容错仍有限。现有记录通过并不表示 GEO/IGSO、所有扩展页面或硬件在线链路均已实测。

尚未生成卫星位置、统一历元伪距或接收机位置；下一阶段先对照 MATLAB `satpos.m` 实现卫星位置与钟差，再建立接收时间模型和伪距，最后做单点定位。本次没有修改 5G 信号算法或增加 GNSS UI。
