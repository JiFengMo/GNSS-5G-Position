# 北斗并行实现与 Linux 验收交接

版本：2026-10-27，第一版。基线提交：`9c8edae`。

本地负责代码修改，Linux 负责实际编译、回归和性能测试。此文随代码交付，**不表示测试已通过**。不要把先前独立实验原型的成绩当成本次正式集成的成绩。

## 实现范围

- `bds-b1c-receiver --threads N`：按 PRN 并行捕获，保留归一化、频点/相位遍历、严格 `>` 并列选择和精搜计算顺序。
- `bds-b1c-track --threads N`：常驻线程池按通道推进，包括原有失锁重捕获。通道独占 tracker、重捕获文件句柄和 IQ 工作区；每颗星的 CSV 单独写入，公共 events.csv 在批次屏障后按原通道顺序写入。
- 在线 Engine：并行完成相关和跟踪，再按原 PRN 顺序提交历元、电文和定位。保留完整帧因果性、断段/代次、搜索间隔和接收机时钟顺序。
- 捕获 FFTW 计划创建/销毁在库内统一互斥，执行使用独立工作区。没有 OpenMP 或 FFTW 内部线程依赖，没有全局信号/本地码缓存。工作区估算上限 512 MiB，必要时降低实际捕获并发数；上限不包括 FFTW 内部计划存储、IQ、导航对象等进程内存。
- `bds-b1c-replay`：新增纯文件 Queue + 生产 Engine 回放工具，不链接 UHD。它包含确定性事件和逐块时间线，不包含真实 UHD、生产监控线程和 Log 轮换成本。

所有线程参数允许 1～63，1 为直接串行执行。库和直接运行二进制默认 1；仓库北斗配置显式选择 8/8。旧配置缺少线程项时保持串行，不强制覆盖用户配置。线程数是请求上限，实际不超过 PRN/通道数；捕获还受工作区预算限制。建议同时复测 8/4 和 8/8，与 5G 共机时再确定最终配置。

`config/GNSS/bds_b1c.f`、`config/GNSS/offline.f` 新增：

```ini
acquisition_threads = 8
tracking_threads = 8
```

保持原有 16 秒在线队列，不预读整个 IQ，不改变任何信号、环路、搜索或定位参数。5G 源码不属于此补丁。

## 应用交付补丁

如果已通过 GitHub 的 `main` 分支拉取本次更新，直接进入下面的编译与正确性测试，**不要再应用交付压缩包中的旧补丁**。在 Linux 工程根目录执行 `git pull --ff-only origin main`；有本地修改时先保存，出现冲突或分支分叉时保留现场并回传信息，不强制重置。

交付目录提供 `gnss-parallel.patch`、`source-manifest.json`、`source/` 源码副本和此文。补丁只覆盖本次北斗相关文件，以 Git 基线为依据；源码副本供审查，不要整体覆盖 Linux 工程。

在 Linux 实际工程根目录执行（将补丁路径替换为解压后的绝对路径）：

```bash
git status --short
git apply --check /absolute/path/gnss-parallel.patch
git apply /absolute/path/gnss-parallel.patch
```

若 check 失败，保留本地修改和完整错误，返回本地负责改代码的 AI；不要强制应用、重置用户文件或由测试端另写一套算法。该补丁不要求切换到旧提交，也不含其他已有的 5G 修改。

## 编译与正确性门槛

Linux 环境沿用工程既有 CMake、C/C++17、FFTW3 和 Threads 依赖。没有新增需要安装的并行库。

```bash
cmake --preset gnss-offline
cmake --build --preset gnss-offline --parallel 4
ctest --test-dir build/gnss-offline -R b1c --output-on-failure
python3 -m unittest discover -s ui/tests -p test_bds_launcher.py -v
```

新增输出在 `build/gnss-offline/src/GNSS/`：

- `bds-b1c-replay`：正式引擎文件回放入口。
- `test_b1c_parallel`：线程池屏障/异常/复用，捕获与冻结旧内核逐字段位一致，独立捕获调用并发，多星在线输出顺序及断流对照。

原有 `bds-b1c-receiver`、`bds-b1c-track` 保留名称和输出格式，增加线程参数。`run.json` 增加线程数元数据。跟踪 CLI 测试增加多星输出与重捕获文件句柄隔离对照。

