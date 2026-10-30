# R8 暖 Data 从现有 export 只读加载：窄设计

新文件，无旧版本。只读源码审查；没有实现、构建、挂载、还原或设备操作。生产输入 SHA256 见同目录 `source-provenance-nova.json`。中央 CHANGELOG 由父代理汇总。

**结论：代码路径可实施。最小方案是在 `NativeRamBlock::start()` 发布设备以前，只尝试一次已就绪的原始 export 读取，完成所有校验后才切换到独立暖盘；未就绪或校验失败时发布完整冷盘。不等待 IOMedia、日志窗口、root 挂载或 Data UUID。**

此方案只解决加载时序与字节完整性，不把 R8 块截面认定为干净 APFS，也不证明 LaunchServices 缓存可复用或启动会加速。父代理负责真实 APFS/缓存离线检查，通过后再决定是否制作还原实验。

## 当前实测输入与容量

已读取父代理产出的 `captured-data-validated-nova/validation-nova.json`，没有重新挂载或解析文件系统：

| 字段 | 已验证字节记录 |
|---|---|
| capture ID | 4013558640 |
| logical bytes | 536870912（512 MiB） |
| packed bytes | 231147936（约 220.44 MiB） |
| extents | 152 |
| cold logical SHA256 | `373880d1f64695c6ba71db05354b019cbfe11d253d32427b8d493bcc085981b3` |
| packed SHA256 | `a8ba0692f680ac463463e7640575c1fa75bde86be6c620ba0af00fabcd8547d9` |
| header SHA256 | `685238b9377d64f172c1f89ae3c98ecba09d41a928faa8394c8fec91fffaa90d` |
| warm logical SHA256 | `372549a9fcb74ba062fbd871a370199c7d32657cfa4e436040c120b6dbfcbd43` |

该记录仍明确 `clean_filesystem_proven=false`、`restored=false`。设计将以上值作为候选白名单输入；不是因为文件存在就自动启用。

## 已核实的启动顺序

| 代码位置 | 事实与设计影响 |
|---|---|
| `KernelConfigTables.cpp:98–117` | RamBlock 匹配 `IOResources` + `IOBSD`；UFS 硬件入口匹配 `IOPlatformExpertDevice`，两个 personality 的 score 不构成跨 provider 的先后保证。 |
| `NativeUfs.cpp:1990–2005,2116–2142` | 硬件操作只从 root-device personality 进入；先 bring-up，再 GPT/root manifest；manifestStatus 合格后才填 `gStoreGeometry`。 |
| `NativeUfs.cpp:2143–2205` | 几何填好后仍会验证各 root 窗口的 owner policy/UUID；再调度独立 `publishThread`。所以 `gStoreGeometry.valid` 不是整个 root 准备成功的同步信号。 |
| `NativeUfs.cpp:1866–1911`; `RootPublication.h:5–22,55–71` | root 发布线程依次发布 SystemOS、其他窗口、最后 OS，并等待各自 IOMedia；它没有 Data/RamBlock 依赖。不能把此媒体等待完成当作暖读取前置条件。 |
| `NativeRamBlock.cpp:256–316` | 冷输入解析、导入与 logical SHA 在 `online=true`、`native_ram_block_ready=1`、`registerService()` 之前完成。暖盘的最终选择必须放在这三步之前。 |
| `NativeRamBlock.cpp:438–479` | `selectVolumeSeed()` 先查 UUID 对应 IOMedia，沿 provider 找到 RamBlock，然后才 `attachStore()`。因此 attachStore 不满足“APFS 任何探测前替换”。 |
| `NativeRamBlock.cpp:331–333` | 现有大盘路径也明确说明 APFS 会在 Data mount/attachStore 之前探测卷 UUID。 |
| `kern_exec.c:7771–7813,8297–8303,9715` | `native_ufs_log_open()` 由 autolog dump 打开，autolog 启动在 init 的后期准备路径；启动时不能靠这个日志消费者为暖读取打开窗口。 |
| `IOService.cpp:1031–1083,1107–1113,1141–1153,4930` | `registerService()` 直接进入 matching；同步性还会继承 provider 状态。不能假定无参调用总是异步，也不能在它之后再替换盘内容。 |

