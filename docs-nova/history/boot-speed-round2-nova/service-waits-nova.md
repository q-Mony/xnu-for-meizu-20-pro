# Nova Boot→Hello 服务等待与诊断开销复核

日期：2026-10-29。此文件为新文件，无旧文件。仅离线读取 Nova 源码、现有日志和历史符号映射；未修改生产代码，未操作设备。CHANGELOG 由主任务统一记录。

## 输入与度量边界

- 日志：[candidate-run2 autolog](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt)。SHA256：`da158d93768f3199bc2be69e7282544081a94d608545418fbbcc3f09b95bfa85`。
- 复核工具：[analyze-boot-nova.py](<nova-root>/artifacts-nova/boot-latency-nova/analyze-boot-nova.py)，以 `python3 -B` 离线执行。
- 用户记录同一候选两轮 Boot→Hello 分别为 237 s、190 s；两次都从 Boot 菜单开始。日志缺少 Boot 操作和 Hello 首帧的同一时基标记，暂不能把 47 s 差值归因于某一个改动。
- `age_ms` 是被采样的某次等待年龄；heartbeat `tick` 是 dump 调用计数，不能用 `tick × 0.05` 当实测墙钟时间；E1 是 EL1 IRQ 计数，也不是时间或存储读取进度。

## 已证实的服务依赖

同一个 SpringBoard PID 26 / TID 253，先有 seq 260 指向 launchd 的等待，最大已观测年龄 35,481 ms（日志 L972、L1206、L1360、L1466），后有 seq 1980 指向 lsd 的等待，最大已观测年龄 100,796 ms（L1628–L1989）。这两次串行等待的已观测下界为 **136,277 ms**。它不是另加在总启动时间之外的预算，也不能把每一层下游等待再累加一遍。

| 证据位置 | 观察 | 能支持的结论 |
| --- | --- | --- |
| L997–L1013 | lsd 主线程 TID 437 的地址匹配 `_LSPreferences`、`_LSDatabaseClean`、`LSDatabaseBuilder createAndSeedLocalDatabase:` 和 first-boot container cleanup | lsd 正在进行 LaunchServices 数据库初始化路径 |
| L1081–L1097 | TID 569 的地址匹配 `LAUNCH_SERVICES_CLIENTS_ARE_WAITING_FOR_THE_DATABASE_TO_FINISH_SEEDING` / `LSDatabaseBlockingFetchClient` | 存在明确等数据库 seeding 完成的消费者；TID 441 的等待也关联同一 group/event |
| L1375–L1391、L1660–L1676 | TID 439 的地址匹配 `MIContainer`、`MILaunchServicesDatabaseGatherer`、`MIFilesystemScanner _scanAppsDirectory:` | seeding 的实际工作涉及应用目录和容器扫描 |
| L1529 | TID 439，seq 2045，send ID 1073742728，peer launchd PID 1 / generation 4 | 一个下游 launchd 请求 |
| L1638 | 同一 TID 439，seq 5184，peer containermanagerd PID 63 / generation 229 | 下游依赖会变化 |
| L1751 | 同一 TID 439，state=4，未观察到 pending IPC 行 | 该工作线程也有运行样本 |
| L1847 | 同一 TID 439，seq 22184，send ID 1073742728，又指向 launchd | 不是同一条下游 IPC 连续卡了 100 s |
| L2018 | `.csstore` mmap，随后 SpringBoard 的请求序列前进 | 与数据库构建后续进展相符，但缺少精确完成标记 |

上述符号名来自只读复用的 [lsdbt-144210-symbols.json]([local-home]/...)：将本轮 PC 减 `0x4bd60000` 后与已有 `unslid` **精确地址**匹配；当前 slide 与 dispatch 精确地址及 `nf` 一致。没有重新核验本轮实际 dyld cache UUID，因此这些是上下文一致的精确地址匹配，不是独立认证的完整符号化。未采用不可信的 nearest system stub 名字。

还不能下的结论：

- seq 260 的 send ID 是 6001009，但具体业务操作尚未确定；不能仅凭 peer=launchd 就修改其服务启动策略。
- seq 1980 是 receive-only，`send_id=0`；之前发送记录已被小 ring 覆盖，不能把另一个历史请求伪装成它的前置发送。
- lsd 接收队列多次 `qcount=0`，但 cookie/epoch 改变、`continuity=unknown`，不能证明 SpringBoard 那一条消息已经出队。
- launchd receiver 在早期匹配，L1292 后 registry 映射不可用；PID 1 的系统调用计数仍增加，常见 syscall 368 是 `workq_kernreturn`。这不能推出 launchd 全部线程冻结，也不能证明服务工人充分运行。
- 当前 `parallelism=1` 的证据属于 Setup，不能外推到 lsd 或 launchd。

