# HackiOS 到魅族 20 Pro 的适配评估 — Nova

日期：2026-10-29（Asia/Shanghai）。新文件，无旧版本。本轮完成固定提交源码审阅、本地驱动盘点和 Android 只读硬件采集；没有移植生产驱动、构建候选或刷机。报告中“作者验收”与“魅族验证”严格分开。

**结论：值得借鉴，但不能把一加 9 的驱动整包搬过来。** 最有复用价值的是电池的有效性与 iOS 上报模型、蓝牙的 H4 传输与队列设计、Wi-Fi 的 iOS 接口，以及图形的语义修复、同步和验收用例。魅族的 Wi-Fi、GPU、触控和电池数据源与一加不同，板级驱动必须按本机实现。[无线与电池审阅](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-radio-battery-nova.md)；[图形与用户态审阅](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-graphics-userland-nova.md)；[魅族硬件基线](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md)。

## 固定版本与证据范围

| 输入 | 本轮固定版本 |
| --- | --- |
| hackios-xnu-kernel | `8aefda9c133a19fad1c1083eb713480348f43d78`，提交时间 2026-10-27 23:33:16 +08:00 |
| hackios-xnu-userland | `3278f7f01796e74ecd634c0d348d2b255a37658f`，提交时间 2026-10-27 06:30:47 +08:00 |
| Linux 设备 ID 对照 | Linux v6.12 `drivers/net/wireless/ath/ath12k/pci.c` |
| 魅族 Android 只读观测 | 2026-10-29 15:06、15:17（北京时间）；当时运行 Android B |

Kernel 仓库已将 userland、loader 与工具合入单仓；独立 userland 作为历史资料。当前主仓文档包含 `2026.10.27-base.2` 的显示、联网、Metal 联合运行记录，不能用独立 userland 的较旧限制否定它。反过来，这些是作者针对一加特定运行包的记录，本轮没有独立复测，也不能外推成魅族可用。[版本与验收说明](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-graphics-userland-nova.md:7)。

Kernel 稀疏检出受网络影响未完整恢复，关键文件已按固定提交另外保存；没有执行下载仓库的脚本。保存文件的来源、长度和 SHA256 见 [pinned-source-manifest-nova.json](<nova-root>/artifacts-nova/hackios-port-review-nova/pinned-source-manifest-nova.json)。源码存在、编译通过、作者记录和本机硬件成功分别记账。

## 五项硬件对照

| 功能 | 上游一加 9 的实现与边界 | 魅族 20 Pro 的证据 | 适配选择 |
| --- | --- | --- | --- |
| Wi-Fi | QCA6490 / PCI `17CB:1103` / ath11k；作者记录 WPA2、DHCP、DNS、HTTP 与重连；目前仅 WPA2-PSK，省电/挂起未完成 | 实机 `wlan0` 绑定 `cnss_pci`，PCI `17CB:1107`、RC0、Kiwi 固件；Linux 官方对应 WCN7850 / ath12k，revision 尚待补齐 | 借用 iOS 网络接入结构；另做 SM8550 PCIe/供电/SMMU 与 WCN7850 后端，不能只换 PCI ID |
| 蓝牙 | 一加 UART/H4、控制器固件加载、LE 广播扫描、Classic inquiry 与原生设置发现；配对、GATT 读写、音频未证实 | 属性为 Hamilton，电源节点为 `bt-qca-converged`；存在 `898000` GENI UART，但尚未取得实际 HCI 身份与完整传输绑定 | 借用 H4 parser、队列、长度校验；先核实本机 UART、电源、控制器与固件，再接 iOS |
| 电池 | BQ28Z610 → 驻留 UEFI I²C → 时间戳缓存 → IOPMPowerSource；有百分比与满电上报，没有充电控制 | Android 实际为 PMIC GLINK → `qti_battery_charger`；本地 `ShengPlatformPower` 只提供 AMFI 所需 nub | 优先复用电池样本/过期处理/IOKit 层；底层改用本机可信持续遥测 |
| GPU / 显示 | Adreno 660、SM8350 DPU、特定面板；新代码已有异步完成、单层 DPU direct 和多层 CPU 双缓冲 | Adreno 740；已有 NativeAdreno/Turnip/NativeMetal 源码路线，当前魅族配置未启用 GPU；当前 Negroni 为软件显示 | 沿本地 A740 主线推进，吸收 AIR/像素/同步修复；DPU 与面板参数另做 |
| 触控 | Samsung S6SY792 / I²C `0x48` | Goodix Berlin D / `0x5d`；已有板级代码，当前 TOUCH/HID 关闭，读取实现是软件 GPIO I²C | 继续本机 Goodix 路线；借用触点生命周期与手势测试，不移植 Samsung 协议 |

