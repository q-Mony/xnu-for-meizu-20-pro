# 启动自动计时边界核实 — Nova

日期：2026-10-30。状态：只读可行性核实；新文件，无旧版本。本项只新增本报告，未改生产源码、构建、刷写或操作设备。中央 CHANGELOG 由集成代理统一记录。

现有日志可以自动统计部分内核阶段、根盘读取/验证和服务等待，但不能精确还原完整的“Boot 菜单确认 → 苹果首次出现 → Hello 首次出现”。缺的是菜单起点及画面语义锚点，不是计时器本身。第六轮候选未加入本报告建议的锚点；不能把本报告写成已实现或已验证。

| 边界 | 已有证据 | 当前能否直接计时 | 最小补点及准确名称 |
|---|---|---|---|
| Boot 菜单启动确认 | Menu.c 有超时、数字键、Enter 等分支；Main.c 随后可能显示确认弹窗 | 否：无 counter，也未写持久日志 | 保存最终确认事件的 `menu_launch`；有弹窗时另存首次按键，明确是否包含用户确认等待 |
| 内核加载与固件交接 | UniXNUBootEntry、DarwinBoot、DarwinEnter 调用链 | 否：文字和绿色条不是时间戳 | 保存 `loader_begin`、`exit_boot_services_ok`、`kernel_branch`，最后一个必须明确在缓存处理前或后 |
| 早期内核入口/MMU ready | entry probe 彩条、NativeBootLog 的 MMU ON | 否：stage value 多为地址/寄存器值 | 入口只保存 counter，安全阶段输出；`kernel_entry` 与 `kernel_mmu_ready` 分开 |
| 根盘/已有服务窗口 | rootio 的 CNTPCT ticks/hz、IPC 的 mach_absolute ticks/absolute_ns | 部分可用：有记录的窗口可统计 | 保留时钟名、generation、窗口边界；未标记阶段输出 unknown |
| 苹果图标首次显示 | framebuffer ready / swap / progress 代码存在 | 否：当前没有已证明的苹果内容标记 | 先查清实际绘制来源，再记录该内容有效提交/拷贝完成；不能用 AppleCLCD 名称猜图标 |
| Setup 进程启动 | successful exec 后 `meizu_setup_started` 和 setup-arm | 进程事件可用，但它不是 Hello；setup-arm 本行不含 started tick | 输出准确的 `setup_exec` 时间点和 attempt/generation |
| Hello 首次画面 | 尚未找到可信标记 | 否 | 需要 Setup 视图/scene 的明确内容 marker 与合成/有效 framebuffer copy 关联；仅首 swap 不足 |

## Boot 菜单起点与持久化

[Menu.c](<nova-root>/unixnu-main-nova/package/UniXNUPkg/Library/UniXNULib/Menu.c:187) 的超时启动、数字键启动及 Enter/空格等启动分支只有文字日志。取消 UEFI timer 和返回 Selected 在第 245 行附近完成。[Main.c](<nova-root>/unixnu-main-nova/package/UniXNUPkg/Library/UniXNULib/Main.c:120) 还可能执行确认弹窗；因此 `UniXNUBootEntry` 前的点表示最终选择完成，不总等于用户第一次按 Boot 的时刻。完整口径应保存被成功采用的一次菜单选择/确认事件及 launch reason，失败返回菜单后开始新的 attempt。

[Boot.c](<nova-root>/unixnu-main-nova/package/UniXNUPkg/Library/UniXNULib/Boot.c:124) 在读取 DT/initrd 之前进入 boot entry，适合增加稳定的 loader 起点；第 99 行才调用 loader。只在内核入口加计时会漏掉此前的文件加载成本。

[UniXNULog](<nova-root>/unixnu-main-nova/package/UniXNUPkg/Library/UniXNULib/Main.c:26) 只拼文字并输出 DEBUG/EFI_SERIAL_IO，没有时间戳或 MEIZULOG 写入。[Log.c](<nova-root>/unixnu-main-nova/package/UniXNUPkg/Library/UniXNULib/Log.c:4) 是读取 NativeUfs 分区尾部日志的 viewer；writer 属于 NativeUfs。不能说菜单日志现在会随 XNU autolog 持久保存。