## 后续最小服务探针

目前可按以下三个假设顺序验证。它们可以同时成立，现有证据不足以直接修改业务行为。

1. **数据库/容器初始化反复进行，是后段主要工作量。** [board.toml:283](<nova-root>/unixnu-meizu20pro-nova/board.toml:283) 使用 `packed_data_ramdisk=true`；[候选封装脚本:125](<nova-root>/artifacts-nova/boot-optimization-nova/packaging-nova/package-candidate-nova.py:125) 读取 `scratch-data.img`，检查 `DBRDISK1` 和逻辑 512 MiB。kernel-only 替换保留同一个 Data seed。以后可单独试验一致、完整初始化后的 Data 快照是否减少 seeding，但目前既没有实现该试验，也不能保证快照一致性或认定它是完整修复。
2. **seeding 过程中多次 launchd/container RPC 或工人调度放大延迟。** [mach_msg.c:1486](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:1486) 已保留每目标最多 32 个线程的 pending 状态，但输出遗漏已有 `when`。未来最小补充为 pending `age_ms`，以及按 peer/message ID 聚合的已完成请求 count/sum/max duration；据此区分多次短请求和个别长停顿。不要逐 IPC 打印。
3. **特定 launchd 请求排在其他工作后面。** [kern_exec.c:7834](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7834) 只对 name=lsd 做 receiver worker 轮次采样。registry 只有 128 槽、256 次捕获上限，L3923 的替换可逐出 PID 1。未来保留一个有界生命周期的 launchd 引用，在 SB 等待超过门槛时采一轮线程/队列/workqueue admission 状态。另保留 receive-only 的前置 send 历史行（ID、端点、时间），明确标成历史相关信息，不能充当当前 ownership。

不要据此禁用系统服务、跳过 Setup 或直接缩短超时。当前主任务的第一候选只做 RootWindowRead 批读和 root 路径聚合计时；本报告没有把以上探针混进去。

## 第四个假设：诊断本身放大启动时间

结论：**源码不能排除，且已找到同步竞争路径；日志数量本身不能证明其为主因。** 需要测量实际时间。下面区分已确认的机制和仍缺的量。

### 持久日志与 root 共享 UFS

[kern_exec.c:7597](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7597) 的 `meizu_autolog_dump()` 以 atomic `busy` 防止并发/递归；已有写入尝试结束后至少 1 s 才准下次尝试，成功后 total 未变可跳过。它全量拷贝当前 retained console ring，再调用 `native_ufs_log_write()`。节流与 `busy` 覆盖显式 flush。

[NativeUfs.cpp:1179](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1179) 将日志按最多 128 KiB 同步写出。[RootCommands.h:241](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootCommands.h:241) 的 root read 和 L301 的 write 共用 `gHost.lock` 和一个 UTP slot；写持锁期间包含 RAM→DMA memcpy、cache maintenance、命令完成轮询。每个 chunk 结束会释放 host lock，不是整份日志一直占锁。轮询中 `IODelay(2)`，读/写 timeout 分别是 1 s / 2 s；这是错误上界，不能当正常耗时。

当前 console ring 上限 8 MiB，但在这两个长等待附近，heartbeat 所记录的实际 snapshot 只有 **27 个 4 KiB 块（L1462）和 39 个 4 KiB 块（L1986）**，即通常 1–2 个传输 chunk；提取末尾才到 293 块。不能用“8 MiB 每秒”描述本轮关键时段。heartbeat 自己追加日志，可能让后续 snapshot 持续 dirty；仍须计时才能估算代价。

`native_ufs_log_sync()` 的 10 s 同步缓存命令没有被这个 autolog 调用；log write 的 FUA 默认也为 false。不能声称每秒额外 SYNCHRONIZE CACHE 或每次等待 10 s。

[kern_exec.c:7933](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7933) 后台循环在各探针/dump 完成后才 sleep `hz/20`。此外，旧 hook 会同步调用 `meizu_autolog_flush()`：如 [kern_prot.c:151](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_prot.c:151) 中 PID≤2 的 getpid，以及 [mach_msg.c:2467](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:2467) 的 PID 22 trap、L2594 的 PID 21/22 return 等。这些调用获得 busy 且到达节流时间后，**在当前调用线程**同步写 UFS，不只在后台线程。日志包含 4 条 getpid hook、24 条 PID 21/22 msg2 return；次数不能推导实际 I/O 时间。panic/exit 路径也有显式 flush，但不能把 panic 开销归入正常到达 Hello 的启动。

console ring 本体位于 [serial_general.c:165](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/console/serial_general.c:165)：每字符 atomic 累加和 RAM 写；snapshot 只是 RAM 复制，没有直接存储 I/O。

