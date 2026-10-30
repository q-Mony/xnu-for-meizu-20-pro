# R7 kern_exec 接线与 timestamp parser 独立审阅

新文件，无旧版本。本次只读审阅生产源码；仅新增本目录证据。未重复完整构建、既有测试或设备操作。中央 CHANGELOG 由主代理统一登记。

## 结论

累计计量、限频、busy 门、阶段边界及离线解析没有发现新增语义阻断。**旧候选仍禁止刷入**：主代理真实二进制审核发现 clockcal 的 `isb; mrs` 只保留 ISB，两个 CNTVCT 读取均未生成。源码已改为换行分隔，但这个源修复必须由新的对象、链接内核及打包镜像审核闭环；本报告本身不确认新包可刷。

明确限制：syntax-only 成功与 host 计量测试成功均不能证明 ARM 寄存器读取存在。旧候选的校准数据不可使用；不要把“语义未见新阻断”写成“旧包通过”。

## 来源和检查记录

- 基线：`snapshot-nova/boot-speed-round7-nova/integration-before-nova/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c`，SHA256 `2f1fac81bf4925751da61d6f56262956c883fea5a94a4cae618769a0ee6ee14c`。
- 语义审阅及 syntax-only 对象：`kern_exec.c` SHA256 `6f4e4116fedaf47d0406cd30594f93b9eb3bc5e98ca293533ea125dcbb01203e`。真实 `kern_exec.o.json` 参数移除生成对象/依赖参数并执行 `-fsyntax-only`，退出 0，前后源哈希一致；记录见同目录 `kern-exec-syntax-{command,result}-nova.json` 和日志。
- 日志共有 59 条编译器 warnings，另有 1 条 driver 缺少 libsa include 目录 warning。把位置与基线 diff 比较，没有 warning 落在新增/修改的行；这不是对旧基线重新编译后的 warning 差分。逐项记录见 `warning-and-guards-review-nova.json`。
- 当前修正的源 SHA256 `6f8bc50d4738c334aeebed0d8e89305a8731ad91f2dac78b32d701f28d5eff9b`。两条 asm 已为 `isb\n\tmrs`，没有改变计量算法。本报告未对其重复完整构建。
- 已读真实提取 autolog 9 项 ASan/UBSan 测试及结果：`../autolog-tests-nova/run-20261029T190658453522Z-nova/result-nova.json`；对应旧源码哈希为 `6f4e4116...01203e`，提取哈希 `f91b0efa498158ed309db2bf7a6beb842f2cfe4f610c747e2421ba6701046cfa`。测试排除了 ARM counter 指令，不能覆盖本次二进制问题。
- 真实参数包含 DEVELOPMENT、ARM64、`XNU_OWNER_CACHE_MOUNT=1`、`XNU_OWNER_DATA_MOUNT=1`、`XNU_BOOT_PROGRESS=0`，helper extern 与调用位于相应 guard 内，没有重新开启 boot_progress。

## clockcal 阻断证据与解除条件

已核对主代理产物 `../linked-diagnostics-audit-nova/rejected-clockcal-audit-nova.json` 和 `../rejected-clockcal-candidate-nova.json`。旧 raw kernel SHA256 为 `21861405c37c149f1aca3e38d7c7e33645e1f40fc066384a7c738882e7cd1036`，旧 boot SHA256 为 `efe30729a5e7ef70cf26412d07ff7c51888f019b15dc7cdc1fbe38d5f114e044`。被审函数在对象与 raw 中 CNTVCT MRS 数为 0，ISB 为 2，CNTFRQ MRS 为 1；非 relocation 字节相符，故是实际包问题。serial 时间戳不是该问题来源。

主代理需要对修正后的实际二进制确认两个 CNTVCT MRS 均存在、它们包围 `mach_absolute_time()`、频率读取存在，并让最终 boot 内核与被审核内核一致。旧包继续保留 DO_NOT_FLASH 标记。

## 计量和锁范围

所有新增累计计数由原有 `meizu_dump_busy` 原子门保护；门竞争/递归 flush 直接返回。新 printf 在 `native_ufs_log_write` 返回后，不持有 NativeUfs transport 锁；`native_ufs_log_read_stats` 只取软件累计统计和架构时间，不进入设备 I/O。

