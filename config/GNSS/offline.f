# run_BDS_only.sh 的离线任务配置；全部相对路径以工程根目录为基准。
# 与 bds_b1c.f 中的实采 USRP 参数独立；此配置不会打开设备。

# IQ 文件路径，不加引号，允许空格；复制到 Linux 后需改为实际文件位置。
# 填写格式：IQ 文件路径，相对于工程根目录；可用绝对路径，允许空格，不加引号。
input_file = ../通导一体化/CU-SDR-Collection-main/L1_IF20KHz_FS18MHz.bin

# ci8：int8 交织 I,Q；cf32：小端 float32 交织 I,Q。
# 选项：ci8：int8 交织 I、Q；cf32：小端 float32 交织 I、Q。
input_format = ci8

# 复数采样率，单位 Hz；应与 IQ 文件真实采样率一致。
# 单位：复数样本/秒（S/s，数值等同 Hz）。
sample_rate_hz = 18000000

# 文件的残余中频，单位 Hz；不是 RF 中心频率。
# 单位：Hz。
if_hz = 20000

# 每通道请求跟踪时长，10 ms 的整数倍，最大 3600 s；需要足够 IQ。
# 单位：s。
seconds = 60

# 捕获搜索 PRN，支持逗号和区间；随后跟踪全部已捕获通道。
# 填写格式：无量纲的 PRN 编号，范围 1～63；逗号分隔，连续范围用短横线。
prns = 1-63

# CPU 工作线程数，1 为串行对照；范围 1～63，实际不超过任务数。
# 不改变采样率、PRN 集合、积分时间或环路参数。
acquisition_threads = 8
tracking_threads = 8

# 北斗独立 UI 端口，避免与 5G 默认 8765 冲突；修改后重新启动脚本。
# 单位：无量纲（TCP 端口号）。
ui_port = 8766
