# sheng 服务、用户态与驱动适配研究（Nova）

日期：2026-10-30，Asia/Shanghai。研究范围为用户态启动、显示/GPU、触控、Wi-Fi、蓝牙和电池；CPU/SMP、UFS/Data 的深入分析由其他研究代理负责。本报告及附件均为新文件，无旧文件；只写本研究目录，未改生产源码、未构建、未执行下载代码、未操作设备。中央 CHANGELOG 由主代理统一追加。

**当前优先级仍是解决 lsd 的长时间等待。** 第五轮用户确认 Boot→苹果 73.03 秒、Boot→Hello 总计 220.56 秒；主代理在本轮日志中测得 SpringBoard 等 lsd 至少 101.427 秒，对应区间根盘 read 1.190 秒、verify 0.230 秒、状态绘制 0.509 秒。此处引用主代理已完成的日志分析，不将网页性能数字当本机实测。当前成功候选 GPU 关闭，以下 GPU 优化均不能解释这一轮启动耗时。

sheng 是 **Xiaomi Pad 6S Pro（小米平板 6S Pro）**。板级 profile 为 iPadOS 17.7.11 / 21H461；魅族当前 root 为 iOS 16.7.16 / 20H392，内核 Darwin 23.6，不能把两个用户态 ABI 视为相同。证据：[sheng 配置](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/board.toml:1)；本机版本沿用主代理当前候选绑定结果。

## 1. 来源固定与证据分级

网页地址：`https://unixnu.com/zh/devices/sheng`。已保存 [页面原文](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-page-nova.html)、[链接清单](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/page-links-nova.json) 和 [完整下载与 SHA256 清单](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/source-manifest-complete-nova.json)。读取时网页的功能描述与其部分代码链接并非同一时间快照，因此本报告分开标明来源，绝不称整个页面有一套完整已锁定依赖。

| 来源 | 固定提交 | 使用范围 |
|---|---|---|
| UniXNU/UniXNU-sheng | `08cde20edc3254ed6f7df8d0f5384fb887786e8f` | 网页直接指向的板级源码、porting.toml、board.toml、Git tree |
| UniXNU/UniXNU | `8d157ee18ae9c4d518aba28cb3040a84e860dcd1` | 网页某个 UEFI 文件引用；仅用其 tree 作旧公共快照对照，**不是整个 sheng 构建锁** |
| AdrenoMetal 旧公共快照 | `454ff2ce83766ce7b3e0f355e83f1d72907708b3` | 与 Nova 字节级相同的 frontend / PrimeShaders 对照 |
| AdrenoMetal 本次当前固定版本 | `ce5e19e33e05e1873d38d818e23cabfe0ac708c3` | 通过只读 git ls-remote 固定，然后只下载该 commit 的源码 |
| Adreno740 本次当前固定版本 | `5c8c1453c67d6ef6b10ec43ab1f8fc570a2f33e1` | GPU BO 查找/缓存等源码差异 |
| ArmSmmu4K 本次当前固定版本 | `3ba92451bd44bb742d11758fbfcd3a446ecad685` | 按页 TLB、增量映射验证等源码差异 |
| Ath12kWifi | `f6bf561f92cc42761bdd417fce1d53fe1db9477c` | 来自板级 tree 的 gitlink；取得 README 和 include/nw_abi.h |

本次还固定了 UniXNU HEAD `066c8d8315f83749d28bcae2faf3595718646be5` 的 components.toml。查询结果与时间见 [当前引用记录](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/component-current-refs-nova.json)。不继续追 HEAD。

证据标签：**本地已有** = 对比得出已有实现；**新提交有差异** = 已保存固定新源码并与 Nova 比较；**板级源码已核实** = 有 sheng 源码但尚未移植；**仅页面声称** = 只作为上游报告，不当真机复验。字节比较和完整差异见 [对比清单](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/comparison-summary-nova.json)、`diffs-nova/`。

## 2. 对当前 Boot→Hello 的直接帮助