当前没有一个已存在的 `native_ufs_root_ready()`/`rootready` 等待接口；`native_ufs_root_step` 是诊断计数，值 3 在 manifest 验证前就写入，不能作为 ready fence。

## 建议的最小改动边界

### 1. 独立的只读 source API，不能借日志/持久化 API 开窗

在 NativeUfs 内增加专用于本次实验的、默认关闭的接口（名称为设计建议）：

- `native_ufs_warm_source_try_open(...)`：仅查询一个已经发布的不可变读取描述符，返回 Ready / NotReady / Refused，**不 sleep、不 bring-up、不查询 IOKit 服务、不打开任何写窗口**。
- `native_ufs_warm_source_read(handle, relative_block, count, dst, budget)`：只允许描述符对应 export 区内部的 4096-byte 块；复用既有 READ(10) 与 128 KiB DMA 上限，逐命令释放 transport lock。调用者不能传入绝对 LBA。

描述符在 `startRoot()` 的窗口准备及 signed identity 验证全部成功后、调度媒体发布以前发布。先填完 immutable 字段，再 release-store 一个独立状态；reader acquire-load。失败分支不得留下 Ready；不能把现在无同步的 `gStoreGeometry.valid` 直接升级为跨线程契约。这个状态只表示“原始传输和已验证 root 几何可供只读访问”，不等于 root 已挂载或 IOMedia 已生成。

可把 ready 的 release-store 放在 `Operations::schedule()` 创建发布线程之前；若 thread creation 失败，应发布失败状态并拒绝后续读。并发已经取得的 reader 仍须每次检查 transport 状态。更严谨的实现可由 `startRoot()` 成功返回前统一发布；两种位置都不得等待 APFS。

只读窗口从当前已验证分区几何求得：

`export_first = partition_last + 1 - 8192 - 65536`；大小固定 65536 × 4096 = 256 MiB。

必须完整验证：分区名精确为 `unixnu`，LUN 为 0；几何无溢出；reserve 至少覆盖 export + 32 MiB log，reserve 小于分区；root image 向上取整后再加 1024 块 guard，仍不超过 `partition_blocks - reserve`；窗口全在分区内且在 READ(10) 可寻址范围。先比较再做减法，避免照搬 `native_ufs_log_open()` 中未经独立 `reserve < partition` 检查的减法。

开窗前后读取 GPT 并与 root 几何的 CRC/目标分区信息核对。新增 reader 使用私有 GPT buffer，避免并发复用现有 `native_ufs_store_gpt_check()` 的静态 scratch。

不要调用 `native_ufs_log_open()`、`hostLogOpen()`、`native_ufs_store_open()`；它们有日志/overlay 状态与写窗口副作用。现有 `native_ufs_export_blocks()` 又依赖 `gLogRegionBlocks`，不足以作早期读取能力接口。

### 2. RamBlock 在 start 内完成一次有界尝试

保留当前冷盘的完整导入和哈希过程，保留冷盘对象不动。在 `online/ready/registerService` 以前执行：

1. 精确开关 `nova-data-warm=1`，且 `nova-data-capture` 关闭、`sheng-data-persist=0`；payload 中不存在可写同一 export 的 GPU writer。新实验不得同时 capture 与 restore。
2. 校验当前冷 seed 的 logical hash 与编译/打包生成的候选白名单，逻辑容量精确一致。
3. 非阻塞 try-open。NotReady/Refused 直接记录原因并走冷盘，**不在 start 内等 UFS、IOResources、IOMedia 或日志线程**。
4. Ready 后只做原始读取、独立内存构建和哈希，不调用文件系统或 IOKit 查询。所有出错路径销毁候选对象，冷对象从未被写过。
5. 成功后一次性把 active backing 换成候选，更新 capacity/resident/身份诊断，释放旧导入页；最后设置 online/ready 并 registerService，恰好一次。原 boot 输入内存仍由 loader/kernel 持有。

这条最小路径不改变 personality 或 root publisher，避免为了试验暖缓存引入新的调度依赖。它可能在偶发源尚未就绪时直接冷启动；日志必须显式输出 skipped-not-ready，不能把该轮当暖盘测速。

