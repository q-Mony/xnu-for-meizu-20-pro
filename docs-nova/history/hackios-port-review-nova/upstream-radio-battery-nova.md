# HackiOS 一加 9 无线与电池源码审阅 — Nova

审阅日期：2026-10-29。固定主仓库提交：`b1ackm4x/hackios-xnu-kernel@8aefda9c133a19fad1c1083eb713480348f43d78`（主代理已核对为 2026-10-27）。本报告只读审阅该提交；稀疏副本的 blob 拉取未完成时，使用固定提交的 `raw.githubusercontent.com` 文件读取核对，未运行仓库下载/构建/设备脚本。报告是新文件，无旧文件；生产源码未改。CHANGELOG 由主代理统一记录。

结论：这是一套有真实硬件驱动路径、且作者报告完成有限真机验收的一加 9 适配，不能理解为“完整 Qualcomm 通用 Wi-Fi、蓝牙和电源栈”。Wi-Fi 最接近可用网络服务；蓝牙已到真实控制器初始化及原生设置发现；电池是特定外置电量计的遥测和 iOS 上报，没有充电控制。[R1][W1][B1][P1]

## 证据分层与最终能力边界

“作者报告真机通过”表示固定提交文档记载的结果。本审阅没有拿到作者私有原始日志、设备专属完整运行镜像或重新操作其 LE2110；不把文档中的哈希/统计当作独立复测。README 明确最近整包验收是 `2026.10.27-base.2`，后续同步二进制仅完成主机检查，公开 CI 组件实验版不能直接启动手机。[R1][R2]

| 功能 | README/专项文档声称 | 固定提交源码能够直接确认 | 公开证据的上限 |
| --- | --- | --- | --- |
| Wi-Fi 扫描 | 有实际无线扫描 | 驱动发起扫描、接收扫描完成事件，并有 station 生命周期 | 作者记录；固件、校准、原始日志和网络输入私有。[W1][W2][W5] |
| WPA2 关联 | WPA2 关联、主动断开/重连成功 | `station_join()` 实现扫描选网、认证关联、PMK/四次握手与 key 安装；仅接受 `REDWOOD_STA_WPA2_PSK` | 文档记录独立 240 秒运行，首次连接和重连各 3/3 HTTP 请求；不能扩展成 WPA3、企业认证、漫游或首次原生密码 UI 已通过。[W1][W5] |
| DHCP / DNS / IP 流量 | README 的联网检查 61 项通过 | 接入 iOS 无线接口/en0，由系统网络服务配置 IP；驱动实际收发帧路径存在 | 文档说明 DHCP/DNS/HTTP/重连通过；不等于长期吞吐、压力与睡眠唤醒通过。[R1][W1] |
| 蓝牙 HCI / LE | 真实控制器身份、固件加载和 LE 广播扫描通过 | SE18 UART H4 收发；真实 Reset、版本、地址读回；被动 LE scan | 文档报告 5 秒 410 条 advertising reports；原生 daemon 收到 2,680+ 广播报告。广播数量不是设备数。[B1][B2] |
| 蓝牙原生 UI | 原生“设置→蓝牙”发现附近 Mac | 提供真实 IORegistry 身份、原生 UART kernel-control socket、BlueTool bootstrap 和限定 iOS 16.5 兼容层 | 文档记载 inquiry 完成、真实发现通知及 1 行设备；IOSurface 记录不是光学实拍。完整整包 14 次 native 状态采样。[B1][B4][B5] |
| GATT / 配对 / 音频 / 外设数据 | README 明确未验收配对、音频、外设传输 | H4/ACL 包传输基础不能证明 GATT 服务发现、read/write、SMP 配对或 A2DP/HFP 完整运行 | 固定提交的专项文档没有 GATT 读写成功验收条目；不能报已支持。睡眠、重复开关和长期稳定性也未验证。[R1][B1] |
| 电池真实百分比/电压等 | 98% 与 100% SOC 贯通 kernel/IORegistry/IOPS/UI；组合运行 10 个不同更新一致 | BQ28Z610 标准寄存器读取、逐字段有效位、时间戳与过期处理；注册 `LemonadeBattery` | 98% 与满电100%有作者记录和提交内源表面图像；连续变化后的 UI、小组件、低电量尚未单独验收。[P1][P2][P3][P4] |
| 插电 / 满电 / 充电控制 | 插电且已满电状态通过 | ChargerEx 外接电源 + gauge FC/DSG/告警位决定上报；没有 charge enable、电压/电流设定或快充控制 | 插拔后的状态更新未验收通过；非满电正在充电 UI 未实证。控制、热保护策略和快充不可据此宣称支持。[P1][P2][P4] |

