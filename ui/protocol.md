# 监看协议 v1

所有 HTTP 接口仅本机 GET，只负责读取。第一版没有写配置、启动/停止接收、上传执行代码或 IQ 重算接口。

| 接口 | 内容 |
|---|---|
| `/api/bootstrap` | 监看子配置、当前磁盘主/5G 配置与路径（不冒充已加载快照） |
| `/api/state?cell=<freq:PCI:SSB>` | 当前最新实验的状态、各信号摘要、选中信号最近快照和最多 300 点结果历史 |
| `/api/recordings` | 配置目录中的 JSONL 分段及实验元数据；演示有 `demo: true` |
| `/api/replay?file=<basename>&offset=<cursor>` | 每页最多 100 条，使用返回的 `next_offset` 继续；包含初始配置 |

后续分页附带 `generation=<config.time_ms>`，检测轮换覆盖；覆盖后返回错误并重新选择文件。每个分段都从 config 行开始，可以独立回看；分页中的状态按该页已读事件重建，不引用之后的事件。不完整尾行等写入完成后再读，损坏行计入诊断。实时启动时最多从每段尾部 4 MiB 读起，跳过旧积压会显示 `reader_skipped_bytes`；完整保留段可分页回看。

JSONL 公共字段：`schema: 1`、`kind`、`run_id`、`time_ms`（Unix 毫秒）。非配置事件携带 `seq`（单次实验严格递增）；sample 的 `event_id`、`hardware_sample`、`abs_sample` 保持字符串，避免 JavaScript 对 64 位硬件采样计数丢失精度。界面时间与硬件采样计数不混作同一时基。

- `config`：来源/时间模型、已加载的主要配置参数、标定声明。轮换时重复写入，参数不从后续磁盘文件重构。
- `runtime`：接收状态、设备最近报告的参考/PPS/时钟锁定、实际采样率、接收块数、采样间断、同步任务丢弃、环形缓冲溢出。
- `sample`：频点/PCI/SSB、真实锁定、PBCH/MIB、失败阶段、频偏、SNR、相关指标、粗定时与跟踪移位、测量相对时延、候选 TOA、质量及可选阶段曲线。失锁/未执行阶段没有伪造曲线。

曲线结构：`curves.<name> = {x: [...], y: [...], original_points: N}`。x 是原采样点/子载波/CIR 索引；y 为线性幅值、相位 rad 或相关功率。非有限值写成 null，不跨缺失点连线。`pss_*`、`cir_magnitude` 在展示压缩时保留分桶最大值的原坐标；相关阈值判定仍使用原算法完整数据。

`dropped` 是监看队列竞争/容量造成的累计跳过次数，和 `rx_gaps`、`sync_dropped`、`ring_overruns` 分开。`io_errors` 记录写入错误；写入失败时接收继续、记录停止增长，页面随后显示数据过期。

`/api/state.position` 预留契约：

```json
{
  "valid": false,
  "xyz_m": null,
  "reason": "not_implemented",
  "supported_time_models": ["shared", "receiver_bias"],
  "used_anchor_ids": []
}
```

未来定位模块必须提供坐标系、观测时刻、实际参与的独立锚点及质量信息；不会从 PCI/SSB 条目数直接推导有效定位。控制接口作为未来独立能力扩展，当前 `capabilities.control`、`iq_recompute`、`position_solver` 均为 false。

## 北斗离线接口 v1

以上 `/api/state`、JSONL 与 `position_solver` 契约用于 5G，继续保持原有语义。北斗使用独立索引与接口，`/api/bootstrap` 的 `gnss` 字段声明 `offline: true`、`live: true` 和已配置离线记录目录。服务支持 `--signal bds` 指定默认页面，`--bds-source live` 指定默认实采视图，`--port` 指定本进程端口；这些参数不控制设备或运行算法。