### IPC/线程采样的锁与次数

- [mach_msg.c:333](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:333)：selected generation 的热路径会取 blocking spin lock，最多查 32 worker 槽并复制 scalar record；ring 为 64 行。一般未选任务先返回。不能因日志打印少就假设热路径零成本。
- [mach_msg.c:1138](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:1138)：端点遍历最多 8 节点，逐 port 使用 try-lock；持 port 锁时计算 salted SHA256 cookie。无 UFS、用户栈读取或 printf，仍有受限 CPU/锁占用。
- [mach_msg.c:1559](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:1559)：drain 最多每秒检查，pending SB 最多每 10 s 输出；在 probe lock 之外打印。
- [thread.c:4611](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/thread.c:4611)：worker snapshot 用 task try-lock，只在其中抓最多 64 个线程引用，然后释放 task lock，再分别用 thread mutex/scheduler try-lock 采样保存寄存器并打印。没有在 task lock 内读用户栈或刷 UFS。receiver/container 轮次为 `{0,10,30,60,120,180}` s，任务 churn 不重置预算。

因此没有发现上述采样会把慢 backtrace/printf/UFS 包在 task lock 中的证据，但尚未测量热路径累计 CPU、spin-lock wait 或 endpoint hash 的持锁时间。

### 目标线程上的可缺页 backtrace

[sys_ulock.c:148](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/sys_ulock.c:148) 的 `meizu_lsd_wait_backtrace()` 在 lsd 的 ulock/semaphore **用户 trap 入口同步运行**。它先验证中断/抢占状态；最多 16 线程、32 traces、16 frames，每线程每 opcode 最多 2 次；释放 probe gate 后才 `backtrace_user(...NULL...)` 和 printf。本日志实际有 22 traces。

NULL control 使用可缺页 copyin，因此目标线程可能因读其用户栈而触发 VM/文件读取；有限次数不保证每次很短。没有在 probe gate 内做 backtrace，也不能据此说 lsd 没有受到观察影响。Setup 同类探针同样需要按已获准 trace 时间计量。

### E1 状态绘制

[BootLog.c:806](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:806) 每 IRQ 累计计数/采样，EL1 分支增加 E1；每 512 IRQ 在 `busy==0` 时同步执行 `blog_status()`。FIQ 另有每 64 次更新。`blog_status()`（L689）清空、重绘 5 行；若使用 Normal framebuffer 映射，还按行 `dc civac` 和 `dsb sy`。没有存储 I/O，但这些像素/缓存工作发生在中断路径，实际时间未记录。

`native_blog_putc()`（L620）还接管 `PE_kputc`；其调用契约是 kprintf 持锁、关闭中断期间，绘字和满屏后滚动同样同步进行。不能仅用 console ring 字节数推算 framebuffer 成本，也不能假设所有 printf 都一定经过这个 kprintf sink。

排除项：[build-meizu-storage-family.py:64](<nova-root>/unixnu-meizu20pro-nova/scripts/build-meizu-storage-family.py:64) 为本板生成空 `boot_progress_poll()`。verified-read 中每块调用该函数，不能按“每块刷新屏幕”归因。`native_ufs_progress_text()` 只读缓存的软件状态，也不执行 UFS MMIO/队列操作。

### 最小计时与单变量试验

先保留同一镜像/Data seed 和一致 Boot 起点，再选择一类诊断做单独比较。无需现在扩大打印量：

1. autolog 记录 snapshot copy 时间、write elapsed 的 count/sum/max、bytes，以及后台/显式 flush 调用来源；root 记录等待 `gHost.lock` 的 count/sum/max。把等待锁和持锁传输分开，避免把总 read elapsed 全解释为硬件慢。仅在阶段边界输出聚合值。
2. 给获准执行的 lsd/Setup backtrace 记录 elapsed count/sum/max，含 copyin 与输出范围；必要时再拆开两段。禁止每次 stack memory read 打印。
3. E1/status 更新记录 refresh count/sum/max ticks，按 IRQ/FIQ 区分；用 IRQ-safe、非阻塞计数，禁止中断里打印；若后续允许单变量候选，可只停周期状态绘制但保留低开销计数与必要 panic 信息。
4. IPC probe 若仍被怀疑，抽样或有界聚合 spin wait / held ticks 及 endpoint traversal ticks。线程数和轮次限制已明确，先测量，再决定是否降低采样范围。不要使用时间戳测量本身淹没热路径。

这些累计时间可能并行或嵌套，不能全部相加为 Boot→Hello 时间。若持久日志占用/栈观察/绘制累计时间很小且不处于关键依赖链，第 4 假设即可降级；反之，再以只改变该一类诊断的候选复测，保留回退和可比较日志。
