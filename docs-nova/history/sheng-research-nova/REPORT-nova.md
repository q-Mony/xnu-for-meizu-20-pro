# Sheng 源码对照与魅族20 Pro下一步 — Nova

2026-10-30（Asia/Shanghai）。新文件，无旧版。按用户要求由三个子代理分别完成CPU/启动、存储/Data、用户态/驱动研究，主代理汇总并核对第五轮实机日志。所有下载、报告和证据保存在Nova，未运行下载代码，未将上游整包替换到本机；本次没有新增生产改动或第六轮镜像。

结论：第五轮E1刷新限频没有观察到端到端提速，当前应先核实CPU实际运行频率与服务准备阶段，再考虑安全复用Data。上游没有现成的更大UFS读取优化能解释本机剩余百秒等待。图形、无线和电池代码中有可用的实现与修复，但必须按魅族硬件和当前系统ABI逐项接入。

## 本机计时与优先级依据

| 版本 | Boot→苹果 | Boot→Hello累计 | 结论 |
| --- | ---: | ---: | --- |
| 上层256KiB读取/签名SHA，同镜像两次 | 90 / 80秒 | 237 / 190秒 | 同镜像波动47秒仍未完全解释 |
| 底层128KiB合并 | 97秒 | 245秒 | cs4k下降，未观察到端到端改善 |
| 根盘数据SHA | 73秒 | 212秒 | 同工作量cs4k44.002→20.209秒；不是Hello139秒 |
| lsd诊断 | 76秒 | 212秒 | lsd等待仍近100秒 |
| E1刷新限频 | 73.03秒 | 220.56秒 | 用户已确认；单次比上一轮多8.56秒 |

第五轮相同23项cs4k总19.356秒；SpringBoard同线程等待launchd≥36.278秒、lsd≥101.427秒。包围lsd可见pending样本的rootio窗口101.520秒，已计量read/verify/copy分别1.190/0.230/0.022秒；对应状态绘制窗口100.247秒仅0.509秒绘制、零滚屏。各窗口不能相加，不一定覆盖完整请求寿命，也不排除未计量的文件系统、锁和调度等待。现有证据不支持继续把E1绘制或实际根盘传输作为主要解释。

三次Setup scene-create watchdog仍在保存的日志中；捕获延续到Hello之后，不能把三个20秒额度加成启动预算。独立复核确认计时、23项工作量和窗口限制正确。详见[第五轮报告](<nova-root>/artifacts-nova/boot-speed-round5-nova/REPORT-nova.md)及[独立复核](<nova-root>/artifacts-nova/boot-speed-round5-nova/device-analysis-nova/independent-review-nova.md)。最后设备记录为Android B，A保留第五轮镜像，B原哈希未变。

## 页面和仓库身份

用户给出的页面是小米平板6S Pro（sheng）。页面源记录固定为UniXNU-sheng `08cde20edc3254ed6f7df8d0f5384fb887786e8f`；该设备使用iPadOS17.7.11/21H461，本机当前iOS16.7.16/20H392。页面的implemented描述源码实现范围，不能当作魅族验收。

特别区分三类版本：页面证据链接、板级实际固定子模块、抓取时仓库提交。UFS页面链接是`e2b32cc`，但板级实际gitlink是`f2d6a4a`；共享UniXNU的`8d157ee`仅被页面用于一份UEFI交接文件，不是所有组件的版本锁。各专项报告已保存完整commit、文件SHA256和下载失败记录；未固定到的最新ArmCpu差分明确保留为限制。

页面副本和链接索引在[页面来源](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/source-manifest-nova.json)与[链接索引](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/page-links-nova.json)。研究聚焦页面下方13类技术清单及其关联仓库，没有将侧栏全部设备视为本机可直接使用的代码。

## 13类清单的覆盖和处理

