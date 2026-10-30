# 屏幕诊断与持久日志成本复核 — Nova

2026-10-30。新报告，无旧版本。仅只读查看源码、构建记录与第四轮日志；未改生产代码、配置、镜像或设备。父会话统一记录 CHANGELOG。

**第五轮可以选择一个实际优化：保留每 512 IRQ / 64 FIQ 的原筛选，在其后添加 IRQ/FIQ 共用的 1 秒刷新上限，只减少 `blog_status()` 绘制。** 原有 E1/ELR/初始进程采样、真实中断处理和触控逻辑应逐项保留。当前没有找到独立且已经真机计时验证的状态刷新开关；是否节省启动墙钟仍须测量。不要以 E1 数字推断已经消耗的秒数。

## 实际源码及构建路径

真正修改源是 [BootLog.c](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c)，本次读取 SHA256 `e88b7315bea8c55aeceec97423c6b9dc353e7711eb26d6d3d4c12499a0028a14`。

工作树中的 `<nova-root>/unixnu-main-nova/out/meizu20pro/tree/drivers/NativeBootLog/BootLog.c` resolve 到上述板级源。构建链如下：

1. [board.toml:55](<nova-root>/unixnu-meizu20pro-nova/board.toml:55)把 `build-native-meizu-bootlog.py` 列为 kernel archive builder。当前选中配置 `<nova-root>/unixnu-main-nova/out/meizu20pro/.config` 启用 `CONFIG_XNU_MEIZU_BOOTLOG=y`；不要用基础 defconfig 推断当前选中配置。
2. 第四轮[构建日志](<nova-root>/logs-nova/boot-speed-round4-nova/build-lsd-timing-nova.log:17)显示实际执行树内脚本。该脚本与板级脚本内容相同，SHA256 均为 `29e9f90bf544e3a3b84a8f472c5acaaa54ba0f49dc73ba5de5e5e12103a3968c`。
3. [构建脚本:66](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/scripts/build-native-meizu-bootlog.py:66)读取 `build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json`，复用内核 C 编译参数，另加 `-mgeneral-regs-only`。输出树内 `build/native-meizu-bootlog/BootLog.o`，再归档成 `libNativeMeizuBootLog.a`。
4. [内核链接脚本:633](<nova-root>/unixnu-main-nova/scripts/build-xnu-4k-baseline.sh:633)在 `XNU_MEIZU_BOOTLOG=1` 时同时将 archive 放入 `-Wl,-force_load` 和 `KERNEL_ARCHIVE_DEPENDENCIES`。读取时对象 SHA256 为 `eb75312f7c7e8ec78eef7620bc71a4a111195af693824eba5788c14164ad3d9c`，archive 为 `47c52fb8da4f8c3360e342273d63e9d203a69ae0ece8aba82f9e3bf077b8d198`。
5. 第四轮构建日志还显示 raw `mach.development.bcm2837.unstripped.noctf` 经 `emit_prelink.py` 生成树外输出 `out/meizu20pro/kernel/kernel.macho`；打包时 entry probe 把 IRQ/FIQ hook 接到已链接的 NativeBootLog 函数。本报告核实依赖路径与构建记录，**未另做 BootLog.o→最终 payload 的逐函数机器码绑定审计**。第五轮应更新对象、archive、raw、prelinked 与 boot 镜像绑定，不能只保存源改动。

## E1 与全部 status 调用点

[IRQ_STUB/FIQ_STUB](<nova-root>/unixnu-meizu20pro-nova/scripts/patch-xnu-entry-probe.py:385)在 `_sleh_irq` / `_sleh_fiq` 的入口调用 NativeBootLog，然后重放被覆盖的首条指令并跳回。板配置 [L201](<nova-root>/unixnu-meizu20pro-nova/board.toml:201)选择这两个入口。

`native_blog_irq()` 每次先增加 `irq_count`；active 时读取 ELR_EL1/SPSR_EL1。`(spsr & 0xf)==0` 计入 E0，其他分支按 EL1 样本处理并增加 E1。它还维护初始进程的 PC、采样次数、总 IRQ 样本数。**E1 是 IRQ 打断内核执行时记录的累计样本数，不是绘制次数、文件读取进度或秒数**；FIQ 没有增加这个 E1。已有计数是普通/volatile 更新，不能当作经过 SMP 原子性证明的精确全局计数。

`blog_status()` 的三个实际调用点如下，除此之外无其他直接调用；声明不算调用：

| 调用方 | 源行 | 条件与频率 |
| --- | ---: | --- |
| `native_blog_kv_ready()` | 612 | 首次成功映射/接管时主动画一次，随后 phase==2 的再次调用提前返回 |
| `native_blog_irq()` | 852 | 所有现有计数与 ELR 采样之后，`busy==0` 且 `(irq_count & 511)==0` |
| `native_blog_fiq()` | 869 | `active && !busy` 且 `(fiq_count & 63)==0` |

