# R8：R7 启动等待的精确符号化与暖数据验证标准

2026-10-30。新文件，无旧版本。只读取既有 R7 日志、Nova 源码和已只读挂载的同版本系统镜像；未操作手机、未修改生产代码、未执行历史下载脚本。

**已确认 lsd 在 R7 执行 LaunchServices 数据库重建，枚举内置应用与应用扩展，并在扫描途中查询应用组容器和扩展的数据容器。** 这给出了可执行的优化方向：让下次启动能够复用有效、完整的初始化后 Data 状态，再量出重建工作是否消失及净启动收益。单凭这些调用栈不能把等待归因于 UFS，也不能把每个容器请求都称为无效重试。

主报告使用 `symbolicated-r7-final-nova.json`，校验使用 `symbol-validation-r7-nova.json`。`symbolicated-r7-nova.json` 是仅有局部符号的历史初稿，已被最终 JSON 取代；初稿曾把部分导出函数错误归到前一个局部符号，禁止用于结论。

## 版本和定位可信度

- 系统元数据：iOS 16.7.16，build `20H392`，SystemImageID `0184D69E-C5E7-4A69-B716-C4E166CD8A97`。
- 使用已挂载的 `/Volumes/SydneyUpdate20H392.D22D221SystemCryptex/System/Library/Caches/com.apple.dyld`。挂载的 `writeable=false`、无 shadow；本任务未执行挂载或卸载。
- Nova 的 `unixnu-main-nova/out/ipsw/20H392/087-86593-047.dmg` 与挂载源 `[local-home]/...` 均重新计算为 SHA256 `9cd738f3b0169055549df84ad6d8d13e50a2da39af618ce0522ca4e06df86849`。原工作区仅作为只读镜像输入。
- 主缓存 UUID `8f970452c9e33e88b114feb1864bc9e0`；主缓存要求的符号缓存 UUID 与 `.symbols` 实际 UUID 都是 `51ccba451fc23f19aed047050bdf009a`。所有用到的 Mach-O `LC_UUID` 与缓存镜像表匹配。
- R7 原始 L484：`check_np start_addr kr=0 a=1bb448000`。未滑动共享区为 `0x180000000`，得到 slide `0x3b448000`，满足 16 KiB 对齐及 `maxSlide=0x55214000`。Nova XNU `vm_shared_region.c` 的 check_np 路径返回 `sr_base_address + sr_first_mapping`，与此定位方法一致。
- 6 份选中栈共 **96 帧**。当前 PC 保持原值；返回地址减去 ARM64 的一条 4 字节指令以定位调用点。每帧符号地址均等于其 `LC_FUNCTION_STARTS` 所属函数起点。79 帧由局部符号命名、17 帧由导出符号命名；独立遍历导出 trie 交叉核对通过。
- 四处栈顶或采样 PC 的反汇编分别匹配 `__ulock_wait`（x16=0x203）、`semaphore_wait_trap`（-0x24）、`mach_msg2_trap`（-0x2f）、`kevent_id`（0x177），都可见紧邻的 `svc #0x80`。保留了实际字节、缓存文件及文件偏移。

限制：R7 没有直接记录运行时缓存 UUID。这里的版本匹配来自同哈希源镜像、映射头地址、镜像/符号 UUID、函数边界及指令检查，不能写成“已抓到运行时 UUID”。独立导出遍历检查的是同一缓存内的数据，并非另一版本构建。符号确认不等于完整回溯或各阶段时长确认。

## 调用路径

下面名称在展示时去掉 Mach-O 额外前导下划线；逐帧原名、运行时 PC、偏移和原始行号在最终 JSON 中。

| 栈 | 原始日志 | 线程 | 已确认路径及含义 |
| --- | --- | --- | --- |
| seq 1 | L975–991 | lsd 主线程 433 | `_LSServerMain → seedDatabase → createAndSeedLocalDatabase: → _LSDatabaseCreateFromPersistentStore → _LSDatabaseClean → LSHandlerPref::CopyHandlers → _LSWithSecurePreferences → dispatch_sync → __ulock_wait`。这是数据库创建/加载、清理和偏好设置同步；单独看到此栈不能断言“磁盘数据库损坏”。 |
| seq 7 / 8 | L1632–1648 / L1667–1683 | lsd worker 435 | 两次相同：`_scanAppsDirectory → enumerateURLsForItemsInDirectoryAtURL → scanExecutableBundle → _scanBundle → enumerateBuiltInSystemContent → _LSServer_LSRegisterICLItem → LSDatabaseRebuildContext registerItems: → dispatch_sync → __ulock_wait`。确实在枚举和注册内置应用。 |
| seq 9 | L1684–1700 | lsd worker 435 | `scanExecutableBundle → _scanBundle → _scanAppExtensionsInBundle → enumerateAppExtensionsInBundle → MIInstalledInfoGatherer bundleRecordWithError → _groupContainersDictionary → MIContainer groupContainerURLsForBundleIdentifier:persona:error: → container_query_iterate_results_sync → container_xpc_send_sync_message → container_xpc_release → __ulock_wait`。应用扩展注册数据正在查组容器。 |
| seq 10 | L1838–1854 | lsd worker 435 | 扩展扫描进入 `MIAppExtensionBundle dataContainerCreatingIfNeeded → MIExecutableBundle _dataContainerCreatingIfNeeded → MIDataContainer dataContainerForExecutableBundle → MIContainer containerWithIdentifier/tempContainerWithIdentifier/_copyContainerForIdentifier → container_query_get_single_result → container_xpc_send_sync_message → container_xpc_shared_copy_connection → __ulock_wait`。明确为扩展数据容器查询/创建路径；函数名中的 create 不证明该次已经新建成功。 |
| seq 17 | L2525–2541 | lsd worker 435 | `_LSServer_LSEnumerateAndRegisterAllBundles → LSRebuildStatisticsGatherer performRebuild: → LSDatabaseRebuildContext withStatsGatherer → _LSServer_ExecuteSyncWithQuiescedInstallationActivity → performContainerizedContentScan: → enum/register block → dispatch_semaphore_wait → semaphore_wait_trap`。确认本次启动发生重建及容器化内容扫描。 |