| 页面分类 | 已核对的内容 | 魅族处理与计划位置 |
| --- | --- | --- |
| 配置 | 固定提交、SoC/OS profile、组件选择 | 保留本机20H392 ABI，P01/P03逐项绑定实际输入 |
| 固件与启动 | FDT initrd、payload发布、GOP和内存交接 | 未找到直接缩短73秒的固件补丁；保留现用固件，补阶段分界，P02/S01 |
| CPU与中断 | EPSS探针、SMP/IPI、AMP/Edge、idle helper | 优先只读频率观测；SMP与调频分开实验，不照搬八核参数，S01.1/X12 |
| 内存与DMA | CPU频率窗口、UFS/GPU/Wi-Fi/DPU各自映射 | SID、窗口、生命周期按本机核验，P03/W03/G02/D02 |
| 存储与根卷 | ShengUfs、VerifiedRoot、RamBlock新策略 | 保留16KiB认证/SHA/分区选择；暖Data先补恢复一致性，S03 |
| 显示 | GOP与DPU扫描输出，显示archive同时装入其他板级对象 | 不整包移入显示archive，面板/stride/扫描缓冲单独验收，D01–D04 |
| 输入 | 上游Novatek协议与板级资源 | 本机Goodix不能替换协议，只借鉴队列/生命周期等结构，T01–T05 |
| GPU | A740/Metal/BO/SMMU实际新差异 | encoder空返回修复优先于优化；当前GPU关闭，不能解释现有lsd等待，G05/X09 |
| 网络 | WCN7850/Ath12kWifi、PCIe/MHI/QRTR、WPA2与Apple80211 | 硬件方向更接近本机，按W01–W07逐层接入；不从Wi-Fi清单推导蓝牙已支持 |
| 音频 | ADSP/GLINK/GPR、板级codec/扬声器和HAL方案 | 共享通信可辅助电池任务；本机codec/线路未验证，不导入平板路由，不新增已支持声明 |
| 电源 | PMGR ABI、EPSS频率、PMIC GLINK/battmgr | 保留本机PMGR；电池首期只读，明确阻断USB_SET等控制写入，B01–B07/X12–X14 |
| 镜像组装 | kernel ADT与固件FDT、addon、source pin | 沿用Nova固定封装和哈希链；不混入Sheng的系统包或Data seed，P01/I03 |
| 诊断 | 状态/IPC/频率/图形统计及源码、host、实机证据层级 | 保留有界日志与每轮单变量；不把上游PASS当本机通过，P02/S01 |

专项依据：[CPU与启动](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/REPORT-nova.md)、[存储与Data](<nova-root>/artifacts-nova/sheng-research-nova/storage-nova/REPORT-nova.md)、[用户态与驱动](<nova-root>/artifacts-nova/sheng-research-nova/services-drivers-nova/REPORT-nova.md)。

## 真正可迁移的内容和限制

1. **CPU频率观测优先。** 本机DTB确认EPSS三个窗口`0x17d91000/0x17d92000/0x17d93000`及3/4/1域划分。当前成功payload为boot-only选择，board.toml的八核参数不代表实测八核在线。先读原始enable/state/request/LUT，验证时钟换算；现阶段还不知道XNU是否处于低频。
2. **不要直接搬完整governor。** 上游固定遍历8核，未注册CPU的idle helper返回0，会被算成100%忙；需在线核过滤。上游PMGR还有iPadOS17 ABI绑定，本机只读探针无需替换PMGR。
3. **Data保留策略有用，但不能直接打开持久化。** RamBlock `4be2d7eb05ef5a98126c91e0302c149140c0e3e1`新增Kept/显式初始化，避免普通启动自动覆盖不匹配overlay；仍未覆盖部分恢复失败、旧有效overlay重置中断等路径，也缺可证明成功的干净关闭流程。当前mode0不打开overlay，这项补丁单独不会提速。
4. **UFS整包升级不是启动方案。** 页面版与板级固定版之间主要新增Wi-Fi/DPU映射和固件读取，根读取上限仍128KiB；整包覆盖会丢失本机分区选项、16KiB认证、数据SHA、owner-root与rootio统计。更大预读/全盘预载只有在重复块命中统计支持后才值得做。
5. **服务裁剪不作为修复。** `sheng-lite=1`实际禁用多项服务，并不是通用lsd修复。不能通过删除本机所需业务或掩盖watchdog制造启动收益。Data复用、容器/数据库初始化和服务依赖继续按实际日志拆解。
6. **图形修复记录为后续具体工作。** 已取得新版Metal encoder拒绝路径、跳过全盘metallib遍历、BO查找及按页TLB操作的固定源码；分别绑定G05/X09验证。PrimeShaders等已有部分不能重复计为新优化。没有启用GPU的本轮不预估这类补丁的Boot收益。
7. **电池遥测必须与充电控制分离。** 上游设置SDP电流参数为0并不保证完全只读，CDP仍可能发送1.5A请求；魅族首期应从协议层阻断USB_SET，先建立真实属性采样、单位和失效处理。Wi-Fi和电池可共享部分远端通信设施，但资源和固件仍须按魅族核对。

