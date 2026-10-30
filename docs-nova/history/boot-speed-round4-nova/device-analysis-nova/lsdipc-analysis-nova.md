# 第四轮 lsd IPC 等待分析 — Nova

2026-10-30。新报告，无旧版本。只读分析 2026-10-29 启动日志；不修改生产代码、不操作设备。父会话负责中央 CHANGELOG。

第四轮 Boot→Hello 仍为 **212 秒（3 分 32 秒）**，与第三轮用户计时相同。SpringBoard 的同一线程先等待 launchd 至少 **35.421 秒**，后等待 lsd 至少 **97.565 秒**。lsd 主等待内部持续完成大量短 Mach trap；当前最明确的调查对象是数据库建立/应用扫描过程中的容器查询与 launchd 查询，尚不能把约 100 秒归因于某一条卡死的 IPC。

[机器可读证据](<nova-root>/artifacts-nova/boot-speed-round4-nova/device-analysis-nova/lsdipc-analysis-nova.json)保存完整快照、区间增量、线程样本、阶段边界与第三轮比较；[解析脚本](<nova-root>/artifacts-nova/boot-speed-round4-nova/device-analysis-nova/analyze-lsdipc-nova.py)可复现。统计对象是探针观察到的 Mach trap 从 begin 到第一次 pending→false，不是已经证明配对的端到端 RPC。

## 输入及记录质量

- [原始日志](<nova-root>/logs-nova/boot-speed-round4-nova/candidate-run-nova/autolog-nova.txt)：1,453,771 字节，SHA256 `ea8a091fabf4ecfca6f97465d347b539a7c10baa7806b3cc5ab3f5470f234ab0`。
- 用户记录：2026-10-29 23:42:00 从 Boot 菜单开始，76 秒出现苹果，之后 136 秒到 Hello，合计 212 秒。该次为单轮观测，无精度超过用户计时的保证。
- 候选镜像 SHA256：`1b05a1b8f83ade84cd5348fa71216b80fb49b6868a7d738a9eaf2be068da33a9`。镜像与 A 分区匹配由父会话另行核对，本报告不重复设备审计。
- capture dropped=0，但日志含 **425 个 NUL**，全部为 L7642 的独立尾行。解析器跳过整个含 NUL 行，未删 NUL、拼接或修复字段。不能据 dropped=0 称整份日志完整无损。
- 保留下来的 22 组 `nova: lsdipc-trap` 快照均通过结构与一致性校验：声明桶数与记录匹配、索引齐全、全局完成数等于各桶加 overflow、histogram 总数一致、sum 在纳秒转换舍入容差内、max 一致。未发现孤立行或字段缺失。尾部 NUL 行无 lsdipc/nova 字面标记。
- 上述验证说明已保留探针记录内部自洽，不证明没有尚未完成、尚未采样或未捕获的操作。

## SpringBoard 的可见等待

| 对端 | SpringBoard PID/TID | seq | 首末样本行 | 最大采样等待龄 |
| --- | --- | --- | --- | --- |
| launchd，PID 1/gen 4 | 25/253 | 254 | L940–1586 | 35.421 秒 |
| lsd，PID 97/gen 271 | 25/253 | 2607 | L1751–2440 | 97.565 秒 |

两次等待为同一线程顺序发生，采样下界合计 **132.986 秒**。第三轮对应下界为 35.644 与 101.036 秒；采样相位不同，不能把相差的 3.694 秒宣称为实际优化收益。外部总计时两轮都为 212 秒。

以下时间均为 **lsd 探针启用后** 的 mach_absolute 时间，不能直接当成 Boot 菜单后的秒数。

| 阶段事件 | 日志行 | 相邻快照夹出的启用后时间窗 |
| --- | --- | --- |
| 首次 framebuffer swap，像素内容未知 | 1625 | 30.154–40.156 秒 |
| 首次采到 SpringBoard 等 lsd | 1751 | 40.156–50.205 秒 |
| 最后采到等 lsd / 第一次 `.csstore` | 2440 / 2444 | 130.396–140.400 秒 |
| 首次 Setup arm | 2564 | 140.400–150.427 秒 |

这些是日志顺序夹出的近似阶段边界。没有同一时钟域内的 Boot/Hello 首帧标记，不能从 `.csstore` 或 Setup 启动推定 Hello 已显示。

## 主要等待内部的 80.191 秒

优先分析 dump 6（L1784，启用后 50.205 秒）到 dump 14（L2365，130.396 秒）：按保留日志顺序，两端都在第一与最后一条 SpringBoard 等 lsd 的样本之间。两次快照的精确时钟差为 **80.191143541 秒**。

这段时间完成 **24,101 次 trap**，所有新增完成均小于 1 秒，包含 overflow 在内没有新增 ≥1 秒完成。

| 完成时长 | 增量次数 |
| --- | ---: |
| <1 毫秒 | 18,930 |
| 1–10 毫秒 | 4,225 |
| 10–100 毫秒 | 921 |
| 100 毫秒–1 秒 | 25 |
| 1–10 秒、10–60 秒、60–300 秒、≥300 秒 | 各 0 |

已完成时长的并发累计增量为 **38.889114636 秒**；其中 overflow 9,575 次、5.583885364 秒。不能用 80.191−38.889 得出 CPU 时间：有调用可能跨越区间边界，有未完成调用、非 IPC 阻塞、调度延迟和多线程重叠。

保留归属中最突出的桶如下。mode 1 为 send，2 为 receive，3 为 send+receive；endpoint_source 1 为本次 send 识别，2 为 receive-link 识别，0 为未识别。归属必须保留该区别。

