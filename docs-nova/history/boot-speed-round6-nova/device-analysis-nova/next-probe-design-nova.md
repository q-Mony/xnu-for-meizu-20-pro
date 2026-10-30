# 下一轮最小调度观测设计 — Nova

2026-10-30。新文件，无旧版本。**仅设计，未实现、未构建、未刷写、未操作设备。** 本项只新增此报告，不改其他工作者的文件。

首选在现有 autolog 线程和固定启动预算内，增加单核 busy/idle 累计读数与最多 16 个 lsd 线程的短状态快照。先判断剩余时间主要伴随系统空闲、lsd 可运行但未获得足够 CPU，还是非 lsd 的 CPU 记账。保留第六轮同 generation 的 task CPU 累计值，暂不添加调度热路径事件、频率控制或全进程扫描。

第六轮 dump5→14 的墙钟约 90.283 秒、lsd task 已记账 CPU 约 30.520 秒，只能得出两者存在约 59.763 秒差额。不能把差额全部命名为 lsd 阻塞时间：task CPU 是所有线程的累计，存在未结算尾部；其他任务运行、调度排队和多个 lsd 线程并发等待都未分解。起止记录见 [日志 1635 行](<nova-root>/logs-nova/boot-speed-round6-nova/candidate-run-nova/autolog-nova.txt:1635) 与 [2348 行](<nova-root>/logs-nova/boot-speed-round6-nova/candidate-run-nova/autolog-nova.txt:2348)。

## 可直接复用的计数与限制

| 现有接口/字段 | 适合下一轮的用途 | 复用限制 |
|---|---|---|
| recount_processor_usage | 读取 processor active 的 user/system 累计值与 idle 累计值 | active 是已记账值；idle 会补到当前时刻；两者及 lsd CPU 不是同一个原子采样 |
| sched_run_buckets[TH_BUCKET_RUN] | O(1) 原子读取系统当前 runnable 数量，记录均值/峰值等观测量 | 包含正在运行线程和 autolog 自身；不是纯排队长度，更不是 lsd 排队时长 |
| lsd thread state / block_hint / wait_result | 识别采样时明确等待、可运行、挂起或终止的线程 | TH_RUN 同时包含 running/on-runq，TH_WAIT 与 TH_RUN 可在过渡阶段共存；无法把瞬时状态当整段时间 |
| last_made_runnable_time / last_run_time | 提供最近唤醒、抢占、切离的上下文 | 前者会在抢占时更新；不能当生命周期排队累计值；运行中也可能保留历史时间 |
| thread runnable_timer | 已有“可运行且包括正在运行”的累计计时，可保留 raw 值和 stamp | timer_grab 只取已累计 all_bits，不补当前尾段；不能直接减 CPU 就宣布精确排队时间 |
| recount_thread_usage | 按同 tid/task generation 读线程 CPU 累计，定位哪几个 lsd worker 消耗 CPU | 在所有任务/线程锁外调用；跨时刻读数和仍在运行的未结算尾段需保留误差 |

[recount.h:365](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/recount.h:365) 声明 processor 读取接口；[recount.c:1236](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/recount.c:1236) 读取 active 后将 idle 补到现在。[processor_cpu_load_info:664](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/processor.c:664) 已采用该接口。诊断应保留 64 位 mach ticks，避免该用户接口转换为较粗 32 位 CPU tick 后再求差。

recount 的 [recount_read_track:296](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/recount.c:296) 使用 seqlock 重试；因此“有界”指本探针的调用次数、线程数和生命周期，不是宣称底层 API 有严格固定的墙钟耗时上限。不在 registry/task/thread/pset 锁内调用 recount，也不扩大中断关闭范围来包住它。每次记录 begin/end、异常长采样与跳过计数。

[sched.h:226](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/sched.h:226) 定义 TH_BUCKET_RUN；[priority.c:794](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/priority.c:794) 在 runnable 转换时原子增减。[sched_average.c:193](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/sched_average.c:193) 示范原子取值。只读原始 count，不机械地减在线 CPU 数后声称得到实际 runqueue 长度。

