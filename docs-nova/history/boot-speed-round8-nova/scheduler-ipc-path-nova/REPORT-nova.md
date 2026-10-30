# R8 调度、IPC 与超时路径只读核查

新文件，无旧版本。未改生产代码、构建或操作设备。父任务负责汇总 CHANGELOG。

结论：尚未发现能解释启动长等待的具体调度/IPC 实现错误。已排除“两条长等待由错误的接收超时配置造成”的解释；不能把 launchd 非零返回计数解释成 2550 次业务失败。现有证据更适合继续定位 lsd 的工作/服务依赖，不支持先修改超时、强制成功或直接启用 SMP。

## 证据与复核入口

- 基线日志：`logs-nova/boot-speed-round7-nova/candidate-run-nova/autolog-nova.txt`，SHA256 `01e6b73d77676d6436c3d898dc9671ce464bcb422d6d8ffb92ed0b735147420a`。
- 本目录 `extract-evidence-nova.py` 读取冻结日志、最终打包 ADT/config 和本地源码，生成 `evidence-nova.json`。源码与基线哈希均在 JSON 内。
- 已执行：`python3 artifacts-nova/boot-speed-round8-nova/scheduler-ipc-path-nova/extract-evidence-nova.py`。
- 结果：`PASS`；2 条长等待无接收超时位；8 组 Setup 接收缓冲恢复对照；ADT 完整解析 14224 字节、8 个 CPU 节点；接收超时分支与 XNU HEAD 完全相同。
- 这是证据回放，不执行真实启动路径，不能替代硬件复现或证明优化有效。

## 长等待没有启用接收超时

| 请求 | 日志中最长已观察 age | opt | timeout | 接收超时位 |
|---|---:|---|---:|---|
| SpringBoard seq258，终点 launchd pid1/gen4 | 35.090 s | `8380000400304003` | 0 | 未设置 |
| SpringBoard seq2514，终点 lsd pid97/gen272 | 90.060 s | `838000040700420e` | 0 | 未设置 |

age 是并发采样的未完成请求年龄，不是精确的最终请求耗时。原始行及完整字段在 JSON；后者日志 L2463。两条请求 `special=1 link_kr=0` 说明特殊回复端口链接成功，不代表对端已经执行完业务。

`osfmk/mach/message.h:1013` 定义 `MACH_RCV_TIMEOUT=0x100`。`osfmk/ipc/ipc_mqueue.c:1072–1090` 仅在设置该位时使用 timeout=0 作为立即超时；未设置则 deadline=0，无期限等待。这个源码片段与 HEAD 相同。`osfmk/ipc/mach_msg.c:2494–2519` 保留原始 options/timeout 传递和 poll-yield 判断。`osfmk/ipc/ipc_policy.c` 整文件与 HEAD 相同，没有本地强制超时策略。

因此“这条 90 秒等待就是 90 秒超时重试”“timeout=0 导致忙轮询”“时间单位错误把这条等待放大”的解释不符合这两条请求的选项。无法因此排除对端自己的内部计时器或其他请求的超时。

## launchd 的 errors 需要真实返回码

R7 dump15，原始 L2708：lsd→launchd `send_id=1073742728 / 0x40000388`，2550 complete/2550 errors，累计 trap 时间 8.368242083 s，最大 .677975677 s。L2709 `0x40000324` 为2552 complete/0 errors，8.923515208 s。这些累计值可能跨线程重叠，不能相加当启动时间预算。

`mach_msg.c:258` 的聚合完成函数只保留 `result != MACH_MSG_SUCCESS` 布尔值，没有精确错误码。它无法区分正常接收扩容、轮询超时、端口错误等。因此当前记录不能证明 lsd 2550 次业务失败，也不能给两个 ID 猜测业务名称。

已有同一 R7 对照：Setup gen885/tid2078，L2940/2941 向 launchd 发送 `0x40000388` 成功，L2944/2945 返回 `0x10004004`，L2946/2947 紧接的 receive-only 成功、reply ID `0x20000000`。定义中 `0x10004004=MACH_RCV_TOO_LARGE`；`0x10004003` 才是 `MACH_RCV_TIMED_OUT`。回放检测到 8 组同类对照；它只证明 Setup/Budd 的恢复行为，不能移植成 lsd 已证实结论。