## Wi-Fi：实际依赖、缺口与可移植部分

硬件路径是 **LE2110/SM8350 → PCIe RC0 → QCA6490 major 1 → MHI/BHI/BHIE → IPCR/QRTR/QMI WLFW → ath11k → iOS 无线接口/en0**，并非 Redwood 的 WCN6750/WPSS/AHB。文档记录 PCI `17cb:1103` revision 1、SoC hardware version `0x400c0110`，BAR0 为 2 MiB。[W1][W2]

板级必需资源包括：PM8350 L6 1200 mV / L5 880 mV、GCC/GDSC/reset、SM8350 QMP PHY 表、WLAN_EN64、PERST94、CLKREQ95、SID `0x1c01`、Apps SMMU `0x15000000`、PCIe BAR/iATU/MSI，以及受限 DMA 内存与 cache/TLB 操作。代码含这些固定地址、引脚和 SM8350 专属 PHY 表，按 `device_type=lemonade` 隔离；模式24才是 station runtime，不能把早期 bring-up 模式1–23的通过当成联网完成。[W2][W3][W9]

固件必须是匹配本机修订的原厂 `amss.bin`，以及同机 board、regdb、M3 文件。源码使用独立 QCA6490 major1 profile，基于固定 Linux 7.0.14 WCN6855 定义作局部调整，保留不同硬件 revision，CE2 上限调成3200。生成器自己说明这是实验性 profile，不代表全部硅片功能等价；这些私有文件不能替换成任意同厂 blob。[W1][W4]

需要保留的限制：

- **认证是代码限制。** `station_join()` 对非 WPA2-PSK 返回 `ENOTSUP`，CCMP/RSN 条件不符也拒绝；不是仅缺少测试记录。[W5]
- **省电明确关闭。** QCA6490 profile 将 `idle_ps`、`supports_sta_ps`、`supports_suspend`、`global_reset` 等置 false。文档说明传输仍轮询，CPU 中断路径与完整省电生命周期未完成；不能根据兼容层中存在别的平台 IRQ 函数推断 lemonade 已走中断。[W1][W4][W6]
- **ath11k 只部分编译。** 构建脚本只编译选定文件，并为未定义的 `ath11k_*`/`__ath11k_*` 符号自动生成 stub；`lx_stub()` 记录前几次调用并返回0。本次未执行构建，没有生成的 `out/stubs.txt`，所以不能断言具体全部缺失函数，也不能把链接成功当成所有功能实现。[W7][W8]
- **测试入口与生产入口分开。** `lx_station_join()` 能从私有网络输入作预置连接；原生首次密码录入流程文档明确未单独验收。未见用固定 SSID/密码伪造成功的证据。[W1][W5]
- **电源/退出尚有边界。** RPMh 供应投票保留至 RAM 会话结束，完整撤销/生命周期仍待实现。文档记载一次复用已返回 RAM loader 后 Wi-Fi 初始化进入 `05c6:900e`，原因未定位；支持流程改为经 Android 冷启动，失败暖启动没有算成功。[W1][B1][S1]

