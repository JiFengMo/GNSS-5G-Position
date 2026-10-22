# run_BDS_only.sh 的离线任务配置；全部相对路径以工程根目录为基准。
# 与 bds_b1c.f 中预留的在线 USRP 参数独立；此配置不会打开设备。

# IQ 文件路径，不加引号，允许空格；复制到 Linux 后需改为实际文件位置。
# 示例：input_file = /data/bds/L1_IF20KHz_FS18MHz.bin
input_file = ../通导一体化/CU-SDR-Collection-main/L1_IF20KHz_FS18MHz.bin

# ci8：int8 交织 I,Q；cf32：小端 float32 交织 I,Q。
# 示例：input_format = ci8
input_format = ci8

# 复数采样率，单位 Hz；应与 IQ 文件真实采样率一致。
# 示例：sample_rate_hz = 18000000
sample_rate_hz = 18000000

# 文件的残余中频，单位 Hz；不是 RF 中心频率。
# 示例：if_hz = 20000
if_hz = 20000

# 每通道请求跟踪时长，10 ms 的整数倍，最大 3600 s；需要足够 IQ。
# 示例：seconds = 60
seconds = 60

# 捕获搜索 PRN，支持逗号和区间；随后跟踪全部已捕获通道。
# 示例：prns = 23,25,27,28,37,43,46
prns = 1-63

# 北斗独立 UI 端口，避免与 5G 默认 8765 冲突；修改后重新启动脚本。
# 示例：ui_port = 8766
ui_port = 8766