`tests/reference/b1c_acquisition_serial.c` 是从基线冻结的测试参照，仅符号重命名，不链接到任何接收机。不得为了让测试通过而修改参照内核或放宽门限。

先保证 Release 回归通过；然后在独立构建目录检查 ASan/UBSan 和适用范围内的 TSan。不要把 sanitizer 构建用于性能测量。若无法运行，保存原始错误并注明覆盖缺口。

## 相同 IQ 的集成测量

以下以报告中的 4.092 Msps、零中频、6.1 秒合成 cf32 文件为例，请指向原文件。跟踪请求 6 秒，为码相位和文件块末尾保留样本余量；不要直接与上份报告的不同计时范围比较。

```bash
python3 src/GNSS/tools/verify_b1c_parallel.py \
  --input /absolute/path/synthetic-eight-prn.cf32 \
  --label synthetic --input-format cf32 \
  --sample-rate 4092000 --if-hz 0 --seconds 6 \
  --output-dir output/gnss-parallel-linux-NEW
```

脚本不负责构建，不调用原来的实验原型，不连接设备，不修改配置。默认三次重复，运行串行/并行捕获、关闭及开启重捕获的跟踪、限速及不限速队列回放；回放对比 1/1、8/4、8/8。任何非预期退出或精确比较差异立即停止，保留已有日志。

输出：

- `measurements.csv`：外部进程墙钟耗时、退出码及 GNU time 的子进程峰值 RSS（若可用）。这是含初始化和导出的时间，不等于此前内核内部计时。
- `comparisons.json`：捕获 CSV、全部逐星跟踪 CSV、events/summary CSV 和完整回放事件的 SHA-256 精确比较；丢样回放明确跳过，不能将其算作同工作量加速。
- `summary.json`：各方案中位数和范围，比较是否完成及跳过数量。
- `inputs.json`、逐项 `*.command.json`、标准输出/错误和原始结果目录：输入/二进制摘要与复现证据。

如已有真实 IQ，必须使用其真实格式、采样率、中频和搜索参数，并标记 `--label real`；不要把 18 Msps 文件以 4.092 Msps 解释。没有真实数据时明确报告合成范围，不虚构真实捕获或定位结果。

真实导航向量仍通过现有电文/定位和 live_vectors 测试验证；不含合法电文的合成 IQ 可能没有定位，这是预期边界。

## 长流、积压与下一步

独立短记录通过后，用连续生成或真实采集的数分钟 IQ 做长流测试。禁止直接循环拼接 6.1 秒文件并冒充相位连续输入。若长文件不存在，先报告缺项。

示例（文件必须实际包含至少 300 秒且参数匹配）：

```bash
mkdir -p output
build/gnss-offline/src/GNSS/bds-b1c-replay \
  --input /absolute/path/continuous-300s.cf32 --input-format cf32 \
  --sample-rate 4092000 --if 0 --seconds 300 \
  --acquisition-threads 8 --tracking-threads 4 \
  --paced 1 --queue-seconds 16 \
  --output-dir output/b1c-long-a8-t4-NEW
```

再以新目录测 8/8。输出 `events.jsonl`、`timeline.jsonl`、`summary.json`；每次输出目录必须不存在，父目录须存在。限速延迟按块处理完成墙钟减原采样结束时刻计算；不限速不定义实时延迟。队列峰值不包括 DSP 已取出的块和捕获窗口。`complete=true` 仅表示文件已读完且队列排空，不能代表零丢样、有效定位或实时验收通过。

分开报告：首次捕获时间、启动最大积压、稳定阶段积压斜率/延迟、30 秒搜索和失锁重捕获后的恢复时间、丢样/断段计数，以及长期内存变化。检查多星弱信号、更多有效通道、与 5G 共机争用 CPU 的情况。

实际 USRP、外部参考/PPS 和完整实时定位只能在用户明确安排的硬件测试中验收。测试 AI 负责运行、记录和回传失败证据；算法、并发和集成代码由本地 AI 修复。请回传完整结果目录、构建/CTest 日志和实际源代码差异，不只回传摘要报告。
