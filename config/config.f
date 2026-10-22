# 是否启用 5G：1 启用，0 关闭；关闭时 5G 程序会拒绝启动。
# 填写示例：enable_5g = 1
enable_5g = 1

# 浏览器监看子配置，相对于本文件；不改变接收算法参数。
# 填写示例：ui_config = UI/monitor.f
ui_config = UI/monitor.f

# 5G 接收子配置，相对于本文件所在目录；保留迁移前默认配置的参数。
# 填写示例：nr_config = 5G/ue.toa.ssb.usrpb210.n1_422910_pci84.precise.f
nr_config = 5G/ue.toa.ssb.usrpb210.f

# 5G 锚点发射子配置，相对于本文件所在目录；仅锚点程序读取此项。
# 填写示例：nr_anchor_config = 5G/anchor1.ssb.usrpb210.f
nr_anchor_config = 5G/anchor0.ssb.usrpb210.f

# 统一模块调度预留标记；当前 5G 程序不会启动北斗。
# 显式执行 run_BDS_only.sh 时由独立脚本启动，不读取此标记。
# 填写示例：enable_gnss = 0
enable_gnss = 0

# 北斗在线设备配置预留；独立离线脚本使用 config/GNSS/offline.f。
# 填写示例：gnss_config = GNSS/bds_b1c.f
gnss_config = GNSS/bds_b1c.f

# 融合启用标记：0 暂不运行，1 为后续计划启用；当前 5G 程序不会启动融合。
# 填写示例：enable_fusion = 0
enable_fusion = 0

# 融合子配置路径，相对于本文件所在目录；当前预留，尚未接入在线加载。
# 填写示例：fusion_config = fusion/wls.f
fusion_config = fusion/wls.f