来源：[无线/蓝牙/电池逐项证据](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-radio-battery-nova.md:10)、[本机运行与静态硬件证据](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md)、[图形接口和触控差异](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-graphics-userland-nova.md:25)。WCN7850 对照来自 [Linux ath12k pci.c](<nova-root>/artifacts-nova/hackios-port-review-nova/pinned-sources-nova/linux-v6.12/drivers/net/wireless/ath/ath12k/pci.c:40)，不由营销规格或 DT 备用节点猜测。

## 具体实施路线

以下为根据现有证据提出的工程计划，尚未实施。并行研究可以分模块进行；设备上的候选应逐项启用，分别保留可回退基线和日志。

### 0. 先固定本机运行契约

锁定每个候选的 kernel、root、dyld cache、Apple kext、固件和配置哈希。当前 `board.toml` 目标是 iOS 16.7.16 / 20H392，而实际候选日志是 Darwin 23.6.0；它们描述不同层，不能据配置名称认定整套 ABI。上游 private hook 绑定 iOS 16.5 / 20F66，一些蓝牙兼容代码还校验特定 bluetoothd UUID 与指令偏移，需要逐项对照本机系统文件。[系统版本与启动链对照](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-graphics-userland-nova.md:73)。

硬件信息只补缺口：Wi-Fi revision/实际固件版本，蓝牙 HCI 身份及 UART 绑定，PMIC GLINK 字段与远端服务，Goodix 版本/TLMM/电源状态，A740 固件与资源。现有只读采集已经确认 Wi-Fi PCI ID 和电池 AP 端数据通路，下一步不用重新猜型号。采集保留字段白名单，不收录 MAC、私有校准内容或电池身份长串。[补采清单](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md)。

### 1. 电池与触控：先做最小可验证功能

**电池上报层是较小的复用切入点，但真实数据后端仍是阻断项。** 拆出上游逐字段有效位、采样序号、时间戳、超时失效和 IOPMPowerSource 更新逻辑，去掉 BQ 型号、2S 电压范围、固定物理缓存与一加 I²C 地址的绑定。数据接口至少区分 SOC、温度、电压、电流、容量、外接电源、正在充电、已满电和各字段有效性。[电池实现与限制](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-radio-battery-nova.md:61)。

本机后端优先研究 Android 所用 PMIC GLINK/charger 服务的消息定义、字段单位、远端可用性和更新通知。若改用厂商 UEFI 遥测，须先证明目标协议可用、进入 XNU 后仍能持续更新；不能把启动时缓存或驻留 core0 方案直接视为运行时电池驱动。当前厂商 UEFI 电源模块存在但配置关闭，已有依赖未解决。[本机电源限制](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md)。

验收从只读开始：真实样本进入内核 → IORegistry → IOPS → 原生状态栏；分别验证数值更新、接电/断电、满电与正在充电的区别，以及数据源中断后的失效行为。上游曾出现内核属性失效但 IOPS 暂留旧值，需纳入本机检查。充电限流、快充、热管理和休眠属于后续独立功能。

**触控沿本地 Goodix 驱动排查。** 先解释当前关闭开关的原因，核对 TLMM 偏移、电源、IRQ 与 Goodix 固件/协议；先做有界读取与单触点事件，再验证按下/移动/抬起、多指和坐标转换。现有软件 I²C 不等于 GENI 硬件 I²C 已可用，避免把可能关中断自旋的路径直接开成默认。[本机触控状态](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md)；[上游触控及 HID ABI](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-graphics-userland-nova.md:67)。

### 2. 蓝牙：先控制器，再系统协议

