# 北斗 B1C 实采配置，由 config/config.f 的 gnss_config 选择。
# sh run_BDS_only.sh 默认打开这台 USRP；离线验证另用 --offline。

# 支持的接收设备型号。
# 选项：usrp_b210：USRP B210；当前该配置仅列出此设备类型。
device = usrp_b210
# UHD 设备类型与其他参数；序列号使用下面独立的 serial 项。
# 填写格式：UHD 键值参数字符串，多个键值以逗号分隔；北斗入口的序列号使用独立 serial 项。
device_args = type=b200
# 可选序列号；留空时默认选 UHD 发现列表中的第一台已连接设备。
# 填写后只选择该序列号，找不到时报错，不回退到其他设备。
# 两台 B210 同时连接时可用此项固定选择北斗设备。
# 填写格式：可选设备序列号字符串；留空选择默认发现设备，填写后严格匹配，找不到报错。
serial =
# B1C RF 载频，Hz；不是参考 IQ 的中频。
# 单位：Hz。
center_frequency_hz = 1575420000
# 复数采样率，Hz；算法使用硬件回读的实际值。
# 单位：复数样本/秒（S/s，数值等同 Hz）。
sample_rate_hz = 4092000
# 主时钟，Hz；32.736 MHz / 8 = 4.092 Msps，支持情况待实机验收。
# 单位：Hz。
master_clock_rate_hz = 32736000
# 窄带 BOC(1,1) 接收带宽，Hz。
# 单位：Hz。
bandwidth_hz = 4000000
# RX 增益，dB；按实际天线和前端调整。
# 单位：dB。
gain_db = 42
# 实际天线接口；此入口只接收，不发射。
# 选项：RX2：接收专用接口；TX/RX：收发共用接口（此项用于接收）。B210 端口取值；其他设备按 UHD 实际支持列表。
rx_antenna = RX2
# 时钟设置须与本次接线一致；未接外部 10 MHz/PPS 时，两项均填 internal。
# 选项：internal：B210 内部参考；external：外部 10 MHz，未接入或未锁定时报错。
clock_source = external
# 选项：internal：内部时间源；external：外部 PPS，未检测到时会报错。
time_source = external
# 信号类型。
# 选项：BDS_B1C：北斗 B1C；当前北斗入口仅支持此信号。
signal = BDS_B1C
# 残余中频，Hz；0 表示调谐到 B1C 载频，不沿用离线文件的 +20 kHz。
# 单位：Hz。
if_hz = 0
# PRN 支持逗号和范围；失锁通道独立重捕获。
# 填写格式：无量纲的 PRN 编号，范围 1～63；逗号分隔，连续范围用短横线。
prn_list = 1-63
# 多普勒下限、上限和粗步长，Hz。
# 单位：Hz（多普勒搜索下限）。
doppler_min_hz = -8000
# 单位：Hz（多普勒搜索上限）。
doppler_max_hz = 8000
# 单位：Hz（粗搜索步长）。
doppler_step_hz = 250
# 精细步长，必须整除粗步长，Hz。
# 单位：Hz。
fine_step_hz = 25
# CU-SDR 联合捕获统计量门限，不是 C/N0。
# 单位：无量纲（捕获统计量门限）。
acquisition_threshold = 10
# 连续跟踪和电文要求 10 ms 完整码历元。
# 单位：ms。
coherent_integration_ms = 10
# DLL / PLL 带宽，Hz；沿用 MATLAB 已验证环路。
# 单位：Hz（DLL 码环带宽）。
dll_bandwidth_hz = 1
# 单位：Hz（PLL 载波环带宽）。
pll_bandwidth_hz = 18
# E/P、P/L 各自偏移，码片。
# 单位：码片（chip）。
early_late_chips = 0.06
# 队列时长，秒；4.092 Msps、16 秒约 500 MiB，最多 2 GiB。
# 满时丢弃旧积压，显式断段并重捕获，不阻塞采样线程。
# 单位：s。
queue_seconds = 16
# 计算线程，1 为串行对照；范围 1～63，不改变信号或环路参数。
# 当前独立北斗实验起点为 8/8；与 5G 共机时需复测 8/4 等组合。
# 单位：工作线程数。捕获按 PRN 并行，FFTW 内部保持单线程。
acquisition_threads = 8
# 单位：工作线程数。跟踪按卫星通道并行，电文和定位按原顺序提交。
tracking_threads = 8
# 定位周期，ms；只用当时已收齐的电文。
# 单位：ms。
position_period_ms = 200
# 仰角门限，度；首次定位后生效。
# 单位：°。
elevation_mask_deg = 5
# 北斗网页端口，与 5G 的默认 8765 独立。
# 单位：无量纲（TCP 端口号）。
ui_port = 8766