对魅族20 Pro / SM8550的推断：可以借鉴 iOS 网络接口、Apple80211/configd 接法、station/WPA2 层、有限 DMA 验证方法和分阶段验收工具；**PCIe/PHY/GCC/RPMh/SMMU/SID/固件及芯片 profile 必须重新适配**。主代理本轮 Android 只读观测指出目标 PCI 为 `17cb:1107`，固件目录为 `kiwi`/`kiwi_v2`，与本源 `17cb:1103`/QCA6490不同；这个目标输入来自同目录 `android-hardware-observation-nova.json`，本代理没有操作设备。主代理另用 Linux v6.12 的 `ath12k/pci.c` 将 `0x1107` 核定为 WCN7850，目标驱动选型证据详见主报告。因此Wi-Fi迁移应面向 **WCN7850/ath12k** 后端；不能仅改 PCI ID 复用本 ath11k/QCA6490 profile。[W2][W3][W4]

## 蓝牙：真实传输已接通，完整协议能力未建立

内核 IORegistry 发布 `qcom,wcn6851-bt`，控制器实际身份由 HCI 读回验证：Product `0x13`、ROM `0x100`、SoC `0x400c0110`。不要把 Wi-Fi 的 QCA6490名称、Redwood 的 WCN6750或未核对的“同 Snapdragon”当成蓝牙芯片证明。此提交代码和文档支持上述精确身份。[B1][B2][B4]

实际板级传输依赖：SM8350 **QUP SE18 UART `0x890000`**、GPIO68–71、BT_EN65、SW_CTRL153，固定115200/8N1并使用硬件 CTS/RFR；FIFO轮询，没有蓝牙DMA路径。7.3728 MHz UART时钟参数及GCC寄存器硬编码。RPMh需要 `smpb11=950mV`、`smpc1=1880mV`、`smpb12=1350mV`、`ldoe7=2800mV`；共享 `lemonade_radio.c` 串行化电源事务，超时未完成会保留槽位并要求重启。[B2][B3][S1]

固件路径读取私有 `hpbtfw10.tlv`，核对 Product/ROM/SoC，再按 board ID 寻找 `hpnv10.bXX` 或 `.bXXXX`，最后允许原厂 `hpnv10.bin` fallback。NVM只在RAM副本中调整真实地址、115200及awake操作；该设备文档报告 board ID0、patch178,532字节、735段、NVM5,753字节、24次ACK。复位后再读patch和真实地址，只有匹配才设置ready。这里有真实设备初始化代码，没有以虚构 HCI success 掩盖缺失控制器的证据。[B1][B2]

上层桥接包括：

- `com.apple.uart.bluetooth` stream kernel-control socket、H4队列、单worker、termios契约。固定115200，其他波特/帧参数并非动态支持；daemon重连先drain/reset/revalidate，不能据此称设置开关及睡眠已验收。[B1][B6]
- `LemonadeBluetooth.cpp` 给原生daemon真实地址与H4编码，`lemonade_bt_bootstrap.c`提供board bootstrap契约；真正的iOS协议栈仍来自原Apple系统文件。[B1][B4]
- `lemonade_bt_compat.c`限定 `lemonade_bt=6`、`qcom,wcn6851-bt`、ready、**特定 bluetoothd Mach-O UUID**及固定偏移/函数字节，修改进程局部数据表；禁用Broadcom专有callback和extended/periodic/scan-core策略，选择legacy HCI路径。这是与iOS16.5二进制强绑定的适配，不是可跨版本移植的通用蓝牙抽象。[B5]

未发现本提交以假设备列表模拟原生扫描成功的路径；文档说明原设置页通过原版 `nanoregistryd`/`nanoregistrylaunchd` 解阻。另一方面，**ACL/H4接受与LE advertising不足以证明GATT、SMP配对、HID或音频**。固定提交只给出真实scan/inquiry/native discovery验收，没有GATT读写完成的可核验记录，README也明确配对/音频/外设数据未验收。[B1][R1]

移植建议：复用H4边界解析、TLV/NVM长度校验、队列/重连状态机与IOKit桥接结构；重新核对目标实际BT控制器、UART控制器及pins/clocks/供电、固件patch/NVM结构、HCI初始化和身份。目标芯片/固件不匹配时，当前 Product/ROM/SoC gate 会拒绝启动，这是需要实现新后端的信号，不能删除验证后硬跑。相同iOS16.5可研究兼容层，但仍要重新核对二进制绑定；更换iOS版本时不能照抄绝对偏移。[B2][B3][B4][B5]

