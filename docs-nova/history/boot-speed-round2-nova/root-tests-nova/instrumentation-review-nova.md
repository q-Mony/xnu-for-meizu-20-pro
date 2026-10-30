# Nova round 2：启动 I/O 统计接入审查

2026-10-29。新建报告，无旧文件。只读生产源码；未改生产、构建配方或 CHANGELOG，未操作手机。此前 host/QEMU 测试已完成，本次不重复运行。

结论：初审发现两处阻止观测生效的问题，均已发送主代理。认可主代理提出的最小修正：沿用现有 NativeUfs 强引用，在 meizu_autolog_dump 的 extern 声明旁增加统计函数声明，删除不生效的 XNU_NATIVE_UFS guard；统计输出改为 printf。除此之外，未发现此次计数器和计时新增导致的阻塞问题。最终修正版本仍需主代理检查构建结果和二进制调用链，本报告不表示已通过设备实测。

## 需要修正的两处接入

1. [kern_exec.c 初审调用点](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7677) 被 XNU_NATIVE_UFS 宏保护，但当前该源文件的实际编译参数没有定义它。使用 kern_exec.o.json 的原参数，仅去掉 -c/-o 并添加 -E -dM，预处理返回 0；最终宏表含 XNU_NATIVE_RAM_BLOCK=1、XNU_MEIZU_AKS_NOTIFY=1、XNU_MEIZU_WATCHDOG_POLICY=1，不含 XNU_NATIVE_UFS。强制包含的 meta_features.h 和 owner-data-volume.h 也未补定义。该宏出现在 KernelConfigTables 的编译参数，不能据此推断 kern_exec 的参数相同。结果是编译可以成功，但 heartbeat 不调用统计函数。
2. [NativeUfs.cpp 初审输出点](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1365) 使用 kprintf。当前 [pe_kprintf.c](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/pexpert/arm/pe_kprintf.c:100) 走 PE_kputc 和 os_log 路径；[vprintf_log_locked](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/subr_prf.c:425) 正常非 DriverKit 情形只设 TOLOGLOCKED，不设 TOCONS。落盘 ring 在 [console_printbuf_putc](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/console/serial_general.c:195) 镜像。不能把 kprintf 的 msgbuf/os_log 当作 meizu ring。改用与现有 heartbeat 相同的 printf 能接入现有落盘路径。

最小修正不需要引入 weak_import：meizu_autolog_dump 已强引用 native_ufs_log_open/write。虽然 kern_exec.c 原有 native_ram_block_gpu_addon 的 weak_import/空指针模式，但该板 [驱动构建器](<nova-root>/unixnu-meizu20pro-nova/scripts/build-arm-driver.py:176) 会为特定未解析弱符号生成 stub，增加弱引用没有必要。

## 并发、锁和计时

- 新累计计数器全部使用 relaxed 原子读写，允许多个字段来自邻近时刻，不能将一行视作事务一致快照。新增代码没有获取 UFS 或 window 锁，也没有改变原有 window→host 锁顺序。
- 根读取的 read/verify/copy 在已有 window 锁范围内；[hostRead4k/hostWrite4k](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootCommands.h:241) 在正常返回前释放 host 锁。heartbeat 位于 native_ufs_log_write 返回之后。meizu_dump_busy 的 acquire/release 防止同一 dump 重入；它不会等待忙标志。
- 不能把整个打印函数描述成无锁、无分配或零设备影响：原子快照部分如此，但标准日志输出可能获取内部锁或走控制台。现有低频调用限制了输出数量，并未证明成本为零。
- timerTicks 读 CNTPCT_EL0，日志同时提供 CNTFRQ_EL0；秒数应在主机端按 ticks/hz 计算。ticks 是计数器读数，不是从 Boot 菜单开始的时间。比较两个样本使用差值，并验证 hz 非零且一致。
- read_ticks 在调用 hostRead4k 前后采样，包含 host 锁等待、命令处理、DMA 数据搬运和调度中断；不含进入 Operations.read 前的 window 锁等待。verify_ticks/copy_ticks 同样可含被抢占时间。多窗口并发时累计时间可重叠，三者之和不能等同启动墙钟时间。
- 本次使用无符号 64 位累计；有效窗口几何将请求限制在 READ(10) 范围，count*512 不会在有效请求中溢出。启动期间累计计数溢出不构成实际问题。meizu_dump_tick 已有 0x7fffffff→0 显式回绕。
- 每 200 次调用是调用次数节流，不保证严格每 10 秒；显式 flush、I/O、调度和采样都可改变间隔。
- NativeUfs 的定义受 NATIVE_VERIFIED_BLOCK=1 保护；[构建脚本](<nova-root>/unixnu-meizu20pro-nova/scripts/build-arm-driver.py:78) 对 NativeUfs 强制开启该宏，当前 build/native-ufs/compile-command.json 也包含它。C++ 导出使用 extern "C"，与 kern_exec 的 C 声明兼容。未验证其他非认证构建配置；旧代码已经存在该配置下未守卫的计时引用。

