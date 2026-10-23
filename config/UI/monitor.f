# 是否开启接收程序的监看记录；0 为关闭，1 为开启。
# 选项：0：关闭监看记录；1：开启监看记录。
enabled = 1

# 本机浏览器服务端口，范围 1024～65535；服务仅绑定 127.0.0.1。
# 单位：无量纲（TCP 端口号）。
port = 8765

# 浏览器状态与结果轮询周期，单位毫秒，范围 100～5000。
# 单位：ms。
poll_interval_ms = 200

# 接收程序状态记录最小间隔，单位毫秒，范围 100～60000。
# 单位：ms。
status_interval_ms = 200

# 各扫描目标阶段波形快照最小间隔，单位毫秒，范围 500～60000。
# 单位：ms。
trace_interval_ms = 1000

# 监看文件目录，相对于仓库 log/；禁止父目录跳转。
# 填写格式：记录目录，相对于工程 log/；禁止父目录跳转。
record_dir = monitor

# 北斗离线 UI 实验目录，相对于仓库 log/；由 ui/import_gnss.py 导入。
# 填写格式：北斗离线记录目录，相对于工程 log/。
gnss_record_dir = gnss-ui

# 每段记录的大小上限，单位 MiB，范围 1～512；达到后轮换。
# 单位：MiB（1 MiB = 1048576 字节）。
segment_mb = 128

# 单次实验保留的记录段数，范围 2～64；覆盖本次最旧段，不删除其他实验。
# 单位：段。
max_segments = 8

# 服务内存中保留的测量事件数量，范围 100～50000；旧事件仍可从文件回看。
# 单位：条。
max_history = 5000

# 超过多久未更新即标记数据过期，单位毫秒，范围 1000～120000。
# 单位：ms。
stale_after_ms = 5000

# 信号来源：controlled 为自建可控锚点，commercial 为运营商现网；仅描述场景。
# 选项：controlled：自建可控信号源；commercial：运营商现网信号。
source_mode = controlled

# 时间模型：shared 为收发共同时间基准，receiver_bias 为仅基站同步。
# 选项：shared：收发共同时间基准；receiver_bias：仅基站同步，接收机钟差待估计。
time_model = shared

# 是否已经独立确认发射时间与硬件延迟标定，0 未确认，1 已确认；不自动配置设备时钟。
# 选项：0：尚未独立确认时间与硬件延迟标定；1：已独立确认。仅为声明，不执行标定。
absolute_toa_calibrated = 0
