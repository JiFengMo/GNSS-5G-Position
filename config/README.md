# 配置入口、用途与来源

`config.f` 的 `nr_config` 只选择一份 5G 接收配置，`nr_anchor_config` 只供单独启动的锚点发射程序读取。目录里的其他配置不会一起生效。当前默认接收配置是 `5G/ue.toa.ssb.usrpb210.f`；北斗独立脚本读取 `gnss_config`，默认指向 `GNSS/bds_b1c.f`，显式 `--offline` 才读取 `GNSS/offline.f`。融合配置和统一调度开关仍预留。

## 注释规则

- 数值参数写含义和单位，不再重复一行相同数字的“填写示例”。没有物理单位的编号、端口、统计量明确写无量纲或计数单位。
- 枚举和开关写全取值，并简要说明每个取值；0/1 和 true/false 开关也属于选项。只列当前入口支持的值，预留字段明确说明尚未加载。
- 路径、设备参数字符串、PRN/扫描列表说明填写格式和相对路径基准，不当作固定枚举。
- 已有范围、特殊值和限制继续保留。修改参数后重启对应程序；修改代码后需重新构建。

5G 的 clock_source/time_source 直接交给 UHD，注释列 B210 的标准选项，gpsdo 需要实物支持，不能因配置中写了这个值就认为可用。北斗启动器目前仅接受 internal/external，注释按其实际限制列出。没有接外部 10 MHz/PPS 时，相应项应使用 internal。

## 为什么 5G 有很多文件

`config/5G/` 原有 26 份 `.f` 和 1 份 `anchors.csv`，来源已按 Git 基线逐个核对；现另增加 1 份独立原始采集配置 `iq_capture.f`，由 `sh run_5G_only.sh --capture-iq` 使用，不能作为普通 `nr_config` 接收子配置。原有配置来源如下：

- **23 份迁移配置**：在 `14802df` 已存在于 `5G/targets/PROJECTS/NR-TOA/CONF/`，原扩展名为 `.conf`。`f9c0fd1`（2026-10-20）整理时改名迁入，主要是此前不同频点、采样率、增益、USB 条件的调试变体。下面未特别标记的 `.f` 均属这一类。
- **2 份整理时新增**：`ue.toa.ssb.usrpb210.local3600.txrx.f` 和 `ue.toa.ssb.usrpb210.oai_n78_pci0_arfcn641280.f` 首次出现在 `f9c0fd1`，属于新增场景参数，不能称为从旧版本继承的实测配置。
- **1 份旧适配器参考**：`nr_n78_reference.f` 来自 `configs/nr_n78_usrp_b210.conf`，字段体系不同，**不能作为在线接收机的 nr_config**。
- `anchors.csv` 从同一旧 CONF 目录迁入，是锚点表，不是接收配置。

文件名用于保留场景线索，不构成有效性或实测通过证明；`precise`、`oai`、`usb2/usb3` 等后缀也不保证适合当前设备与现场。此次仅整理注释与说明，没有删除、移动这些配置，也没有切换默认参数。

## 5G 配置用途索引

下表频率/采样率直接来自当前文件。`ue.toa.ssb.usrpb210.` 是大多数文件的共同前缀；为便于识别，仍使用完整文件名。

| 文件 | 用途或参数差异 |
|---|---|
| `ue.toa.ssb.usrpb210.f` | 当前默认接收配置；3600 MHz，30.72 MS/s。 |
| `ue.toa.ssb.usrpb210.lowrate.f` | 3600 MHz 低采样率变体，7.68 MS/s。 |
| `ue.toa.ssb.usrpb210.local3600.txrx.f` | 整理时新增；3600 MHz、30.72 MS/s，TX/RX 天线端口和固定中心频率场景。 |
| `ue.toa.ssb.usrpb210.ctc3509.fixed.f` | 3509.76 MHz、7.68 MS/s，关闭全频段扫描。 |
| `ue.toa.ssb.usrpb210.ctc3509.lowrate.f` | 同频点/采样率的另一增益参数组。 |
| `ue.toa.ssb.usrpb210.n1_2114.fixed.f` | 2114.55 MHz、7.68 MS/s，关闭全频段扫描。 |
| `ue.toa.ssb.usrpb210.n1_422910_pci84.precise.f` | 2114.55 MHz、30.72 MS/s，固定 PCI 84 与中心频率。 |
| `ue.toa.ssb.usrpb210.n1_422910_pci84.ultrarate.f` | 同频点/PCI 的 61.44 MS/s 变体，硬件/吞吐能力需核对。 |
| `ue.toa.ssb.usrpb210.oai_default_b210.f` | 3509.76 MHz、30.72 MS/s，目标 PCI 236。 |
| `ue.toa.ssb.usrpb210.oai_n78_pci0_arfcn641280.f` | 整理时新增；3619.2 MHz、30.72 MS/s，目标 PCI 0。 |
| `ue.toa.ssb.usrpb210.multi-target.f` | 多目标列表：2114.55 MHz@PCI84、3550.08 MHz@PCI188、3558.72 MHz；30.72 MS/s。 |
| `ue.toa.ssb.usrpb210.unicom3550.f` | 3550 MHz、30.72 MS/s 的基础参数组。 |
| `ue.toa.ssb.usrpb210.unicom3550.lowrate.f` | 3550 MHz、7.68 MS/s，接收增益 45 dB。 |
| `ue.toa.ssb.usrpb210.unicom3550.lowrate60.f` | 同低采样率，接收增益 60 dB。 |
| `ue.toa.ssb.usrpb210.unicom3550.midrate.f` | 3550 MHz、15.36 MS/s。 |
| `ue.toa.ssb.usrpb210.unicom3550.oai.f` | 3550 MHz、30.72 MS/s，开启全频段扫描。 |
| `ue.toa.ssb.usrpb210.unicom3550.usb2.f` | USB2 场景命名；3550 MHz、7.68 MS/s，开启扫描。 |
| `ue.toa.ssb.usrpb210.unicom3550.usb2.fixed355008.f` | 固定 3550.08 MHz、7.68 MS/s。 |
| `ue.toa.ssb.usrpb210.unicom3550.usb2.fixed355872.f` | 固定 3558.72 MHz、7.68 MS/s。 |
| `ue.toa.ssb.usrpb210.unicom3550.usb3.fixed355872.f` | 固定 3558.72 MHz、30.72 MS/s。 |
| `ue.toa.ssb.usrpb210.unicom3550.usb3.sweep.f` | 3550 MHz、30.72 MS/s，开启扫描。 |
| `ue.toa.prs.usrpb210.f` | PRS 测量模式配置，与 SSB 路径区分。 |
| `ue.toa.ssb.limesdr.f` | 历史 LimeSDR 配置；接口未完成，当前 B210 不使用。 |
| `anchor0.ssb.usrpb210.f` | 独立锚点发射程序参数，3600 MHz、30.72 MS/s；当前主配置选择它。 |
| `anchor1.ssb.usrpb210.f` | 另一份锚点发射参数文件；当前不由主配置选择。 |
| `nr_n78_reference.f` | 旧适配器参数参考，不能作为在线 nr_config。 |

23 份迁移配置的完整旧路径可将新文件名的 `.f` 换成 `.conf`，加上上述旧 CONF 目录获得。没有逐个场景的新实机验证前，不根据文件名认定运营商、小区或硬件条件仍然一致。