容器请求中还发现了一个明确的客户端同步点：seq 9 的 `container_xpc_release` 在 unslid `0x1c4c2ed04` 经优化分支桩调用 `os_unfair_lock_lock_with_options`；seq 10 的 `container_xpc_shared_copy_connection` 在 `0x1c4c2e4c8` 调用 `os_unfair_lock_lock`。两者都把 **`0x1e1d85200`** 的同一个静态锁地址传为 x0。分支桩最终落到 libsystem_platform 的精确导出符号 `0x1d1b09604` / `0x1d1b095ec`，并非只靠最近符号猜测。

这两帧说明当时 worker435 在连接管理的客户端锁上等待；没有锁持有者或精确持续时间，不能宣布它解释了全部 41.7 秒容器 trap 累计量，也不能据此修改锁的语义或删除同步。

SpringBoard 主线程 253 的重复 PC/LR 仅能定位为 `mach_msg2_trap / mach_msg2_internal`，说明在 IPC 等待；没有足够高层栈来补造具体 SpringBoard 函数。lsd 主线程 433 后期 PC/LR 是 `kevent_id / _dispatch_kq_poll`，已进入事件循环。434/742 可见 `__ulock_wait / _dlock_wait`。不能把“lsd 的工作线程重建”泛化为“lsd 主线程持续在重建”。

## 与计时及消息数量的关联

引用已完成的 `artifacts-nova/boot-speed-round7-nova/device-analysis-nova/lsd-timing-nova/REPORT-nova.md`，不重跑或改写 R7 时间解析：

- SpringBoard 成功 exec 捕获 L702 到 Setup armed 捕获 L2762 为 **152.523992969 秒**；lsd 成功 exec 捕获 L893 到 Setup 为 **146.035655573 秒**。两者不是 Boot→Hello。
- SpringBoard 对 lsd 的同一 seq=2514 有九次 pending 观察，最后 phase age 90.060 秒；它不是精确请求时长或严格等待下界。
- lsd dump6→13 经过 70.286143854 秒，CPU 记账 24.959822552 秒，单核等效占比 35.5117%。非 CPU 记账时间不能直接归为 UFS。
- dump15，L2712：容器接收完成 **3,838**，累计 trap 经过 **41.724101250 秒**；发送桶 L2711 也为 3,838。下一快照 L4585 与后续至 L6244 仍为 3,838，且同代。Setup 捕获位于这些稳定快照之间。因此 3,838 是这轮启动的可用稳定参考量，而不是只根据某一张早期快照估算。
- dump15，L2708 / L2709：launchd ID `0x40000388` / `0x40000324` 的完成数分别 2,550 / 2,552。不能由 errors=2,550 断言 2,550 次业务失败。R4 的另一个客户端 Setup/Budd 里，前一 ID 曾是 `MACH_RCV_TOO_LARGE` 后 receive-only 成功；不能无证据套用到 lsd。当前未验证这两个 ID 的准确 launchd 业务名称，也未证明所有这些调用由上述容器路径引起。

跨线程 trap 累计时间可能重叠，不能与 CPU 或其他阶段时间相加。R7 IPC 诊断 ring 有 overwritten/dropped=267,031，采样缺帧也不能作为某条路径没执行的证据。

## 下一轮暖数据复用：成功与失败的可量化判据

R7 证明本轮存在重建。是否“每轮都被迫重建”还应结合 Data 打包和 mode0 每次丢弃 RAM 写入的证据；该项由 Data 分支报告单独负责。以下是下一轮验证方案，不代表暖复用已实现或已提速。

