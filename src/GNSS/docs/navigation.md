# 北斗 B1C 导航电文与星历

从 `bds-b1c-track` 的相关结果继续处理，完成导频子码同步、B-CNAV1 帧提取、BCH 检查、解交织、双 CRC24Q 校验、逐帧时间与星历解析。算法主要移植自用户提供的 CU-SDR MATLAB；真实信号对照见 [本阶段验收记录](validation_navigation_2026-10-21.md)。

## 编译与运行

在工程根目录、Linux/WSL 执行：

```bash
cmake --preset gnss-offline
cmake --build --preset gnss-offline --parallel 2
ctest --preset gnss-offline

# 已按 tracking.md 完成跟踪；采样率必须与该次跟踪一致。
./build/gnss-offline/src/GNSS/bds-b1c-nav \
  --tracking-dir log/gnss/tracking-01 \
  --sample-rate 18000000 --output-dir log/gnss/navigation-01
```

输出目录须为新目录，工具会创建父目录，防止覆盖已有结果。程序按 PRN 顺序读取 `prn_01.csv` 至 `prn_63.csv`；CSV 使用跟踪工具的列名，可含其他列。缺少必要列、空文件、非法数字、非有限相关值或采样率与积分时间不符时返回非零，`run.json` 标记不完整。没有同步或没有通过校验的帧属于正常信号结果，输出可以只有表头。

本机已有的真实跟踪结果可直接用于验证，无需重新读 IQ：

```bash
./build/gnss-offline/src/GNSS/bds-b1c-nav \
  --tracking-dir build/gnss-tracking/automatic_60 \
  --sample-rate 18000000 --output-dir log/gnss/navigation-example
```

预期在该记录上得到 7 个通道、21 帧有效电文。其他记录的卫星数和有效帧数由实际信号决定。

| 文件 | 内容 |
|---|---|
| `frames.csv` | 每个完整子码匹配候选的 PRN、跟踪代次、帧起止采样点、极性、失败原因及双 CRC 状态；仅通过校验的帧附带 878 个信息/CRC 比特 |
| `ephemerides.csv` | 每个有效帧独立解析的 BDT 周号、帧时间、轨道、钟差、群延迟和健康/完整性字段；Page 1 电离层/UTC、Page 3 BGTO 参数按页提供 |
| `summary.csv` | 各 PRN 历元数、有效跟踪历元数、连续性中断数、同步候选数、有效帧及各类失败计数 |
| `run.json` | 输入目录、采样率、算法范围、通道与有效帧数、运行完整性 |

`complete=true` 表示读完所有输入，允许没有有效帧；实际可用结果查看 `decoded_frames`。错误退出后保留诊断文件，不能把部分输出当作完整验收结果。输出不包含原始 IQ，重放本阶段不会重算相关器。

## 与 MATLAB 一致的算法路径

1. 每个完整主码周期产生一个符号，约 10 ms。窄带模式使用 `pilot_q_p` 的正负号匹配 1800 长度导频子码，相关绝对值必须等于 1800，与 MATLAB 的 `>=1799.5` 相同。一帧长 18 秒。
2. 数据符号使用 `data_i_p>0` 判为 1。先检查 BCH(21,6)，必要时反转整帧极性，再检查 BCH(51,8)，得到 PRN 和 SOH。沿用参考阈值 20/50，实际上要求无误码码字，暂不做 BCH 纠错。
3. 后 1728 个符号按 36×48 矩阵解交织；零基行 2、5、…、32 组成子帧 3，其余行组成子帧 2。
4. **与参考一样，暂跳过 LDPC 纠错**，直取子帧 2 的前 600 位、子帧 3 的前 264 位。CRC24Q 分别检查，只有两者均通过才能输出星历。LDPC 校验部分暂未使用，不能把 CRC 通过称为已经实现完整信道译码。
5. 解析 14+600+264=878 位信息/CRC，检查 PRN 一致及基本时间/卫星类型范围。33 位有符号参数使用 64 位整数解析；轨道角采用 MATLAB 的常量 `3.1415926535898`。

增加了工程连续性约束：1800 个符号必须全部为 `valid=1`、`state=locked`，采样点和历元编号连续，且属于同一 `generation`。失锁、缺采样或重捕获会清空未完成窗口，文件尾不满一帧不会输出。原 MATLAB 不含这些质量门限；现有真实记录的有效帧均满足条件。

弱信号中只要一个导频符号出错，当前严格同步就可能漏帧；系统信息位出错会被 CRC 丢弃。先以参考路径打通可运行链路，后续再根据实测需要增加软判决同步与 LDPC 纠错。

## 字段与时间约定

字段名主要沿用 MATLAB，便于逐项核对：`WN` 为 BDT 周号，`HOW` 为周内小时数，`SOH` 为已乘 18 的小时内秒数，`TOW=HOW*3600+SOH`。本工具每个有效帧都刷新时间和星历，避免把第一帧的时间用于后续帧。原参考全段调用保留首个 SF2，并累计后续 SF3 页面；其首帧锚点含义仍保持不变。

