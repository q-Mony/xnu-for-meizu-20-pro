# sheng 上游存储代码研究 — Nova

日期：2026-10-30（Asia/Shanghai）。范围：UFS、根盘认证读取、缓存、RAM Data 持久化与暖启动。仅下载和静态分析；未执行下载代码、未改生产源码、未编译候选、未操作手机。初建无旧版本；补充板级 gitlink 身份前的稿件保存在本目录 `snapshot-nova/REPORT-before-board-identity-nova.md`。

**结论：上游最值得选择性引入的是 RamBlock 的“仅明确初始化才重建 overlay”修复；它是安全复用 Data 的前置条件，不会直接加速当前关闭持久化的启动。没有发现可直接移植、能消除剩余约百秒等待的新版 UFS 传输优化。** 下一步仍应优先完成父会话的 lsd/用户态等待定位，再把暖 Data 作为独立受控实验。不能整份替换 ShengUfs 或 VerifiedRoot。

## 1. 输入、固定版本与结论适用范围

研究入口为用户给出的 sheng 设备页，其源码记录提交为 `08cde20edc3254ed6f7df8d0f5384fb887786e8f`；页面 UFS 证据链接固定在 `e2b32cc429e8cf1501161c1d8c517f8994ceb9b2`。但该板级提交实际的 `drivers/ShengUfs` gitlink 已是 `f2d6a4a72f5e7be7d2c9b78eba16cf373868fa30`，恰与本次抓取仓库 HEAD 相同。必须区分“页面引用的功能证据”和“板级构建固定的子模块”，不能把新增代码描述成板级提交之后才存在。依据：[页面副本](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/sheng-device-page-nova.html)、[服务代理保存的板级 Git tree](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-tree-nova.json)。设备页的“支持”清单不能替代本机跨重启、故障恢复或性能验收。

抓取发生于 2026-10-29 16:54 UTC（本地 2026-10-30 00:54）。下表的 HEAD 是抓取当时解析到的提交，不宣称之后不再变化。归档及逐文件 SHA256、来源 URL、获取时间保存在 [pinned-source-manifest-nova.json](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/pinned-source-manifest-nova.json)，初始 API/页面来源在 [initial-fetch-manifest-nova.json](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/initial-fetch-manifest-nova.json)。解包拒绝链接/设备节点，仅保存普通文件；没有运行上游测试。

| 仓库/来源 | 固定提交 | 用途 |
| --- | --- | --- |
| UniXNU/ShengUfs，设备页所引 | `e2b32cc429e8cf1501161c1d8c517f8994ceb9b2` | 旧的页面功能证据链接 |
| UniXNU/ShengUfs，板级 08cde20e gitlink，亦为抓取时 HEAD | `f2d6a4a72f5e7be7d2c9b78eba16cf373868fa30` | 实际板级固定代码；UFS 与附属 SMMU 差异审阅 |
| UniXNU/RamBlock，抓取时 HEAD | `4be2d7eb05ef5a98126c91e0302c149140c0e3e1` | 2026-10-29 显式初始化修复 |
| UniXNU/VerifiedRoot，抓取时 HEAD | `def026642c6c6e71be0dea267d989df1d29ddc64` | 认证单位与 SHA provider 兼容性 |

本地对照来自 Nova 工作区：主仓库提交 `6dccadbd72b1840ee7f637f73be5d57a576f723d`，NativeUfs 子仓库基底 `af785c1777161a62931425c2faaa536ce0bebd5b`，NativeRamBlock 基底 `d4d22d9819b0162eda49f6402235dddc2279410d`。实际比较的是含既有 Nova/继承改动的工作文件，不把这些未提交改动误归为上游变化；本次没有重置任何仓库。

## 2. 与启动耗时的关系

父会话第四轮报告中，包围 lsd 等待的 **101.616 秒**窗口，根盘读取计时增量 **1.627591 秒**、认证 **0.244891 秒**、复制 **0.024225 秒**。这只覆盖已埋点的驱动执行时间，不能独自排除文件系统、锁、调度或用户态等待；但已经不支持用“把根盘每次读得更大”解释或消除余下百秒。[第四轮报告，第 56 行](<nova-root>/artifacts-nova/boot-speed-round4-nova/REPORT-nova.md:56)

用户已确认第五轮 Boot→苹果 **73.03 秒**、Boot→Hello **220.56 秒**；本子任务未读取第五轮真机日志，不能从总时间反推 E1 刷新成本、UFS 性能或 lsd 内部分段。该轮归因由父会话的新日志分析决定。