| 项目与证据级别 | 源码实际做什么 | 对当前魅族启动的判断 | 最小实验及验收 |
|---|---|---|---|
| sheng-lite：板级源码已核实；本机未有同等服务列表策略 | 生成 launchd disabled.plist，禁用 identityservicesd、locationd、CommCenter、bluetoothd、photoanalysisd、fairplayd.A2、coreduetd 等约 22 个服务；注释说上游发现同步 XPC 等待环及重启/CPU争用。 | 是带功能取舍的 bring-up 策略，不能宣称“修好了 lsd”。蓝牙/网络后续适配时须恢复对应依赖。不能整套复制后拿变短时间当完整系统优化。 | 先用本机 20H392 的实际标签和调用链确认一个争用/等待对象，再以可撤销 bootarg 只改变这一项；记录 lsd CPU、就绪时刻、注册数量、服务退出次数和 Hello。没有对应调用链则不启用。 |
| boot-ui-follow：板级源码已核实 | 从 SpringBoard 采样记录中选第一个可解析对端，再追其对端；支持 focus2/3/4 定点采样。 | 可改进当前“在等谁”的观测，本身不提速；它只是采样候选链，不是完备等待图，也不证明所有非零 Mach 返回都是失败。主代理已有 lsd 诊断优先复用。 | 若当前日志无法区分 lsd 的 CPU 执行、调度等待和同步 IPC，再只补缺失字段，使用有界采样；检查采样开/关基线，避免诊断成本混入收益。 |
| sheng-staged-apps：板级源码已核实 | 在 launchd 前将被 Data 挂载遮住的 staged_system_apps 从 OS 拷到 Data；标记完成，mobile 所有权，1 MiB读取块、每文件 fsync、迭代遍历。 | 解决可卸载系统应用及 widget 缺失，不是“跳过应用注册”的加速器；首次拷贝还会增时。21H461 的目录、清单、权限不能直接用于 20H392。 | 只有本机日志证明系统应用迁移/占位符缺失才做；先核对源目录和迁移器，再验证完整拷贝与第二次启动跳过拷贝，不与本轮 lsd 实验混做。 |
| watchdogd policy：本地已有受限版本 | sheng 默认关闭未实现硬件 watchdog 后端；Meizu现有实现只允许成功挂载的易失 RAM Data、拒绝持久store、O_EXCL不覆盖既有策略。 | “关闭 watchdogd”本地已有；不能靠额外关闭 Setup/chronod 超时来证明启动问题解决。保留 Meizu 已有 Data 保护约束。 | 无新的性能实验；仅对当前候选的有效 bootargs 与 policy 状态做绑定核对。 |

源码：[lite与watchdog策略](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/BootWatchdogPolicy.c:1)；[等待链采样](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/BootProcessProbe.c:92)；[首个对端选择](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/BootUiTraceProbe.c:281)；[staged_system_apps补丁](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/shared-pinned-nova/patches/xnu/zzy-staged-system-apps.patch:8)；[Meizu现有watchdog保护](<nova-root>/unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c:15)。

**本次没有找到能直接移植、已经证明消除当前约 101 秒 lsd 等待的上游补丁。** 最值得借鉴的是服务调用链诊断和逐项可撤销隔离；网页“更多硬件已工作”不能作为 lsd 或 Setup 等待已消失的证据。建议下一轮继续锁定 lsd 的执行/调度/IPC阶段，不再同时换读取尺寸、UI刷新、驱动和服务清单。

## 3. 显示与 GPU：真实可迁移差异

