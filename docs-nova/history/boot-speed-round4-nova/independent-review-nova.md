第四轮有界 lsd 统计：独立审阅（Nova，2026-10-29）

结论：**本轮差异未发现阻断测试的源码问题**。新增统计的锁使用、完成去重、generation 隔离、固定容量和输出上限与本轮诊断范围一致。它提供下一轮定位证据；尚不能证明设备稳定性、诊断开销大小或启动时间改善。

本报告新建，无旧版本。审阅以 `snapshot-nova/boot-speed-round4-nova/source-before-nova` 的三份源码为比较基线；只读取已完成的生产差异、测试结果和构建绑定证据，没有改动源码、重跑既有测试、构建或操作设备。CHANGELOG 由主任务统一维护。

| 已审源码 | 审阅时 SHA-256 |
|---|---|
| [mach_msg.c](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:155) | `cdc4514cb54744e32ca097d70fddc0ff3f1b09a85baa423cb4942aee15d7634e` |
| [sys_ulock.c](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/sys_ulock.c:125) | `b367556bd580eb4db4a7ee0d9a198fc860b2dd268461aae2730ceec306c8a087` |
| [kern_exec.c](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7970) | `8f71d41874836d6cffb869435cdc9273b7aa6c0e33fb9f45b97c3ec507c8e4b9` |

**IPC 聚合的并发和生命周期**

新增聚合复用既有 `meizu_ipc_lock`。进入、完成、换代与 dump 快照均在该锁内处理；完成路径没有新增分配、用户内存读取、端点查找、任务/端口锁或 printf。桶查找最多 32 项，直方图最多比较 7 个阈值，没有随启动事件数增长的查找表。

新增的 `begin_ticks` 与 `begin_mode` 在获准的外层 begin 时设置，不随后续 wait 阶段改写。嵌套 begin 不覆盖外层记录；finish 先核对 sequence 与 nested，再由统计函数核对 `pending`。首次完成在 `pending` 清零前计数，后续 DONE/EXIT 不再累加，因此既有接收完成与 trap 返回双路径不会重复统计。每次新 begin 用结构体整体初始化，`send_seen`、send id 和端点不会沿用上一次操作。

只有 `meizu_ipc_current_workers == &meizu_ipc_workers[1]` 的 lsd 记录进入新增总计，另有 task generation、首次捕获后的 300 秒窗口、generation 起点、时间前后顺序检查。Setup、SpringBoard、container 和非目标进程不会计入 lsd 聚合。generation 改变会清空本代统计与桶，却保留首次启动时间、dump 次数和频率预算；旧 generation 的迟到完成不污染新代次。

新增字段仍位于已有通用 probe 记录中：原本已被捕获的其他目标也会写入 begin 时刻、取得完成时刻，既有 worker snapshot 也会打印操作年龄。这些是很小但真实的额外观察成本，不能表述成非 lsd 路径“完全没有新增指令”。没有进入现有 probe 的普通非目标路径仍在已有筛选处退出。

dump 用 try-lock，在锁内取得统一 `sample_now` 并复制纯标量，随后解锁再转换时间和打印。新增约 2.3 KiB 的 snapshot 为静态存储，不压到内核栈；当前源码中两个 dump 各只有同一 autolog 循环中的一个调用点，静态 scratch、report 状态因此依赖这个单调用线程约定。未来若增加并发调用点，需要重新处理串行化，不能直接复用当前实现。

原有 `meizu_ipc_record` 的 LP64 布局由 256 增为 272 字节；既有 `rows[8]` 数组由 2048 增为 2176 字节，增加 128 字节。这是明确的数组布局增量，不是编译后完整栈帧或整条调用链栈上限的测量。没有新增栈回溯或递归层级。

**IPC 输出预算与解释范围**

桶上限 32，满后汇入独立 overflow，total 仍计入这些完成；worker 槽位仍为 32，满表拒绝沿用既有 `full` 计数。dump 间隔至少 10 秒，总计最多 31 组，generation 更换不续期。每组最多总览、直方图和 32 个桶，即至多 34 行、全轮至多 1054 行新增 IPC 汇总。IPC 汇总没有“内容不变则跳过”的去重；其约束是频率和总量。完成去重与输出去重应分别描述。

采样在首次 lsd 捕获后 300 秒终止；最后一组可由窗口届满后下一次符合间隔的 autolog 调用输出，不保证恰在 300 秒打印。长时间未完成、窗口后才完成、容量不足或代次切换导致未收录的操作，都不在 completed 直方图中；`pending`、`worker_full` 与 `nested` 是解读覆盖范围的必要信息。当前采样规则下，跨度达到 300 秒的完成记录不会进入该窗口，因此 `ge300s=0` 不能证明没有这种等待。

send bucket 优先使用本次已经观察到的发送端点与 send id；receive-only 可使用本次 reply linkage，否则保留 unknown。输出保留 endpoint source、stop、kind、peer generation，避免把未完整追踪的端点当成已证实的服务因果关系。

