# 旧 5G 离线适配器参数参考，不可作为在线接收机的 nr_config 子配置。
# 每项的示例用于说明填写格式，不会覆盖实际值。

# 设备类型；旧 5G 适配器参考参数，尚未接入在线加载。
# 填写示例：device = usrp_b210
device = usrp_b210

# 设备选择参数；旧 5G 适配器参考参数，尚未接入在线加载。
# 填写示例：device_args = type=b200
device_args = type=b200

# 中心频率，单位 Hz；旧 5G 适配器参考参数，尚未接入在线加载。
# 填写示例：center_frequency_hz = 3550000000
center_frequency_hz = 3550000000

# 采样率，单位复数样本/秒；设备可能调整为实际支持的值。
# 填写示例：sample_rate_hz = 30720000
sample_rate_hz = 30720000

# 接收带宽，单位 Hz；旧 5G 适配器参考参数，尚未接入在线加载。
# 填写示例：bandwidth_hz = 30000000
bandwidth_hz = 30000000

# 接收增益，单位 dB；旧 5G 适配器参考参数，尚未接入在线加载。
# 填写示例：gain_db = 58
gain_db = 58

# 参考时钟来源：internal 为内部，external 为外部 10 MHz。
# 填写示例：clock_source = external
clock_source = external

# 时间基准来源：internal 为内部，external 为外部 PPS。
# 填写示例：time_source = external
time_source = external

# NR 频段编号；-1 表示未指定，用于选择扫描和时域参数。
# 填写示例：nr_band = 78
nr_band = 78

# 测量模式：SSB_TOA 为 SSB，PRS_TOA 为预留的 PRS 路径。
# 填写示例：mode = SSB_TOA
mode = SSB_TOA

# SSB 子载波间隔，单位 kHz；当前处理支持 15 或 30，0 使用原有自动选择。
# 填写示例：ssb_scs_khz = 30
ssb_scs_khz = 30

# 目标小区 PCI：-1 自动搜索，指定值为 0～1007。
# 填写示例：target_pci = -1
target_pci = -1

# 处理实现：ssb/software_ssb 为 SSB 软件算法，prs 为预留 PRS 算法。
# 填写示例：provider = software_ssb
provider = software_ssb