| 项目 | 证据状态与差异 | 迁移条件、最小验收 |
|---|---|---|
| shader后台预热基本能力 | **本地已有。** Nova PrimeShaders.h 与旧固定AdrenoMetal `454ff2ce`字节相同；不要再次“新增全系统shader预热”。 | 首先确认GPU候选实际加载此plugin、cache可写、prime日志可持久读取。 |
| 原生GPU context基本类 | **本地已有。** NativeAdrenoGpuContext.cpp 与本次新 Adreno740 提交仍字节相同。 | 不把同名组件重拉当优化。 |
| encoder拒绝路径 | **新提交有差异。** runtime创建encoder失败时，旧版返回nil；新版仍返回绑定commandBuffer的encoder，让该buffer以错误结束。上游源码说明RenderBox缓存IMP，nil会导致调用地址0。范围是runtime拒绝路径；foreign attachment等提前检查仍可return nil。 | 可单独移植并测试null handle下set/end/commit、错误状态、完成回调恰好一次、不提交GPU工作；再验证WidgetRenderer不重启。此项是后续GPU稳定性修复，当前GPU关闭无启动收益。 |
| shader目录遍历、空闲门限与cache身份 | **新提交有差异。** 新版有有效primelist即不再全目录walk；每250ms提交数>8时延后编译、恢复安静2秒后继续，最多等10秒；prime完成身份加入runtime与Turnip build。旧版始终扫描列表之外目录。 | 先单独移植“有效列表不重复遍历”；按本机root生成清单，不能带162这个上游数量。门限按60Hz/当前UI行为重新测。冷/热cache分组，检查所有目标库可发现、持久缓存命中、Hello与后续动画不回退。 |
| BO handle、CPU view查找 | **新提交有差异。** UserClient由线性find变索引查找、计数live对象与空槽hint；Publish由遍历view数组变token-1直达槽。 | 须连同相关header/index生命周期成组移植，不只拷.cpp。验证释放后token复用、失败分配/隔离对象、重复unmap、客户端销毁；再测CPU map/unmap分位数。 |
| GPU写入目标缓存属性 | **新提交有差异。** 新版只允许GPU不写的导入内存使用coherent映射；带写权限的import不走该路径。注释记录GPU在end-of-pass cache clean卡住的问题。 | 先核对本机cache-coherence证据；用可重复离屏读回与sysmem渲染验证内容、fence、GPU fault。不能把UI看起来正常当数据一致性验证。 |
| ArmSmmu4K小范围unmap | **新提交有差异。** ≤512页用TLBIVAL逐页invalidate并TLBSYNC，大范围退回ASID；旧版unmap整ASID。 | 要求独占CB/ASID、固定上层页表、map/unmap串行、失败保留资源。复用/扩展host生命周期和失败注入测试，再测GPU频繁map/unmap、无SMMU fault和错误像素。 |
| ArmSmmu4K增量检查 | **新提交有差异。** 新版复用alias类计数，增量更新跳过已验证既有range反复扫描，并新增exclusive契约。 | 和ownership/generation/调用者契约共同审查；不能删除验证只追速度。与按页TLB拆开验收，免得无法归因。 |
| 提前触发completed handler | **新提交保留代码但已禁用。** earlyOn()明确return false，注释记录blur读到了未完成frame。 | 不开启。源码含“优化”代码不代表有效或可用。 |
| “325 maps均值0.20ms、idle130fps”、对象16384、submit不再flush等 | **仅页面声称或本次未取得对应完整实现。** 前两个性能数字属于上游某次sheng运行；未把所有数字对应到完整构建。 | 不对魅族承诺这些数值；再启GPU时以本机日志与构建hash绑定。

证据：[encoder修复](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/adrenometal-current-pinned-nova/NativeBridgeMetalFrontend.mm:1064)；[Nova旧encoder路径](<nova-root>/unixnu-main-nova/drivers/NativeMetal/NativeBridgeMetalFrontend.mm:834)；[新primelist和空闲调度](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/adrenometal-current-pinned-nova/PrimeShaders.h:80)；[prime缓存身份](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/adrenometal-current-pinned-nova/PrimeShaders.h:168)；[BO索引/缓存权限](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/adreno740-current-pinned-nova/NativeAdrenoUserClient.cpp:35)；[CPU view直接索引](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/adreno740-current-pinned-nova/NativeAdrenoPublish.cpp:161)；[页TLB维护](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/armsmmu4k-current-pinned-nova/ArmSmmu4KProgram.c:190)；[增量映射和unmap](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/armsmmu4k-current-pinned-nova/ArmSmmu4KLive.c:324)；[明确禁用early completion](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/adrenometal-current-pinned-nova/NativeBridgeMetalFrontend.mm:1017)；[页面构建说明与上游观测](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/porting.toml:148)。

显示板级边界：sheng的GopFramebuffer/LegacyDisplayProbe、Adreno740/Metal与魅族当前1080×2400软件显示不是同一个生效栈。共享A740不代表GOP布局、panel、GMU votes、ZAP固件可直接通用。构建脚本把公共Sm8550Platform与板级ShengGmuVotes分开，并锁定shader/GMU/ZAP输入。应保留魅族面板尺寸、缓冲区格式/stride、供电/firmware来源；GPU bring-up先离屏读回，再接显示，不把120Hz参数直接塞进60Hz软件vsync路径。证据：[GPU构建入口](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/scripts/build-adreno740-kits.py:1)；[显示probe](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/LegacyDisplayProbe.cpp:414)；[本机图形基线](<nova-root>/artifacts-nova/hackios-port-review-nova/upstream-graphics-userland-nova.md)。

