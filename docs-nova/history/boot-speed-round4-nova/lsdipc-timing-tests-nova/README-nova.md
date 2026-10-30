# Round 4 lsd IPC trap 耗时测试

本目录文件均为新文件，无旧版本。生产代码只修改 Nova XNU 的 `osfmk/ipc/mach_msg.c`；修改前文件由主任务保存在 `snapshot-nova/boot-speed-round4-nova/source-before-nova/` 下原相对路径，SHA-256 为 `e95a2a01218352bd59befcae8ee956de3e8aa8583cda5ecaf750b28d9279ae09`。本子任务没有构建内核、操作设备、修改其他生产文件或中央 CHANGELOG。

通过测试的生产 SHA-256：`cdc4514cb54744e32ca097d70fddc0ff3f1b09a85baa423cb4942aee15d7634e`。

运行结果位于 `run-20261029T094332592936Z-nova/result-nova.json`：**PASS，10 组实际生产生命周期场景，ASan + UBSan**。统计结构为 2296 字节；dump 使用静态快照，不把整个结构放在内核栈上。

`run-tests-nova.py` 从当前真实 `mach_msg.c` 提取结构、新统计 helper、dump，以及 17 个原有生命周期函数；测试没有另写一份统计实现。主机仅替换时钟、锁、当前线程/任务、endpoint 采样及 printf。用 100 ns/tick 的非恒等时基验证单位转换；printf stub 断言打印时没有持有 probe 锁。

测试覆盖：

- 完整 begin→send→sent→link→wait→receive→DONE，以及后续 EXIT/重复 DONE 的一次计数；begin tick/mode 不随 phase 更新。
- receive-only 对未知 endpoint 和新 link 的处理；前一个 trap 的 send ID、peer 不泄漏到下一次。
- nested begin/finish 不覆盖外层，不记独立完成；外层首次 pending→false 才汇总。
- SB、Setup、container 和未被观察的任务不进入 lsd timing 统计。
- 32 个 pending worker 槽耗尽后拒绝第 33 个，原槽不被覆盖。
- 32 个 key 桶用满后的明确 overflow 计数/耗时/错误；已存在 key 继续正常聚合，全局 count/sum 与分桶加 overflow 相等。
- generation 变化清空统计，旧 sequence/旧 generation 完成不进入新统计；首次 300 秒总预算和 dump 次数不续期。预算到期后原有观察仍可重新 arm，新 timing 拒绝新 generation。
- 固定时长直方图边界、错误结果计数、sum/max；31 次 dump 上限、10 秒节流、到期最后快照、忙锁跳过。
- pending 行的 op_age 与 phase_age 使用同一个采样时刻，分别来自 immutable begin_ticks 与 phase when。

输出语义：`nova: lsdipc-trap` 的 completed/sum/max 是从 trap begin 到第一次 pending→false 的观察耗时，包含 send/receive/copyout 等路径，不是已证明的端到端 RPC 时延。多线程 sum 可以超过墙钟时间。`ticks`/`absolute_ns` 的时基是 `mach_absolute_time`，不是 Boot 菜单起点。

key 的 `mode` 为 begin 时发送/接收位：0=两者均无、1=send、2=receive、3=send+receive。`endpoint_source` 为 0=未观察到、1=本 trap 实际观察的 send endpoint、2=本次 receive linkage；有 send 时优先保留直接 send 目标。`stop` 和 `kind` 保留现有端点观察结果，未知/未追踪完成的端点不能被解释为完整目标证明。

全局 8 档直方图区间为 `[0,1ms)`、`[1,10ms)`、`[10,100ms)`、`[100ms,1s)`、`[1,10s)`、`[10,60s)`、`[60,300s)`、`[300s,+∞)`。只接收预算内完成，所以最后一档在正常单调时钟下应为零。`overflow` 是容量之外的新 key 的完成次数，其 count/sum/max/error 仍包含在全局总量中；`worker_full` 则表示原有 pending 槽不足导致未记录的探针尝试，二者含义不同。

首次 lsd arm 固定 300 秒新统计预算。窗口内换 generation 会重置当前代统计，但不会重置起点或 31 次输出额度。到期后最多在首个满足 10 秒节流的心跳打印最后一次冻结快照；之后不再输出。dump 由主任务接入单个 autolog 调用方，静态快照依赖这一单调用方约束。

复现：从 Nova 工作目录执行 `python3 -B artifacts-nova/boot-speed-round4-nova/lsdipc-timing-tests-nova/run-tests-nova.py`，每次创建独立的带时间戳目录，保存抽取源码、编译日志、测试日志和结果 manifest。主机测试不替代真正的内核构建或实机并发/性能验证。
