# R8 A3 捕获独立设计审核

新文件，无旧版本。只读审核，不修改 NativeRamBlock/NativeUfs/BootLog/XNU 生产代码；未构建、刷机、重启或实现恢复。A3 目标是取得 mode0 运行后的 Data 块内容，离线检查体积、文件系统与 LaunchServices 缓存。

**可以推进 capture-only；不能将导出成功解释为 APFS 干净一致、Hello 已显示或可直接 restore。** 建议先实现下述锁、触发和存储边界，再做有限主机验证。当前库没有现成的 clean-unmount 后 RamBlock export API。

## 1. 锁顺序与一致性边界

现有路径：`NativeRamBlock.cpp:250` → `IOLockLock(gate)` → direct/overlay store → `native_ufs_store_write` → `hostWrite4k` → `gHost.lock`。UFS 根盘读取另为 windowLock→gHost.lock；`RootCommands.h:293–317` 的写入/flush 都在函数内获取、释放 gHost.lock。所读实现中未见 hostWrite/Sync 内反向调用 RamBlock。A3 可遵循 **RamBlock gate → UFS transport lock**。

必须满足：

- 调用 capture 前已释放 `meizu_setup_lock`、日志 ring lock、`meizu_dump_busy` 所保护的日志工作区，以及任何 VFS/mount/task/IPC/IORegistry 锁。不要从 NativeUfs completion、console printf、panic/interrupt 上下文触发。
- 对象查找、身份验证与 `retain` 在锁住 gate 之前完成；整个捕获使用同一个被保留的 NativeRamBlock 实例，末尾释放。不能从 `selectVolumeSeed()` 返回后保留未持有引用的裸指针。该函数当前会走 registry 且调用 attachStore，不适合作为 gate 内重入入口。
- 128KiB stage、头页、hash context等内存准备在 gate 之前完成；避免持 gate 时进行可能需要 Data 页回收/文件 I/O 的分配、registry查询、VFS sync或路径解析。
- **计数/布局计算与流式读取需要同一次 gate 临界区。** 若两遍之间或逐块释放 gate，Data 写入可改变非零页数、run布局和内容，不能称同一时点的块快照。
- gate 保证到达驱动的写入停在边界，但不能保证所有用户缓冲、VM脏页或 APFS事务已提交。准确描述是 `live-block-snapshot`，没有 `clean` 标志。自身“invalid→payload→commit”的持久化协议只保证导出容器完整，不保证内部 APFS/SQLite 的应用一致性。
- mode0 要求 `native_ufs_store_mode()==0`，并确认 `direct==false`、overlay未启用；isBig>2GiB路径本身没有完整 volatile write语义，应拒绝未明确支持的形态。不能为了捕获把 mode0 偷换成 mode2。
- 本轮只捕获一次；每个UFS错误/容量不足/时限超出都释放 gate与引用，保留无效头，不继续commit。记录真实持gate时长和payload字节数；导出会阻塞Data I/O，必须延迟到服务启动之后。

## 2. 可复用的UFS API与GPU冲突

可复用入口在 `NativeUfs.cpp`：

| API | 当前用途与限制 |
|---|---|
| `native_ufs_export_blocks()` L1194 | log window已经打开时返回65536个4KiB块，即256MiB；否则0 |
| `native_ufs_export_write(rel, blocks, src)` L1196 | 只写log下方固定export区域；内部按kDataMax拆分，不写root/store |
| `native_ufs_log_sync()` L1210 | 同一LUN的SYNCHRONIZE CACHE，可用于export提交顺序 |
| `native_ufs_log_open()` L1151 | 开一次，第二次会报already open；应让既有autolog先打开，不重复抢开 |

现有export区域没有generation所有权/整段事务锁。`gHost.lock`仅串行单条命令，**不能防止另一个导出者在两条命令之间覆盖同一offset**。GPU路径 `NativeAdrenoPublish.cpp:458–471` 的方法0x7100调用weak `boot_log_export_put`；`NativeMetal/CacheExport.h`规定它占用同一256MiB export区域的一半作为blob上限。

本地Meizu实际 `unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c` 未定义 `boot_log_export_put`，当前 `libNativeMeizuBootLog.a` 的global symbols也无此符号。仍应在最终链接内核确认，而不是依赖源码搜索断言GPU writer绝不存在。若链接入其他BootLog实现，capture bootarg要在共同入口做独占分流：整个boot的A3 export与GPU export互斥，旧GPU入口拒绝，A3使用独立受控入口；不能仅让A3函数自己设busy而GPU入口不检查。

日志区和export区地理上分离，日志继续写不破坏A3 payload；同时的UFS flush可能等待日志写，但不改变区域所有权。A3禁止直接裸LBA写、改reserve、扩容write window、触碰mode2 store或签名root分区。

