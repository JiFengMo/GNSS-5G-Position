# 实验参数沿用迁移前配置；修改前请记录原值。
# 每项的示例用于说明填写格式，不会覆盖实际值。

# 射频后端：usrp 为 USRP；lime 为尚未完成的 LimeSDR 接口。
# 填写示例：sdr = usrp
sdr = usrp

# UHD 设备选择字符串；多设备时用 serial=序列号指定。
# 填写示例：sdr_addrs = serial=2512531
sdr_addrs = serial=2512531

# 参考时钟来源：internal 为内部，external 为外部 10 MHz。
# 填写示例：clock_source = internal
clock_source = internal

# 时间基准来源：internal 为内部，external 为外部 PPS。
# 填写示例：time_source = internal
time_source = internal

# 接收/发射中心频率，单位 Hz；与本次实验频点保持一致。
# 填写示例：center_freq_hz = 2114550000
center_freq_hz = 2114550000

# 采样率，单位复数样本/秒；设备可能调整为实际支持的值。
# 填写示例：sample_rate_hz = 61440000
sample_rate_hz = 61440000

# 接收增益，单位 dB；有效范围由设备决定。
# 填写示例：rx_gain_db = 58
rx_gain_db = 58

# 发射增益，单位 dB；仅发射程序使用。
# 填写示例：tx_gain_db = 0
tx_gain_db = 0

# 测量模式：SSB_TOA 为 SSB，PRS_TOA 为预留的 PRS 路径。
# 填写示例：mode = SSB_TOA
mode = SSB_TOA

# 全频段扫描开关：0 关闭，1 开启；指定扫描列表时优先使用列表。
# 填写示例：full_band_sweep = 0
full_band_sweep = 0

# 严格固定中心频率：0 关闭，1 开启。
# 填写示例：strict_center_freq = 1
strict_center_freq = 1

# 接收增益扫描开关：0 关闭，1 开启。
# 填写示例：gain_sweep_enable = 0
gain_sweep_enable = 0

# 目标小区 PCI：-1 自动搜索，指定值为 0～1007。
# 填写示例：target_pci = 84
target_pci = 84

# 候选 IQ 导出开关：0 关闭，1 开启。
# 填写示例：iq_dump_enable = 1
iq_dump_enable = 1

# SSB 子载波间隔，单位 kHz；当前处理支持 15 或 30，0 使用原有自动选择。
# 填写示例：ssb_scs_khz = 15
ssb_scs_khz = 15

# SSB 周期，单位 ms，用于调度及跟踪预测。
# 填写示例：ssb_period_ms = 20
ssb_period_ms = 20

# 锚点 CSV 文件路径，相对于当前子配置文件所在目录。
# 填写示例：anchor_db_path = anchors.csv
anchor_db_path = anchors.csv
