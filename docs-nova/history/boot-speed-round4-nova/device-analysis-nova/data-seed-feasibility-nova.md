# Data512 预初始化与持久化可行性 — Nova

2026-10-30。报告初建时无旧版本；本次收尾前已备份至 `snapshot-nova/data-seed-feasibility-final-20261030T001420-nova.md`。只读检查 Nova 源码及第四轮固定交付物；未运行 seed 修改/打包脚本、未读写设备、未修改生产代码。

结论：**已有可选的块级 Data overlay，以及离线 DBRDISK1 打包工具；没有找到可直接复用的、对本机运行中 Data 提供一致性保证的快照/干净导出流程。** 保留已经完成的 LaunchServices 初始化在机制上可行，但现有接口不足以直接把它认定为可用暖 seed，也没有本机提速证据。

## 当前候选实际行为

第四轮交付物 `1b05a1b8…33a9` 的 `final-extracted-nova/EFI/UniXNU/Payloads/scratch-data.img` 为 DBRDISK1 v1、4096字节页、8个extent，编码1507488字节，逻辑536870912字节（Data512）；编码SHA256为 `0f1a60ef3e23e355cec56d6a860db5606e2da14cba51660318422efdd99f7c46`。`unixnu.toml` 没有 `sheng-data-persist`，因此 `storeMode()` 返回0。本轮日志L215确认Data以读写方式挂载成功。

[DarwinBoot.c:676](<nova-root>/unixnu-main-nova/drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c:676) 将packed输入原样放入启动内存并填写ADT的PackedRAMDisk范围。[NativeRamBlock.cpp:205](<nova-root>/unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp:205) 从packed页建立稀疏RAM磁盘；512MiB走小盘分支，随后写入改的是该RAM磁盘。当前mode0没有存储后端写回，下一次启动重新从同一seed开始。

### “每次重置”的已观测证据与边界

本次只读复核了三个实际封装目录：`root-batch128-rootio-fixed-nova`、`root-data-sha-candidate-nova`、`lsd-timing-candidate-nova`。三个 `scratch-data.img` 的SHA256均为上面的 `0f1a60ef…9f7c46`；三个配置均无 `sheng-data-persist`，均选择 `native-ufs-partition=unixnu`。

| 独立候选启动 | Data checkpoint | 挂载与模式记录 |
| --- | --- | --- |
| [round2日志L208](<nova-root>/logs-nova/boot-speed-round2-nova/candidate-run-nova/autolog-nova.txt:208) | largest/best xid均17，位置1；desc/data均0 | L215读写挂载成功；L218 `volatile Data` |
| [round3日志L208](<nova-root>/logs-nova/boot-speed-round3-nova/candidate-run-nova/autolog-nova.txt:208) | largest/best xid均17，位置1；desc/data均0 | L215读写挂载成功；L218 `volatile Data` |
| [round4日志L208](<nova-root>/logs-nova/boot-speed-round4-nova/candidate-run-nova/autolog-nova.txt:208) | largest/best xid均17，位置1；desc/data均0 | L215读写挂载成功；L218 `volatile Data` |

**观测事实**是三轮相同初始checkpoint、同一seed、相同mode0配置。结合 `storeMode()` 默认0、每次加载seed、`attachStore()` 只在mode非零时接后端，可确定当前路径每轮重建RAM Data，不保留上一轮的运行时写入。checkpoint一致本身不单独证明“任意一个文件消失”；本次没有开展文件标记跨重启实测，也没有因此量化lsd重建的因果耗时。`volatile Data`这句仅作辅助记录，不能代替参数与实际代码路径的核对。

## 已有接口及调用链

