# 北斗 B1C 实采配置，由 config/config.f 的 gnss_config 选择。
# sh run_BDS_only.sh 默认打开这台 USRP；离线验证另用 --offline。

# 支持的接收设备型号。
# 示例：device = usrp_b210
device = usrp_b210
# UHD 设备类型与其他参数；序列号使用下面独立的 serial 项。
# 示例：device_args = type=b200
device_args = type=b200
# 可选序列号；留空时默认选 UHD 发现列表中的第一台已连接设备。
# 填写后只选择该序列号，找不到时报错，不回退到其他设备。
# 两台 B210 同时连接时可用此项固定选择北斗设备。
# 示例：serial = 1234567；使用 uhd_find_devices 查询。
serial =
# B1C RF 载频，Hz；不是参考 IQ 的中频。
# 示例：center_frequency_hz = 1575420000
center_frequency_hz = 1575420000
# 复数采样率，Hz；算法使用硬件回读的实际值。
# 示例：sample_rate_hz = 4092000
sample_rate_hz = 4092000
# 主时钟，Hz；32.736 MHz / 8 = 4.092 Msps，支持情况待实机验收。
# 示例：master_clock_rate_hz = 32736000
master_clock_rate_hz = 32736000
# 窄带 BOC(1,1) 接收带宽，Hz。
# 示例：bandwidth_hz = 4000000
bandwidth_hz = 4000000
# RX 增益，dB；按实际天线和前端调整。
# 示例：gain_db = 42
gain_db = 42
# 实际天线接口；此入口只接收，不发射。
# 示例：rx_antenna = RX2；若实际接 TX/RX，则填写 TX/RX
rx_antenna = RX2
# 已确认接外部 10 MHz / PPS；启动时检查实际状态。
# 示例：clock_source = external
clock_source = external
# 示例：time_source = external
time_source = external
# 信号类型。
# 示例：signal = BDS_B1C
signal = BDS_B1C
# 残余中频，Hz；0 表示调谐到 B1C 载频，不沿用离线文件的 +20 kHz。
# 示例：if_hz = 0
if_hz = 0
# PRN 支持逗号和范围；失锁通道独立重捕获。
# 示例：prn_list = 19-30,37,43,46
prn_list = 1-63
# 多普勒下限、上限和粗步长，Hz。
# 示例：doppler_min_hz = -8000
doppler_min_hz = -8000
# 示例：doppler_max_hz = 8000
doppler_max_hz = 8000
# 示例：doppler_step_hz = 250
doppler_step_hz = 250
# 精细步长，必须整除粗步长，Hz。
# 示例：fine_step_hz = 25
fine_step_hz = 25
# CU-SDR 联合捕获统计量门限，不是 C/N0。
# 示例：acquisition_threshold = 10
acquisition_threshold = 10
# 连续跟踪和电文要求 10 ms 完整码历元。
# 示例：coherent_integration_ms = 10
coherent_integration_ms = 10
# DLL / PLL 带宽，Hz；沿用 MATLAB 已验证环路。
# 示例：dll_bandwidth_hz = 1
dll_bandwidth_hz = 1
# 示例：pll_bandwidth_hz = 18
pll_bandwidth_hz = 18
# E/P、P/L 各自偏移，码片。
# 示例：early_late_chips = 0.06
early_late_chips = 0.06
# 队列时长，秒；4.092 Msps、16 秒约 500 MiB，最多 2 GiB。
# 满时丢弃旧积压，显式断段并重捕获，不阻塞采样线程。
# 示例：queue_seconds = 16
queue_seconds = 16
# 定位周期，ms；只用当时已收齐的电文。
# 示例：position_period_ms = 200
position_period_ms = 200
# 仰角门限，度；首次定位后生效。
# 示例：elevation_mask_deg = 5
elevation_mask_deg = 5
# 北斗网页端口，与 5G 的默认 8765 独立。
# 示例：ui_port = 8766
ui_port = 8766