### 3. 完整校验和内存隔离

头校验与现有离线 reader 对齐：exact NVDCAP1、version/header size/state/flags、payload offset=4096、已知非零 capture ID、reserved 全零；清空 header hash 字段后计算完整 4096-byte SHA；header 中 cold identity、encoded length、extent count、payload SHA 都与候选白名单一致。不得只验证“头内自带的 hash”，也不得挑选“最新一个看起来有效”的 capture。

运行时不依赖上次日志仍可读取：父代理已用真实成功 ack 验证的 capture ID、header/packed/logical SHA 及 cold identity 应进入候选 manifest/只读常量。这样若 export 后来被改写或存在另一轮 capture，会拒绝。当前 header flag 仍为 BLOCK_CUT_NOT_CLEAN，不得改称 CLEAN。

推荐流式构建一个全新 `RamSparseDisk candidate`：

- 先检查 DBRDISK1 header 和整个 extent 表。逻辑容量必须等于当前 512 MiB，extents <=65536；offset/length 4 KiB 对齐、非零、排序不重叠、无整数溢出、所有 payload 恰好用完。
- 按 128 KiB 块读取 packed payload，同时计算包括 32-byte header、extent table、所有数据在内的 **完整 packed SHA256**。分配的候选页只属于 candidate；不向 cold sparse.pages 复制。
- 检查最后一块 encoded 结尾的 padding 全零；对候选的整个 512 MiB 地址空间计算 **完整 logical SHA256**，空洞按零计入。两种摘要都要等于白名单。
- 结束时再次检查头与 GPT 一致性；只有全部成功才 commit。packed hash 已能拒绝读取过程中混入不匹配内容，头复读用于更清晰地识别源变更。
- staging/解析元数据/候选页都在 publication 前分配；失败释放每一个已经分配的页和页表，不影响冷对象。

现有 `ramSparseImport()` 要求完整连续 packed buffer，且分配失败可留下部分 candidate 页；绝不能把当前 cold disk 直接传给它再期待自动回滚。可以先完整读入 packed buffer、校验、用现有 importer 导入独立 candidate 作为更少解析代码的原型，但会额外占用约 220.44 MiB packed buffer，加约 220.44 MiB 页数据与 1 MiB 指针表。流式路线避免这块双份大缓冲，但必须用真实 fixture/实际 capture 做互操作测试。

身份建议明确分开：`baselineSeedHash` 保存冷 seed 来源，`seedHash`/`native_ram_block_seed_sha256` 表示本次实际发布的 seed logical hash；warm 成功时用 warm logical SHA，fallback 时用 cold SHA。现有 `seedInfo()` 把 seedHash 返回给 `native_owner_data_seed_sha256`，所以不得把暖内容谎报成冷盘字节摘要。warm candidate 强制 persistence 和 capture 关闭，避免旧 overlay seed 绑定语义被混入本实验。

## 循环依赖判定

| 写法 | 判定 |
|---|---|
| start 内先检查独立 Ready 状态，Ready 才直接 READ(10) | 没有新增 IOKit/挂载依赖；底层 hostSubmit 自己轮询硬件完成，不等 IOStorageFamily callback。 |
| start 内等待 `native_ufs_export_blocks()!=0` | 不可用：依赖晚期 autolog 打开日志，形成早期 Data 发布等待后期 init 的关系。 |
| start 内等待 Data UUID 或调用 selectVolumeSeed/attachStore | 自身循环：该 IOMedia 必须先由本 Data 的 registerService 后续探测产生。 |
| start 内等待 root 全部 IOMedia ready 或 waitQuiet | 有 matching 线程/资源依赖风险；root 发布也需要 IOKit 的匹配线程继续工作，不能引入该等待。 |
| 认为 registerService() 无参一定异步，再在返回后换盘 | 错误；源码允许继承同步状态，且异步时也有探测竞争。 |
| 新建 worker 后立即返回 start，随后晚发布 Data | 不是本轮最小方案：init 的 native_owner_data_prepare 对 Data UUID 做即时查询；若 root 先可挂载，worker 未完成会出现 Data ENOENT。需要另加 root-last-publication/Data-ready 协调，不能只加线程。 |
| 给 personality 新加永远不发布的 source-ready 资源 | 会破坏 cold fallback；若未来使用资源门控，必须保证 source 成功和失败都发布 terminal decision，而且无 UFS 时也能冷启。 |