线程 state 定义在 [thread.h:473](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/thread.h:473)。现有 [thread.c:4404](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/thread.c:4404) 已采用 `thread_mtx_try → splsched → sched_lock try` 的短快照。可以复用锁协议，仅复制标量；不调用现有包含寄存器/打印的完整 helper，不读用户内存、不取栈、不解引用 wait_event。

## 最小 scope

1. **接线和预算**：复用 [kern_exec.c:3581](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:3581) 已验证的同代 lsd registry anchor 与引用生命周期，在现有 autolog 循环中调用新的只读 helper。建议最多每秒一次、启动后固定 300 秒内最多 301 次尝试；按现有 IPC 的约 10 秒 cadence 汇总，最多 31 次输出。进程重启/换代不延长整轮预算，不新增线程或定时器。
2. **系统负载先限定单核**：读取 master_processor 的 recount active/idle，并记录 cpu_id、processor_avail_count 前后值与采样区间。只有本轮保持已验证 boot-only 配置、可用 CPU 数与身份稳定为一时，才把该 processor 称为本轮全系统 CPU。发现非单核或拓扑变化则标 `unsupported-topology` 并停用该归因；不把未上线核心的零值加入平均。相关定义见 [processor.h:424](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/processor.h:424)。计数前后相同不能证明两个采样间绝无离线/上线，因此本轮也不混入 SMP/hotplug 实验。
3. **最多 16 线程**：anchor 引用在 registry try-lock 内借用，立即释放 registry 锁；task_lock_try 后只收集最多 16 个线程引用与 task generation/thread_count，达到上限立即停止，不为查漏再扫描尾部。离开 task 锁后逐个 try-lock 复制 state、active、hint、wait_result、时间戳和 runnable_timer 标量，立即释放。每次单次尝试，忙则记 skip，不自旋重试。最终逐个释放引用。现成有界收集模式见 [thread.c:4488](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/thread.c:4488)，仅复用边界与生命周期，不照搬其输出或其他对象身份。
4. **计数放锁外**：沿用第六轮 task CPU，必要的逐线程 CPU 用 recount_thread_usage 在锁外读取。全部数据带 generation、tid、begin/end、valid、truncated、lock_busy 和采样数量。出生/退出、更换线程集、generation 改变、计数倒退或超长采样时重建基线，不能补零。跨样本线程差值仅针对两端同一 tid/generation；不把不完整线程集之和当整个 lsd 的排队累计。
5. **输出控制**：每个 10 秒窗口输出系统 active/idle 增量、lsd task CPU 增量、runnable 观测分布、各状态采样次数及完整性计数；少量线程细项最多 16 行。状态次数叫“采样次数/比例”，不能乘一秒变成精确阻塞秒数。统计/打印均在锁外。第一窗口只建基线。

实现文件预计限于现有 `kern_exec.c` 的预算/接线和 `thread.c` 的私有标量快照 helper，必要时增一个小型私有声明/结构文件；**不改** recount.c、scheduler 的切换路径、UFS、显示、服务超时或频率寄存器。本报告没有实施这些改动。

## runnable timer 值得保留，但首轮不包装成精确排队计时

[thread.h:669](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/thread.h:669) 明确 runnable_timer 包含 running；[唤醒路径:887](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/sched_prim.c:887) start，[等待切离:3700](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/sched_prim.c:3700) stop，[量子到期:218](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/priority.c:218) 和 [切入路径:3248](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/sched_prim.c:3248) update。

[timer_grab:114](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/timer.h:114) 只返回 all_bits；[timer.c:92](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/timer.c:92) 中 stop 不会清空 tstamp，所以不能仅看 tstamp 非零就补尾。切入的 timer_update 还在 thread_unlock 后执行，不能泛化为“只要持有目标 sched_lock，SMP 下所有 timer 更新都被冻结”。首轮限定单核，记录原始 all_bits、stamp、state、采样时间；暂不引入改变 timer 的 update/stop，也不强制目标线程停下。