本地 Data512 当前每轮使用相同 seed，实际成功配置未启用 `sheng-data-persist`，第二至第四轮重新挂载相同初始 checkpoint xid17。第四轮 lsd 栈包含数据库建立、应用扫描和容器查询，因此“保留初始化结果可减少后续工作”是值得测的假设，尚未取得因果收益数据。依据：[Data 可行性报告](<nova-root>/artifacts-nova/boot-speed-round4-nova/device-analysis-nova/data-seed-feasibility-nova.md:13)、[lsd 分析](<nova-root>/artifacts-nova/boot-speed-round4-nova/device-analysis-nova/lsdipc-analysis-nova.md:78)。本报告新增的是上游修复及兼容性核对，不重复宣称已有可靠暖快照。

## 3. 可迁移项：RamBlock 的显式初始化策略

上游 2026-10-29 提交增加 `Open::Kept`。当 overlay 为空、损坏、大小或 seed 身份不匹配，普通读写启动不再自行覆盖旧窗口：只清零内存 bitmap，返回 Kept，物理存储保持原状；只有明确 reset 才建立新 header/bitmap。同 seed 有效 overlay 在普通启动仍可 Resumed；只读探测保持 Blank/Resumed 语义。主要实现：[PersistOverlay.h，第 69 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/RamBlock-head-nova/PersistOverlay.h:69)、[初始化分支，第 98 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/RamBlock-head-nova/PersistOverlay.h:98)。

调用方增加状态 7；Kept 时关闭 overlay，继续以原 seed 的 RAM Data 启动，并提示通过 Install and boot 显式初始化。`sheng-data-reset=1/2` 才请求 reset。[NativeRamBlock.cpp，第 79 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/RamBlock-head-nova/NativeRamBlock.cpp:79)、[Kept 处理，第 112 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/RamBlock-head-nova/NativeRamBlock.cpp:112)

适用价值：更换 seed、遇到损坏头或选择了不同 reserve 时，普通启动不应自动重建现场。可选择性引入这段策略和状态输出；需同时检查枚举消费方，不能只复制头文件。当前 mode0 根本不打开 overlay，所以这项改动单独不会缩短本轮 Hello 时间。

### 修复尚未覆盖的两个具体失败路径

**A. 恢复一部分后出错，RAM Data 没有回到原 seed。** mode1 的 loader 逐页分配/覆盖 `sparse.pages`；后续读或分配失败时仅将状态置 4、overlay.active 置 false，已复制页面不回滚，`attachStore()` 不把失败传递给选盘。因此注释所称“失败后等同纯易失 seed”并非该路径的保证。根文件系统/数据库能否容忍这种混合状态未验证；不得作为暖启动基线。[NativeRamBlock.cpp，第 125 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/RamBlock-head-nova/NativeRamBlock.cpp:125)

最窄修复方向：恢复失败即拒绝该 Data 选盘/挂载；若产品需要回退，必须销毁整份部分恢复结果并从原 seed 完整重建。只关闭 overlay 不够。验收必须覆盖首批、中途、最后一批读失败和页面分配失败。

**B. reset 旧有效 overlay 时，bitmap 先变、旧有效 header 仍在。** reset 分支只在内存中把 `header` 分类改为 3；随后逐块清 bitmap、sync，最后才写新 header。若当前介质已有同 seed 的有效头，清掉第一个 bitmap 块后第二次写失败/掉电，下次可仍接受旧头及混合的新旧 bitmap。页级混合会丢失部分既有改动，不能称为已原子完成的重置。这是源码路径推演，不是本次真机复现。[PersistOverlay.h，第 89 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/RamBlock-head-nova/PersistOverlay.h:89)

最窄修复方向：清 bitmap 之前，先持久撤销旧有效 header 并确认同步；之后初始化 bitmap，最后发布新有效 header。对每一步失败/重启做状态机验收；这种单头方案仍依赖实际介质写完成语义，不能据此宣称实现了任意断电下的原子快照。

上游测试已新增 blank/stale/damaged 的 Kept 与显式 reset、同 seed 成功 reset。现有“初始化失败后无有效头”用例从全新 junk 介质开始，未覆盖旧有效 overlay 的 reset 中断；模型 read 总返回成功，也没有 mode1 内存分配/部分恢复失败用例。本次只读测试源码，未运行。[Test/persist.cpp，第 12 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/RamBlock-head-nova/Test/persist.cpp:12)、[第 39 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/RamBlock-head-nova/Test/persist.cpp:39)、[第 91 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/RamBlock-head-nova/Test/persist.cpp:91)

