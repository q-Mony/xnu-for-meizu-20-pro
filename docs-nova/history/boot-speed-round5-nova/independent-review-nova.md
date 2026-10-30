# 第五轮 BootLog 状态绘制限频独立审阅 — Nova

2026-10-30。只读审阅生产源码、第五轮基线快照和第二次构建对象，未修改生产代码、未操作设备。初建报告无旧版本；源码/对象审阅稿备份：`snapshot-nova/round5-independent-review-20261030T003508-nova.md`；补齐Host验收前备份：`snapshot-nova/round5-independent-review-final-20261030T004003-nova.md`。

**最终结论：源码、第二次目标构建对象及Host行为测试验收通过，未发现尚未解决的新增阻断。19个ASan/UBSan场景通过，4个负对照均按预期拒绝。该结论不证明真机启动收益。** 该结论限定于周期状态绘制与新增计量；现有全console的SMP问题未被这次补丁解决。

## 固定审阅版本

| 对象 | SHA256 |
| --- | --- |
| `unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c` | `e5d936a54810e1d0ae4aa605ad48a7416a789d7f6f65a919607eea7a5061973f` |
| `unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c` | `27f8cdfea3e2d6a42ab5f2bcfb04d17baa48da7f0b39ec0ff0d6207595a8afdf` |
| `unixnu-main-nova/out/meizu20pro/tree/build/native-meizu-bootlog/BootLog.o` | `5f4bc2b03d02f63975e01d7d8b10af388d777ab1666d66bf9801075361689b93` |

第二次构建记录： [build-result-nova.json](<nova-root>/artifacts-nova/boot-speed-round5-nova/build-result-nova.json)，退出码0，构建前后源哈希一致。比较基线为 `snapshot-nova/boot-speed-round5-nova/source-before-nova/`。

## 已解决的审阅意见

1. **共享额度的SMP保护**：旧 `busy` 只是volatile检查与赋值，不能充当并发锁。候选使用独立 `periodic_gate` 单次weak CAS保护周期预算、绘制计量和快照；失败直接返回。没有把旧busy包装成SMP安全的全局console锁。
2. **汇总时钟回退**：初稿只检查 `now-started_at`，报告后回退到仍晚于started_at的时间会绕过10秒门限。修正版同时检查 `now-reported_at`，异常后结束该次boot的报告，不重置31条预算。
3. **最终报告间隔**：初稿final绕过10秒门限。修正版final也必须满足10秒间隔；因此300秒之后的第一条合格heartbeat才是最终报告，可以晚于300秒。
4. **计时边界**：CNTVCT内联汇编加 `memory` compiler clobber；通过准入检查、置busy之后重新读取起点，结束点紧随 `blog_status()`。频率读取和主要准入检查不再混入draw_ticks。

## IRQ、FIQ、SMP与采样

- 与基线逐函数比较确认：`native_blog_irq()` 从入口到原 `busy || (n & 511)` 门限之前的计数、EL0/EL1分类、init采样和CPU内核PC采样文本完全相同；FIQ入口及原 `(n & 63)` 筛选保留。新增时间门限在这些操作之后，且只在原候选绘制事件上读取时钟。
- 两种中断入口都转到同一个 `blog_periodic_status()`。首个有效候选立即绘制；之后以**上次绘制结束到下次准入至少1秒**为界，IRQ和FIQ没有各自的额度。实际刷新可能低于1Hz，原事件门限、busy、gate竞争、无有效映射都可能跳过。
- 持有periodic gate时不打印、不分配、不睡眠、不获取阻塞锁。FIQ嵌套或其他CPU遇到被占用gate会立即返回，不能等待被中断的绘制者。
- ARM64对象检查：周期入口 `0x1d1c LDAXR → 0x1d30 STXR`，失败跳到返回路径 `0x1d54`；汇总 `0x1b18 LDAXR → 0x1b38 STXR`，失败跳到返回路径 `0x1a88`。没有回跳到LDAXR重试。目标对象未引用外部atomic/sync helper。
- `native_blog_kv_ready`、`native_blog_stage`、`native_blog_putc` 与 `blog_status` 函数文本均未改；首次KV状态和非周期检查点、字符输出不受新1秒门限影响。

**保留限制**：旧busy、irq_count及部分采样计数并非完整SMP原子协议；其他绘制入口也不参与periodic gate，因此不能宣称修复了所有并发绘制、确保所有旧计数在SMP下精确，或把新增gate测试当成整个console并发正确性的证明。独立gate保证的是周期绘制之间的预算和新增计量一致性。

