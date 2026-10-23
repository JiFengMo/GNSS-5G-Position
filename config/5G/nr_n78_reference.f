# 旧 5G 离线适配器参数参考，不可作为在线接收机的 nr_config 子配置。
# 注释说明单位、可选项或填写格式；修改后需重启对应程序。

# 设备类型；旧 5G 适配器参考参数，尚未接入在线加载。
# 选项：usrp_b210：USRP B210；当前该配置仅列出此设备类型。
device = usrp_b210

# 设备选择参数；旧 5G 适配器参考参数，尚未接入在线加载。
# 填写格式：UHD 键值参数字符串，多个键值以逗号分隔；北斗入口的序列号使用独立 serial 项。
device_args = type=b200

# 中心频率，单位 Hz；旧 5G 适配器参考参数，尚未接入在线加载。
# 单位：Hz。
center_frequency_hz = 3550000000

# 采样率，单位复数样本/秒；设备可能调整为实际支持的值。
# 单位：复数样本/秒（S/s，数值等同 Hz）。
sample_rate_hz = 30720000

# 接收带宽，单位 Hz；旧 5G 适配器参考参数，尚未接入在线加载。
# 单位：Hz。
bandwidth_hz = 30000000

# 接收增益，单位 dB；旧 5G 适配器参考参数，尚未接入在线加载。
# 单位：dB。
gain_db = 58

# 参考时钟来源：internal 为内部，external 为外部 10 MHz。
# 选项：internal：设备内部参考；external：外部 10 MHz；gpsdo：板载 GPSDO 参考，须设备实际安装并支持。
clock_source = external

# 时间基准来源：internal 为内部，external 为外部 PPS。
# 选项：none：不选 PPS 同步输入；internal：内部时间源；external：外部 PPS；gpsdo：板载 GPSDO 的 PPS，须设备实际安装并支持。
time_source = external

# NR 频段编号；-1 表示未指定，用于选择扫描和时域参数。
# 单位：无量纲（NR 频段编号）；-1 表示未指定。
nr_band = 78

# 测量模式：SSB_TOA 为 SSB，PRS_TOA 为预留的 PRS 路径。
# 选项：SSB_TOA：SSB 测量路径；PRS_TOA：PRS 测量路径。
mode = SSB_TOA

# SSB 子载波间隔，单位 kHz；当前处理支持 15 或 30，0 使用原有自动选择。
# 选项：0：沿用自动选择；15：15 kHz 子载波间隔；30：30 kHz 子载波间隔。
ssb_scs_khz = 30

# 目标小区 PCI：-1 自动搜索，指定值为 0～1007。
# 单位：无量纲（PCI 编号）；-1 自动搜索，0～1007 指定小区。
target_pci = -1

# 处理实现：ssb/software_ssb 为 SSB 软件算法，prs 为预留 PRS 算法。
# 选项：ssb：SSB 软件实现；software_ssb：ssb 的别名；prs：PRS 软件实现；留空：按 mode 选择。
provider = software_ssb
