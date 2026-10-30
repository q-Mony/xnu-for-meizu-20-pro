# m2381 三仓近期更新复核 — Nova 研究报告

- 任务：检查 `RiverKawa271828/ios-m2381-unixnu` 及其拆出的设备仓、UEFI 仓最近更新，判断能否改善 Nova 魅族 20 Pro XNU 的启动速度或解决现有 bug。
- 复核时间：2026-10-30（Asia/Shanghai）。输入为 `artifacts-nova/m2381-upstream-review-nova/` 下保存的 GitHub API 记录与 `staged-sources-nova.json` 绑定的三份源码；源码提交固定为下文表中的 SHA。
- 方法：只读源码/文档/API 记录，定点 diff 本机生产树；未执行任何仓库脚本、submodule 初始化、构建、安装、刷机、切槽、重启、USB/ADB/Fastboot 操作，未读取密钥/凭证/设备私有身份。
- 结论标注：**[观察]** = 源码或保存记录直接可见；**[推断]** = 由观察推导；**[未知]** = 证据不足。

## 0. 结论摘要

1. **[观察]** 三仓最近一次提交都在 2026-10-29 凌晨（+08），最新主仓 `82d8dc7` 与设备仓 `8c81849` 是同一时刻；UEFI 仓 `5297334` 早 18 分钟。最近这批提交的主体是“把设备仓和 UEFI 仓拆出去/收编”以及注释换线，不是内核、驱动或启动耗时补丁。
2. **[观察]** 真正新增的可执行代码很少：设备仓多了充电栈的 25 个源文件（r80，但未接线），UEFI 仓多了 `m2381Pkg`/BootShim 变体和一个未加入 FDF/DXE 的 `UfsWakeDxe.c`。没有可引用的 AMP 调度补丁，也没有 lsd/容器初始化的新修复。
3. **[观察]** 目标硬件是魅族 20 Classic `m2381`（SM8550、16 GB、1080×2400 BOE）；本机是魅族 20 Pro `M2391`（SM8550、约 12 GB、1440×3200 Samsung）。同 SoC、不同板；对方 `boot_b` 放 UEFI、`recovery_a` 放 ESP、整个 `userdata` 做 iOS root 的布局不能套到本机仍保留 Android B 的 A/B + `unixnu` 分区布局。
4. **[观察]** 在本任务固定的三个提交及保存的公开记录中，未找到对方到达 Hello/桌面的证据；最新已记录实机结果是主仓 r78b 的 logo 1:04、进度条 1:20 冻结，设备仓 README 仍停在 r55e，records 最后一轮 r78b；r79 改回 iOS 16，r80 因设备 8% 电量先做充电且只完成“半接线”。这只说明本次研究范围，不能推断对方有未公开的新运行状态；这与本机历史已到 Hello（R6）不是同一进度，不能把对方 logo/进度条时间当作启动提速对照。
5. **[观察]** 没有直接修复本机 warm policy `e22` 的上游证据。对方 `BootWatchdogPolicy.c` 甚至不检查目录属性；本机 R11 明确失败在 `CHECK_DIRECTORY`/`op=9`，R12 只加了实际 mode/uid/gid 日志且尚未安装。R12 的 stat ABI 复核已排除 mode/uid/gid 偏移布局假设；实际 mode/owner 或 APFS 呈现仍未知。必须等 R12 的真实目录属性，不能靠删谓词或抄对方代码“修复”。
6. **[推断]** 对本任务最有价值的不是“拿一个上游提速补丁”，而是两条低优先级的旁证：`sheng-lite`/`sheng-disable-jobs` 的启动期作业裁剪清单，以及 DSP 电池管理消息编解码。但前者被本机 `e22` 挡在 policy 之前，后者在对方也未接线；本轮都不应直接进生产。

## 1. 三个仓库的最近提交与“真正新增”

### 1.1 固定 SHA 与时间

保存的 API 记录中，提交时间字段为 UTC；按 Asia/Shanghai 换算如下。

| 角色 | 仓库/分支 | 固定提交 | 提交时间（UTC） | 提交时间（+08） | 父提交 |
|---|---|---|---|---|---|
| 主工作区 | `RiverKawa271828/ios-m2381-unixnu`，`m2381` | `82d8dc7d0670106a254532ca88adda9b79246f73` | `2026-10-28T21:59:40Z` | **2026-10-29 05:59:40** | `9f1a6ff9…` |
| 独立设备仓 | `RiverKawa271828/UniXNU-m2381`，`main` | `8c818496611300f2672dd874dd9b59482d485013` | `2026-10-28T21:59:40Z` | **2026-10-29 05:59:40** | `b8a3eaf…` |
| 独立 UEFI 仓 | `RiverKawa271828/uefi-m2381`，`main` | `5297334c0a8f57495a6b32cf1eb52bac370dda35` | `2026-10-28T21:41:42Z` | **2026-10-29 05:41:42** | `0eb4514…`（上游 uefi-sheng） |

时间证据：`commits-api-nova.json:3,9`、`device-nova-latest-commit-api-nova.json:3,9`、`uefi-nova-latest-commit-api-nova.json:3,9`。主仓 `repository-api-nova.json:69-70` 另记 `updated_at=2026-10-28T22:19:51Z`（+08 06:19:51）、`pushed_at=22:19:23Z`（+08 06:19:23），晚于最后一次提交，可能是同一次推送后的仓库更新时间，不另算一次提交。