主代理另提供目标只读线索（`android-bluetooth-observation-nova.json`）：蓝牙属性为`hamilton`，rfkill关联`bt-qca-converged`，存在`898000.qcom,qup_uart`绑定；它们与本源SE18/WCN6851/hpbtfw10路径不同。**这些线索尚非目标HCI身份或已打通传输证明**，本报告不据`hamilton`推定商品芯片型号，具体确认交由主报告。

## 电池：可优先复用上报模型，数据源必须改

链路是 **BQ28Z610 → 驻留 Qualcomm UEFI I²C → v2时间戳缓存 → XNU → IOPMPowerSource**。其实现不需要完整ADSP/PMIC GLINK，但必须保留core0的UEFI调用上下文。它不是“只移植一个XNU电池文件就能读所有高通设备”的方案。[P1]

硬编码和固件契约包括：I²C实例3（SE2 / `0x988000`）、地址`0x55`、I2CDxe revision`0x10000`；先写非持久`Control(DEVICE_TYPE)`查询并要求`0xffa5`。ChargerEx只接受revision`0x10003`或`0x10004`，只读第一槽`GetChargerPresence`；不会调用一加QcomCharger的Initialize/TakeAction。标准寄存器读SOC`0x2c`、Voltage`0x08`、Temperature`0x06`、Current`0x14`、Capacity`0x10/0x12`、Cycles`0x2a`、Status`0x0a`。[P1][P2]

可复用的设计较完整：逐字段有效位、成功采样ID、generation、奇偶序号快照、CNTVCT频率校验、15秒过期、重复样本不续期；单位明确为整包µV、充电正µA、串联整包µAh，不乘2。IOKit移除无效属性并调用`updateStatus()`；满电来自FC，不能由SOC=100推导，外接电源与是否充电分开上报，主动不设置未经验证的`ExternalChargeCapable`。[P2][P3][P4]

不过共享header也固定BQ28Z610 ID、2S电压允许范围4.5–10V、物理缓存地址区间和对齐规则；仅改服务名会拒绝或误解释另一块板。XNU入口仅接受lemonade平台及显式boot-arg。移植时应抽出无硬件依赖的sample/freshness/IOKit模型，为目标另建已核对的数据源与字段映射。[P3][P5]

真机证据边界：文档记录98%时36次采样/28次对照、暂停后内核和IORegistry按15秒失效、恢复后同一服务继续；但**IOPS在过期期间7次仍保留98%**。已接电且满电100%时`FullyCharged=1`、`IsCharging=0`通过，源表面截图提交仓库；不是尚未满电正在充电、充电控制或安全充电策略的证据。USB物理拔插后的日志通道重连失败，所以插拔更新未验收；低电量、SOC变化后UI及小组件未单独验收。[P1]

主代理本轮目标观测指出魅族设备运行`qti_battery_charger` PMIC GLINK路径；这不证明目标有相同BQ28Z610/UEFI I²C3，也不证明OnePlus ChargerEx ABI可用。应核对目标PMIC GLINK服务/属性编号/单位和远端生命周期，或者目标固件已验证的只读遥测入口，再接复用的电池模型。目标具体电池型号/身份串不在本报告复制。充电控制、限流、温控、快充及退出后所有权是独立后续工作；本源没有实现可直接移植的这部分。[P1][P2][P4]

## 复用优先级与阻断项