## 字段解释

| 字段 | 实际含义和分析限制 |
|---|---|
| requests / requested | 通过参数、方向、online 检查后进入根读取的次数和请求字节；包含后续失败的请求 |
| cmds | 调用 hostRead4k 的尝试次数；验证/poison 拒绝可能尚未实际提交硬件 READ(10)，不要称为精确硬件命令数 |
| ok_cmds / physical | 成功 hostRead4k 次数和其传输字节；包含 4 KiB 尾部填充及重复读取，不是唯一磁盘数据量 |
| verified / verify_bytes | 成功认证的单位数和字节；verify_ticks 也累计失败尝试耗时 |
| copied / copy_ticks | writeBytes 实际返回字节数和调用耗时；短复制时 copied 可能增加，而请求 done 仍仅包含此前完整批次 |
| bad_hash | 所有 vr_verify_block 非 VR_OK 结果；[验证器](<nova-root>/unixnu-main-nova/kits/VerifiedRoot/src/verified_root.c:47) 对参数/长度/哈希函数失败也返回 VR_BLOCK，不能只解释为介质哈希不匹配 |
| native_ufs_block_reads | 成功复制次数；批量读取后每次复制覆盖更多单位，不能与旧版数值直接比较成读取字节或设备命令量 |

对设备日志做归因时，优先看相邻样本的字节、次数、耗时差值和外部 Boot→Hello 计时，不根据单一累计计数推断秒数。

## 初审固定输入

以下 SHA-256 对应发现问题时读取的源码，不会随着主代理后续修正而自动更新。上文行号也是初审位置。

| 文件 | 初审 SHA-256 |
|---|---|
| NativeUfs.cpp | 680b295d2be26eec2f838a30a109d0566af76b5c81e0e7f1d525aded7cb399fa |
| kern_exec.c | e52c23ed582664d003aff572c72efcd052e95a518f2b58bb286a31c975713238 |
| RootCommands.h | 11f88727514b42c823c166b2d8da1090189d69d797f29bdff6532498f66b86ec |
| build-arm-driver.py | 952118ec9d8514abbe0c8ecdb0212f709601d8c98de2cf5132f1014bb019ef96 |

对照备份位于 snapshot-nova/boot-speed-round2-nova/production-before-nova，含隐藏 .cache 路径下的 kern_exec.c：NativeUfs.cpp SHA-256 为 05672c9e3334f29eecce1b0ffca031153f484499dfcd6d580c4cebd3e7215010；kern_exec.c SHA-256 为 ff20a531ddff36044b29814965bddb7e9cb3c54c4d5a507b6be70daa31ebf91c。精确 diff 显示 kern_exec.c 只新增上述 heartbeat 调用块；NativeUfs.cpp 新增累计统计、打印函数、copy 计时和认证单位注释修正。


## 修正后源码复核

主代理修正已在活动源码中确认：kern_exec.c:7601 声明强引用 native_ufs_log_read_stats，heartbeat 中保留每 200 次调用节流并直接调用，已删除 XNU_NATIVE_UFS guard；NativeUfs.cpp:1365 已改为 printf。两个初审接入问题均在源码层面解决，未发现剩余阻塞项。本报告前文保留发现过程和初审固定输入，最终源码判断以本节为准。

| 修正后文件 | SHA-256 |
|---|---|
| NativeUfs.cpp | f719119ec580a68d1d4817bd01290e7cd0e0e171b782220a9b696647a4d1ee83 |
| kern_exec.c | da6059318757f7cd1e42c60eb2a913669cb928944ebe8abf49682e1b19408ba1 |

计数器命名和累计耗时解释仍适用前文限制。RootWindowRead.h 未变化，不重复此前通过的 host/QEMU 测试。最终链接和二进制调用链由另一审查代理负责，设备收益需要后续实际 Boot→Hello 测量。

本次追加前，初审报告已备份到 snapshot-nova/boot-speed-round2-nova/root-tests-nova/instrumentation-review-before-fix-confirmation-nova.md；未修改生产或 CHANGELOG。
