# 实验参数沿用迁移前配置；修改前请记录原值。
# 注释说明单位、可选项或填写格式；修改后需重启对应程序。

# 射频后端：usrp 为 USRP；lime 为尚未完成的 LimeSDR 接口。
# 选项：usrp：USRP/UHD 后端；lime：历史 LimeSDR 接口，当前未完成。
sdr = usrp

# UHD 设备选择字符串；多设备时用 serial=序列号指定。
# 填写格式：UHD 设备参数字符串；serial= 后填写目标设备序列号，多个键值以逗号分隔。
sdr_addrs = serial=2512531

# 参考时钟来源：internal 为内部，external 为外部 10 MHz。
# 选项：internal：设备内部参考；external：外部 10 MHz；gpsdo：板载 GPSDO 参考，须设备实际安装并支持。
clock_source = internal

# 时间基准来源：internal 为内部，external 为外部 PPS。
# 选项：none：不选 PPS 同步输入；internal：内部时间源；external：外部 PPS；gpsdo：板载 GPSDO 的 PPS，须设备实际安装并支持。
time_source = internal

# 接收/发射中心频率，单位 Hz；与本次实验频点保持一致。
# 单位：Hz。
center_freq_hz = 3600000000

# 采样率，单位复数样本/秒；设备可能调整为实际支持的值。
# 单位：复数样本/秒（S/s，数值等同 Hz）。
sample_rate_hz = 7680000

# 接收增益，单位 dB；有效范围由设备决定。
# 单位：dB。
rx_gain_db = 35

# 发射增益，单位 dB；仅发射程序使用。
# 单位：dB。
tx_gain_db = 0

# 测量模式：SSB_TOA 为 SSB，PRS_TOA 为预留的 PRS 路径。
# 选项：SSB_TOA：SSB 测量路径；PRS_TOA：PRS 测量路径。
mode = SSB_TOA

# 锚点 CSV 文件路径，相对于当前子配置文件所在目录。
# 填写格式：锚点 CSV 路径，相对于当前子配置目录。
anchor_db_path = anchors.csv