提交协议建议保持父任务已选顺序：无效头写入并flush成功 → 完整payload与hash写入并flush成功 → 绑定cold seed digest、布局、逻辑大小、payload大小/hash的新commit头写入并flush成功。每一步失败都停止。最后commit或flush失败可能留下物理可见但未确认的头，提取端必须完整重算长度/hash且只作分析，不把日志的成功标志当唯一可信依据。

DBRDISK1 自有约束见 `SparseDisk.h:14`：packed总大小≤256MiB、run count≤65536、4KiB对齐、无重叠、严格消费全部字节。另有外层capture头/对齐空间，需要从export容量扣除。null页隐式为零；非null但全零的页也可省略，但计数和输出必须使用同一稳定内容。

## 3. 触发API点

现成准确锚点是 `kern_exec.c:3690 meizu_setup_capture()`：只接受已提交可执行路径 `/Applications/Setup.app/Setup`，保存 generation 与 `mach_absolute_time`。**这是成功exec，仍不证明Hello已绘制。**

建议 bootarg默认关闭，仅当A3 bootarg明确启用且mode0时：记录独立的Setup成功exec时间/代际；由既有 `meizu_autolog_thread()` 检查 `now-started >= 90s`，满足后一次性触发。新状态使用boot-wide固定槽/标量，不让timer创建另一个周期线程。

现有Setup观测器在20秒后释放 `meizu_setup_thread`（`kern_exec.c:3921–3927`）。因此90秒判定不能写成“thread非空才触发”；也不能复用20秒的observer活跃位。可以在Setup确切捕获处发布独立时间/代际标记，或在短暂持Setup锁时复制已有非零时间和有效attempt，再释放锁。若选择最新generation重新计90秒，需明确记录该策略，避免首次尝试和后继exec混用。

无可靠Hello marker时，命名和日志写成“Setup成功exec后90秒捕获”，不要打印“Hello已确认”。若要严格保证人工Boot→Hello计时不受影响，则需等待用户确认Hello后的另一个可控触发；固定90秒只是保守延迟，无法证明每次都晚于画面显示。

最简单的调用位置是 `meizu_autolog_thread()` 的 `meizu_autolog_dump()` 返回之后（现L8163）：此时日志函数已在L7875清除busy。先短暂复制触发状态并解锁，再调用捕获。capture返回后打印结果，再调用现有dump或让下一轮写入。**不要将capture插入meizu_log_snapshot的锁内、meizu_autolog_dump持busy区、Setup capture持锁区。** 若最终改用其他BootLog worker，也应在它释放自有log/export锁后调用。

仅在确认export_blocks非零后尝试，避免重新打开已打开窗口。一次成功或明确失败后retire；准备未好可有限重试，记录attempt上限和deadline，不每50ms反复扫描Data。捕获状态应包含Setup gen/start、capture begin/end、mode、seed digest、logical/resident/nonzero/packed bytes、结果阶段和准确错误，方便区分启动问题与捕获开销。

## 4. 关闭/冻结路线的短结论

停止此路线研究，A3不采用全用户进程freeze。`task.c:3982` 明确task hold等待线程停止执行**用户代码**，不意味着文件系统所有kernel worker、脏页和挂起I/O已经排空；逐个冻结进程还不能原子阻止新进程创建。它不提供干净磁盘快照协议，不应为了capture新增全局调度/暂停操作。

已有正常入口是 `reboot_kernel()`（`kern_shutdown.c:177`），依次停止进程、sync、两次vfs_unmountall、buffer cleanup，再host_reboot。然而 `vfs_subr.c:4265` 调用 `dounmount(...MNT_FORCE...)`，`vfs_unmountall()`无状态返回、失败可打印后继续；“vfs_unmountall end”不等于clean成功。`dounmount`仅在非force路径调用并检查 `VFS_SYNC(...MNT_WAIT...)`（`vfs_syscalls.c:2845`）。所以当前没有可以直接借用且证明干净卸载的hook。

NativeRamBlock的pages在free才释放（L349）；stop只令online=false（L343），理论上可通过保留实例等将来在确切成功非强制卸载后导出，但需独立设计卸载结果、对象生命周期、API上下文和掉电路径，超出本轮A3。不得在PEHaltRestart/panic或中断关闭后临时做普通UFS I/O。

## 5. 本轮审核禁止项

- 不自动restore捕获结果，不替换cold seed，不改变Data persistence模式或ephemeral身份。
- 不以task_suspend、关闭调度、全局spinlock或关中断代替磁盘一致性协议。
- 不以`sync()`/RamBlock `doSynchronizeCache()`返回成功证明APFS clean；当前mode0的后者本身只是online检查后返回。
- 不对未通过容量/布局/hash校验的capture执行可写挂载或文件系统修复；离线检查保持原始捕获副本。
- 不因capture只写保留区域就跳过镜像哈希、实际分区与设备当前状态核对；设备步骤仍由父任务统一执行。