`absolute_sample` 是输入 IQ 文件中接收到该帧首符号的零基采样位置，`end_sample` 为末符号之后的采样位置；`sample_time_s=absolute_sample/fs` 是记录相对接收时间。`TOW` 是电文携带的 BDT 发射时间标签，两者不能直接相减当作传播时延。还需要统一接收历元和接收机钟差模型，才能构造伪距。

轨道距离项单位为米，角度为弧度，导数按秒计；钟差和群延迟单位为秒，钟差导数对应 s/s、s/s²。`deltaA` 是相对参考轨道的半长轴差，不能当作旧占位接口的 `sqrt_a`。`SatType` 为 1=GEO、2=IGSO、3=MEO。Page 1 的 `alpha1..alpha9` 按 MATLAB/ICD 的 BDGIM 模型标度提供，不是 GPS Klobuchar 系数。

只有当前帧携带的扩展页面才有数值，其余字段为 `nan`；不把前一页遗留值当成当前帧字段。Page 2、4 及其他页面目前仅提取通用健康/完整性信息，未解析其中的历书等附加内容。

`valid=1` 仅代表通过本工具的帧校验及基本字段检查。健康状态 `HS`、完整性标志 `DIF/SIF/AIF`、精度指标 `SISMAI` 原样输出，定位使用前还要检查健康、时效和适用性，不能据此宣称已经可用于定位。

有一项有依据的参考修正：MATLAB `ephemeris.m` 用子帧 2 的 22:32 位读取 IODC，实际多读了 IODE 的最高位。本实现按 [北斗官方 B1C ICD 图 6-6、7.4.2 节](https://www.beidou.gov.cn/xt/gfxz/201712/P020171226740641381817.pdf) 使用 22:31 共 10 位，IODE 仍为 32:39。对照报告单独记录此差异，不修改原参考文件。

旧的 `0xEB90`、192 位占位解析已删除；`bds_b1c_decode_navigation()` 现在只接受正确的 878 位信息/CRC，并进行真实校验。该占位接口在工程内没有其他调用者。

## MATLAB 基线复现

已有上一阶段的 `tracking.mat`，无需重新运行耗时的 MATLAB IQ 跟踪。在 MATLAB 工程根目录运行：

```matlab
addpath('src/GNSS/tools');
referenceRoot = 'D:/My control center/3-实验室/通导一体化/CU-SDR-Collection-main';
make_b1c_navigation_baseline(referenceRoot, ...
    'build/gnss-tracking/matlab/tracking.mat','build/gnss-navigation/matlab');
make_b1c_navigation_baseline(referenceRoot, ...
    'build/gnss-tracking/automatic_60','build/gnss-navigation/matlab_automatic');
```

脚本用原始 `BCNAV1decoding.m`、BCH 函数、`comm.CRCDetector` 和 `ephemeris.m` 逐帧解码，每次初始化星历，保留原参考目录。MAT 输入验证 MATLAB 跟踪到电文的链路；C CSV 输入额外验证 PRN25 重捕获后的通道。输出 `frames.json` 包含原始 1800 符号、878 位参考结果和全部原始星历字段，`manifest.json` 记录源码与输入哈希。需要原参考使用的 MATLAB 工具箱；缺少 CRC 工具箱会明确失败，不跳过校验。

Linux/WSL 对照上述自动重捕获结果：

```bash
python3 src/GNSS/tools/compare_b1c_navigation.py \
  --reference build/gnss-navigation/matlab_automatic \
  --actual log/gnss/navigation-example \
  --tracking build/gnss-tracking/automatic_60 \
  --output build/gnss-navigation/comparison-example.json
```

脚本检查逐帧比特、极性、起始采样点、全部解析字段，以及对应 1800 个输入历元的状态、连续性、末端采样点和相邻帧时间递增。IODC 按官方更正值比较，其余值要求与原 MATLAB 一致。测试使用的小型真实参考保存在 `tests/data/b1c_navigation_matlab.json`，日常 CTest 不需要原始 2.4 GB IQ 或 MATLAB。

## C API 与下一步

公开接口见 `bds5g/b1c_navigation.h`：`bds_b1c_decode_frame()` 处理单个 1800 符号帧，`bds_b1c_navigator_*()` 可直接接收跟踪回调产生的历元。每通道只保留一个 1800 符号窗口；完整子码匹配时同步回调，包括 BCH/CRC 失败的诊断，不能重入同一个 navigator。单帧接口本身不检查跟踪质量，连续输入应使用 navigator。

实现沿用 CU-SDR/SoftGNSS 相关函数，作者包括 Yafeng Li、Nagaraj C. Shivaramaiah、Dennis M. Akos，保留 GPL-2.0-or-later 归属，参见工程已有 [GPL 正文](../reference/GNSS_SDR/license.txt)。

卫星位置与钟差、统一接收历元伪距和带接收机钟差的北斗单点定位已在后续阶段接通，并通过 MATLAB `satpos.m` / `postNavigation.m` 对照，见 [单点定位说明](positioning.md)。本模块的有效帧标记仍仅代表电文校验，定位模块另行检查数据质量和求解状态。