**不要为了本次加载引入 worker 或修改 IOResourceMatch。** 先用 fail-fast 的 source try-open 验证真实排序；如果出现 NotReady，单独处理明确的 source-decision 资源门控设计，不能通过延长 start 内 sleep 来“修好”。

## 回退承诺与时间预算的实际边界

- Header、版本、身份、hash、布局、内存失败：可以在发布以前丢弃候选，发布原冷盘。
- 软件预算到期、尚未提交新命令：可以停止尝试并发布冷盘。记录 read/hash/import 各阶段时长，避免只给总耗时。
- **真实硬件命令 timeout/fatal/protocol 错误：不能承诺本轮还能冷启动。** `RootCommands.h:89–94,124–129` 将共享 `gHost` 标记 poisoned/ready=false；根盘也共用它。保留冷 seed 字节不等于恢复 UFS root 通道，必须保留现有隔离策略，禁止为本优化清 poison 或强行 reset/retry。
- 当前 hostRead4k 每次 READ 上限 1 秒，但 `IOLockLock(gHost.lock)` 没有等待截止时间；128 KiB 分批也不等于总时间有界。建议专用 reader 带软件总预算，取锁改为 deadline-aware try-lock，命令 timeout 取剩余预算与 1 秒的较小值；同时避免在低于完整命令预算时贸然发起会毒化通道的短 timeout。优先在发命令前检查总预算，允许一条已发命令按原 1 秒上限结束。
- 即使如此，普通内存分配/回收和哈希阶段仍可能越过软件预算；不得把检查点预算描述为硬实时 deadline。每批分配、读取、哈希后检查，若要严格墙钟上限需另审分配器和调度，不在这次窄实现中扩大范围。
- **commit/registerService 之后不支持热回退冷盘**：APFS 已可能缓存/改写盘状态。语义不兼容需要下一轮关闭 warm 开关或重新启动，不能原地切回。真实 APFS/LS 离线检查是父代理单独的前置工作。

## 后续最小验收

1. 默认关闭及缺符号、source NotReady、错误分区/几何/GPT、capture 开关冲突均保持原冷路径；不新增 UFS write/sync。
2. 实际 R8 fixture + 捕获数据经过候选导入，packed/logical SHA 与离线 reader 相同；每个解析边界、截断、尾部 padding、篡改和分配失败都在 commit 前回退。
3. Fake source 模拟每一次读失败和中途源改变，确认 cold bytes/hash 完全不变、候选全部释放、恰好一次 registerService，且第一次 block read 前已确定 backing。
4. 源先 ready/后 ready、同线程同步 matching 模型都不等待自身。wrapper 不调用 waitForService/waitQuiet/selectVolumeSeed/native_ufs_log_open。
5. 目标 ARM64 编译、最终 symbol/callgraph 检查：默认关闭、无 export writer 冲突、无捕获/还原同时启用。
6. 实机首先验证日志中的 source-ready/selected=warm、完整 SHA、prepublish end 早于第一次 Data 读，再比较 Boot→苹果和 Boot→Hello 累计时间。若 selected=cold，该轮不能声称已测暖 seed 效果。

本设计新增的启动成本是读取约 220 MiB、构建内存盘并校验 512 MiB logical 内容；它是否能换掉更长的每次缓存重建，必须实测。现有 capture 写入时长不能当作该读取/哈希路线的性能预测。

## 本地证据链接

- [NativeRamBlock start](<nova-root>/unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp:256)
- [NativeRamBlock late attachment](<nova-root>/unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp:438)
- [NativeUfs root setup](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:2116)
- [NativeUfs ordered publication](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1866)
- [Existing log/export API](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1151)
- [READ and quarantine policy](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootCommands.h:89)
- [IOService matching semantics](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/iokit/Kernel/IOService.cpp:1031)
- [Data mount lookup](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:9376)
- [R8 validated capture record](<nova-root>/artifacts-nova/boot-speed-round8-nova/captured-data-validated-nova/validation-nova.json)