这些计时从已有 Mach trap 观察点到首次观察完成，包含阻塞、调度、拷贝和现有诊断开销，**不是端到端 RPC 响应时延**。多线程 `sum_ns` 可以重叠，不能相加当 Boot→Hello 墙钟时间；输出已有 `sum_is_concurrent=1`。待完成记录的 `op_age_ms` 从 begin 起算，`phase_age_ms` 从最近阶段起算，二者不能互换，也不能与完成累计时间相加。

**既有 backtrace 计时的并发和生命周期**

计时起点位于既有 admission 成功之后、`backtrace_user` 之前，终点位于现有 trace 行和 PC 行输出之后。计入栈回溯、cookie 计算和这些 printf 的 elapsed；不计 admission 前检查、新增统计原子发布及新增 dump 自身。因此它衡量现有 lsdbt 主体的经过时间，不是整个诊断系统的全部开销，也不是纯 CPU 时间。

原来的 16 个槽、每个 opcode 两次、全启动 32 个 trace、每次最多 16 帧限制保留。槽位不复用，generation/tid 在既有 gate 下发布；busy 从获准 trace 前保持到计时发布后，保证单槽只存在一个写入者，并保留递归抑制。没有将 admission gate 跨越用户栈读取或 printf 持有。

新增 timing_version、count、total 与 max 使用顺序一致原子操作。单写者先置奇数版本，发布所有字段，再置偶数；dump 若发现奇数或前后版本变化就放弃该轮，不等待和重试。最大仅 32 次发布，版本不会在本轮预算内回绕。dump 在原 gate 下读取不可复用槽和 admitted 计数，对当前 generation 求和，之后再次核对 generation；旧 generation 在后台完成只更新自己的槽，不进入新代次汇总。

单槽计时数据可作一致读取；多槽是不同瞬间的样本。busy 是独立的瞬时标记，完成发布与 busy 清除之间可能短暂出现 completed 已增加而 busy 仍为 1。代码没有把 busy 直接当 pending，pending 单独采用 admitted−completed，输出保留 `snapshot_nonatomic=1` 与 `elapsed_sum_not_wall=1`，口径正确。

首次捕获用 armed 的 0→1→2 发布流程保护不可重置的启动时刻；dump acquire 看到 2 后才读取该时刻。报告限于首捕获后 300 秒、至少 10 秒间隔、最多 31 条，而且只打印有 admitted 且内容变化的汇总。原有 trace 的全启动次数预算与这个新增报告时间窗口是两种不同约束；不应称“超过 300 秒后原有 backtrace 也被禁用”。

**应保留的非阻断限制**

- lsd 身份沿用成功 exec 后的完整 vnode basename 精确等于 `lsd`，源码没有在这个分支要求完整路径匹配。任务 generation 防止 PID 复用混淆，但不能把现有识别描述成完整路径白名单。
- backtrace dump 会短暂占用既有非阻塞 admission gate。恰好并发进入的 trace 可能因此跳过；这不是 syscall 等待语义变化，但意味着新增统计不是零干扰，host 的 admission 条件测试也不能证明设备上捕获次数逐次不变。
- IPC 桶扫描、记录尺寸增大、时钟读、原子操作、汇总打印和其后的日志持久化都有额外成本。固定容量与预算证明的是开销有界，不是开销为零。只能通过后续实机数据判断是否值得继续保留这些诊断。

**已核对的验证证据**

| 证据 | 核对结果与边界 |
|---|---|
| [IPC lifecycle 测试结果](<nova-root>/artifacts-nova/boot-speed-round4-nova/lsdipc-timing-tests-nova/run-20261029T094332592936Z-nova/result-nova.json) | 生产结构体、聚合及 begin/phase/finish/arm/dump 函数提取后，ASan/UBSan 的 10 组情景 PASS；覆盖完成去重、错误/未知端点、嵌套、非目标、worker/桶容量、换代与窗口、直方图、dump 频率和 pending 年龄。时钟、锁、线程与端点为 host 替身。 |
| [BT 测试结果](<nova-root>/artifacts-nova/boot-speed-round4-nova/lsdbt-timing-tests-nova/run2-nova/results-nova.json) | 12 组 PASS；包含真实 pthread 进行中快照、代次隔离、旧代次完成、原预算/递归限制、频率与生命周期。publication overlap 用奇数版本注入边界；不代表内核调度或设备真实栈成本。 |
| [内核构建记录](<nova-root>/artifacts-nova/boot-speed-round4-nova/build-result-nova.json) | exit_code=0；上列三份源码 hash 与构建开始、结束记录一致。 |
| [最终链接与打包绑定审计](<nova-root>/artifacts-nova/boot-speed-round4-nova/linked-timing-audit-nova/audit-nova.json) | 状态 PASS；11 个选定函数 raw/payload 字节一致，7 个 object/最终函数排除 relocation 后无差异，autolog 两个 dump 调用已绑定，non-kernel 打包部分保留。此为主任务生成的静态证据，本审阅读取核对结果，没有另行重跑整个绑定审计。 |

上述证据支持进入下一轮实机诊断。实机分析仍应将 Boot→苹果、苹果→Hello 分别计时，结合 pending 年龄、completed 分布、endpoint 元数据以及 lsdbt 主体 elapsed，才能判断剩余 139 秒主要花在哪里；不能提前把新增统计视作启动优化收益。