## 4. Wi-Fi、蓝牙、触控、电池的可复用边界

| 功能 | 已核实可复用部分 | 魅族必须补齐的前置 | 分阶段验收 |
|---|---|---|---|
| Wi-Fi | **板级源码已核实。** sheng和本机均WCN7850 / PCI17CB:1107、RC0；Ath12kWifi提供纯C nw_abi，把allocator/DMA/fw/cfg/MSI/clock与station队列接口分开。 | 重新绑定魅族PCIe0、power/clocks/rails、PERST/WAKE、SID/IOMMU、MSI、固件revision/board data/NVM；不能拷小米校准数据。本机已有GPIO80/81/82线索相近，仍需由本机DT及枚举核对电源依赖。 | 1只读identity；2独立PCIe link；3DMA/MHI/QMI/固件ready；4scan；5WPA2关联和DHCP；6RX/TX、重连及退出清理。每阶段不能用“模块已加载”替代成功事件。 |
| iOS Wi-Fi界面接入 | **板级源码已核实。** WifiNet创建Ethernet ifnet、无线registry与Apple80211 ioctl适配；scan1164、assoc592、channel-list4824字节有static_assert。 | 这些私有布局来自17.7.11，必须按本机20H392核验IOCTL长度、字段、请求码、事件和daemon期望；假IO80211类名发布不等于原版完整驱动ABI。 | 超界请求必须拒绝；确认系统能scan/连接/断线，netstack与UI状态一致；密码持久化、WPA3、休眠唤醒另立项。 |
| 蓝牙 | **无已取得的sheng完整HCI实现证据。** 页面功能列表、Wi-Fi成功都不能证明蓝牙已支持。 | 本机仅有Kiwi/QCA6490候选DT、GENI HS UART0x898000等线索；需实际HAL/init/固件/transport绑定，尚不能选定HCI实现。 | 独立线路：UART/HCI reset/version→固件下载→scan→配对→ACL；与WLAN共用GPIO/供电需协调。不要从sheng-lite禁用bluetoothd倒推驱动支持。 |
| 触控 | **硬件不同，不能直接移植控制器驱动。** sheng是Novatek SPI，TouchBoard为GENI0xa90000、19.2MHz SPI、GPIO44–47；魅族是Goodix Berlin D/GT9916，I²C0x5d、IRQ25/reset24、1440×3200。 | 共用的是SM8550 GENI/clock/IRQ概念与HID事件层；本机已有Goodix/RPMh/HID代码，当前probe用100kHz软件I²C。以成功payload与meizu-touch=4为准，不用旧.config否认现有触控。 | 不改控制器协议；先验证当前单点/多点、release/取消事件、坐标缩放与旋转、长按/无触摸时CPU，再单独恢复硬件I²C传输。 |
| 电池/充电信息 | **协议类型相近、板级源码已核实。** sheng使用ADSP→GLINK→PMIC_RTR_ADSP_APPS、owner32778；本机Android实测qti_battery_charger也是pmic_glink。可复用codec结构、异步单请求状态机、消息ring与IOPMPowerSource发布方式。 | 本机ADSP carveout、PAS/DTB、SMEM/SMP2P/GLINK、远端charger_pd和board property表必须重做。sheng属性11后偏移1、双fuel gauge0x8a/8b及0xa6/a7为小米板级，容量算法不可照抄。 | 先仅读SOC/电压/电流/温度/在线状态，分别与Android白名单遥测对照单位/符号；通知/断连/2秒timeout能恢复、不阻塞Boot；最后才集成UI。充电策略与快充另立项。 |

Wi-Fi证据：[本机实测硬件基线](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md:9)；[WCN7850板级transport](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/WifiProbe.cpp:1)；[Ath12k板级接口](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/ath12kwifi-pinned-nova/include/nw_abi.h:1)；[Apple80211布局](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/WifiNet.cpp:73)。上游porting.toml声称WPA2 5GHz/80MHz、31秒内DHCP及吞吐数据，属于**页面/上游观测**，没有本机验证，也没有本研究重新跑网络测试。

触控证据：[sheng SPI板级常量](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/TouchBoard.h:7)；[Meizu Goodix板级常量](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchBoard.h:8)；[当前触控bus实现](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchProbe.cpp:435)。

