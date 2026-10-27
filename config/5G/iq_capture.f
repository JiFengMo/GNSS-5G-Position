# 专用原始 IQ 采集配置：sh run_5G_only.sh --capture-iq
# 独立于 config/config.f，不改变普通接收/监看配置。
# 频点、采样率和增益参考现有 n1 / PCI84 实验；不代表当前现场一定有信号。

# UHD 参数字符串；多个键值以逗号分隔。序列号使用独立 serial 项。
device_args = type=b200
# 设备序列号；默认沿用已有 5G 配置。留空时必须只有一台匹配设备。
# 填写后严格选择该序列号，不回退其他设备。
serial = 2512531
# internal：内部参考；external：外部 10 MHz，必须检测到参考锁定。
clock_source = internal
# internal：内部时间源；external：外部 PPS，启动时必须检测到跳变。
# 不复位硬件时钟，也不把设备时间解释为 UTC。
time_source = internal
# RX2：接收专用端口；TX/RX：收发共用端口（本模式仅接收）。
rx_antenna = RX2
# 中心频率，Hz；固定频点，不扫频。
center_freq_hz = 2114550000
# 复数采样率，S/s；输出元数据保存 UHD 回读实际值，不做取整覆盖。
sample_rate_hz = 30720000
# 主时钟，Hz；0 表示由 UHD 选择。设备支持情况需实机确认。
master_clock_rate_hz = 30720000
# 模拟接收带宽，Hz；这是硬件前端设置，不是软件重采样。
bandwidth_hz = 30000000
# 接收增益，dB；当前允许 0～76，按实际信号强度调整。
rx_gain_db = 64
# 采集时长，s；范围 (0,60]。3 秒、30.72 MS/s 的 sc16 IQ 约 368.64 MB。
duration_s = 3
# 每次最多读取的复数样本数，4096～1048576；每样本为 I/Q 两个 int16，共 4 字节。
block_samples = 262144
# 写盘队列块数，2～128；默认最多约 32 MiB 队列，实际吞吐需实机验收。
# 队列满、overflow、超时或时间戳间断时停止，不静默丢弃后继续拼接。
queue_blocks = 32
# 输出子目录，相对于工程 log/；不能使用绝对路径或 ..。
# 每次创建唯一实验目录，保留历史，不覆盖旧数据。
record_dir = iq-capture

# 以下为后续回放的场景提示，不影响采集，也不是实际解码结果。
# SSB 子载波间隔，kHz；可选 15、30。
ssb_scs_khz = 15
# SSB 周期，ms；可选 5、10、20、40、80、160。
ssb_period_ms = 20
# SSB 中心相对调谐中心的频率偏移，Hz；未知时先按实际配置核对。
ssb_freq_offset_hz = 0
# 目标 PCI 提示，无量纲；-1 表示未知，0～1007 指定。
target_pci = 84
# NR 频段编号提示，无量纲；-1 表示未知。
nr_band = 1