第一阶段确认实际 UART、pins/clocks、BT_EN/供电、流控与唤醒方式，并取得控制器 Product/ROM/SoC/patch 身份。`hamilton` 是配置线索；不能由此直接选一加的 `hpbtfw10.tlv` 或猜商品芯片名称。[本机蓝牙观测](<nova-root>/artifacts-nova/hackios-port-review-nova/android-bluetooth-observation-nova.json)。

第二阶段移植 H4 包边界、队列和匹配本机的固件加载，验收 HCI Reset/版本回读、LE scan 与 Classic inquiry；失败时报告真实错误。第三阶段才接 IOKit/UART socket 与本机 iOS 的 bluetoothd，验证设置页。GATT 读写、配对、HID、音频、重复开关和睡眠唤醒分别验收，不能用“发现设备”替代。[上游蓝牙机制与边界](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-radio-battery-nova.md:42)。

### 3. Wi-Fi：WCN7850 专项适配

这是无线部分最大的结构性差异。保留本机 Kiwi 固件、board data/校准职责与硬件 revision 的对应关系，建立 SM8550 RC0 的电源/reset/PHY、PCIe 枚举、BAR/MSI、SMMU/SID 和 DMA 路径；随后实现所需 MHI/QMI 固件握手和 WCN7850/ath12k 收发后端。[本机 PCIe 与无线证据](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md)；[上游无线依赖](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-radio-battery-nova.md:23)。

再借用上游 iOS 网络接口和 station 控制层的组织方式，按“枚举 → 固件 ready → 扫描 → WPA2 → DHCP/DNS/HTTP → 断线重连”逐级验收。ath11k 构建中存在缺失符号自动 stub，移植时需明确每个未实现入口，不能把成功返回或链接完成算作网络成功。WPA3、吞吐、中断、省电和休眠另立验收目标。

### 4. GPU / 显示：延续现有 A740 主线

先核实本地 NativeAdreno 的 A740 板级资源、GMU/SQE 与必要安全加载、SMMU 和真实 GPU 提交，再推进 Turnip/NativeMetal。上游 RedwoodGPU 使用 msm DRM 风格 selector，而本地 NativeAdreno 是 version 1 自定义 ABI，动态库不能直接混用。[ABI 与平台差异](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-graphics-userland-nova.md:25)。

优先吸收上游 half/导数/varying、SSBO 偏移、blend/clear、资源提前释放等像素与状态回归；异步提交的 completed 回调与完成状态必须等真实 GPU fence，scheduled 回调则对应提交落实。显示再按本机 DPU/DSI/DSC/面板配置做硬件后端：direct 路径验证“GPU 完成 → DPU 传输完成 → 撤映射 → 释放源缓冲”；多层 CPU 合成路径的输入图层在最后一次 CPU 读取后即可释放，内核自有合成表面需等 DPU 完成并撤映射后再复用。最初目标是正确输出与安全回收，120 Hz 和性能优化随后单独测量。[可复用图形修复与显示契约](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-graphics-userland-nova.md:40)。

## 与当前启动优化的关系

这些仓库的 init、双 RAM disk、私有注入和系统版本构成另一套启动架构。它们的桌面性能改进不能直接换算成 Nova Boot→Hello 的缩短。此前 Nova 的 256 KiB 上层读取仍被根盘按 16 KiB 认证读取，另有服务串行等待；这个问题保留为独立工作项，本轮未实施新的读取优化，也未因驱动研究替换启动链。[Nova 读取分析](<nova-root>/artifacts-nova/boot-optimization-nova/read-batching-analysis-20261029-nova/REPORT-nova.md)；[HackiOS 启动架构差异](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-graphics-userland-nova.md:73)。

## 本轮交付与状态

- 三份专项报告：本机硬件、无线/电池、图形/用户态；每份包含本地或固定提交源码引用。
- 两份实机只读观测；未切换无线设置、未重启、未切槽、未刷写。
- 固定提交关键源码与 SHA256 清单，保存在 Nova 研究目录；没有修改 Sol/Astra 工作区。
- 此总报告与验证记录。下一轮可以按上述模块形成候选；本轮没有已适配成功的 Wi-Fi/蓝牙/电池/GPU 镜像。

复用代码时逐文件保留原版权、许可与第三方说明；Apple 系统文件和设备固件/校准不等于开源仓库的再分发内容。[来源与许可审阅](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-radio-battery-nova.md:85)。
