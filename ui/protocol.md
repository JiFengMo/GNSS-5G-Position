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