## 时钟、统计与汇总

- 计时为CNTVCT，频率为CNTFRQ；频率0或超过u32拒绝。unsigned差值允许实际u64回绕；反向差值大于INT64_MAX或频率改变时，周期路径记clock_errors、重设参考并跳过本次。汇总路径遇到自身频率改变或逆向时间就停止本boot的报告。
- 计量是 `blog_status()` CPU执行区间的墙钟delta，可能包含被其他执行流打断的时间；它不等于专属CPU时间或面板最终呈现完成。没有为测量额外添加DSB/cache clean来改变原绘制行为。
- `draws`只在调用完成后增加；有效delta累计到sum并更新max，异常delta不加入sum/max而记录clock_errors。解释平均耗时或跨报告换算时，应要求clock_errors为0且hz一致。新增统计不记录因busy/gate竞争跳过的次数；throttled只代表时间门限拒绝。
- 快照用同一非等待gate，读完标量后先释放再printf，sum/max/draws不会读到周期更新中间态。`scrolls`单独做原子读取，它是实际进入原滚屏分支的计数，不是持久日志行数，也不是滚屏耗时；快照后的本次printf影响只能在后续样本中看到。
- 新dump只在 `kern_exec.c` 的现有autolog循环一处调用，内核变更仅声明和调用这两行。报告从第一次有效dump调用起算；最多31条，相隔至少10秒，300秒后输出一条合格final并结束。它不是从Boot菜单起算，也不在300秒关闭周期显示。
- 对象中dump的release store位于 `0x1b70`，printf调用在 `0x1bc0`；绘制前后CNTVCT读在 `0x1e50` 和 `0x1e5c`，中间 `0x1e54` 调用 `blog_status()`。这些地址只对应上面的固定BootLog.o哈希。

## 行为测试与设备验证状态

已核对测试执行器、受测源码抽取、关键用例与结果文件： [results-nova.json](<nova-root>/artifacts-nova/boot-speed-round5-nova/status-refresh-tests-nova/run-20261029T163645236617Z-nova/results-nova.json)。结果文件SHA256为 `652e518c3ab92dd5c689de435b1548afaf1c968d3e43fa29c36193b1aa8123fb`；source/kern_exec哈希与本报告固定版本一致，运行期间源未变，19份正例日志均存在。

测试使用真实生产函数与声明，时钟、framebuffer绘制、硬件中断现场为明确替身；受测节流器和汇总函数未重写。ASan/UBSan、编译告警检查通过；未运行TSan和LeakSanitizer。

| 测试组 | 已通过覆盖 |
| --- | --- |
| 周期预算与计量 | 首绘、从绘制结束起的一秒边界、绘制独立计时、inactive/busy/映射不可用不消费额度 |
| 计时异常 | u64实际回绕、0/超范围频率、频率改变、准入前/绘制中时钟倒退 |
| 入口与并发 | 原IRQ/FIQ计数与现场采样、交替来源共享预算、递归try-enter、pthread持有绘制gate时另一绘制者和汇总立即跳过、完成后读一致快照 |
| 汇总与回归 | 内容与10秒间隔、31条上限、300秒final、回绕/无效频率/频率变化、开始时间及上次报告后的逆向、gate失败不消费预算 |
| 滚屏计数 | 提取的真实 `blog_newline()` 触发实际滚动分支后增加scrolls，非滚动不增加 |

特别核对两个审阅回归：`0→10→9→20秒` 仅输出前两条；`0→295→300→305秒` 在300秒不输出，305秒输出final。四个负对照为：删除生产周期函数导致编译失败；改为绘制开始起算额度、恢复提前final、删除相对上次报告逆向检查均触发断言失败。证明这些验收能拒绝对应回归，不是只检查测试自身固定结果。

Host的“并发通过”仅覆盖新增periodic gate/快照和受控调度，不替代真机IRQ/FIQ嵌套、硬件时钟、cache clean、面板呈现验证，也不覆盖旧busy全console同步。完整镜像封装及最终链接审计由父任务与另一个审阅任务负责，本报告未独立重做这些检查。

Host验收已通过；是否缩短Boot→Hello、实际绘制次数与累计成本仍须第五轮真机日志和同一计时口径确认。不能将新版本的低绘制累计时间直接当作旧版本成本或已经节省的启动时间。
