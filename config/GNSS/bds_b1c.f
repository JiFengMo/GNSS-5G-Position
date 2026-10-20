# 实验参数沿用迁移前配置；修改前请记录原值。
# 每项的示例用于说明填写格式，不会覆盖实际值。

# 设备类型；当前 GNSS 配置保留，尚未接入在线加载。
# 填写示例：device = usrp_b210
device = usrp_b210

# 设备选择参数；当前 GNSS 配置保留，尚未接入在线加载。
# 填写示例：device_args = type=b200
device_args = type=b200

# 中心频率，单位 Hz；当前 GNSS 配置保留，尚未接入在线加载。
# 填写示例：center_frequency_hz = 1575420000
center_frequency_hz = 1575420000

# 采样率，单位复数样本/秒；设备可能调整为实际支持的值。
# 填写示例：sample_rate_hz = 4092000
sample_rate_hz = 4092000

# 接收带宽，单位 Hz；当前 GNSS 配置保留，尚未接入在线加载。
# 填写示例：bandwidth_hz = 4000000
bandwidth_hz = 4000000

# 接收增益，单位 dB；当前 GNSS 配置保留，尚未接入在线加载。
# 填写示例：gain_db = 42
gain_db = 42

# 参考时钟来源：internal 为内部，external 为外部 10 MHz。
# 填写示例：clock_source = external
clock_source = external

# 时间基准来源：internal 为内部，external 为外部 PPS。
# 填写示例：time_source = external
time_source = external

# 接收信号类型；当前 GNSS 配置保留，尚未接入在线加载。
# 填写示例：signal = BDS_B1C
signal = BDS_B1C

# 卫星 PRN 列表，逗号分隔；当前 GNSS 配置保留，尚未接入在线加载。
# 填写示例：prn_list = 19,20,21,22,23,24,25,26,27,28,29,30
prn_list = 19,20,21,22,23,24,25,26,27,28,29,30

# 多普勒搜索下限，单位 Hz；当前 GNSS 配置保留。
# 填写示例：doppler_min_hz = -8000
doppler_min_hz = -8000

# 多普勒搜索上限，单位 Hz；当前 GNSS 配置保留。
# 填写示例：doppler_max_hz = 8000
doppler_max_hz = 8000

# 多普勒搜索步长，单位 Hz；当前 GNSS 配置保留。
# 填写示例：doppler_step_hz = 250
doppler_step_hz = 250

# 相干积分时间，单位 ms；当前 GNSS 配置保留，尚未接入在线加载。
# 填写示例：coherent_integration_ms = 10
coherent_integration_ms = 10
