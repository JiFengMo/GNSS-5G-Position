# 实验参数沿用迁移前配置；修改前请记录原值。
# 注释说明单位、可选项或填写格式；修改后需重启对应程序。

# 坐标系；ECEF 为地心地固坐标，单位米；融合配置暂未接入加载。
# 选项：ECEF：地心地固坐标；预留配置，目前仅约定此值，尚未接入加载。
coordinate_frame = ECEF

# 时间标记约定；UTC_NS 表示 UTC 纳秒；融合配置暂未接入加载。
# 选项：UTC_NS：UTC 纳秒时间标记；预留配置，目前仅约定此值，尚未接入加载。
time_scale = UTC_NS

# 融合解算方法；当前为 WLS，配置暂未接入加载。
# 选项：WLS：加权最小二乘；预留配置，目前仅约定此值，尚未接入加载。
solver = WLS

# 最大迭代次数；融合配置暂未接入加载。
# 单位：次。
max_iterations = 15

# 位置收敛阈值，单位米；融合配置暂未接入加载。
# 单位：m。
position_tolerance_m = 0.001

# Huber 降权阈值，单位为观测标准差倍数；融合配置暂未接入加载。
# 单位：无量纲（观测标准差的倍数）。
huber_threshold_sigma = 3.0

# 北斗观测标准差，单位米；融合配置暂未接入加载。
# 单位：m。
bds_measurement_sigma_m = 2.0

# NR 观测标准差，单位米；融合配置暂未接入加载。
# 单位：m。
nr_measurement_sigma_m = 1.5

# 是否分别估计北斗和 NR 钟差：true/false；融合配置暂未接入加载。
# 选项：false：不分别估计两系统钟差；true：分别估计北斗与 NR 钟差。仅为预留配置。
estimate_independent_clock_biases = true

# 是否要求外部 10 MHz：true/false；仅保留配置，当前不强制检查。
# 选项：false：不要求外部 10 MHz；true：要求外部 10 MHz。仅保留声明，尚未执行检查。
require_external_10mhz = true

# 是否要求外部 PPS：true/false；仅保留配置，当前不强制检查。
# 选项：false：不要求外部 PPS；true：要求外部 PPS。仅保留声明，尚未执行检查。
require_external_pps = true
