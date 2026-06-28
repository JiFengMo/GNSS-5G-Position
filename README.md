# 基于 USRP 的北斗 B1C—5G NR 融合定位系统

北斗 B1C 与 5G NR 多源融合定位系统源码，覆盖射频采样、卫星信号处理、蜂窝同步与测距、统一观测建模以及联合位置解算。

## 功能组成

- 北斗 B1C 主码生成、码相位/多普勒捕获、DLL/PLL 跟踪和导航数据接口。
- 5G NR PSS/SSS/PBCH 同步、信道估计、CIR、SSB/PRS TOA 测量。
- USRP B210/UHD 射频设备接口与硬件时间戳管理。
- 北斗卫星伪距与 5G 基站伪距的统一 ECEF 观测模型。
- 同时估计接收机位置、北斗钟差和 5G 时间偏差的鲁棒加权最小二乘解算。
- IQ 回放、公共 CSV 观测接口和数值测试。

## 工程结构

```text
.
|-- apps/                   # 接收、观测转换和定位命令行程序
|-- include/bds5g/          # 公共数据结构与C API
|-- src/common/             # 观测文件读写
|-- src/bds_b1c/            # B1C码、捕获、跟踪和导航处理
|-- src/nr5g/               # NR TOA与统一观测适配
|-- src/fusion/             # 双时间偏差鲁棒WLS解算
|-- configs/                # B210、n78和融合参数
|-- docs/                   # 架构、数据格式和硬件说明
|-- tests/                  # 定位解算数值测试
|-- 5G/                     # NR-TOA完整信号处理与UHD接收代码
`-- GNSS/                   # SoftGNSS算法参考与历史采样材料
```

系统架构见 [docs/architecture.md](docs/architecture.md)，公共文件格式见 [docs/data_formats.md](docs/data_formats.md)。

## 构建

### 核心算法和命令行程序

核心构建不依赖 UHD：

```bash
cmake -S . -B build -DBDS5G_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

生成文件：

- `bds-b1c-receiver`
- `nr5g-toa-receiver`
- `bds5g-positioning`
- `test-fusion-wls`
- `libbds5g_core.a`

### UHD/USRP 5G接收链

```bash
cmake -S . -B build-uhd -DBDS5G_BUILD_LEGACY_NR=ON
cmake --build build-uhd --parallel
```

该构建要求 Linux、UHD 开发包、pthread 和 `pkg-config`。

## 命令行程序

### B1C捕获

输入为交织 `float32 I/Q` 文件：

```bash
./build/bds-b1c-receiver \
  --input capture_b1c.cf32 \
  --prn 19 \
  --sample-rate 4092000 \
  --doppler-min -8000 \
  --doppler-max 8000 \
  --doppler-step 250
```

输出字段包括 PRN、多普勒频移、码相位、相关峰指标和载噪比。

### 5G TOA观测转换

```bash
./build/nr5g-toa-receiver \
  --output nr_observations.csv \
  --timestamp-ns 1000000000 \
  --anchor-id 0 --pci 100 \
  --x -2268000 --y 5009200 --z 3221010 \
  --toa-ns 845.0 --delay-ns 34.0 \
  --snr-db 28.5 --peak-metric 9.2
```

程序执行固定射频链路延时修正，并输出公共伪距观测。后续测量可使用 `--append` 追加。

### 北斗—5G联合定位

```bash
./build/bds5g-positioning \
  --observations positioning_epoch.csv \
  --initial-x -2267800 \
  --initial-y 5009340 \
  --initial-z 3220990 \
  --output solution.json
```

输出包含 ECEF 坐标、北斗钟差、5G时间偏差、残差 RMS 和参与解算的观测数量。

## 统一观测模型

公共观测方程为：

```text
pseudorange_i = ||receiver_ecef - source_ecef_i||
                + clock_bias(system_i)
                + measurement_error_i
```

北斗和 5G 分别设置时间偏差状态，防止两套射频设备或两个系统的时间差被吸收到位置结果中。解算器依据每条观测的标准差构造权矩阵，并使用 Huber 权降低异常观测的影响。

## 射频配置

- [configs/bds_b1c_usrp_b210.conf](configs/bds_b1c_usrp_b210.conf)：1575.42 MHz B1C 接收参数。
- [configs/nr_n78_usrp_b210.conf](configs/nr_n78_usrp_b210.conf)：n78 SSB/TOA 接收参数。
- [configs/bds5g_fusion.conf](configs/bds5g_fusion.conf)：坐标系、时间尺度与解算参数。
- [5G/targets/PROJECTS/NR-TOA/CONF](5G/targets/PROJECTS/NR-TOA/CONF)：USRP B210、LimeSDR、PCI扫描和锚点配置。

双 USRP B210 运行时应共享 10 MHz 参考时钟和 PPS，并统一使用 UHD 硬件时间戳；USB 数据包到达时间不参与精密 TOA 计算。

## 测试

`tests/test_fusion_wls.c` 检查混合卫星/基站几何条件下的位置和双时间偏差联合估计。5G 子工程还包含 SSB同步、SSB TOA、PRS TOA、FPGA provider 和定位解算相关测试入口。

## 第三方代码

`GNSS/GNSS_SDR` 包含 SoftGNSS 衍生 MATLAB 代码。公开发布时应保留其 `license.txt` 和原始版权声明。其他来源与发布检查见 [LICENSES/README.md](LICENSES/README.md)。