| 字段 | 实际覆盖范围 |
| --- | --- |
| attempts | 真正进入 snapshot 尝试路径，包括 log-open/容量失败；不包括 busy、1 秒 throttle、unchanged noop |
| writes | 每次真正调用 `native_ufs_log_write`，含失败 |
| ok / bytes | 完整成功写入次数及成功的 blocks × 4096 字节；不统计失败调用可能已写入的部分 |
| copy_ns | 仅 ring snapshot，未含 boot-video 前缀格式化、header 和 padding |
| write_ns | `native_ufs_log_write` 整体经过时间，成功失败都计入 |
| attempt_ns | 获得 busy 后至 attempt end，包括 open、格式化、copy、header、padding 和 write；不包括随后本次 autologio/clockcal/heartbeat 的输出 |
| max_write_ns | 单次 write 最大经过时间 |

copy/write 是 attempt 的内部部分；这些值不能相加形成启动预算，也不是纯 CPU 时间。elapsed 倒退或累计溢出会增加 clock_errors，拒绝该次加值。原有 throttle 时间相减的倒退行为没有在此轮改变。

报告仅在真实尝试后生成，至多每 10 秒一次、31 次、从首 attempt 起 300 秒内；计数之后仍继续累计。报告输出会进入 ring 并使后续 snapshot 变脏，这是有界的诊断开销。所谓“unchanged noop”只表示不新增 snapshot、write 和本轮计量报告；旧 heartbeat/rootio 路径仍可能输出并使 ring 变化，不能宣传整函数无变化零输出。

clockcal 是 autologio printf 之后独立采样的 `CNTVCT before → Mach → CNTVCT after` bracket。它给局部偏移范围，不是两个时钟原点相同的证明，也不是 autologio 的同一时刻；更不能把 NativeUfs 的 CNTPCT 直接混减。宽 bracket 只能保留为宽的不确定范围。

## 捕获点和阶段边界

`exec-map` 在 successful-exec 捕获函数入口保存 Mach tick，并在后续工作后输出该保存值；它不是首次用户指令、服务 ready 或 Hello。Setup `armed` 在既有 setup mutex 内保存 capture 时刻，解锁后转换/输出；`status=boot-cap-reached` 的 capture ticks/ns 为 0，必须当未知。记录有既有限额，缺少记录不证明没有发生 exec。

`systemos_prepare` 包围完整 `native_owner_cache_prepare`，`data_prepare` 包围完整 `native_owner_data_prepare`；名称准确反映 prepare，不应标成纯 mount 耗时。Data end 在 `meizu_keystore_notification_publish` 之前，所以不含 AKS notify。phase begin 在打印前取时，begin 日志成本属于该阶段经过时间。

## 冻结离线 parser 复核

只读审阅 `../timestamp-analysis-nova/analyze-timestamps-nova.py`，SHA256 `1123a3db4dc93320018d5d58b69a895894f72e3d98b57e09848b33825a4e4408`；既有测试源码 SHA256 `99d9b76f44aa900cf6a36972b4abf7f4e14660397b4c6358f936fa32be6b1d8f`。已读 26 项测试源码和 `tests-20261029T191337565957Z-nova` 的通过记录，未重复执行。

- 只移除固定宽度小写 hex 的完整 P/L/R token；按 bytes 保存原件、派生正文与 source-map，内联 token 亦可回指，LF 行号不被裸 CR 改写。
- 坏/未知 token 保留原字节并阻止 gap 桥接；零 Hz、无 payload、截尾不参加时间计算；tick 倒退或频率改变分段，不排序遮掩倒退。P/L/R 不承诺整条消息原子或 snapshot 原子。
- gap 只表示相邻有效标签的差值，字段明确不归因前一函数、不证明全程无日志输出，也不把未加标签字节继承最近时间。
- phase 只按白名单、同 id/name、完整 begin/end 的内部 Mach ns 配对。重复、缺边、错误顺序、交叉嵌套、内部计数倒退、损坏记录中断均不产生有效阶段时长。嵌套阶段独立报告，不能视为可相加总量。
- Setup cap-reached/0 捕获不生成 point。exec/Setup point 均明确不等于服务 ready/Hello；Boot、苹果、Hello 的外部视觉里程碑仍为 null。
- autologio 使用严格字段，保留重叠累计值，不相加；时钟错误、计数倒退或损坏记录阻止差值推导。clockcal 只留合法局部 bracket 与 offset 区间，不做全局对齐、不映射 CNTPCT。
- legacy parser 绑定 SHA256 `bb0f3137325c86a995e790d09c16e386bc340ff80fe4c0db7ceb5ab712425297`，仅消费派生正文，附原件哈希与映射；前缀计时没有混入旧统计。输出目录必须是工具目录内的新目录，原件写为独立副本，输入二次读取比对。

parser 没有发现阻断性误归因。该结论仅覆盖协议/静态语义和已有离线测试证据，不是 R7 真机运行结果，也不能自动定位实际耗时最大阶段；需要新的有效 R7 日志。