1. **先证明恢复对象正确。** 为原始冷 seed、暖数据来源与恢复后的完整对象记录哈希/版本、长度、校验结果、恢复字节数、开始/完成标记，以及是否 fallback。预期恢复必须是“校验成功且完整提交”，不能把“读了部分数据”“仍是 mode0”或“没报错”当成成功。mode0 暖 RAM 恢复应同时保留 mode 和独立 restore 状态，避免 mode=0 被误判为没恢复。若格式有 base/overlay 关联，必须核验 base 身份一致。确切字段名由实现决定；上述是要求的证据，当前日志尚不具备全部字段。
2. **再证明重建工作减少。** 最有力的机制指标是同一 build、同一应用集合、同样 Setup 状态下，完整记录的 `performRebuild` / 全量 enum-register 启动计数从非零降到 0，并有成功复用数据库的结果。如果只保留当前离散回溯，看到重建栈能证明路径仍存在，没采到不能证明次数为 0。不要为了验证去跳过强制注册或 Setup。
3. **记录服务工作量。** 在同一 lsd 代际、启动到 Setup 的同一业务区间比较 containermanagerd 发送/接收数量、累计和单次最大，及上述两个 launchd ID 数量。桶编号、pid、tid 可能变化，要按当轮成功 exec 映射后的服务身份、send_id、mode 匹配；不能硬编码 bucket24/pid63。R7 容器参考为 3,838；**减少至少 80%（≤767）可预先作为“全量扫描大幅消退”的实验目标**，不是声称正常暖启动一定该为 767 或零。若计数降幅小，也必须如实报告，不能仅凭未达到目标宣布恢复失败。
4. **测量关键区间。** 同一 Mach 时钟记录 SpringBoard exec→Setup armed，参考 152.524 秒；lsd exec→Setup armed 参考 146.036 秒。为第一次验证预先设定 **前者缩短至少 50%（≤76.262 秒）** 的强收益目标。它是实验目标，不是收益承诺。记录 lsd CPU 总量和容器累计作为解释，但不将其相加。新探针须在 Setup 时或紧邻的稳定快照给出计数，避免暖启动已完成后固定第15个采样包含不同比例的后台工作。
5. **最后核验净收益。** 用户全程不重置秒表，记录 Boot→苹果、Boot→Hello。优先同一候选做冷 seed 对照与暖恢复；新增暖数据读取时间也算入总耗时。最终应至少连续两次完整启动复现 Hello 且总时长下降。阶段变快但额外读取使 Boot→Hello 不降，只能算机制有效、方案尚未净提速；不能报成功。Hello 仍需用户观察或新加首帧锚点，不能拿 Setup exec 代替。

| 观察到的组合 | 结论与下一步 |
| --- | --- |
| 校验/恢复失败或回落冷 seed；重建与约3,838次容器请求再现 | 先修导出/恢复的一致性、完整性或身份匹配；本轮不能否定暖复用本身。 |
| 恢复完整成功；全量重建与类似请求规模再现 | 检查数据库和容器元数据是否真正包含、LS为何判为需重建、版本/路径/权限/身份是否变更；不能继续只增大 UFS 读取块。 |
| 恢复成功；重建明显减少；SB→Setup显著缩短，但总启动未缩短 | 确认新增预加载/解压/恢复成本，缩小或优化暖数据读取；机制有作用但当前交付无净收益。 |
| 恢复成功；请求数下降，但SB→Setup仍接近152.5秒 | 转查剩余的确定等待链；不能再拿旧3,838次工作解释新的长等待。 |
| 初次暖启动快，下一次又退回重建 | 暖状态保存/恢复链不稳定，或快照本身不完整；不得宣称可重复优化。 |
| 全量重建少、SB→Setup与Boot→Hello均下降，连续启动且Hello/应用注册正常 | 机制与用户体验两层均支持优化有效，保留原冷seed作为回退对照。 |

修复优先级因此是“初始化后 Data 的一致性和复用”，随后处理仍被证据指向的剩余等待。现有证据不足以先改 UFS，也不足以直接 patch 掉 LS 注册、容器检查或同步锁。

## 文件与复核

- `symbolicate-r7-nova.py`：只读符号化，输入固定原始 SHA256，输出拒绝覆盖。
- `symbolicated-r7-final-nova.json`：6 栈/96 帧及相关 sampled PC/LR；完整文件、UUID、偏移、字节。
- `validate-symbols-r7-nova.py` / `symbol-validation-r7-nova.json`：独立导出遍历、局部符号与函数起点核验，四个 syscall 指令核验，以及容器共享锁调用点和精确导出证据；实际执行通过。
- `PROVENANCE-nova.json`：文件哈希、来源标识和备份路径。

本目录新增文件无旧版本。既有 reader 的两次修正均已先备份到 `snapshot-nova/round8-symbol-reader-mmap-fix-nova/` 和 `snapshot-nova/round8-symbol-reader-exports-nova/`，各含改前 SHA 清单。中心变更记录由父代理统一追加，避免并发写入。
