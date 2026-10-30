# 第六轮真机日志分析 — Nova

2026-10-30。新文件，无旧版本。用户已确认到达 Hello；已回 Android B 并回收日志。此次没有人工总计时，也没有 Boot 菜单与 Hello 首帧日志锚点，因此 **Boot→Hello 总时间未知，未证明端到端提速**。

新的实机证据把重点指向桌面启动中的服务依赖与 CPU 分配。lsd 在一个 90.283 秒观察窗口内累计执行 30.520 秒；根盘读取与状态刷新在分别包围这些观察记录的窗口内只占少量时间。不能据此认定全部剩余时间都是 lsd 睡眠，也不能排除其他任务占用 CPU。

## 实测与证据范围

原始日志 [autolog-nova.txt](<nova-root>/logs-nova/boot-speed-round6-nova/candidate-run-nova/autolog-nova.txt) SHA256 为 `367d82822557acd8967eb53f522a8c3d7db70dc268fc6fbb02f8a3337d882ad8`。冻结复算入口为 [analyze-round6-nova.py](<nova-root>/artifacts-nova/boot-speed-round6-nova/device-analysis-nova/analyze-round6-nova.py)，当前结果为 [analysis-final-nova.json](<nova-root>/artifacts-nova/boot-speed-round6-nova/device-analysis-nova/analysis-final-nova.json)。早期 analysis-nova.json 保留为历史草稿，正式报告不引用其旧字段语义。

| 测量对象 | 结果 | 解释边界 |
|---|---:|---|
| 同代 lsd CPU/IPC 快照 | 22 对，全部有效 | gen/taskgen/taskgen_after=270，pid=96；不混代 |
| 主线程相同锁等待位置观察内侧的 CPU 窗口 | 90.282698 秒 | CPU dump 5→14；不是完整 Boot 或已证明连续锁等待时长 |
| 此窗 lsd 用户态 / 内核态 CPU | 16.983074 / 13.537083 秒 | 合计 30.520156 秒，单核等效 33.805%；不是全系统 CPU 使用率 |
| 同窗已完成 trap 数 | 26,223 | trap 的累计经过时间不是 CPU 时间，不能与上述相加 |
| SB 工作线程等待 lsd 的阶段年龄 | 至少 36.498 秒 | 同一 seq=2383 的锁内采样；不等于主线程整个等待时长 |
| 根盘独立窗口 | 111.649080 秒 | CNTPCT，按日志顺序包围相关 SB 观察记录 |
| 该根盘窗 read / verify / copy | 1.796024 / 0.279908 / 0.027343 秒 | 全局累计调用经过时间；read 包含锁、轮询、缓存维护，不能称纯介质时间 |
| 状态绘制独立窗口 | 110.324491 秒 | CNTVCT；75 次绘制累计 0.544940 秒，滚屏增量为零 |
| 同工作量 23 次 cs4k | 本轮 19.371 秒，上轮 19.356 秒 | 字节数、读取次数与顺序完全相同；没有新的读取/哈希收益 |
| 本次保留日志中的 Setup / chronod watchdog | 0 / 1 | 未见 Setup watchdog 不等于已修复；不能与更长历史采集直接按数量比较 |

CPU 计时由同一 `mach_absolute` 域相减，全部 22 个 IPC ticks/absolute_ns 对都满足 `ns=floor(ticks×10^9/19200000)`。rootio 的 CNTPCT、绘制的 CNTVCT 与 mach_absolute 分开计算；不直接混减绝对时刻。跨域窗口通过记录行序包围，边界不是同一原子时刻，不能把这些项目相加成完整启动预算。

CPU 记账函数见 [kern_exec.c:3590](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:3590)，真实转换见 [recount.c:437](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/recount.c:437)。记账未强刷正在运行的尾段；全 task 时间不是某一工作线程时间。22 次 recount 内部采样窗口合计 28.906 微秒，最大 1.927 微秒；这不包含 registry 查找、printf 和日志写盘，不能称整个诊断成本。