后续若要计算某个稳定线程的 `runnable增量 − CPU增量`，须另行验证可运行状态的尾段补齐、同一时点误差、运行中 CPU 未结算尾段以及线程生命周期。它是候选的“可运行但尚未记入执行的差额”，未经这些验证不能命名为精确排队时间。task 多线程该量还可以超过墙钟时间。现有 [thread_read_times:2234](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/thread.c:2234) 只是读取这些计数，不自动解决尾段对齐。

runq 可作为后续 hint，但本轮无需为它引入额外嵌套 pset 锁。[thread.h:371](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/thread.h:371) 说明它的特殊锁协议，[thread_get_runq:3792](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/thread.c:3792) 与 locked 版本有不同断言。TH_RUN 不足以拆分 running/queued；单核中观察线程运行本身还会挤走目标线程，采样到 runnable 不能单独认定调度饥饿。

## 现成定时统计为何不直接作为主答案

内核已有 sched load EWMA 和定时维护：[sched_average.c:144](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/sched_average.c:144) 定义按优先级加权的 runnable 负载，[sched_prim.c:7010](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/sched_prim.c:7010) 只在 non-clutch 路径维护全局 EWMA。当前 sched_prim 编译记录含 CONFIG_SCHED_TRADITIONAL、CONFIG_SCHED_MULTIQ、CONFIG_SCHED_TIMESHARE_CORE。EWMA 可作为背景参考，但不是 CPU 空闲百分比，也不含某个 lsd 的排队分解；下一轮直接读取已维护的原子 runnable count 即可，不新启一套统计。

runq_stats.count_sum 虽是队列深度时间积分，[sched_prim.c:7299](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/sched_prim.c:7299) 提供更新逻辑，但调用宏受 DEBUG 与 sched_stats_active 门控，见 [sched_prim.h:487](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/sched_prim.h:487)。不能读取零就认定没有排队，也不为首轮只读观测新增开启热路径统计的行为。

task power-info 的 runnable 结果也不能直接拿来替代有界采样：[task.c:6585](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/task.c:6585) 合并已退出线程累计后，遍历整个 task->threads 并逐个 thread_lock；不满足本轮“最多 16 线程、try-lock、不长时间持锁扫描”的范围。

## 下一轮能够与不能够回答的问题

| 新证据组合 | 合理解释/下一步 | 保留的边界 |
|---|---|---|
| 系统 active 接近墙钟，lsd CPU 显著较少 | 有明显非 lsd 的已记账 CPU 工作；再定位相应依赖服务或少数指定任务 | 差额包含内核任务、观测者等，不能立即归给某个进程；中断记账口径不能重复相加 |
| 系统 idle 很高；完整样本中的 lsd 线程多为稳定 TH_WAIT，IPC pending 指向固定依赖 | 优先追阻塞依赖、服务唤醒/超时 | 是多个观测吻合，不证明整个 60 秒均阻塞；线程截断/锁忙时不能说“全部线程都在等” |
| 系统 busy 高、runnable 多；同代 lsd 多次可运行且 CPU 增量很低 | 值得深入调度竞争/优先级或 runnable timer 差额验证 | 瞬时 TH_RUN 和采样比例不是精确排队时间，也不单独证明 starvation |
| lsd CPU 接近系统 active，且主要由少数 worker 消耗 | 优先查这些 worker 的实际计算/循环，再结合已验证频率来源 | 不直接推出 CPU 频率过低；busy 时间与 UFS 访问可能重叠 |

最小验收是有清晰的采样预算、引用释放、锁忙/截断标记、单核范围检查、时钟及 generation 对齐，并能用上述组合选择下一条排查路径。仅这轮采样不能把 59.763 秒精确拆成阻塞/排队/其他进程的互斥三项，也不能自动认定 Hello 首次显示时刻。
