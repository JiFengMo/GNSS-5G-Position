# 是否开启接收程序的监看记录；0 为关闭，1 为开启。
# 填写示例：enabled = 1
enabled = 1

# 本机浏览器服务端口，范围 1024～65535；服务仅绑定 127.0.0.1。
# 填写示例：port = 8765
port = 8765

# 浏览器状态与结果轮询周期，单位毫秒，范围 100～5000。
# 填写示例：poll_interval_ms = 200
poll_interval_ms = 200

# 接收程序状态记录最小间隔，单位毫秒，范围 100～60000。
# 填写示例：status_interval_ms = 200
status_interval_ms = 200

# 各扫描目标阶段波形快照最小间隔，单位毫秒，范围 500～60000。
# 填写示例：trace_interval_ms = 1000
trace_interval_ms = 1000

# 监看文件目录，相对于仓库 log/；禁止父目录跳转。
# 填写示例：record_dir = monitor
record_dir = monitor

# 北斗离线 UI 实验目录，相对于仓库 log/；由 ui/import_gnss.py 导入。
# 填写示例：gnss_record_dir = gnss-ui
gnss_record_dir = gnss-ui

# 每段记录的大小上限，单位 MiB，范围 1～512；达到后轮换。
# 填写示例：segment_mb = 128
segment_mb = 128

# 单次实验保留的记录段数，范围 2～64；覆盖本次最旧段，不删除其他实验。
# 填写示例：max_segments = 8
max_segments = 8

# 服务内存中保留的测量事件数量，范围 100～50000；旧事件仍可从文件回看。
# 填写示例：max_history = 5000
max_history = 5000

# 超过多久未更新即标记数据过期，单位毫秒，范围 1000～120000。
# 填写示例：stale_after_ms = 5000
stale_after_ms = 5000

# 信号来源：controlled 为自建可控锚点，commercial 为运营商现网；仅描述场景。
# 填写示例：source_mode = controlled
source_mode = controlled

# 时间模型：shared 为收发共同时间基准，receiver_bias 为仅基站同步。
# 填写示例：time_model = shared
time_model = shared

# 是否已经独立确认发射时间与硬件延迟标定，0 未确认，1 已确认；不自动配置设备时钟。
# 填写示例：absolute_toa_calibrated = 0
absolute_toa_calibrated = 0