| 模块 | 可复用价值 | 对SM8550/魅族的必改项 | 当前阻断项 |
| --- | --- | --- | --- |
| 电池sample/freshness/IOPMPowerSource | 高，板级外耦合较少 | 拆BQ硬编码、目标单位/字段/源和stale UI | 目标可信持续遥测入口；充电控制不在本实现内。[P3][P4] |
| BT H4/队列/固件parser | 中高，边界校验和生命周期可借鉴 | UART/供电/固件协议/身份；原生bridge硬件gate和iOS ABI | 目标控制器与冷启动链路未核；配对/GATT/音频缺验收。[B2][B5][B6] |
| iOS网络接口/STA/WPA2 | 中，原生系统接入可借鉴 | WCN7850/ath12k后端和能力映射，认证范围需扩展 | 目标`17cb:1107`与上游QCA6490不匹配，本源没有目标后端。[W2][W4][W5] |
| SM8350寄存器/PHY/RPMh/DMA初始化 | 仅作为方法参考 | SM8550地址、资源、SID/SMMU、时钟、reset、板级时序重做 | 不能直接运行或改少量常量后视作已移植。[W3][B3][S1] |
| 整包用户态与固件 | 启动/验收组织方式可参考 | 目标本机固件和系统文件获取、校验及构建绑定 | README未交付完整公开已验收包，私有固件/校准/Apple文件不随开源代码提供。[R2][R3] |

## 许可证与分发事实

仓库README将XNU许可指向根`LICENSE`（APSL2.0），用户态指向`licenses/userland-MIT.txt`。MIT文本要求保留版权和许可，且只覆盖该仓库自有代码；第三方保持自身许可。本次核对的几个新增driver文件开头没有独立SPDX声明，不能因为使用Linux参考便直接把全部判成GPL，也不能把整个组合笼统当MIT。[R3][L1][L2]

Wi-Fi构建脚本声明并逐文件检查ath11k的BSD-3-Clause-Clear及一个ISC头，FreeBSD LinuxKPI/headers来自BSD许可输入；`prebuilt/NOTICES.md`提供详细第三方声明。脚本称driver源“unmodified”是原上游构建说明，lemonade另有profile生成器实际修改复制后的ath11k源，移植应保留这些改动来源与原版权。[W4][W7][L3]

Apple系统二进制、原机firmware、board/NVM校准、设备地址和凭据不在公开仓库；README明说不提交这些，BT/Wi-Fi代码也要求本机匹配的私有输入。开源源码许可不自动提供这些外部二进制的分发许可。这是材料边界记录，不是对完整衍生发行包的法律合规结论。[R3][W1][B1]

## 固定提交来源索引

以下均为本次固定主仓库commit的可核对行号；没有引用第三方文章作为实现证据。

[R1]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/README.md#L5-L36
[R2]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/README.md#L38-L54
[R3]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/README.md#L56-L71
[W1]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/docs/WIFI-LEMONADE.md#L3-L58
[W2]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/docs/WIFI-LEMONADE.md#L60-L76
[W3]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/lemonade_wifi.c#L260-L296
[W4]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/devices/oneplus-9/tools/lemonade-ath11k-profile.py#L1-L89
[W5]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/wifi/lx/src/lx_80211.c#L1565-L1855
[W6]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/wifi/lx/src/lx_bus.c#L735-L804
[W7]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/tools/mk-ath11k.sh#L1-L197
[W8]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/wifi/lx/src/lx_base.c#L76-L98
[W9]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/lemonade_wifi.c#L1615-L1663
[B1]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/docs/LEMONADE-BLUETOOTH.md#L3-L184
[B2]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/lemonade_bluetooth.c#L228-L310
[B3]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/lemonade_bluetooth.c#L312-L508
[B4]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/iokit/Kernel/LemonadeBluetooth.cpp#L1-L61
[B5]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/lemonade_bt_compat.c#L1-L220
[B6]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/lemonade_bt_native.h#L1-L190
[S1]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/lemonade_radio.c#L1-L155
[P1]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/docs/LEMONADE-BATTERY.md#L3-L83
[P2]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnuloader/lemonade_battery.c#L1-L165
[P3]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/sys/lemonade_battery.h#L1-L146
[P4]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/iokit/Kernel/RedwoodBattery.cpp#L103-L201
[P5]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/lemonade_battery.c#L40-L122
[L1]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/LICENSE#L1-L14
[L2]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/licenses/userland-MIT.txt#L1-L26
[L3]: https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/prebuilt/NOTICES.md#L1-L102