`MACH_RCV_LARGE` 下保留消息并回报需要的大小，随后再次接收的分支未见本地语义修改。R4 的 message-review 报告也只给出了 Setup/Budd 对照。原始 sbipc ring 丢失/覆写267031条（L6256），不能把没捕捉到完成理解为完成没有发生。

## CPU 声明与上线条件

最终打包 `timestamp-candidate-final-nova/unixnu-cpufreq-nova.toml` 无 `sheng-smp`、`cpus`、`cpumask` 参数。不能用 board.toml 默认值替代实际打包参数。

最终 ADT 的 SHA256 为 `e619e34a7ba160ee427c2d401bd86e90777f61d6295b2f8d641bc24b108a1654`。它包含 cpu0…cpu7，reg=0x0…0x700；cpu0=running，其他=waiting。`drivers/ArmCpu/ArmCpu.cpp:175–193` 对副核要求显式 `sheng-smp!=0`；本轮配置因此只接受 boot CPU 的驱动实例。

这仍不是已测得的运行时 active/available 数量。`ml_parse_cpu_topology` 解析 ADT；`ArmCpu.cpp:411` 根据 topology 创建中断控制器；`IOCPU.cpp:586` 调用 `ml_set_max_cpus`。`osfmk/arm/machine_routines_common.c:1273–1295` 的 `ml_wait_max_cpus()` 等的是初始化完成标志，**不等待所有声明核心上线**，没有固定“等待8核”的超时逻辑；相关 common 文件与 HEAD 相同。

`pthread_workqueue.c:2050` 使用 `ml_wait_max_cpus()` 初始化部分最大值；普通 `sched_qos_max_parallelism` 经 host_info 读取实时 logical_cpu（`sched_prim.c:8237`、`host.c:231`），不能只凭 ADT 宣称所有 QoS 限额必为8。此处声明/可用拓扑差异值得记录，但尚无证据证明它造成152秒服务窗口。确认运行时可用数和 scheduler 类型后才能设计对照；本轮不启用副核、不修改调度准入。

## 本地钩子可达性与其他兼容改动

- `sched_prim.c` 本地差异只有3行，调用 Setup semaphore offcore 观察钩子，没有改变调度决策。钩子 `sync_sema.c:223` 在 active=0 时立即返回。
- `mach_msg.c:914` reply-edge 钩子在 `msb_active=0` 时立即返回。`kern_exec.c:3690` 只对确切 `/Applications/Setup.app/Setup` 装配 Setup 观察器；R7 Setup 在 capture_ns=199999085833 才被 armed。该有界观察器的主要记录路径不覆盖此前 SB→Setup 的152.524秒服务窗口。
- workqueue 观察器 `msp_start` 先检查最多3个历史槽的 generation；目标 Setup 未装配时不取完整快照、不打印。其现有准入数学与基线保持一致。没有据此量化所有诊断的总开销。
- sync_sema/sys_ulock 的可见改动以诊断、快照和记录为主，保留原来的 deadline/wake 返回语义；未发现能与已记录的 lsd 长等待直接关联的改动。
- `mach_ports_register/lookup` 的本地 MIG/数组兼容实现确有语义差异，但现有日志不支持把它们作为启动根因。不要把未关联的正确性疑虑包装成已发现的启动 bug。

## 下一步最小观测方案（未实现）

优先继续父任务的持久化状态/重复注册路径分析。若需要消除 launchd errors 歧义，仅在已有 lsd completion 聚合处增加**精确最终返回码直方图**，保留现有 generation、endpoint、send_id、mode 和时窗；针对 `0x40000388/0x40000324` 限定槽数，其他值归 overflow。打印放在既有 autolog drain，不在持锁热路径打印。

如发现 `MACH_RCV_TOO_LARGE` 占主导，再增加少量原始/所需接收大小以及同一线程的下一次 receive-only 成功记录；使用固定槽和引用生命周期，不做未知端口解引用，不更改返回值或缓冲处理。

若精确返回码正常，而服务窗口仍长，使用 R6 `next-probe-design-nova.md` 已审查的 busy/idle + lsd线程状态设计补充“其他任务忙/等待回复”的区分，先核实实际单核运行前提。不要通过 wall−task CPU 直接宣称精确阻塞时间。R7 lsd 37.538690 CPU s / 140.409746 wall s 只能说明两者有差距。