[DarwinBoot.c](<nova-root>/unixnu-main-nova/drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c:780) 成功 ExitBootServices 后进入 [DarwinEnter](<nova-root>/unixnu-main-nova/drivers/DarwinBoot/DarwinBootPkg/Driver/Entry.S:10)。后者先画绿色条、处理缓存和 MMU，再 `br x1`；现代码没有读 counter，也没有修改 CNTVOFF。新增交接点必须说明在绿色条/缓存操作前还是后，否则阶段成本会被归错。

可行候选是菜单端保存 raw CNTVCT、CNTFRQ、attempt/version，通过版本化 `/chosen` 属性或已验证的保留内存传入内核，在 autolog 可用后输出；早期入口只写有界内存记录，不直接进行 UFS I/O。DarwinBoot 已有版本化 `/chosen` GOP 属性机制，但这不意味着时间属性已存在，也不应随意改 boot_args ABI。实际打包固件还须绑定本次所查源码：第六轮沿用固定旧固件封装，查到源码不等于已部署锚点。

## 时钟能复用，但要验证同源和偏移

独立复核修正（2026-10-30）：[NativeUfs.cpp](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1343) 的 `timerTicks()` 读取 `cntpct_el0`，所以 rootio 属于 CNTPCT；NativeBootLog 和 CPU 探针属于 CNTVCT。即使记录频率相同，这些域也不能直接相减；下文的 CNTVCT/Mach 对齐建议不能自动涵盖 CNTPCT。需要根盘与菜单时钟对齐时，另外保存相邻 CNTPCT/CNTVCT 采样并验证偏移及误差。原报告的 rootio CNTVCT 标注已修正。

本地 Meizu 配置 [meizu20pro.dsc](<nova-root>/uefi-sheng-nova/Platforms/Meizu/meizu20proPkg/meizu20pro.dsc:49) 引入 KailuaPkg；[KailuaPkg.dsc.inc](<nova-root>/uefi-sheng-nova/Silicon/Qualcomm/KailuaPkg/KailuaPkg.dsc.inc:15) 设置 `USE_PHYSICAL_TIMER=0`。[SiliciumPkg.dsc.inc](<nova-root>/uefi-sheng-nova/Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc:139) 因而选择 virtual counter library；[GetPerformanceCounter](<nova-root>/uefi-sheng-nova/Mu_Basecore/ArmPkg/Library/ArmArchTimerLib/ArmArchTimerLib.c:174) 调用 ArmGenericTimerGetSystemCount，[virtual 实现](<nova-root>/uefi-sheng-nova/Mu_Basecore/ArmPkg/Library/ArmGenericTimerVirtCounterLib/ArmGenericTimerVirtCounterLib.c:92) 读取 ArmReadCntvCt。以上证明当前源码默认选择，不代替最终固件构建选项与二进制核验。

[BootLog.c](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:153) 显式 `isb; mrs cntvct_el0` 并读取 CNTFRQ。XNU [mach_absolute_time](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm/rtclock.c:166) 返回 ml_get_timebase；[ml_get_timebase](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm64/machine_routines.c:2221) 返回硬件计数加 `cpu_base_timebase`。本地常规硬件读取路径是 CNTVCT；[cpu_timebase_init](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm64/cpu.c:975) 在唤醒时会改变 offset，DEBUG/DEVELOPMENT 还允许 timebase_offset 参数。因此不能无条件把 raw CNTVCT 与 mach_absolute tick 直接相减。

建议内核保存一组相邻的 `cntvct_before, mach_absolute, cntvct_after, cntfrq`，保留采样误差区间，再校验跨 handoff 单调连续、频率一致和 offset；同源确认后才换算到 menu T0。缺 T0 时必须标 `relative_to_first_sample`，不得叫 Boot 耗时。不同 reboot 的 raw tick 不得拼接。

本报告只据当前本地源码与第五轮实际日志判定可观测边界；没有用外部架构说明代替本设备固件连续性验证。[BootLog 的频率读取](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:161) 来自 CNTFRQ。第五轮日志的 19.2 MHz 是该计时源频率，不是已测得的 CPU 运行频率。

## 已有内核时间字段的含义