IRQ 和 FIQ 当前使用**各自独立的计数触发**，不是两者合计每 512 次，也不是共享时间预算。脚本/board 中“每 64 IRQ”的注释已落后于 BootLog.c 的真实 512 掩码，应以实际 C 和最终机器码为准。非触发 IRQ 也继续计数和采样；不要把时间判断移到这些更新之前。`active=0` 会连带关闭 ELR 等采样，不是合适的仅关闭绘制方式。

## 单次绘制的静态工作量

第四轮日志原始 boot-video 为 1080×2400、stride=4320、32 bpp。使用该几何时，`cols=67`、`top=187`、`r=61`、文本 `rows=56`，状态区 5 行，每行像素高 32。若后续有效 vinfo 替换几何，按替换后的参数重新计算；原始 handoff 不等于运行时映射遥测。

| 路径 | 明确的源码工作量 | 解释边界 |
| --- | --- | --- |
| 单字符 `blog_glyph()` | 16×32×4 = 2,048 字节像素写 | 包含前景和黑色背景；尚无真机耗时 |
| 一次 `blog_status()` 清状态区 | 5×67×16×32×4 = **686,080 字节**，171,520 次 32 位像素赋值 | 这之后还会重新绘制所有状态文字，每字符再加 2,048 字节 |
| Normal 映射下清状态行的 cache maintenance | 5×32×4320 = 691,200 字节行范围；64 字节对齐后约 10,800 个 clean/invalidate（首地址非 64 字节对齐时每行多 1），再加每行 `dsb sy` | `clean_rows=0` 的 Device 映射不会执行这组 cache 操作 |
| 文本满后的每次换行 | 复制 **7,603,200 字节**，清末行 138,240 字节 | 约 7.74 MB 写、7.60 MB 读，另有 glyph/cache 成本 |

[blog_status:698](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:698)先通过空格 glyph 清满五行，再格式化/绘制；它不是简单更新 E1 的几个数字。运行在 IRQ/FIQ hook 中，没有调度到后台。

[映射选择:561](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:561)若内核 vinfo 已有效，则沿用映射且 `clean_rows=1`；否则 `ml_io_map_unmappable(..., VM_WIMG_IO)`，`clean_rows=0`，以 Device 内存直接写像素。板注释说明后者是预期路径，但当前 autolog 没有实际 `clean_rows`/映射状态字段，不能把某一路的成本当成实测事实。

E1 约 20 万至 30 万不表示重画了 20 万至 30 万次。实际绘制还受 IRQ 总计、E0、FIQ、busy 与映射条件影响；当前没有累计 redraw 计数，无法从 E1 单独还原绘制总成本。

## kprintf 滚屏可能更贵，但不能用 autolog 行数代替

[blog_newline:322](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:322)在文本区满后，每换行用 64 位循环把 55 行逐行上移，再清末行。[native_blog_putc:620](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:620)是 [L925](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:925)接管的 `PE_kputc` sink。

[pe_kprintf.c:115](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/pexpert/arm/pe_kprintf.c:115)在关闭中断并持有 kprintf_lock 后调用 sink，直到格式化/绘制结束才解锁恢复。所以若实际有数千次 BootLog 滚屏，可能比周期状态重画更重；这是明确的成本机制，**尚不是已证明的启动根因**。

第四轮 1,453,771 字节、7,642 行 autolog 来自 [console_printbuf_putc:195](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/console/serial_general.c:195)中的持久 ring 镜像。常规 printf→console_write→console ops 与 kprintf→PE_kputc 是不同路径，不能把这些日志行全部算作 BootLog 换行。当前编译记录还明确 `XNU_BOOT_PROGRESS=0`，不要把另一套 boot_progress 控制台当成 NativeBootLog。

若第五轮只改 status 刷新，可只加实际 `blog_newline` 滚屏次数/字符数等极少量计数来判定规模；不要同时改滚屏策略，否则总耗时变化不能单独归因于 1Hz 状态刷新。status 绘制时间也不能当成全部 framebuffer 诊断时间。

## autolog 当前已节流，仍会重写全量快照

[kern_exec.c:7584](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7584)已有“上一写尝试结束后至少 1 秒”的 UFS 快照节流。成功且 ring_total 未变时跳过。显式 flush 共用该上限；不是每次 hz/20 的 autolog 循环都写盘。

每次仍会把当前保留的 ring 全量复制并重写，从头写 header+prefix+ring+块对齐填充；ring 上限 8 MiB。[native_ufs_log_write:1180](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1180)按 `kDataMax` 分块调用 `hostWrite4k`。[hostWrite4k:293](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootCommands.h:293)取得与根盘传输共用的 `gHost.lock`，复制到 bounce/DMA 区并同步等命令结束。[hostSubmit:78](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootCommands.h:78)有 cache maintenance 和 polling；其 delay 为 IODelay(2)。单独线程不意味着没有 CPU/总线或共享锁成本。