### 仍缺的完整闭环

新提交没有增加 clean shutdown/unmount、Data freeze、快照或完整逻辑盘导出。UFS GPT 前检仍在调用方被记录，但没有作为 `attachStore()` 继续打开窗口的硬门槛。[NativeRamBlock.cpp，第 94 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/RamBlock-head-nova/NativeRamBlock.cpp:94)

现有 mode2 将写入送 UFS overlay，会增加 FUA 写成本；mode1 可将既有 overlay 恢复到 RAM，再让本轮写入留在 RAM，适合固定暖起点的性能对照。首先必须获得明确完成初始化并干净关闭的可恢复状态，不能把看见 Hello 或强制重启等同于干净快照。其余缺项及窗口边界证据见 [Data 可行性报告](<nova-root>/artifacts-nova/boot-speed-round4-nova/device-analysis-nova/data-seed-feasibility-nova.md:39)。

## 4. 不应整体覆盖 UFS／VerifiedRoot

| 合约 | 上游固定快照 | Nova 实际路径 | 迁移要求 |
| --- | --- | --- | --- |
| 根盘 GPT 分区 | RootLayout 固定查 `userdata` | 支持 `native-ufs-partition`，成功配置为 `unixnu` | 保留 Nova 分区选择及实际布局校验，不能照抄 userdata 假设 |
| 认证单位 | VerifiedRoot 与读取循环为 4096 字节 | `VR_BLOCK_BYTES=16384`，读取按签名窗口相对对齐 | 保持 16 KiB 合约；如有意迁移块大小，必须连签名清单/工具/镜像一起设计 |
| 物理传输上限 | 128 KiB | 128 KiB | 上游没有更大的新传输路径可直接采用 |
| 数据 SHA | 使用 `vr_xnu_crypto` | 已分离 `vr_xnu_data_crypto` 与原策略验证 provider | 保留数据加速及策略/清单原验证链 |
| 根身份接入与诊断 | 无 Nova 补充的一组 owner-root 接入和细分统计 | 含授权/清单桥接、请求/字节/read/verify/copy 统计 | 不可因同步上游丢失本机启动所需接入与测量能力 |

对应一手源码：

- 分区：[上游 RootLayout.h，第 106 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/ShengUfs-head-nova/RootLayout.h:106)；[Nova RootLayout.h，第 27 行](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootLayout.h:27)；[Nova 启动参数，第 1995 行](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1995)。
- 认证：[上游 verified_root.h，第 12 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/VerifiedRoot-head-nova/include/verified_root.h:12)；[Nova verified_root.h，第 12 行](<nova-root>/unixnu-main-nova/kits/VerifiedRoot/include/verified_root.h:12)；[Nova 批量读取，第 23 行](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootWindowRead.h:23)。
- 传输与 SHA：[上游 RootTransportLayout.h，第 7 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/ShengUfs-head-nova/RootTransportLayout.h:7)；[Nova provider，第 25 行](<nova-root>/unixnu-main-nova/kits/VerifiedRoot/adapters/xnu_crypto.c:25)；[Nova 数据调用，第 1734 行](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1734)。
- 根身份：[Nova owner-root 接入，第 1606 行](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1606)。

上游 VerifiedRoot `src/verified_root.c` 与 Nova 文件一致，差异主要在头文件块大小、工具及加密适配器；没有找到新的校验核心算法可移植。ShengUfs 页面固定版与抓取 HEAD 的 31 个文件中，仅 `NativeUfs.cpp` 变化；`RootWindowRead.h`、队列、传输布局与测试均未变化。所检查路径没有新加入的跨请求认证数据缓存。

根盘预读或“全盘一次读完”不是现成的上游开关：当前传输和 bounce 有 128 KiB 上限，整盘预载还会引入内存占用及未使用数据的读取/校验成本。若以后研究缓存，须先记录重复逻辑块的比例、命中潜力和内存预算；缓存必须保存已认证的字节快照并绑定窗口/策略身份。只记“某 LBA 验过”后对新读入字节免验，会破坏当前认证语义。现阶段已测量耗时不支持把它列在 lsd 问题之前。

另有一个待核对的细节：上游发生短 copy 时仍将实际 copied 加入完成字节数，Nova 仅在整段复制成功时增加。它可能影响错误请求的完成字节语义，不能凭差异就定性为启动 bug；应独立核对 IOKit completion 合约及故障路径，保持现有“整批认证后才暴露”边界。[上游 RootWindowRead.h，第 47 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/ShengUfs-head-nova/RootWindowRead.h:47)、[Nova 第 61 行](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootWindowRead.h:61)

