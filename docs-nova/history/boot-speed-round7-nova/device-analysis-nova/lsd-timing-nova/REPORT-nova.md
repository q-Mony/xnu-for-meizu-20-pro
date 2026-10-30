# R7：lsd / SpringBoard 启动耗时证据

2026-10-30。新文件，无旧版本。仅分析已采集日志，未操作设备或修改生产代码。

**本轮能直接量出 SpringBoard 成功 exec 捕获到 Setup armed 捕获相隔 152.523992969 秒。** lsd 成功 exec 到同一个 Setup 捕获相隔 146.035655573 秒。这两个值来自记录内部的同一 Mach 时钟；不是 Boot→Hello，也不等同于 lsd 自身启动耗时。用户已确认 Hello，但本日志没有首帧锚点。

原件：[autolog-nova.txt](<nova-root>/logs-nova/boot-speed-round7-nova/candidate-run-nova/autolog-nova.txt)，SHA256 `01e6b73d77676d6436c3d898dc9671ce464bcb422d6d8ffb92ed0b735147420a`。本报告所有行号都是原始日志行号。

| 成功 exec 捕获点 | 原始行 | Mach capture_ns | 含义 |
| --- | ---: | ---: | --- |
| SpringBoard，pid 26 / gen 55 / 主线程 253 | 702 | 47475092864 | 内核成功 exec 捕获点 |
| lsd，pid 97 / gen 272 / 主线程 433 | 893 | 53963430260 | 同上 |
| Setup，pid 231 / gen 885 / 主线程 2071 | 2762 | 199999085833 | status=armed 的捕获点 |

这些绝对数值不是从 Boot 点击开始的累计时间，也不是北京时间。来源见 [SpringBoard](<nova-root>/logs-nova/boot-speed-round7-nova/candidate-run-nova/autolog-nova.txt:702)、[lsd](<nova-root>/logs-nova/boot-speed-round7-nova/candidate-run-nova/autolog-nova.txt:893)、[Setup](<nova-root>/logs-nova/boot-speed-round7-nova/candidate-run-nova/autolog-nova.txt:2762)。

SpringBoard 这一轮出现直接等待 lsd 的形态。L1759、1868、1942、2045、2124、2197、2298、2368、2463 共九次 `sbipc latest` 保持同一 seq=2514、pid=26、tid=253、phase=4，解析后的 endpoint 为 lsd 的 pid=97 / gen=272；L1613 等 `receiver-map` 也确认身份。最早到最晚标签的观测跨度为 **81.597313698 秒**，最后记录的阶段年龄为 **90.060 秒**。这些是日志标签跨度和阶段年龄观测；pending 快照与输出时计算的 age 并非同一原子采样，不能写成严格等待下界或精确请求耗时。

前一组 seq=258 的对端是 launchd。L965、1251、1485、1569 的标签观测跨度为 30.604422917 秒，最后阶段年龄观测为 35.090 秒，适用同样限制。不要将 launchd 的 35.090 秒、lsd 的 90.060 秒与其他累计量相加成启动预算。

L2644 后续首次观察到 SpringBoard 主线程 `status=state-only`；L2678–2679 则显示主线程已进入 seq=39551，对端是 chronod（pid 94 / gen 278）。这说明启动在推进。**本次保留日志没有 seq=2514 的明确完成事件，无法给出它准确解除的时刻。** 不以 state-only、下一次请求或 Setup exec 代替该请求完成，也不借跨时钟减法制造边界。

**lsd 确实执行了大量工作，但并未持续占满一个核心。** 19 对 CPU/IPC 快照全部同代、有效，gen/taskgen/taskgen_after=272、pid=97。19 个 IPC ticks/absolute_ns 对全部符合 `floor(ticks × 10⁹ / 19200000)`，CPU 的 Mach ticks 换算有内部证据；没有借 CNTVCT 前缀去映射 CPU 绝对时刻。

| lsd CPU 记账窗口 | 经过时间 | 用户态 / 内核态 CPU | 总 CPU | 单核等效占比 |
| --- | ---: | ---: | ---: | ---: |
| R7 dump 6→13，L1797→2411，按行序位于九次直接等待观察内侧 | 70.286144 秒 | 13.788144 / 11.171679 秒 | 24.959823 秒 | 35.5117% |
| R7 dump 5→14，L1704→2482，按行序包围上述九次观察 | 90.324523 秒 | 17.999545 / 14.421712 秒 | 32.421257 秒 | 35.8942% |
| R6 同 dump 5→14，旧日志 L1635→2348 | 90.282698 秒 | 16.983074 / 13.537083 秒 | 30.520156 秒 | 33.8051% |
| R7 dump 1→15，L894→2685，最后一个采样仍在 Setup 捕获之前 | 140.409746 秒 | — | 37.538690 秒 | 26.7351% |
| R6 同 dump 1→15，旧日志 L868→2488 | 140.451880 秒 | — | 38.071652 秒 | 27.1065% |