## 本轮等待链换了形态

第 1608–2427 行有 11 次主线程 tid=252 的相同 event/PC/LR、hint=10 等待状态。它们只证明各次采样看到相同位置，不保证采样之间从未醒来。

第 [1679 行](<nova-root>/logs-nova/boot-speed-round6-nova/candidate-run-nova/autolog-nova.txt:1679)、1783、1939 行的 ulock 记录显示：SpringBoard 主线程 tid=252 的观察到的 ownerTID 为 1166。第 [1682 行](<nova-root>/logs-nova/boot-speed-round6-nova/candidate-run-nova/autolog-nova.txt:1682)、1786、1942 行显示该工作线程在同一 seq=2383、phase=4 中等待 peer=96、peer_gen=270，也就是本轮 lsd；阶段年龄为 5.898、16.088、36.498 秒。

```text
SpringBoard 主线程 252
  → ulock 的观察到的 owner：工作线程 1166
    → IPC peer：lsd（pid 96，generation 270）
```

因此旧解析器没找到“SB 主线程直接等 lsd”的长 pending，不表示依赖消失。三个 owner/worker 快照并非跨对象原子快照，owner 字段也是 wait 时观察值；用它们确定后续排查方向，不假设锁的完整生命周期已经重建。

另一个计时语义修正：旧 `sbipc latest` 在复制 pending 后才重新读时钟计算 age，27.660 秒只能叫 **pending 快照的阶段年龄观测值**，不能声称严格等待下界。历史报告对同类 36.278/101.427 秒使用“≥”的措辞也需同样收窄，数值本身未变。worker 路径在 [mach_msg.c:1713](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:1713) 锁内保存 sample_now 并复制 pending，所以本轮 worker 的 36.498 秒不受该重读时钟问题影响；使用 phase_age，而非任意操作总年龄。

## CPU 频率与日志核查

一次 domain0 四字只读寄存器采样成功，enable=3、state=0x2e000c57、dcvs=0x9b30、request_index=12。19.2 MHz 是计时器频率，12 是请求索引；没有完成魅族输入时钟/LUT 验证，不能报告实测 CPU MHz，更不能推断整个启动过程中低频。细节见 [独立核查报告](<nova-root>/artifacts-nova/boot-speed-round6-nova/device-analysis-nova/cpufreq-log-review-nova.md)。

原始窗口和正文 SHA256 与 capture 元数据相符。正文 1,118,707 字节比 ring_total 多 173 字节，已逐字节核实是正常 boot-video 前缀；dropped=0、NUL=0。这里只能确认这份持久快照的封装和提取一致，复位前未刷盘的尾部仍可能缺失。

## 下一项开发

下一轮的首要问题是：**lsd 没有记到账上的那部分时间，究竟是在等依赖、等 CPU，还是因其他任务大量运行而被推迟？** 先补同一时钟、同一采样窗口的 CPU busy/idle、少数关键服务 CPU 记账和 lsd 工作线程状态，再决定修改哪条服务或调度路径。单次状态不累计为等待秒数；要报告精确排队时间须先验证现有 scheduler 计数的语义。

自动时间表继续保留 Boot 点击、苹果首次显示、Hello 首次显示为 unknown。下一轮同时应让 pending 状态与采样时间在锁内一致，保存 Setup exec 的准确时刻与跨时钟配对校准。完整 Boot→Hello 自动计时另需固件菜单锚点及已验证的 Hello 画面标记，不能用 Setup exec 或首 swap 代替。

本次日志分析没有改生产代码、生成新候选或再次操作设备。设备最后确认在 Android B，boot_a 为第六轮候选 `a2114a6a…79b5bd1e`，boot_b 原哈希保持；记录见 [postrun-device-state-nova.json](<nova-root>/logs-nova/boot-speed-round6-nova/postrun-device-state-nova.json)。