| 能力 | 现成实现与调用者 | 能做什么、不能证明什么 |
| --- | --- | --- |
| 选择Data、接入overlay | [NativeRamBlock.cpp:369](<nova-root>/unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp:369) `selectVolumeSeed()` 按精确UUID选IOMedia、找到NativeRamBlock后调用 `attachStore()`；[kern_exec.c:9164](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:9164) 的 `native_owner_data_prepare()` 调用 `native_ram_block_volume_seed()`，再挂载 `/private/var`。 | overlay接入发生在Data挂载前；UUID和当前seed都必须匹配。 |
| 持久改动页 | [NativeRamBlock.cpp:80](<nova-root>/unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp:80) `attachStore()`，以及 [PersistOverlay.h:69](<nova-root>/unixnu-main-nova/drivers/NativeRamBlock/PersistOverlay.h:69) 的 `Overlay::open/writeRun/forEachPresent`。 | header保存SHNGDATA、页数、精确seed SHA256、generation及CRC；mode2直接把变更页放UFS，之后读overlay页，其余读原seed。它是块后端，不是应用事务或APFS快照接口。 |
| 只读恢复为RAM | `sheng-data-persist=1`；`attachStore()` 在header匹配时通过 `forEachPresent()` 把overlay页恢复进稀疏RAM，`overlay.readOnly=true`、`direct=false`。 | 本轮的后续Data写入继续在RAM，不能写回overlay。因此已有“固定已初始化overlay → 多次相同暖RAM起点”的基础；前提是overlay本身已可信完成。 |
| 物理存储窗口 | [NativeUfs.cpp:1056](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1056) `storeMode()`、`native_ufs_store_open/read/write/sync`。 | mode1只读窗口；mode2开写窗口。写入调用WRITE(10) FUA；创建header时调用SYNCHRONIZE CACHE。能力依赖设备真实完成语义，需要本机验证。 |
| 块层同步 | [NativeRamBlock.cpp:174](<nova-root>/unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp:174) `doSynchronizeCache()/doSynchronize()`。 | 在线时直接成功，依据是“写请求完成前FUA已落盘”。它不会要求lsd/容器服务提交用户态事务，也不是freeze或unmount；mode0仍是易失RAM。 |
| 离线packed编码 | [packed_ramdisk.py:97](<nova-root>/unixnu-main-nova/drivers/DarwinBoot/Test/packed_ramdisk.py:97) `pack()` 与 `inspect()`。 | 可把已经静止的原始逻辑磁盘编码成DBRDISK1并校验完整逻辑摘要；不负责使源文件系统静止。编码上限256MiB/65536 extent；新seed还必须容纳于固定boot封装。 |

窗口备注：源注释仍多处写“userdata”，**实际几何来自 `native-ufs-partition` 所选GPT分区**。本候选是 `unixnu`，见 [RootLayout.h:123](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootLayout.h:123) 和 [NativeUfs.cpp:2141](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:2141)。不能照旧userdata文档写Android数据分区。Data512需要131077个4KiB块，即512MiB+20KiB；默认在分区尾部1GiB保留区之前，且与签名root映像至少隔4MiB。具体LBA和现有内容必须在将来实验前重新核验与备份；本报告没有做设备窗口核定。源码注释所称“至多四分之一分区”没有对应的当前显式比例判断，不能当现成保护。

## 没有现成一致性导出的证据

1. **NativeRamBlock没有freeze、snapshot或导出完整磁盘的方法。** 每次I/O受 `gate` 串行保护，`stop()`只把online置false；这不等于服务已停写、APFS已提交并卸载。稀疏盘 `seedInfo()` 返回base=0及原seed身份，不是当前Data内容的连续内存地址。
2. [gdb-stop.py:1668](<nova-root>/unixnu-main-nova/scripts/qemu-entry/gdb-stop.py:1668) 的现成 `--capture-scratch-data` 只用于暂停的QEMU连续内存范围，报告明确 `guest_flushed=False`、`consistency=paused live block image; filesystem may be unclean`。当前packed Data512的base=0也不满足它的范围检查。既不能直接用于手机，也不能把暂停CPU等同于干净文件系统。
3. [NativeUfs.cpp:1194](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1194) 有256MiB `native_ufs_export_blocks/write` 原始窗口，但在检查的NativeRamBlock/DarwinBoot、板级驱动及活动XNU源码里未找到Data快照调用者。它没有事务边界、freeze、完整性封装或恢复协议；原始Data512还大于该窗口。
4. 通用XNU VFS含unmount入口，但Nova Data准备代码没有把它接成“停止所有Data消费者 → 提交 → 成功卸载 → 导出”的流程；APFS本身也没有在本次范围找到已经接好的Data快照实验接口。一般sync、块层FUA、或者拥有IOLock，都不能替代这一流程。

## 最小实验方案仍缺的能力

优先考虑**同一个原seed + 暖overlay的受控恢复实验**，不急着重新制作seed：