日志中有 240 条 heartbeat。相邻成功 heartbeat 的 saved_total 变化能够证明至少一次新快照成功，但 heartbeat 并不逐写输出，因此只能形成下界：

| 按日志顺序选取的窗口 | 首/末 heartbeat 行 | 排除首条后不同 saved_total 数 | 相应写入字节下界 |
| --- | --- | ---: | ---: |
| 全部保留 heartbeat | 201 / 7640 | 235 | 88,965,120（84.84375 MiB） |
| dump 6→14 内部窗口 | 1822 / 2364 | 78 | 15,699,968（14.97265625 MiB） |

这里按每个变化后的 `blocks×4096` 累加。排除首条避免把窗口之前完成的快照算入；同一 saved_total 的重复 heartbeat 没重复计数。它不是精确总写量、I/O 独占时间或日志造成的墙钟延迟。根盘 rootio 的读耗时不能直接推算这批写耗时。

存在 `sheng-log=0` 开关关闭 UFS 持久日志，但它会失去本轮离线诊断能力，不适合混入状态刷新单变量实验。现有“1 秒一次”已实施，不能再次把这个改动当作新优化。

## 1Hz 候选的时钟与并发边界

对这个覆盖早期中断入口的独立模块，推荐采用板级已经使用的 **CNTVCT_EL0 + CNTFRQ_EL0**，只在现有 512/64 筛选通过后读 `isb; mrs cntvct_el0`，一秒间隔直接用 CNTFRQ ticks。依据是 [ArmCpu.cpp:125](<nova-root>/unixnu-main-nova/drivers/ArmCpu/ArmCpu.cpp:125)已经用相同计数器进行 GIC 等待；频率读取也已存在。

`mach_absolute_time()` 在正常中断环境可用，但 [rtclock.c:166](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm/rtclock.c:166)最终调用 [ml_get_timebase:2221](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm64/machine_routines.c:2221)，后者读取 `getCpuDatap()->cpu_base_timebase`，含睡眠/恢复重试；DEVELOPMENT 可有 timebase validation/CAS。`arm_init` 明确在 serial 初始化之前初始化 timebase。直接 CNTVCT 让这个早期 BootLog 模块少依赖 CPU 数据初始化和时间转换基础设施。

这是对该候选的依赖选择，不是否定 mach_absolute_time 本身。也可选择已有 rootio 使用的 CNTPCT，但必须全程同一个计数器。**不能将 CNTVCT 的绝对 tick 与 rootio CNTPCT 或 mach_absolute 起点直接相减。** 自身耗时使用同源前后差值，并输出 `clock=cntvct`、实际 `hz`、ticks；不要硬编码 19.2 MHz。若频率无效/时钟倒退，要有明确跳过与重新基准规则，避免无符号差值导致刷新风暴。需考虑睡眠恢复后的原始计数变化；第五轮目标是启动区间。

并发上必须区分“共用刷新额度”和“整个 framebuffer 互斥”：

- 现有 `volatile busy` 为 check-then-set，不是 SMP 原子锁。IRQ、FIQ、putc、stage、kv_ready 都会读写它。不能只把新增路径当作已经全局串行。
- IRQ/FIQ 应共用一个非阻塞原子 gate 或 deadline claim；竞争失败立即返回，不能在中断等待另一 CPU，更不能在被中断的持锁者上自旋。两个分别的 1Hz timer 会允许合计约 2Hz，不符合共用上限。
- 保留原 512/64 筛选，时间判断放在 IRQ 原有计数与 ELR/init 采样之后。只给实际获准绘制记 count/time；分别记 time-throttled 与 busy-skipped，避免把无机会绘制说成节流收益。
- 若将 `busy` 升级为统一 atomic try-lock，需要所有写入者一致采用，并保留初始化阶段的序列假设。若只给周期绘制加独立 CAS gate，应明确其只防止 IRQ/FIQ 相互重入，不能声称修复 putc/stage 的既有 SMP 竞争。不要顺手改原计数的非原子语义或中断 hook。
- 总耗时/max 应以 `blog_status()` 前后同源计数器测量，语义为调用经过时间。IRQ/FIQ 分别记录 actual draws，可加共用累计；汇总从 autolog 发出，每 10 秒、固定启动预算，打印前释放 gate/busy，禁止中断内 printf。
- 自身绘制计时若只包 status，不包含每 IRQ 的寄存器采样、gate 检查、kprintf 滚屏、汇总打印或持久写盘。报告必须保留这个边界。

验收至少比较原候选与第五轮同起点 Boot→Apple / Boot→Hello、日志中的原服务等待、实际刷新数/节流数/累计及最大绘制时间，并核对触控/画面仍可用。启动计时改善和自身绘制成本下降应分开呈现；若无明显改善，保留证据后再选择 kprintf 滚屏或 autolog 写成本，避免继续叠加同时改动。