电池关键注意：不能把上游驱动称“全只读”。ShengBattmgr默认SDP设900mA；`sheng-usb-sdp-ma=0`只令SDP不提高限制，`wanted_limit()`对CDP仍返回1.5A，然后会发USB_SET opcode0x33。**魅族第一阶段必须在codec/策略边界显式禁发所有USB_SET，不能仅传这个bootarg。** 证据：[默认900mA及USB_SET生成](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/ShengBattmgr.c:57)；[CDP仍返回1.5A](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/ShengBattmgr.c:119)。这是本次源码直接发现的行为，不依赖页面说明。

ShengBattery的消息回调仅复制到16×64字节ring并触发event；工作loop负责解析，2秒请求timeout、60秒refresh、慢字段每10轮，适合借鉴为不阻塞启动的服务。ADSP本体则包含sheng固定内存、SMEM item、PAS、IPCC IRQ和firmware装载，不能复制常量。证据：[异步电源源实现](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/ShengBattery.cpp:1)；[board property与单位](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/ShengBattmgr.c:4)；[ADSP板级常量](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/package/AdspProbe.cpp:44)。页面还记载低电量关机在unmount/vnode_rele_ext panic，应作为未完成边界，不当作成熟关机路径。证据：[电池限制与观测](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/sources-nova/sheng-pinned-nova/porting.toml:234)。

## 5. 建议纳入主计划的开发项

| 顺序 | 独立开发项 | 完成标准 |
|---|---|---|
| P0 | 解释当前lsd等待：本机调用链、CPU执行/就绪/同步IPC阶段归因 | 101秒长区间能被实测阶段解释；一次只改一个原因，Hello计时同口径；保留Setup和业务服务完整度记录。 |
| P0条件项 | 日志指向的单服务依赖隔离或注册路径修复 | 有20H392真实调用链；该服务恢复/替代后的功能结论明确；启动收益重复可见，不能只靠关闭失败的服务结束等待。 |
| P1 | GPU离屏和显示生效栈确认 | hash/bootargs/driver绑定一致，fence与像素读回正确，故障不会破坏现有软件显示基线。 |
| P1 | encoder拒绝路径修复 | 不返回导致IMP地址0的对象缺失；拒绝状态和回调正确，WidgetRenderer不循环崩溃。 |
| P1 | shader primelist与cache身份/空闲调度 | 有效清单不全盘遍历，冷/热cache分别验证；不抢Hello前主线程/渲染工作。 |
| P2 | BO索引、GPU写入缓存属性、页TLB分别迁移 | 独立生命周期测试及真机fault/读回检查通过，记录CPU/GPU耗时而非仅凭FPS观感。 |
| P2 | 本机ADSP/GLINK只读电池遥测 | 全部限流写路径禁发；单位和有效位与Android一致，断连可恢复，Boot无新增同步等待。 |
| P2 | WCN7850分层bring-up | PCIe/DMA/固件/scan/关联/DHCP依次达到；最后适配20H392 Apple80211用户态。 |
| P2 | 现有Goodix触控回归与硬件I²C | 多点/释放/坐标正确、idle CPU降低；不换Novatek驱动。 |
| P3 | 蓝牙独立transport与系统接入 | 硬件身份及HCI通路已证实，再做配对和数据链路。 |

所有项目均为研究建议，未实施、未宣称真机提速。GPU/电池/Wi-Fi应在P0启动定位稳定后逐项推进，避免一次增加多个常驻线程、firmware加载与电源操作而失去归因。

## 6. 完整性与不足

保存63个来源文件及SHA256；形成9组Nova/上游比较，旧frontend、旧PrimeShaders、当前GPU context三组字节一致，其余已保存diff。下载中Ath12k完整tree受GitHub API限流403、尝试的NativeMetalServer.mm路径404；失败在清单中原样记录，未据此推断仓库没有其他实现。Ath12k仅核实公开接口及板级集成，未逐行审计整个移植Linux无线栈。

未取得完整构建依赖锁和上游真机原始日志；网页的吞吐/FPS/host-test通过数值只算上游报告。本研究未执行下载代码或其测试，未访问用户网络凭据、MAC、SSID或其他私人身份数据。CPU/SMP、UFS/Data及其他页面类别由并行报告覆盖。本报告足以筛出明确可迁移的源码差异与适配前置，不能替代下一轮魅族真机验收。