| GET 接口 | 内容 |
|---|---|
| `/api/gnss/runs` | `gnss_record_dir` 下已完成导入的实验摘要，以导入时间倒序排列 |
| `/api/gnss/state?run=<id>&time=<seconds>&prn=<number>&window=30` | 选定采样时刻的状态、四种数量、逐星跟踪/观测/排除信息、单星曲线、定位历史、事件与最近帧 |
| `/api/gnss/frames?run=<id>&time=<seconds>&prn=<number>&offset=0&limit=20` | 该游标前已收齐帧的分页列表、总数与游标，最多 50 条 |

`state` 中 `time` 省略时使用实验默认时刻：最后一个定位历元，或跟踪记录末尾；`prn` 省略时选择首颗记录卫星。时间限制到实验边界，非有限参数和不属于实验的 PRN 返回 HTTP 400。窗口必须大于 0 且不超过 120 秒。实验 ID 只允许导入目录名，不接受任意路径；SQLite 以 `mode=ro` 打开，没有写入 HTTP 接口。

`run` 包含算法配置快照、源文件 SHA256、采样率和 `offline_retrospective: true`。`cursor_s` 与各对象 `time_s` 是输入文件相对采样时间；`absolute_sample`、`generation`、`epoch` 等 64 位计数以字符串返回。`satellites[].tracking` 仅在当前时刻落入该跟踪历元的半开采样区间时存在；`last_tracking` 可作为诊断保留，但不冒充当前有效数据。`navigation_ready` 要求已收齐的有效帧与当前有效跟踪属于同一连续段。

`position` 是游标之前最近的离线结果；还需检查 `position_current` 和 `position.valid`。`position.available_s` 表示用星对应的完整电文最晚收齐时刻，允许晚于定位采样时刻；客户端必须标注离线回溯性质。`position_history` 只含游标前结果，采样压缩保留无效分隔项（只有 `time_s` 和 `valid: 0`）；客户端不应把这些项当作零坐标。`frames[].available_s <= cursor_s`，不会提前列出尚未收齐帧。

单星 `series` 仍使用 `{x,y,original_points}`，横坐标单位为秒，值的单位见对应导出列。分桶保留极值，代次变化/间断/无效位置插入 null 断线；不包含完整捕获曲面或原始 IQ。`events` 最多返回游标前最近 60 项，当前视窗之前的事件可在事件表中追溯。导入要求和展示边界见 [gnss.md](gnss.md)。

## 北斗实采接口 v1

`GET /api/gnss/live?prn=<number>&window=30` 读取 `log/gnss-live/` 最新实验分段，返回与北斗离线 `state` 类似的结构以及 `live`；没有记录时返回 `waiting: true`。`GET /api/gnss/live/frames?prn=<number>&offset=0&limit=20` 查询当前缓存的已收齐帧，最多 50 条。没有写入或设备控制接口。

实采 JSONL 与 5G JSONL 使用独立目录和记录类型：`config.data.kind = b1c_live`，另有 `hardware_origin`、`runtime`、`acquisition`、`tracking`、`frame`、`position`、`event`、`gap`。每段重复配置；计数均保留字符串，非有限数序列化为 null。`runtime` 包含 `running`、`stage`、收到/处理/队列样本数、队列丢弃、UHD overflow、间断、延迟、参考与 PPS 状态。实际硬件参数保存在已加载配置的 `device` 中。

`live.connected` 要求程序报告运行且状态不超过 3 秒；`live.current` 另要求处理滞后不超过 2 秒且 reader 没有未读积压。当前锁定和定位用星以此为门限，历史数据仍保留作诊断。定位还检查 `position_current` 和 `position.valid`。在线 `position.available_s <= position.time_s`，观测只能使用已完整接收且同代次/连续段的有效帧；不会沿用离线回溯模式。

reader 每请求最多读取 4 MiB、单行 256 KiB；每星 12000 跟踪历元/256 帧、800 定位项、240 事件。半行等待续写，损坏行计数并跳过；跳过数据的曲线断开。只保留滚动历史，不提供完整实采归档回放。