[BootLog.c](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:509) 的 native_blog_start 画 MMU ON，native_blog_stage/队列只保存 name/value；现有 entry stage 的 value 并非时间。

[native_blog_status_timing_dump](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:910) 已有 `clock=cntvct ticks hz age_ticks`。started_at 是首次调用状态汇总的时刻，不是菜单或内核入口。第五轮 [日志第 185 行](<nova-root>/logs-nova/boot-speed-round5-nova/candidate-run-nova/autolog-nova.txt:185) 的 `ticks=450263575 hz=19200000 age_ticks=0` 只能建立首样本相对时间；E1 是中断计数，不是秒数。第 877 行开始的 lsdipc 使用 mach_absolute/absolute_ns，可分析其已定义的窗口。

[meizu_autolog_dump](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7689) 把 console snapshot 通过 native_ufs_log_write 保存。它有写入节流，故 flush 时间不是事件发生时间。补点应在事件发生时保存 tick，稍后异步输出/持久化，并保留 run/attempt 标识，防止上一轮保留日志混入本轮。

## 为什么 Setup 和首 swap 不能当 Hello

[meizu_setup_capture](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:3690) 通过执行文件路径确认 Setup，在 successful exec 后保存 mach_absolute_time；setup-arm 文本本身没有打印这个 tick。这是进程启动，不证明 scene 创建成功或画面已合成。

第五轮有直接反例：setup-arm pid 244/270/276 分别在 [2680](<nova-root>/logs-nova/boot-speed-round5-nova/candidate-run-nova/autolog-nova.txt:2680)、[5500](<nova-root>/logs-nova/boot-speed-round5-nova/candidate-run-nova/autolog-nova.txt:5500)、[7959](<nova-root>/logs-nova/boot-speed-round5-nova/candidate-run-nova/autolog-nova.txt:7959) 行，而三者随后都出现 20 秒 scene-create watchdog，见 5331、7897、10324 行。故 Setup launch、LaunchServices 响应、进程存活不能命名为 Hello 首次显示。

[NegroniFramebuffer.cpp](<nova-root>/unixnu-main-nova/drivers/NegroniDisplay/NegroniFramebuffer.cpp:201) 的 AppleCLCD 启动日志表示显示服务 ready，AppleCLCD 名称不说明出现苹果图标。vsyncFired 使用 mach_absolute_time，但由 IOTimerEventSource 周期调度，不能当物理 panel 扫描完成证明。

同文件 [swap](<nova-root>/unixnu-main-nova/drivers/NegroniDisplay/NegroniFramebuffer.cpp:259) 只输出前 16 条 layer 日志，且日志在 surface/矩形有效性检查之前；随后可能跳过，真正 memcpy 在第 328 行分支内。现有 swap 日志既没有时间戳，也没有内容语义。可以增加 `first_valid_framebuffer_copy_complete`，但仍不能把它叫作苹果或 Hello。首次用户态帧可能是黑屏、启动图、其他界面或失败的 Setup 尝试。

目前查到的 PE_display_icon/default_noroot、console progress、AppleBoot 图像定义也不足以确认本设备苹果图标的实际首次显示来源。Hello 若要可靠自动化，需要找到 Setup 内部明确的 Hello 视图提交事件，再与 compositor/有效 copy 关联；另一候选是经过实际画面对照验证的内容检测。即便确认 copy-complete，也应与肉眼首次可见区分，因为当前驱动未提供真实 scanout 完成确认。

## 建议的最小推进顺序

1. 立即让离线报表读取现有 rootio/IPC/status 的原始时钟和窗口，不制造缺失的 Boot/Apple/Hello 时间。人工确认的 Apple/Hello 仅标为 external observation。
2. 下一次独立计时改动增加 menu launch、handoff、kernel entry/MMU、root ready、init exec、Setup exec 等低频锚点，验证最终镜像与持久日志的完整链路。锚点只读时钟、有限写内存；不改变服务等待或频率策略。
3. 单独核实苹果与 Hello 的真实画面提交来源，再增加语义标记及实际画面对照。未验证前，继续以清晰名称报告代理事件，不能承诺日志已能替代完整人工计时。

本项不判断 UFS 或 CPU 是主瓶颈，不改变第六轮诊断范围。它解决的是每个耗时数字的起止事件是否可信。