## 5. 新增硬件代码的用途与本机边界

ShengUfs 新代码主要是三组附属功能，并非 UFS 顺序读取加速：

| 新代码 | 位置 | 本机移植前提 |
| --- | --- | --- |
| Wi-Fi endpoint 的独立 SMMU stream，SID `0x1401`，32-bit IOVA 对应高物理地址，约 48 MiB 有效窗口 | [NativeUfs.cpp，第 1023 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/ShengUfs-head-nova/NativeUfs.cpp:1023) | 本机 DT、SID/CB、寄存器范围、地址位宽、缓存一致性需实证；不能复用 sheng 数值即视为兼容 |
| DPU SID `0x1c00/0x1c02` 的状态探测与可选 stage1 映射，寄存器回读、TLB、故障回退 | [第 1116 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/ShengUfs-head-nova/NativeUfs.cpp:1116)、[第 308 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/ShengUfs-head-nova/NativeUfs.cpp:308) | 本机显示控制器与 boot framebuffer 映射需重新确认；驱动能力工作与当前启动瓶颈分开验收 |
| 按 GPT 名只读查找/读取其他分区，以获取设备固件 | [第 1370 行](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/sources-nova/ShengUfs-head-nova/NativeUfs.cpp:1370) | 核定本机 LUN/分区名称与只读边界；接口存在不证明 Wi-Fi/DSP 固件已适配 |

这些新增路径受启动参数门控；Wi-Fi 路径还会进行逐页翻译检查、清理映射缓存，显示路径有 50 ms 故障观察。不能在未验证板级条件时启用来尝试提速。上述位置供相关硬件子任务继续审阅，本报告没有判断它们在本机可用。

## 6. 建议开发项及验收顺序

| 次序 | 开发项 | 最小验收 | 可宣称收益的条件 |
| --- | --- | --- | --- |
| 1 | 父会话继续第五轮日志归因及 lsd/容器扫描定位 | 对齐累计 Boot 计时、稳定 generation/阶段、同源计时区间 | 确认具体等待或工作量下降，避免把采样差异当优化 |
| 2 | 选择性移入 Kept/显式初始化策略 | blank/stale/damaged 不写；same-seed 正常恢复；明确 reset 才重建；mode0 不变 | 属于保留数据正确性，单独不承诺提速 |
| 3 | 修复部分恢复失败与 reset 中断 | 首/中/末读取及分配失败不挂载混合盘；旧有效 overlay reset 各步中断后拒绝半成品 | 允许后续受控实验，不等于任意掉电保护已完成 |
| 4 | 强化 store 打开前的 GPT/精确窗口检查 | `unixnu` 身份、当前布局、root+guard、日志/export reserve 均通过；失败不开窗口 | 防止暖实验落入错误位置，仍无直接性能承诺 |
| 5 | 建立可证明完成的初始化与干净关闭流程 | 关键服务/数据库提交、Data 消费者停止、APFS 成功关闭；失败不发布暖基线 | 获得可以复用的已初始化状态 |
| 6 | 固定 seed+暖 overlay 的 mode1 对照 | 冷 seed mode0 与只读恢复 mode1 分别重复；相同 boot 起点、内核、OS/SystemOS、配置；记录 restore 时间、lsd 扫描量、苹果与 Hello 累计时间 | 扣除恢复成本后，稳定降低总时长才接受优化 |
| 后续 | 认证缓存/更大 I/O、Wi-Fi/DPU/固件访问 | 分别用命中统计或板级硬件证据开题 | 当前不作为消除百秒等待的捷径 |

暖实验必须保留 Hello/Setup 功能与根验证链，不以禁用初始化业务或换用不匹配 iOS seed 制造表面提速。物理窗口、备份与设备状态由父会话在真正设备实验时核定；本报告没有提供未经核实的写入地址。

## 7. 本次验证与交付

完成静态逐项差异审阅、版本固定和归档/逐文件哈希核对；4 个归档与 86 个解包源码文件均匹配清单，报告本地链接及行号范围通过检查。未执行上游测试，未验证任何新路径在手机上成功。两条恢复故障是明确代码路径推演，性能收益仍待受控实验。研究目录只新增文档、稿件备份、下载来源元数据与只读源码快照；请父会话将本目录及本报告登记到 Nova 中央变更记录。
