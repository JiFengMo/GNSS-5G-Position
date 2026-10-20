# 实验参数沿用迁移前配置；修改前请记录原值。
# 每项的示例用于说明填写格式，不会覆盖实际值。

# 坐标系；ECEF 为地心地固坐标，单位米；融合配置暂未接入加载。
# 填写示例：coordinate_frame = ECEF
coordinate_frame = ECEF

# 时间标记约定；UTC_NS 表示 UTC 纳秒；融合配置暂未接入加载。
# 填写示例：time_scale = UTC_NS
time_scale = UTC_NS

# 融合解算方法；当前为 WLS，配置暂未接入加载。
# 填写示例：solver = WLS
solver = WLS

# 最大迭代次数；融合配置暂未接入加载。
# 填写示例：max_iterations = 15
max_iterations = 15

# 位置收敛阈值，单位米；融合配置暂未接入加载。
# 填写示例：position_tolerance_m = 0.001
position_tolerance_m = 0.001

# Huber 降权阈值，单位为观测标准差倍数；融合配置暂未接入加载。
# 填写示例：huber_threshold_sigma = 3.0
huber_threshold_sigma = 3.0

# 北斗观测标准差，单位米；融合配置暂未接入加载。
# 填写示例：bds_measurement_sigma_m = 2.0
bds_measurement_sigma_m = 2.0

# NR 观测标准差，单位米；融合配置暂未接入加载。
# 填写示例：nr_measurement_sigma_m = 1.5
nr_measurement_sigma_m = 1.5

# 是否分别估计北斗和 NR 钟差：true/false；融合配置暂未接入加载。
# 填写示例：estimate_independent_clock_biases = true
estimate_independent_clock_biases = true

# 是否要求外部 10 MHz：true/false；仅保留配置，当前不强制检查。
# 填写示例：require_external_10mhz = true
require_external_10mhz = true

# 是否要求外部 PPS：true/false；仅保留配置，当前不强制检查。
# 填写示例：require_external_pps = true
require_external_pps = true