| 桶 | 已观察对端 / 类型 | 完成次数 | 非零返回 | 并发累计秒 | 平均毫秒 |
| --- | --- | ---: | ---: | ---: | ---: |
| 24 | containermanagerd，receive-link，mode 2，send_id 0 | 3,147 | 0 | 19.482234 | 6.1907 |
| 20 | launchd，send 来源，mode 3，ID 1073742728 | 2,052 | 2,052 | 5.347533 | 2.6060 |
| 16 | 未识别 receive-only，mode 2 | 2,771 | 324 | 4.231673 | 1.5271 |
| 21 | launchd，send 来源，mode 3，ID 1073742628 | 2,052 | 0 | 4.069274 | 1.9831 |
| 23 | containermanagerd，send-only，mode 1，ID 1073741824 | 3,147 | 0 | 0.112882 | 0.0359 |

container 的 send/receive 次数相同，两个 launchd 桶次数也相同；这些是聚合层面的相等，**不证明逐次配对、请求语义或属于同一线程**。不能把它们拼成未经验证的完整 RPC。

`errors` 仅表示 trap 返回值非零。尤其 launchd ID 1073742728 的 2,052 次非零返回，当前没有具体返回码；不能称为 2,052 次服务失败或重试。它可能包含接收缓冲调整、超时、中断或其他情况，需要实际返回码区分。日志中别的进程出现的 `10004004` 不能移用到 lsd。

这个结果排除了“该内部区间有一条已完成、耗时约 100 秒的 lsd Mach trap”解释，且支持大量短调用持续执行。它不排除一直未完成的调用、用户态扫描、其他系统调用、非 IPC 锁等待或调度问题。

## 为什么不采用全程 130 秒累计作为根因

最终 dump 22（L7589，启用后 210.599 秒）共 30,897 次完成，非零返回 3,211 次，并发累计 **130.398889479 秒**，最大 **23.576044166 秒**；overflow 12,398 次、46.273605052 秒。该累计覆盖主等待之前与之后，且多线程时长可重叠，不能称“IPC 导致开机多花 130 秒”。

主等待结束附近的 dump 15 最大值仍仅 8.062460468 秒。12.560577291 秒最大值首次出现在 dump 17，23.576044166 秒首次出现在 dump 18，均在最后一条 SpringBoard 等 lsd 样本与首次 `.csstore` 之后。不能把这些后段完成时长反向归因于此前约 100 秒的主等待，也不能仅凭完成时刻断言它们全部在该等待之后开始。

所有快照 `worker_full=0`、`nested=0`。最终仍有 1 条 pending；这些探针计数未显示槽满或嵌套丢失，但不能据此覆盖未被本探针观察的活动。

## 线程与扫描上下文

保留的 lsd 栈与线程记录支持数据库建立和扫描路径：`_LSDatabaseClean`、`-[LSDatabaseBuilder createAndSeedLocalDatabase:]`、`LAUNCH_SERVICES_CLIENTS_ARE_WAITING_FOR_THE_DATABASE_TO_FINISH_SEEDING`、`MIFilesystemScanner _scanAppsDirectory`、`MILaunchServicesDatabaseGatherer scanExecutableBundle`、`container_query_execute`、`container_xpc_create_connection`。

这些名称来自当前日志 `0xe600000` dyld slide 与只读历史符号表的精确 unslid PC 匹配，作为上下文证据；本次没有重新完成二进制 UUID 匹配审计，不能以它们声称新的源码级百分比归因。

TID 444 多次处于 state 4/state-only；保留的两个明确 pending 样本分别为 L1919 的 launchd ID 1073742728（op_age 48 毫秒）和 L2148 的 ID 1073742628（op_age 0 毫秒）。没有保留到某一个 lsd worker 连续挂在同一条约 100 秒的操作上。该现象与短调用/扫描持续推进相符，但 state 数字本身不能换算 CPU 占用。

另见[栈观察耗时报告](<nova-root>/artifacts-nova/boot-speed-round4-nova/device-analysis-nova/lsdbt-analysis-nova.md)：其已测量范围总耗时约 1.82 毫秒，不解释分钟级等待；它没有测量所有日志、绘制或其他探针成本。

## 下一处应补的最小证据

当前没有足够证据安全删减数据库建立、容器查询或 launchd 查询。最窄的下一步是针对上述两个 launchd ID，在既有有界聚合里记录**实际 trap 返回码**及对应 count/sum/max；同时保留对端代次、mode、endpoint_source，避免把 receive-only 归属到历史 send。

归属容量需要调整：dump 2（启用后 10.062 秒）已占满 32/32 个桶，其中 **14 个桶**仅因不同 kernel-kind ID 分开。它们为 3405、200、206、3418、8000、3409、412、3410、4811、4817、4807、205、2877、2880。内部 80 秒中三个仍活跃 kernel 桶合计仅约 0.06148 秒，却挤占长期服务归属容量。

建议把 kernel-kind 按 kind/mode 聚合或使用独立小预算，为真实服务保留有界容量；也可添加不依赖 send_id 的固定 peer 汇总。不要仅扩大 32 桶后继续让零散内核 ID 占满。保留现有包含 overflow 的全局 histogram/max，维持无动态分配、无逐调用打印、既有启动时限与输出上限。

这次补证应回答：非零返回究竟是缓冲扩容、超时、中断还是其他结果；是否确有同一服务查询在同一阶段被大量重复；overflow 的主要服务是谁。若证实反复扩容，再评估接收缓冲策略；若证实重复 bootstrap/容器查询，再定位可复用结果的调用层和失效条件。当前不能仅凭“非零返回”跳过调用或硬编码成功。

已有证据优先降低了“单次已完成的超长 Mach trap”和“已计量 lsd 栈观察造成分钟级延迟”这两个假设。用户态扫描、未计量等待和其他观察开销仍需父会话结合 rootio 与其它计时判断。