对应的 GitHub 固定提交页：

- 主仓：https://github.com/RiverKawa271828/ios-m2381-unixnu/commit/82d8dc7d0670106a254532ca88adda9b79246f73
- 设备仓：https://github.com/RiverKawa271828/UniXNU-m2381/commit/8c818496611300f2672dd874dd9b59482d485013
- UEFI 仓：https://github.com/RiverKawa271828/uefi-m2381/commit/5297334c0a8f57495a6b32cf1eb52bac370dda35

### 1.2 主仓最新提交做了什么

**[观察]** 最新提交的 file list 已核验：只改 `AGENTS.md`、`README.md`、`docs/STATE.md`、`docs/UPSTREAM.md`（`main-latest-commit-detail-nova.json:43-89`）。message 自述是：用新仓 `uefi-m2381`（上游 uefi-sheng `0eb4514` + overlay `5297334`）替代旧 fork 改名方案；把 mars Binaries `036ba9f7` 从实据树收编；并续注 `AGENTS`、`UPSTREAM`、`README`、`STATE`。提交信息见 `commits-api-nova.json:16`；同一提交在源码树中的落点见 [AGENTS.md#L3-L19](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/AGENTS.md#L3-L19) 与 [docs/STATE.md#L8-L19](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L8-L19)。这属于仓库编排/文档变更，不声称改内核或启动路径。

**[观察]** 主仓最近的代码/施工记录在 r79/r80：

- r78b 是设备仓最后一轮已判读的实机结果：`wait-standin` 全 0、sel6 Busy 族未解、屏面 logo 1:04/进度条 1:20 冻结（[STATE.md#L615-L630](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L615-L630)）。
- r79 拍板停 `=y` 主屏注册链，改回 iPhone10,3/iOS 16.7.16/20H392；理由是 A11 经典 arm64 kernelcache 无需 fileset 重绑定器（[STATE.md#L632-L644](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L632-L644)）。
- r80 只把 25 个充电栈源文件拷入设备仓 `package/`，产出 ADSP 固件 squash 产物；明确“未接线”：构建脚本、调用点、args、boot_b 追加段均为零构建影响（[STATE.md#L646-L651](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L646-L651)；提交信息 `commits-api-nova.json:142`）。
- AMP 仍是“已拍定抄”的计划项，不是已落地代码：`zzyf-sheng-amp.patch + dts cluster-type 注记，m2381 patches/xnu/series 现为空文件直接加`（[STATE.md#L650](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L650)）。

**[观察]** 主仓的 `devices/m2381` 已退役，设备仓和历史路径映射写在 `STATE` 顶部；三仓格局与 `UPSTREAM` 的 pin 表一致（[docs/UPSTREAM.md#L7-L18](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/UPSTREAM.md#L7-L18)）。没有发现第四个“隐藏”的近期代码仓；旧 `device-m2381` orphan 分支只是冻结回退镜像。

### 1.3 设备仓最新提交做了什么

**[观察]** 设备仓最新提交的 file list 已核验：只改 `board.toml` 的 7 行（4 additions / 3 deletions，`device-latest-commit-detail-nova.json:43-53`），内容是 `[firmware]` 注释终版（checkout 指向 `../uefi-m2381`），不是驱动或策略代码。

**[观察]** 但设备仓树中包含 r80 搬运过来的充电资产：

- `package/AdspProbe.cpp`、`AdspFirmware.c`、`BatteryProbe.cpp`、`ShengBattery.cpp`、`ShengBattmgr.c/.h`、`QcomGlink/Qrtr/Smem/Smp2p/Qmp/Rpmh/Mdt` 等（`package/` 文件列表）。
- 这些文件没有被写进 `[kernel] archives`：[board.toml#L113-L170](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/board.toml#L113-L170) 中没有 battery/ADSP 构建项；`kernel_args`（[board.toml#L286-L315](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/board.toml#L286-L315)）也没有 `sheng-adsp=2` 或 `sheng-battery=1`。
- 因此只能算“源码/设计资产”，不能算对方已具备充电或电池遥测能力。

**[观察]** 设备仓 README 的状态仍停在 r55e（“full iOS chain runs … currently iterating SpringBoard text fixups against a displayIdentity assertion”，[README.md#L15-L21](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/README.md#L15-L21)）。README 没有随 r62–r78 更新；不能把它当最新进度，也不能把旧的 173 进程描述当成 Hello/桌面证据。

### 1.4 UEFI 仓最新提交做了什么

**[观察]** `5297334` 是“把实据树 overlay 收编”的提交，内容集中在 `Platforms/Meizu/m2381Pkg`、`Resources/Configs/m2381.toml`、`BootShim` 变体和三处 Silicon 文件。提交信息见 `uefi-nova-latest-commit-api-nova.json:16`。

**[观察]** 本机 `uefi-sheng-nova` HEAD 已由 Sol 核对为 `0eb4514db0744b8136a3897ab1af93b713c2c27b`，与这里引用的上游基座一致；因此本报告不再扫描整个本机 UEFI 树，UEFI 侧仍只保留 staged partial snapshot 的范围限制。

**[观察]** 与上游 uefi-sheng `0eb4514` 对比：

- `SimpleFb.c`、`RamManager.c`、`IortDtDxe.c` 的实际差异只有调试打印 `M2381>> ... ENTRY/EXIT`；没有显示初始化、内存管理或 IORT 逻辑变化。`SimpleFb.c` 的入口打印见 [SimpleFb.c#L259-L262](https://github.com/RiverKawa271828/uefi-m2381/blob/5297334c0a8f57495a6b32cf1eb52bac370dda35/Silicon/Silicium/SiliciumPkg/Drivers/SimpleFbDxe/SimpleFb.c#L259-L262)。
- `BootShim.S` 增加了 Gunyah watchdog disable 的 SMC 分支（[BootShim.S#L22-L36](https://github.com/RiverKawa271828/uefi-m2381/blob/5297334c0a8f57495a6b32cf1eb52bac370dda35/BootShim/BootShim.S#L22-L36)），但主仓的 `bootimg.py` 编译 shim 时只传 `FD_BASE`、`FD_SIZE`、`REQUIRES_KERNEL_HEADER`，没有传 `DISABLE_GUNYAH_WATCHDOG`（[bootimg.py#L23-L30](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/scripts/unixnu/bootimg.py#L23-L30)）。配置里的 `disable_gunyah_watchdog=true`（[m2381.toml#L4-L6](https://github.com/RiverKawa271828/uefi-m2381/blob/5297334c0a8f57495a6b32cf1eb52bac370dda35/Resources/Configs/m2381.toml#L4-L6)）在这条构建路径上不会生效。**[推断]** 该功能即使在 m2381 也处于“源码存在、构建未接线”状态。
- `UfsWakeDxe.c/.inf` 是新文件，但没有出现在 `m2381.fdf`、`DXE.inc` 或 `APRIORI.inc`：FDF 只列 RAM Manager、IORT、SimpleFb 等（[m2381.fdf#L82-L106](https://github.com/RiverKawa271828/uefi-m2381/blob/5297334c0a8f57495a6b32cf1eb52bac370dda35/Platforms/Meizu/m2381Pkg/m2381.fdf#L82-L106)），DXE 里是 OEM `UFSDxe` 而非 `UfsWakeDxe`（[DXE.inc#L49-L82](https://github.com/RiverKawa271828/uefi-m2381/blob/5297334c0a8f57495a6b32cf1eb52bac370dda35/Platforms/Meizu/m2381Pkg/Include/DXE.inc#L49-L82)）。**[推断]** 它未被编进当前 FV，不能算“已实现 UFS 唤醒”。
- `m2381.dsc` 的面板参数固定为 1080×2400（[m2381.dsc#L57-L66](https://github.com/RiverKawa271828/uefi-m2381/blob/5297334c0a8f57495a6b32cf1eb52bac370dda35/Platforms/Meizu/m2381Pkg/m2381.dsc#L57-L66)），与本机 1440×3200 面板不一致。

**[观察/未知]** staged UEFI 是 `partial_snapshot: true`，只包含最新提交触及的文本文件加 `m2381Pkg`/mars 板文件，固件/依赖 blob 未纳入（`staged-sources-nova.json:20-24`）。因此本报告能判断“这些文件的接线”，不能判断完整 FV 是否另有外部构建脚本把 `UfsWakeDxe` 或 `disable_gunyah_watchdog` 接进去。

**[范围说明]** 本任务指定的“三仓”是主工作区、独立设备仓、独立 UEFI 仓。它们的依赖仓——上游 `UniXNU/UniXNU`（pin `f85b5a6`）、`UniXNU-sm8550-common`（feed `23b2049f`）、上游 `uefi-sheng`（`0eb4514`）、sheng 设备仓——不在这三份固定 SHA 的 staged 源码内；本报告没有把它们的新提交算作“本次更新”。唯一例外是 STATE 引用的 sheng AMP 计划（`76780d7`），已按“未取原文、不算已交付”处理。

## 2. 目标硬件与板级差异

### 2.1 同 SoC，不是同型号/同板

| 项 | 对方 m2381 | 本机 Nova | 证据 |
|---|---|---|---|
| 商业型号 | 魅族 20 Classic | 魅族 20 Pro | [m2381 board.toml#L1-L10](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/board.toml#L1-L10)；本机 [README.md](<nova-root>/unixnu-meizu20pro-nova/README.md:41-45) |
| DT machine/model | `Meizu M2381(kalama MTP)`；DTS `meizu,meizu-20` | `Meizu M2391 (kalama MTP)`；`meizu,board-id=<0x2 0xf>` | [HARDWARE-m2381.md#L3-L5](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/HARDWARE-m2381.md#L3-L5)；[dts/m2381.dts#L24-L29](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/dts/m2381.dts#L24-L29)；本机 README L43 |
| SoC | SM8550（kalama），8 核 1×X3+2×A715+2×A710+3×A510 | SM8550（kalama），8 核 | [HARDWARE-m2381.md#L10-L14](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/HARDWARE-m2381.md#L10-L14)；本机 README L41 |
| RAM | 16 GB LPDDR5X | 11501072 kB（Linux MemTotal，约 10.97 GiB；12 GB 级设备） | 同上；本机 README L42 |
| 面板 | 1080×2400 BOE AMOLED，cmd-mode DSC | 1440×3200 Samsung AMOLED，cmd-mode DSC，120 Hz | [HARDWARE-m2381.md#L15-L15](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/HARDWARE-m2381.md#L15-L15)；本机 README L45 |
| DSI | 未在 m2381 硬件表列出；m2381Pkg 固定 1080×2400 | ctrl0 `0xae94000`、ctrl1 `0xae96000` | 本机 README L46；m2381 dsc L57-L66 |
| 触摸 | Goodix GT9916（berlin），GENI I²C4 `0xa90000`，0x5d | 同为 Goodix Berlin D / GT9916，I²C5 `a90000`，0x5d，但坐标 1440×3200 | [HARDWARE-m2381.md#L53-L64](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/HARDWARE-m2381.md#L53-L64)；本机 README L48-L50 |
| 存储/UFS | Samsung KLUFG8RHHD-B0G1 512 GB，控制器 `0x1d84000` | 未在 README 固定型号；ADT/注释同样使用 UFS `0x1d84000` | m2381 HARDWARE L43-L51；本机 [board.toml](<nova-root>/unixnu-meizu20pro-nova/board.toml:268-270) |
| 充电/PMIC | PMK8550 + SPMI，B2 阶段未碰 | PM8550/PM8550B 面板 rail；电池后端未实现 | m2381 HARDWARE L18；本机 README L47 |

**[观察]** 两者只可证实“同为 SM8550/kalama、8 核、同一 UFS 控制器地址族、同一触摸控制器族”。面板、RAM 容量、商业型号、DT model/board-id 不同。

**[推断]** 因此 m2381 的 UEFI 帧缓冲常量、面板时序、ADT 产品身份、内存窗口不能按“同 SoC”直接复制；能复用的最多是 SoC 级 GIC/UFS/CPU 常量和驱动结构，而且仍要按本机 DT 核实。

**[未知/本机待核对]** 本机 `meizu20pro.dsc` 仍把 SimpleFb 设成 1080×2400（[meizu20pro.dsc](<nova-root>/uefi-sheng-nova/Platforms/Meizu/meizu20proPkg/meizu20pro.dsc:76-87)），而本机实测面板是 1440×3200（[README.md](<nova-root>/unixnu-meizu20pro-nova/README.md:45)）。这说明本机显示交接自身还有一个待核对项；它不影响“不能把 m2381 的 1080×2400 常量当成已适配本机面板”的结论。

### 2.2 不能直接套用的分区/固件约束

**对方布局（m2381）**

- `boot_b` 放 UEFI 固件；`recovery_a` 是 100 MiB FAT32 ESP，放 UniXNU.efi、kernel、ADT、trust cache、Data seed；`userdata` 整个重建为 iOS root。见 [AGENTS.md#L60-L64](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/AGENTS.md#L60-L64)；设备仓 `[firmware]`/`[payload]`（[board.toml#L182-L242](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/board.toml#L182-L242)）。
- 该布局的前提是设备整机让给 iOS：boot_b 不再是 Android，userdata 不再是 Android 数据。`STATE` 的分区表写死 `boot_b=本仓 UEFI`、`recovery_a=r77 payload`、`userdata=CI 重建 iOS root`（[STATE.md#L333-L338](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L333-L338)）。
- m2381 的 `boot_b` 是 96 MiB，放 Mu UEFI（[HARDWARE-m2381.md#L135-L135](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/HARDWARE-m2381.md#L135-L135)；[AGENTS.md#L79-L86](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/AGENTS.md#L79-L86)）；`recovery_a` 100 MiB 是 ESP 硬顶，payload 卷必须 ≤96 MiB（[board.toml#L224-L237](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/board.toml#L224-L237)）。

**本机布局（Nova）**

- UniXNU 写 **boot_a**，boot image 末尾带 stock `boot_a` 的 chained vbmeta 与 AVB footer；Android 保留在 **boot_b**，其 KernelSU root 在 `init_boot_b`。切换用 `bootctl` 改 GPT 属性字节，或菜单里的 `BootAndroid`。见 [docs/bring-up-status.md](<nova-root>/unixnu-meizu20pro-nova/docs/bring-up-status.md:100-119) 与 [docs/bring-up-status.md](<nova-root>/unixnu-meizu20pro-nova/docs/bring-up-status.md:41-44)。
- `userdata` 没有整块让给 iOS：2026-10-23 从 userdata 尾部切出 GPT 条目 `unixnu`（64 GiB），镜像只写这个条目，原 userdata 保持到 LBA 44613375。见 [board.toml](<nova-root>/unixnu-meizu20pro-nova/board.toml:25-35)。
- 本机 `boot_b` 也是 96 MiB，但它是 Android 的 home；不能把对方“boot_b=UEFI、recovery_a=ESP、userdata=iOS root”的三件套直接搬过来。见 [board.toml](<nova-root>/unixnu-meizu20pro-nova/board.toml:166-168) 与 [board.toml](<nova-root>/unixnu-meizu20pro-nova/board.toml:253-266)。
- 本机菜单的 Android 切槽工具在 `MEIZU_VENDOR_HW=0` 下因没有 UFS DXE 而看不到 GPT，当前禁用；切槽由 fastboot/bootctl 完成（[board.toml](<nova-root>/unixnu-meizu20pro-nova/board.toml:253-266)）。这也说明不能把对方依赖 OEM UFSDxe 的菜单方案直接复制过来。

**[推断]** 任何“把 UEFI 写 boot_b、把 payload 写 recovery_a”的建议在本机都会破坏 Android B 的回退路径；本机只能继续走 boot_a + `unixnu` 分区，除非用户明确放弃 Android B。该判断不需要设备操作，仅由分区布局和 AVB footer 代码可证。

## 3. 最多 5 项“可借鉴/需排除”的改动

**筛选口径**：必须能给出精确提交、文件行号/函数、实际接线与构建条件；只有文档描述、未接线源码或未进入构建的驱动，不列为“已完成能力”。本轮只有 2 项达到“值得进一步看”的门槛，另有 3 项明确排除。

### 3.1 可借鉴项 A：`sheng-lite` + `sheng-disable-jobs` 的启动期作业裁剪

- **精确来源**：设备仓 `8c81849`，[board.toml#L286-L315](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/board.toml#L286-L315)（`sheng-lite=1`、`sheng-disable-jobs=com.apple.transparencyd,com.apple.assistantd,com.apple.biomed,com.apple.liveactivitiesd`）；实现 [package/BootWatchdogPolicy.c#L17-L32](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/package/BootWatchdogPolicy.c#L17-L32)（lite 作业表）、[#L65-L87](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/package/BootWatchdogPolicy.c#L65-L87)（解析与追加）。
- **实际接线/构建条件**：`sheng_boot_watchdog_policy()` 解析 `sheng-lite`、`sheng-disable-jobs`，把标签写进 `/private/var/db/com.apple.xpc.launchd/disabled.plist`；其运行前提是 policy 已链接并在 Data 挂载后调用。作业表包含 identityservicesd、locationd、CommCenter、bluetoothd、photoanalysisd、mediaanalysisd、findmy、assistantd、biomed 等（`#L24-L32`）。
- **本机对应状态**：本机 `BootWatchdogPolicy.c` 只有 cold 的 `com.apple.watchdogd` 一项和 warm 的四个精确条目（[package/BootWatchdogPolicy.c](<nova-root>/unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c:21-24)、[同文件](<nova-root>/unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c:104-112)），不解析 lite/jobs；本机 `kernel_args` 也没有这两个参数（[board.toml](<nova-root>/unixnu-meizu20pro-nova/board.toml:220-250)）。**尚未具有**。
- **适用性**：这是真实写入 disabled.plist 的生产代码，不是文档。它可能减少首次 Data 上 launchd 的作业竞争，但对方 r78b 仍停在 SpringBoard 显示链，且没有“lite 减少启动秒数”的对照；本机当前更早的阻塞是 warm policy `e22`，policy 都过不去，lite 也不会执行。**低优先，不能声称提速**。
- **下一步最小离线验证**：用本机已有的 R8 容器/launchd 记录，核对这些 label 在启动窗口内是否存在高频 respawn/CPU 竞争；只做日志对照，不改 policy、不刷机。若没有证据，保持不引入。

### 3.2 可借鉴项 B：DSP 电池管理消息编解码（仅协议设计）

- **精确来源**：设备仓 `8c81849`，[package/ShengBattmgr.h#L1-L38](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/package/ShengBattmgr.h#L1-L38)（PMIC GLINK `PMIC_RTR_ADSP_APPS`、owner `32778`、op `0x30/0x32/0x33/0x50`、24/28 字节报文）；字段与快照在 `#L40-L86`，接口函数 `sheng_battmgr_init/next/timeout/receive/snapshot` 在 `#L73-L86`；消费者 [package/ShengBattery.cpp#L1-L90](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/package/ShengBattery.cpp#L1-L90)（`ShengBattery::pump()` 在 `#L86-L90`）；启动器 [package/BatteryProbe.cpp#L1-L26](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/package/BatteryProbe.cpp#L1-L26)（`sheng_battery_probe()`）。
- **实际接线/构建条件**：源码已存在，但**未接线**：`[kernel] archives` 没有 battery/ADSP 构建项（[board.toml#L113-L170](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/board.toml#L113-L170)），`kernel_args` 没有 `sheng-adsp=2`/`sheng-battery=1`（[board.toml#L286-L315](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/board.toml#L286-L315)），main STATE 明说“未接线”（[STATE.md#L648-L651](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L648-L651)）。
- **本机对应状态**：本机 B01–B07 全部“待开始”；已有 `ShengPlatformPower.cpp` 只解决 AMFI/PMGR nub，不是电池后端；尚无 ADSP/GLINK 电池服务。见 [PLAN-nova.md](<nova-root>/artifacts-nova/meizu-development-plan-nova/PLAN-nova.md:56-64)（B 段状态）。**尚未具有**。
- **适用性**：协议常量、请求/应答形状、快照字段可作为 B01/B02 的参考，减少从 Android 侧重新逆向；但 m2391 的 PMIC GLINK 通道、ADSP 固件、owner/字段是否与 m2381 相同均未证实，且对方没有可运行镜像。**只能作设计输入，不能算已完成能力**。
- **下一步最小离线验证**：把本机已有的 Android 电池/充电 dmesg 与 `ShengBattmgr.h` 的 op/字段逐项对照，先确认 m2391 是否同一 `PMIC_RTR_ADSP_APPS` 与 owner；不接内核、不构建、不安装。

### 3.3 排除项 C：`UfsWakeDxe`（未进入构建，且针对 m2381 的 ABL PowerDown）

- **来源/内容**：UEFI 仓 `5297334`，[UfsWakeDxe.c#L1-L26](https://github.com/RiverKawa271828/uefi-m2381/blob/5297334c0a8f57495a6b32cf1eb52bac370dda35/Platforms/Meizu/m2381Pkg/Drivers/UfsWakeDxe/UfsWakeDxe.c#L1-L26) 说明 m2381 ABL 把 UFS 留在 PowerDown（`bCurrentPowerMode=0x32`），OEM UFSDxe 的 sleep→wake 路径到不了，所以要在 DXE 派发时先 SSU Active。入口 `UfsWakeDxeEntry()` 在 `#L864-L977`。
- **接线**：没有加入 FDF/DXE/APRIORI（见 1.4 的 fdf/DXE 引用）。即使源码完整，当前 FV 不会包含它。
- **本机状态**：本机已经能过 UEFI→XNU→launchd 到 Hello（R6 已确认，[PLAN-nova.md](<nova-root>/artifacts-nova/meizu-development-plan-nova/PLAN-nova.md:196)）；本机 UFS 路径不是这个“BDS 在 DiskIo flood 卡死”的形态。**不适用**。
- **结论**：不进入候选。只有未来本机 UEFI 在 XNU 前出现 UFS `04/02` 重试或 BDS 卡死，才值得按本机 ABL/控制器状态重新评估。

### 3.4 排除项 D：BootShim 的 Gunyah watchdog disable（当前构建路径不生效）

- **来源**：UEFI 仓 [BootShim.S#L22-L36](https://github.com/RiverKawa271828/uefi-m2381/blob/5297334c0a8f57495a6b32cf1eb52bac370dda35/BootShim/BootShim.S#L22-L36)（`_Start` 内的 `#if DISABLE_GUNYAH_WATCHDOG` 分支），配置 [m2381.toml#L4-L6](https://github.com/RiverKawa271828/uefi-m2381/blob/5297334c0a8f57495a6b32cf1eb52bac370dda35/Resources/Configs/m2381.toml#L4-L6)。
- **接线问题**：主仓 `bootimg.py` 的 `compile_shim()` 只传 `-DREQUIRES_KERNEL_HEADER`，不传 `DISABLE_GUNYAH_WATCHDOG`（[bootimg.py#L23-L30](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/scripts/unixnu/bootimg.py#L23-L30)）。`BootShim/Makefile` 虽然支持该宏，但当前 `./unixnu bootimg` 路径不用 Makefile。**源码能力 ≠ 当前构建能力**。
- **本机状态**：本机 `Resources/Configs/meizu20pro.toml` 没有 `disable_gunyah_watchdog`（[meizu20pro.toml](<nova-root>/uefi-sheng-nova/Resources/Configs/meizu20pro.toml:4-6)）。本机也没有“慢 UEFI 初始化被 Gunyah WDT 打断”的已记录症状。**不适用/未验证**。
- **结论**：不进入候选；若未来出现 UEFI 初始化超时复位，先验证本机是否 Gunyah 链、再补构建参数，不能只抄汇编。

### 3.5 排除项 E：CPU/AMP（三仓没有可引用补丁）

- **[观察]** 主仓 `STATE` 把 AMP 写成“已拍定抄”的计划：sheng `76780d7` 的 `zzyf-sheng-amp.patch` + dts cluster-type，且明确 `m2381 patches/xnu/series 现为空文件直接加`（[STATE.md#L650](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L650)）。
- **[观察]** staged 主仓 `patches/xnu/` 没有 `zzyf-sheng-amp.patch`；设备仓 `patches/xnu/series` 是空文件；本机也没有该 patch 或 `cluster-type` 配置。**三仓中没有 AMP 代码可评**。
- **结论**：不能把 sheng 的 AMP 计划当成“最近更新已交付”，也不能声称上游 AMP 能改善本机启动。若要做，必须另取 sheng 仓固定提交、审 patch 与 DTS，并单独验收；不在本任务五项内。

## 4. 对方是否到 Hello/桌面

### 4.1 结论

**[观察]** 在本任务固定的三个提交及保存的公开记录中，未找到对方已到 Hello 或桌面的证据；这只说明本次研究范围，不代表对方未公开的新运行状态。

- 主仓 r78b 的实机判读：`wait-standin 0`、sel6 Busy 族原样、`applied=26`、屏面 **logo 1:04 / 进度条 1:20 冻结**（[STATE.md#L615-L630](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L615-L630)）。
- 主仓 §6“不能声称”：SpringBoard 未到 home screen，keybag 未考（[STATE.md#L486-L499](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L486-L499)）。
- 设备仓 `records/rounds.md` 最后一条实机判读也是 r78b，文案同“logo 1:04, progress bar 1:20 frozen”（[records/rounds.md#L85](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/records/rounds.md#L85)）。
- r79 之后没有新的 boot verdict；r80 只做充电，且设备 8% 电量、充电未完成（`STATE` 施工单 §8 步骤 0；[STATE.md#L646-L651](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L646-L651)）。
- 设备仓 README 的状态文字停在 r55e，不是新进展（[README.md#L15-L21](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/README.md#L15-L21)）。

**[推断]** 对方的用户态生态是存在的（历史记录 173–200 进程、真 IOMFB swap、SpringBoard 画 boot progress 菊花/进入 wallpaper/今天视图 widget），但“用户态起来了”不等于 Hello/桌面完成。r78b 的 SB 崩溃链和冻结进度条说明 GUI 尚未完成。

### 4.2 iOS 版本、用户态与本机的区分

- 对方 r78b 时用 iPad7,1 / iPadOS 17.7.11 `21H461`；r79 拍板改回 iPhone10,3 / iOS 16.7.16 `20H392`（[STATE.md#L632-L644](https://github.com/RiverKawa271828/ios-m2381-unixnu/blob/82d8dc7d0670106a254532ca88adda9b79246f73/docs/STATE.md#L632-L644)）。这个“换 iOS16”是后续计划，不是已完成的 Hello 证据。
- 本机是 iPhone10,3 / iOS 16.7.16 / 20H392（[board.toml](<nova-root>/unixnu-meizu20pro-nova/board.toml:15-23)），历史上已到 Hello；R6 记录“Hello 已确认”（[PLAN-nova.md](<nova-root>/artifacts-nova/meizu-development-plan-nova/PLAN-nova.md:196)）。但 R11 的 warm 分支在 `CHECK_DIRECTORY` 报 22、未到 Hello（[R11 REPORT](<nova-root>/artifacts-nova/boot-speed-round11-nova/failure-analysis-nova/REPORT-nova.md:3-7)）；R12 未安装（[R12 REPORT](<nova-root>/artifacts-nova/boot-speed-round12-nova/REPORT-nova.md:3-7)）。
- **[推断]** 对方即使后续用同一 iOS16 donor，面板、显示栈、内存和板级驱动也不同；不能把对方“logo 出现时间/进度条时间”与本机 Boot→Hello 做可比实验。对方的时间是人工视觉观察，本机是明确的阶段锚点；两者不应相加、相减或互相证明提速。

## 5. warm policy `e22`：没有直接修复证据

### 5.1 本机事实

- R11 实机：`nova-r11 policy fail e=22 b=2 op=9 save=1 @kern_exec.c:9822`，未到 Hello；warm=1、R8 暖加载 status 0、两项固定 SHA verified 1、Data mount/prepare 返回 0，失败点是 warm 目录的 type/mode/uid/gid 谓词，不是 open flags。见 [R11 REPORT](<nova-root>/artifacts-nova/boot-speed-round11-nova/failure-analysis-nova/REPORT-nova.md:3-7)。
- 本机谓词当前接受：目录 `0755` 且 `(uid=501,gid=20)` 或 `(0,0)` 或“本次新建的 `(0,20)`”（[package/BootWatchdogPolicy.c](<nova-root>/unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c:284-295)）。文件谓词另行要求 `uid=0,gid=0, regular, 0644, size`（[同文件](<nova-root>/unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c:391-398)）。
- R12 只在失败前增加目录实际 mode/uid/gid/mkdir errno 和文件元数据日志，谓词未改；747 个主机函数测试通过，但尚未上机。见 [R12 REPORT](<nova-root>/artifacts-nova/boot-speed-round12-nova/REPORT-nova.md:3-7) 与 [NEXT-STEPS](<nova-root>/artifacts-nova/boot-speed-round12-nova/NEXT-STEPS-nova.md:1-9)。状态为 `BUILT_AWAIT_USER_INSTALL_COMMAND`。

### 5.2 上游代码是否提供修复

**[观察]** 设备仓的 `BootWatchdogPolicy.c` 写入 policy 后检查的是**文件**元数据：`st_uid==0 && st_gid==0 && S_ISREG && (st_mode&0777)==0644 && size`，失败返回 `EINVAL`（[package/BootWatchdogPolicy.c#L119-L121](https://github.com/RiverKawa271828/UniXNU-m2381/blob/8c818496611300f2672dd874dd9b59482d485013/package/BootWatchdogPolicy.c#L119-L121)）。它没有本机 `CHECK_DIRECTORY`/`op=9` 的目录 tuple 检查，也没有打印实际目录 mode/uid/gid。

**[推断]** 这不能证明“上游有 e22 修复”。它只是没有做本机这一步目录校验；把上游代码照搬到本机，等于删除/绕过本机的目录安全检查，不是修复 `e22`。本次 staged 三仓源码/文档中也没有 `e22`/`op9`/`CHECK_DIRECTORY` 的修复记录可引用。

**[未知]** R12 的 stat ABI 复核已排除“PID 1 尚未 exec / 32 位布局导致 mode/uid/gid 偏移错误”的假设：[stat-abi-review-nova/REPORT-nova.md](<nova-root>/artifacts-nova/boot-speed-round12-nova/stat-abi-review-nova/REPORT-nova.md:3-24) 记录 PID1=64，policy stat64、syscall stat64、user64_stat64、user32_stat64 的 mode@4、uid@16、gid@20 一致。实际运行时 mode/owner 或 APFS 呈现仍未知；在拿到 R12 的真实目录字段前，不能判断是 mode 不是 `0755`、uid/gid 不在允许集合，还是目录准备/对象选择另有问题。

**边界**：R12 的最小判据是读到 `nova: policy-metadata probe=12 kind=directory mkdir_errno=… created_directory=… mode=0… uid=… gid=…`，再与固定 R8 fixture 对比。只有该行能区分“目录新建但 owner/mode 不符”与“已有目录 tuple 不符”。在拿到它之前，任何“放宽 0755/501:20/0:0”的改动都是猜测。

## 6. 推荐的下一个最小行动

**只做一件事：等用户明确安装命令后，安装 R12 到非活动 A，读取 `policy-metadata probe=12` 的目录行，与固定 R8 fixture 对齐；本轮不安装、不构建、不改生产代码。**

为什么优先它：

1. 本机当前 R11/R12 候选路径上最早的已知阻塞是 warm policy `e22`；R12 已构建、已过 747 个 host 函数测试和安装控制流检查，是现成的、可逆的诊断件（[R12 REPORT-nova.md](<nova-root>/artifacts-nova/boot-speed-round12-nova/REPORT-nova.md:3-7)）。它不是本机排他的启动瓶颈（冷 Data 3–4 分钟、lsd/Setup 长期等待仍未解决），R12 也不是排他行动；在当前证据下，它是边界最清楚、最可逆的一步。
2. 上游三仓没有 `e22` 修复；抄对方 policy 只会绕开目录校验（见 §5.2），不能解释根因。
3. 这一行动不引入 `sheng-lite`、电池、UFS wake 或 AMP 等未验证改动，保持单变量；拿到实际属性后再决定是修目录准备/属主/权限语义，还是只修谓词。
4. 当前设备应为 Android B；R12 只写非活动 A，保留 B 回退；但必须等用户明确命令，且安装前重新核对设备状态、A/B 哈希和授权（[PLAN-nova.md](<nova-root>/artifacts-nova/meizu-development-plan-nova/PLAN-nova.md:14-15)）。

**预期产出**：一行真实目录属性；不是 Hello、不是提速声明。若 R12 未出现目录行，先核对镜像身份、日志保存/有效 ring、以及执行是否到达 probe；不能仅凭缺少该行推断问题在 stat/copyin 或更早路径。只有这些前置都确认后，才决定是否追加更早路径诊断，不应继续改谓词。

## 7. 观察、推断、未知与未覆盖

### 7.1 观察

- 三仓固定 SHA、时间、父提交和最新提交 message 均来自保存的 API 记录；源码行号来自 staged 固定提交。
- 主仓最新提交是仓库拆分/文档换线；设备仓最新提交是 `board.toml [firmware]` 注释；UEFI 最新提交是 m2381 overlay 收编。
- 充电 25 文件未接线；UfsWake 未进 FV；Gunyah watchdog 宏未被当前 `bootimg.py` 传入；AMP 仅计划。
- 对方没有 Hello/桌面证据；本机历史 Hello、R11 e22、R12 待安装。

### 7.2 推断

- 三仓最近更新对本机启动速度没有可直接验证的收益；没有可引用的上游提速补丁。
- m2381 的板级/分区/显示常量不能直接迁移到 m2391。
- 上游 policy 的“无目录检查”不是 e22 修复；R12 是必要边界。

### 7.3 未知

- UEFI staged 是 partial snapshot，无法证明完整 FV 是否有外部脚本接入 `UfsWakeDxe`/Gunyah 宏；本报告只对 staged 文件与主仓 `bootimg.py` 负责。
- 未读取 sheng 设备仓的 `zzyf-sheng-amp.patch` 原文（不在三仓 staged 输入内），因此不评价其正确性/收益。
- 未取得 R12 实机目录属性；未验证 m2391 的 ADSP/PMIC GLINK 是否与 m2381 同 owner/字段。
- 未重做 lsd/容器全量分析；本机 lsd 等待仍以既有 R4–R6 报告为准，不能把上游“lite 作业表”当成 lsd 修复。

### 7.4 未覆盖点

- 未对三仓做全文件逐行 diff；最新主/设备提交 file list 已由 `main-latest-commit-detail-nova.json:43-89` 与 `device-latest-commit-detail-nova.json:43-53` 核验：主仓仅 `AGENTS.md`、`README.md`、`docs/STATE.md`、`docs/UPSTREAM.md`，设备仓仅 `board.toml` 的 7 行注释；更早历史和全树仍未逐文件 diff。UEFI staged 仍是 partial snapshot。
- 未构建/运行任何 UEFI 或内核；未验证 m2381 的 `UfsWakeDxe` 是否能在真实 FV 中工作，也未验证其未接线是否被外部脚本补上。
- 未分析对方充电固件/ADSP 二进制；未验证电池协议在 m2391 的兼容性。
- 未做设备操作；R12 的实际目录属性、Hello 与启动时间仍是未知。

## 8. 引用汇总（固定提交/本地证据）

- 主仓固定树：https://github.com/RiverKawa271828/ios-m2381-unixnu/tree/82d8dc7d0670106a254532ca88adda9b79246f73
- 设备仓固定树：https://github.com/RiverKawa271828/UniXNU-m2381/tree/8c818496611300f2672dd874dd9b59482d485013
- UEFI 仓固定树：https://github.com/RiverKawa271828/uefi-m2381/tree/5297334c0a8f57495a6b32cf1eb52bac370dda35
- 保存的 API：`artifacts-nova/m2381-upstream-review-nova/repository-api-nova.json`、`commits-api-nova.json`、`device-nova-latest-commit-api-nova.json`、`uefi-nova-latest-commit-api-nova.json`
- 本机：`unixnu-meizu20pro-nova/board.toml`、`package/BootWatchdogPolicy.c`、`docs/bring-up-status.md`、`README.md`；`uefi-sheng-nova/Resources/Configs/meizu20pro.toml`、`Platforms/Meizu/meizu20proPkg/meizu20pro.dsc`；`artifacts-nova/boot-speed-round11-nova/failure-analysis-nova/REPORT-nova.md`、`artifacts-nova/boot-speed-round12-nova/REPORT-nova.md`、`artifacts-nova/meizu-development-plan-nova/PLAN-nova.md`。
