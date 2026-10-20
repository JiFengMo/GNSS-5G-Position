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
# 填写示例：center_freq_hz = 3600000000
center_freq_hz = 3600000000

# 采样率，单位复数样本/秒；设备可能调整为实际支持的值。
# 填写示例：sample_rate_hz = 7680000
sample_rate_hz = 7680000

# 接收增益，单位 dB；有效范围由设备决定。
# 填写示例：rx_gain_db = 35
rx_gain_db = 35

# 发射增益，单位 dB；仅发射程序使用。
# 填写示例：tx_gain_db = 0
tx_gain_db = 0

# 测量模式：SSB_TOA 为 SSB，PRS_TOA 为预留的 PRS 路径。
# 填写示例：mode = SSB_TOA
mode = SSB_TOA

# 锚点 CSV 文件路径，相对于当前子配置文件所在目录。
# 填写示例：anchor_db_path = anchors.csv
anchor_db_path = anchors.csv