## 下一项：S01.1受控CPU频率观测

这是已完成的实验设计，尚未实现/构建/刷入。首个候选只读boot domain0的少量寄存器，采样一次后解映射，日志重复输出时保留相同sample_id/sample_ticks。它只验证首次访问及该时刻状态，不能证明随后100秒一直低频。访问与LUT/时钟语义核验后，才单独增加阶段采样和CPU活动观测。整个诊断路径按以下假设区分原因：

| 顺位 | 假设 | 可以推翻或支持它的信号 |
| --- | --- | --- |
| 1 | 当前可用CPU持续做初始化工作，但频率策略没有建立 | 服务慢区间的state/request/LUT显示低档，并有CPU活动证据；频率正常或主要睡眠则转向服务依赖 |
| 2 | 每次从同一Data seed重新建立数据库/容器状态 | 可控且安全的暖Data对照减少扫描量和等待；只改变seed而没有一致性证明不算有效实验 |
| 3 | lsd/launchd下游依赖或锁等待导致延迟 | 请求/任务代次和执行、睡眠区间能定位到具体依赖；频率变化不减少该等待则削弱算力假设 |

实施约束：独立小模块、默认关闭、有限寄存器范围、只读原始字；首个访问候选不扫描全部LUT，LUT摘要在访问验证后单独扩展；使用现有有界日志；不引入governor、L3 vote、SMP、服务裁剪或Data写入。保留raw ticks及阶段标记，避免只记录一个难以解释的MHz值。

实现前仍需核定MMIO故障边界：本机`ml_probe_read`及`ml_probe_read_64`是panic占位，`ml_nofault_copy`也不是MMIO异常隔离。既有USB GCC探针可借鉴映射/解映射与独立构建方式，但映射成功不证明硬件访问一定安全，有限读取次数也不能避免总线异常。源码设计和最终产物必须如实说明这个边界；不能把探针失败静默解释为正常零频率。

验证要求：首先检查关闭路径零映射/零读取、一次性guard、偏移白名单、快照发布及重复输出保留原时间；扩展LUT时再做真实解析函数的坏表/全零/越界/终止条件host检查；对象与最终kernel确认没有性能窗口写入；新候选沿用现有封装、Data和服务配置；上机记录访问进展、实际频率状态和原Boot→苹果/Hello累计计时。若频率证据支持，再单独设计受限非turbo档对照；不把请求值变化当作实际提频，更不预先承诺分钟级收益。

## 完成与未完成

已完成：三个子代理的固定源码研究、13类清单覆盖、候选迁移项与故障路径识别、第五轮日志及用户计时归档、下一项可验证实验设计。来源校验见同目录`verification-nova.json`。

未完成：启动慢的最终根因修复、CPU探针真机测量、暖Data闭环、新驱动在魅族上的验证。没有以网页的implemented、上游测试或静态推演代替这些结果。开发状态以[总计划](<nova-root>/artifacts-nova/meizu-development-plan-nova/PLAN-nova.md)为准。