1. 固定当前iOS 16.7.16 / 20H392对应的OS/SystemOS、Data UUID、原seed哈希、配置与内核；用实际lsd/容器完成标志确认初始化阶段结束，不能只以出现Hello作为所有事务已完成的证明。
2. 先把overlay当新存储功能审阅：核定 `unixnu` 内窗口、备份内容、只读检查header，验证FUA/错误传播、重新启动后读取同一页及恢复结果。mode2的blank/stale/damaged会走重建路径，不能把启用参数视为纯读取。现有host模型把成功FUA写当原子完整块，未证明手机断电下多块写/覆盖已有页的原子性；没有双版本完整快照提交。
3. 补齐或证明可靠的**干净关闭**：服务停止写入、数据库提交、APFS同步并成功卸载/正常关机，然后才能把磁盘窗口作为可复用的训练结果。直接物理重启的overlay只能至多作为待恢复的磁盘状态，不能包装成已一致快照。
4. 失败必须闭合：当前mode1 `forEachPresent()` 中途失败可能已把一部分页写进RAM，随后state=4/overlay失活，却仍继续返回设备。最小试验需要验证失败时停止挂载/丢弃全部已恢复页回到原seed，不能把部分恢复结果作为暖基线。
5. 暖基线只读恢复成功后，以mode1重复跑相同Boot起点，保留Hello/Setup逻辑，比较lsd初始化请求数量和耗时；若收益出现，再讨论持久化产品化。mode2每个写入增加UFS/FUA成本，不能预先承诺总启动必然更快。

### 后续最小代码修复范围（本轮未实现）

| 范围 | 最小处理 | 必要验收 |
| --- | --- | --- |
| `NativeRamBlock.cpp` 的 `attachStore()` / `selectVolumeSeed()` | 将恢复结果传播到选盘；mode1读页失败或分配失败后禁止该部分恢复磁盘进入Data挂载。最窄修复为失败即拒绝选盘；若选择回退原seed，必须销毁并从原seed重建整个RAM磁盘，不能仅关overlay。 | 对首批、中途、最后一批读失败及分配失败注入，证明无部分恢复数据被挂载，且mode0原路径保持可用。 |
| `NativeUfs.cpp` 的store窗口开启及 `RootCommands.h` 写边界 | 保留现有分区、root映像+4MiB、地址/长度、日志窗口重叠检查；将当前仅记录的GPT前检结果变成开启持久化的硬门槛；显式固定实验的分区身份和reserve。注释中的¼上限如属设计要求，应补代码与边界测试，否则删掉该虚假保证。 | 精确落在预先确认的 `unixnu` 允许窗口；GPT改变/失败、越界、与日志导出重叠均拒绝。重新核定实际设备布局和备份属于实验前置工作，报告不提供未经核对的刷写LBA。 |
| `PersistOverlay.h` 的 `open()` | 实验应拒绝自动重建未知/损坏/不匹配窗口，只有明确初始化模式才允许清空。重建旧有效overlay时，在改bitmap前先可靠撤销旧header并同步，最后才发布新header；当前“bitmap先写、header后写”在显式reset旧有效header时，若中途断电，旧header仍可能匹配而bitmap已部分清空。 | 覆盖reset每一步失败和恢复、旧seed/新seed、损坏/外来header；在FUA完成语义假设下验证状态机。真实断电原子性仍须设备验证。 |
| Data关闭控制（现有 `native_owner_data_prepare()` 旁的显式实验入口） | 新增“初始化完成→停止Data写入者→提交→APFS成功卸载/正常关机”的控制与明确成功证据；关闭失败不发布暖基线。它不是改一个启动参数可以完成的能力。 | 未完成事务、仍有消费者、sync/unmount失败都阻止发布；成功后mode1恢复，检查卷与关键数据库可用。 |

这些是“可靠暖overlay对照实验”的最小工作包，不是本轮启动优化补丁。把bitmap/数据页做完整性保护、双代快照发布或完整导出，需要单独设计，不能靠现有header CRC、seed SHA或FUA宣称已经具备。最小试验可以先坚持干净关闭和mode1只读基线；不能将未知断电恢复状态当作已验证seed。

若选择**预初始化seed文件**，上述干净结果之外还需要：按逻辑块读取原seed+overlay的合并视图、完整性/代次清单、输出中断不发布半成品、离线只读APFS和卷UUID检查、packed往返逻辑哈希验证，以及明确允许替换Data输入的新封装路径。当前固定打包器刻意锁定旧seed哈希，只替换kernel，不能悄悄替换它。

现有 [seed-data-skip-setup.py:28](<nova-root>/unixnu-main-nova/scripts/seed-data-skip-setup.py:28) 硬编码 `17.7.11 / 21H461`，会改Setup、migration和watchdog偏好，目标与本机当前16.7.16不符；即使其离线操作最后detach，也不是本机已完成LaunchServices初始化的证据。本次不使用它，不以跳过Setup或移植别的iOS seed替代启动优化。

当前顺序：先完成不改Data存储的启动优化实验；暖Data留作后续独立项目，届时先完成上述恢复失败处理、窗口校验与干净关闭，再决定是否制作新seed。现阶段没有可直接刷入且已验证一致的暖Data快照。OS/SystemOS原验证链和现有签名检查继续保持；seed摘要绑定只用于选择正确基础，不是绕开这些验证的理由。