CPU 是整个 lsd task 的已记账执行时间；不是某一工作线程，也不是全系统忙闲率。采样未强制刷新正在运行的尾段，且 CPU 与 IPC 分别采样。窗口内没有记到 lsd 的时间不能直接归为睡眠、调度饥饿、UFS 等待或某个服务卡住。R6/R7 的等待形态不同，上表用于比较工作规模，不能据此宣称端到端提速或退化。R7 在 dump 15 之后的四个约十秒窗口只新增约 0.0533、0.0122、0.0206、0.0024 秒 CPU；此前的大量 lsd 工作此时已明显减少，这仍不是 Hello 显示标记。

**下一步最值得查的是 lsd 的重复服务请求。** 在 R7 dump 15（L2686）前已完成 30,672 个被探针统计的 trap。下列已完成操作的累计量显示，容器管理及 launchd 两类调用值得追到具体调用点和请求内容。

| 目标及 bucket | R7 来源行 / 完成数 | R7 累计经过时间 / 单次最大 | R6 同 dump 15 |
| --- | --- | --- | --- |
| containermanagerd pid 63 / gen 208，bucket 24，mode=2 | L2712 / 3,838 | 41.724101250 / 3.368373177 秒 | 3,838 次；44.686820468 / 10.696553229 秒，旧 L2515 |
| launchd pid 1 / gen 4，bucket 20，send_id=1073742728 | L2708 / 2,550，errors=2,550 | 8.368242083 / 0.677975677 秒 | 2,549 次，errors=2,549；7.114967187 秒，旧 L2511 |
| launchd pid 1 / gen 4，bucket 21，send_id=1073742628 | L2709 / 2,552 | 8.923515208 / 2.902392343 秒 | 2,551 次；6.129176041 秒，旧 L2512 |

这是跨线程可能重叠的 trap 经过时间累计；不能称为服务端 CPU 时间，不能与 lsd CPU 或阶段时间相加。`errors` 是探针的返回结果分类，单凭这一字段无法确定业务失败原因。容器管理身份由 L812 的成功 exec 和 L1651/1783/1967 的 container-map 共同确认。两轮恰好都是 3,838 次是稳定重复工作量的线索，尚未证明请求是冗余的。

间接依赖方面，R7 没有保存 R6 那组三次“SpringBoard 主线程 owner→worker→lsd”的记录，不能把缺记录解释成间接依赖被修好。R6 已验证同 seq worker 等待阶段的采样下界为 36.498 秒；R7 直接等待形态的 90.060 秒阶段年龄观测与它不是同一指标，不能相减比较。

R7 lsd 工作线程 435 在 L1768、1951 分别被观察到等待 containermanagerd，phase_age 为 65 ms、34 ms，seq 分别为 5507、9782；**不同 seq 不合并成一段持续等待**。四轮线程采样还看到主线程 433 以及线程 434/742 反复处于相同等待位置，而线程 435 有时处于 state-only。它们是离散观察，不能累计成等待秒数或确定 CPU 饥饿。container 主线程 seq=75 的长 receive 记录 peer=0、stop=0、class=0，也没有识别到阻塞 lsd 的具体对端，不能据其年龄给容器管理服务定罪。

下一轮应先符号化 lsd 工作线程 435 在重复容器操作附近的调用栈，再对已确认的调用点记录少量请求类别、返回码、单线程起止和计数，区分正常枚举、无效查询反复重试与实际服务等待。优先对准上述约 3,838 次容器请求及约 2,550 次 launchd 消息；在能指出具体重复路径后再做一个可回退的优化候选。

日志完整性需要分层看：console ring 的 dropped=0、NUL=0，只说明本次导出的环形正文情况；**L6256 的 IPC 诊断 ring 已 dropped/overwritten=267,031**。L1470 的 exec-map 捕获达到 256 上限。缺少完成事件、后续 exec 或线程状态不能推断事件没有发生。长等待期间仍有 heartbeat 等日志，最大相邻标签 gap 约 5.88 秒不能解释成启动只等待了 5.88 秒。

复算入口：[analyze-lsd-timing-nova.py](<nova-root>/artifacts-nova/boot-speed-round7-nova/device-analysis-nova/lsd-timing-nova/analyze-lsd-timing-nova.py)，结果与逐条样本：[analysis-nova.json](<nova-root>/artifacts-nova/boot-speed-round7-nova/device-analysis-nova/lsd-timing-nova/analysis-nova.json)。脚本固定核验 R6/R7 原件 SHA256、代际、tick/ns 换算、CPU 单调性及关键身份，输出拒绝覆盖；本报告没有重跑或覆盖主时间戳解析器。

JSON schema_version=1。`capture_intervals` 是 Mach 捕获点差；`sb_direct_lsd` / `sb_launchd` 分开保存 CNTVCT 标签跨度和非原子的 age 观测；`R7_cpu_windows` / `R6_comparison_windows` 是同代 task CPU 记账；`selected_trap_snapshots` 保留并发累计量；`indirect_workers` 保留身份和各次样本；`integrity` 与 `limits` 保留不可推断的边界。所有数值都可以回到原始行，秒数字符串的小数位数不代表实际测量精度。
