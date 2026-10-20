# 5G 模块使用说明

## 功能与文件

| 位置 | 责任 |
|---|---|
| `app/nr-toa-uesoftmodem.c` | 接收主循环、候选处理、同步状态与测量输出 |
| `app/nr-ssb-anchor.c` | 锚点信标发射入口 |
| `include/nr_config_router.h`、`positioning/common/nr_config_router.c` | 主配置解析与子配置路径选择 |
| `positioning/common/nr_toa_cfg_loader.c` | 原有 5G 参数解析和默认值 |
| `radio/USRP/usrp_lib.cpp` | UHD 设备接入、采样与硬件时间 |
| `positioning/common/nr_iq_ring.c` | IQ 环形缓冲 |
| `positioning/ssb/nr_ssb_sync.c`、`nr_pbch.c` | PSS/SSS、频偏估计、PBCH/DMRS 与 MIB |
| `positioning/ssb/nr_ssb_chest.c`、`nr_ssb_toa_est.c` | 信道、CIR 与 SSB TOA |
| `positioning/prs/` | PRS 提取、信道与 TOA |
| `positioning/common/nr_epoch_mgr.c`、`nr_meas_assoc.c`、`nr_pos_solver.c` | 历元、关联及定位解算 |

在线 USRP 接收链路是当前重点。Lime 保留历史接口，尚不能据此认定完整设备收发可用；PRS、商用基站定位和 GNSS 融合仍需分别验证。SSB 同步成功不等同于定位链路已完成验证。

## 第三方依赖是什么

- **OAI（OpenAirInterface）**：开源蜂窝通信实现。当前工程使用 OAI 风格接口和部分派生代码，并非编译完整 OAI 基站或终端。直接外部包含依赖只保留 `third_party/oai/openair1/PHY/NR_UE_ESTIMATION/filt16a_32.h` 插值系数表，不再要求根目录存在整套 OAI 源码。随附上游 LICENSE 和 NOTICE.md；历史 tracer 文本位于 `third_party/oai_compat`，未参与链接。
- **UHD**：USRP 主机驱动及 C++ 开发库，用于在线硬件收发；离线算法和回放不需要它。
- **pthread、C/C++ 数学和运行库**：线程、同步、数学运算的基础依赖。
- **CMake、Ninja、pkg-config**：构建和依赖查找工具；它们不承担同步算法计算。
- **Python**：日志统计工具需要，核心 C/C++ 接收程序不依赖 Python。CSV 导出与绘图脚本当前仍有占位实现，不能视为已完成工具；后续绘图实现可选用 NumPy、Matplotlib。

OAI 派生代码及 MATLAB 参考的原有声明均予保留。许可证文件说明见 `../../fusion/docs/licenses/README.md`；本次整理没有重新许可源码。

## 实验与回放

在仓库根目录执行：

```bash
# 输出：终端与指定日志、IQ 和 CSV；下一步检查 SSB_LOCK_EVENT / sync_result
bash src/5G/tools/run_ssb_outdoor.sh config/config.f outdoor/receiver.log

# 输出：log/precise_trials_<时间>/ 下的主子配置、日志和 IQ
# 下一步对照各增益的锁定次数、PBCH 结果和延迟稳定性
bash src/5G/tools/run_n1_pci84_precise_trials.sh 58 61 64

# 输出：同步及解码诊断；下一步与该采样的原始日志比较
./build/offline/src/5G/replay_toa_iq /path/to/capture.c16 30720000
```

在线脚本默认二进制路径为 `build/radio/src/5G/nr-toa-uesoftmodem`，可用 `NR_TOA_BIN` 指定其他编译目录。批量实验脚本生成临时主配置引用对应试验子配置，不绕过主配置入口。

工具中的 `NR_TOA_IQ_DUMP_DIR`、`NR_TOA_DELAY_CSV` 继续用于选择诊断输出路径，但现在必须位于仓库根目录 `log/` 内：相对值一律相对于 `log/`，绝对值也只能指向其内部，禁止 `..` 路径。例如 `NR_TOA_DELAY_CSV=outdoor/delay.csv` 会写入 `log/outdoor/delay.csv`；原来的 `/tmp/...` 值需要改写。批量脚本的 `OUT_DIR` 和户外脚本的第二个参数遵循同一规则。

直接启动接收机时，延迟 CSV 默认写入 `log/nr_ssb_relative_delay_<时间>.csv`，IQ 默认写入 `log/iq/`；锚点发射记录写入 `log/tx_event.csv`。两个启动脚本会同时记录标准输出和标准错误；直接运行二进制时终端输出仍显示在终端，可用 `tee log/receiver.log` 保存。历史采样和诊断数据已移至本机 `log/archive/5G-testdata/`，不会随 Git 克隆下载。

`config/5G/nr_n78_reference.f` 是旧离线适配器的参考参数，不能作为在线接收子配置直接使用。

## 配置规则

主配置接受 `enable_5g`、`nr_config`、`nr_anchor_config`、`enable_gnss`、`gnss_config`、`enable_fusion`、`fusion_config`。启用项只能填 0 或 1；未知项、重复项、缺失的运行子配置会报错。使用整行 `#` 注释，不在赋值行末添加注释。5G 子配置保持原解析器和参数默认值；后续新增参数应同时补中文解释、单位和例子。
