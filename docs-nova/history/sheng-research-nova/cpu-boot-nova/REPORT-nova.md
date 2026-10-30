# Sheng CPU / boot 对照研究 — Nova

2026-10-30（Asia/Shanghai）。新增报告，无旧版本。研究范围：CPU、SMP、GIC/IPI、调度、PMGR/频率控制和固件交接。只读分析并保存公开源码；没有运行下载的脚本，没有改生产源码/启动参数，没有构建或操作设备。主代理统一记录 CHANGELOG。

## 可直接推进的结论

优先做一个**默认关闭的 EPSS CPU 频率只读探针**。当前 Nova 尚无此调频模块；本地魅族 DTB 已证明三个 CPU 频率窗口及 3/4/1 核分组与 Sheng 的来源一致。先测实际状态，能判断低频是否值得作为后续加速方向；当前证据不能断言低频是 220.56 秒启动的原因，也不能承诺调频能消除 lsd 等待。

Sheng 的 `CpuFreqProbe.cpp` 是独立板级模块，随显示 archive 链接，**不是** `ShengPlatformPower.cpp` 的一部分。没有必要为了频率观测替换 Nova 已工作的 ApplePMGRNub ABI 适配或先打开 SMP。完整 mode4 governor 不能照抄到当前 boot-only 环境：它会把未注册 CPU 的零 idle 时间计算为 100% 忙。

本轮父代理最新真机结论：Boot→苹果 73.03 秒、Boot→Hello 220.56 秒；lsd pending 下界 101.427 秒；相应 100.247 秒可见采样窗口仅绘制 0.509 秒、零滚屏。这使 E1 绘制难以解释主要停顿。此处转录父代理结果，最终时序与原始第五轮日志以主报告为准；本研究不重复归因。

## 固定来源与证据边界

| 来源 | 固定版本 | 用途 |
| --- | --- | --- |
| UniXNU/UniXNU-sheng | `08cde20edc3254ed6f7df8d0f5384fb887786e8f` | 页面源记录、板级参数、CpuFreqProbe、AMP 和 idle 补丁 |
| UniXNU/UniXNU-sm8550-common | `23b2049fcd9e30356161def84231334fb55ecca9` | 由板级 feeds/lock 锁定；SoC CPU/GIC DTSI |
| ianchb/sm8550-mainline | `42f3b40c702abbc37c3a04c95af008cd283aeaf3` | 公共 SoC README 引用的硬件来源；核对 EPSS、L3、SM8550 DTSI |
| UniXNU/UniXNU | `8d157ee18ae9c4d518aba28cb3040a84e860dcd1` | 板级记录明确引用的 UniXNURamDiskDxe 源码；仅适用于该交接文件 |

页面所指设备是 **Xiaomi Pad 6S Pro 12.4（sheng）**。其板级配置为 iPadOS 17.7.11 / 21H461，而 Nova 为 iPhone10,3 / iOS 16.7.16 / 20H392 ABI 目标。页面将源码覆盖、host check、device observation 分开；`implemented` 不表示在魅族上已经验证。Sheng 还有 GPU、触控、持久 Data、staged apps、`sheng-lite=1` 等不同输入，因此不能拿它的整体启动表现直接对比 Nova。[源记录](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/porting.toml)；[板级配置](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/board.toml)。

Sheng 的 board lock 没有锁住 UniXNU 主仓库和 ArmCpu 的当前有效来源。上列主仓提交只因被记录引用而下载交接文件，不能拿来声称 ArmCpu 也运行于该版本。GitHub tree API 返回限流；固定主仓路径中的 ArmCpu 和 work-interval patch 返回 404；Git 远程查询受本机连接配置阻断。因此本报告没有“已完成最新 ArmCpu 全量差分”结论，也没有声称找到新的 GIC/IPI 修复。下载失败同样保存在 manifests。

本地来源副本均在 `sources-nova/`，SHA256、URL、固定 commit 和下载时间见该目录的 `*-manifest-nova.json`。关键 CPU 源：`CpuFreqProbe.cpp` SHA256 `e769faf92d195e9151a4554b15af254bde5a83ec7f0aafd8c3475d1f9d7e2bfe`。原文件保留版权/许可头，未修改。

## CPU 频率：实际寄存器、魅族依据与下一轮探针

### 魅族 DTB 已核对的部分

直接解析 Nova 的两个现有固件 DTB，二者字节相同，SHA256 为 `a28fd23de748395474cc8723272854c61883dcd06d19aa85286045164fddf720`。不是仅依据“同为 SM8550”推断。

- `/soc/qcom,cpufreq-hw` 的 compatible 是 `qcom,cpufreq-epss`，phandle 为 `0x5`；三个 `reg` 窗口分别为 `0x17d91000/0x1000`、`0x17d92000/0x1000`、`0x17d93000/0x1000`。
- `cpu@0/100/200` 的 `qcom,freq-domain` 为 `<0x5 0>`；`cpu@300/400/500/600` 为 `<0x5 1>`；`cpu@700` 为 `<0x5 2>`。这证明 DTB 声明的 3/4/1 分组，但不是运行在线数。
- DTB clocks 以 `xo`、`alternate` 命名。尚未通过该魅族 DTB 的完整 clock graph 和真机寄存器确认频率换算值，探针应保留原始字，不能只输出换算 MHz。

机器可读证据：[nova-dtb-cpu-evidence-nova.json](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/nova-dtb-cpu-evidence-nova.json)。DTB 文件是 Nova 已有输入；本次未重新提取设备固件，也未把固件 DTB 当作 XNU 实际 ADT。后者需在打包时另行核验。

### 寄存器清单

下表来自固定 `CpuFreqProbe.cpp` 59–67、81–126、137–155 行，并与其引用的 Linux `qcom-cpufreq-hw.c` 414–422 行 EPSS 表对齐。窗口来自上面的魅族 DTB；Linux 行号和转换代码在本地固定副本中可复核。

| 读项 | 三个 domain 内的偏移 | Sheng / Linux 解释 | 首轮用途 |
| --- | --- | --- | --- |
| enable | `0x000` | bit0 表示 domain 启用 | 未启用时记录原始值，不进行写入或假定频率 |
| domain state | `0x020` | `raw & 0xff` × XO；Sheng 以 19.2 MHz 换算 | 同时保存 raw 和假定换算 MHz；这是状态读数，不是周期实测 |
| DCVS control | `0x0b0` | bit0 表示 per-core performance-state rows | 判断 request 表布局 |
| frequency LUT | `0x100 + 4*i`，最多40行 | source bits31:30、core-count18:16、L bits7:0；相邻频率重复终止 | 有界读取，保存 raw/解析值、最高非 turbo 项和异常标志 |
| requested performance index | `0x320` | 当前请求的 LUT 索引 | 索引先做范围验证，再查 LUT；区别“请求值”和“状态值” |
| 每核请求（DCVS bit0=1时） | `0x320 + 4*c` | 各 domain 的 c 范围分别0–2、0–3、0 | 只读，不假定所有 CPU 在线 |

Sheng 的 `lutKHz` 把非零 source 解释为 `19200 × L` kHz、source0解释为300000 kHz；`pickLevel` 排除 core-count=1 的 turbo 项，并应用频率上限。Linux 以实际时钟来源求 XO/alternate，而 Sheng 用常数。移植先核对 raw LUT 与魅族 clock graph，保留异常表、全零、全 `0xffffffff`、重复首项、无有效非 turbo 档、请求越界等状态，不能只信注释和硬编码数值。[CpuFreqProbe.cpp](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/package/CpuFreqProbe.cpp)；[Linux EPSS](https://github.com/ianchb/sm8550-mainline/blob/42f3b40c702abbc37c3a04c95af008cd283aeaf3/drivers/cpufreq/qcom-cpufreq-hw.c)。

L3 单独位于 `0x17d90000`；Sheng mode3/4写其 `0x90` vote、读 `0x0` enable及`0x100` LUT。这属于另一组会改变内存性能的控制，本轮只读 CPU 探针不需要引入。不要把 L3 vote 和 boot domain 调频放在同一个首轮 A/B。[Linux L3](https://github.com/ianchb/sm8550-mainline/blob/42f3b40c702abbc37c3a04c95af008cd283aeaf3/drivers/interconnect/qcom/osm-l3.c)。

### 四种模式的真实差别

| 模式 | 源码行为 | 对 Nova 的价值/要求 |
| --- | --- | --- |
| 1 | map三个domain，仅刷新 enable/state/request；不写性能寄存器 | 首选观测起点，但原版输出并未实际遍历/展示最高LUT项，需补只读表摘要 |
| 2 | domain0启用后，写最高非turbo项；不写L3 | 可作为后续单独实验，先证明频率偏低且LUT/访问正确 |
| 3 | 所有启用domain写最高非turbo项，另写L3 | 同时改变多项，首轮难归因 |
| 4 | L3最高vote；每16ms计算负载、调频，含触控/帧活动提升 | 依赖idle helper、online CPU过滤及可核对的域映射；不直接迁移 |

`sheng-cpufreq-cap` 会被 `pickLevel` 读取，所以源码的 mode2 也受 cap 约束，不限于文件注释所举 mode3。首次写频实验应选确认过的非turbo档，记录写前/写后request与state，以及是否发生硬件限制；不把请求提高等同于实际频率提高。

### Nova/iOS16 编译与挂接约束

1. Nova 没有 `CpuFreqProbe.cpp`、`sheng_cpufreq_*`、`native_cpufreq_*` 或 `sheng_cpu_idle_mach` 实现；当前 PMGR 仅提供目标 AMFI 所需的原始 ApplePMGRNub 兼容接口。其对象大小、元类、factory 和 vtable 绑定是目标 ABI 的固定合同，频率探针无需修改它。[Nova PMGR](<nova-root>/unixnu-meizu20pro-nova/package/ShengPlatformPower.cpp:1)。
2. 上游 `build-gop-legacy-display.py` 在显示 archive 中编译/链接 CpuFreqProbe；Nova 使用自己的显示/NativeBootLog路径，不能执行或移入这个上游大脚本。应增加一个小型独立 archive，沿用本轮 Nova 实际 kernel 编译记录、头文件和链接审计规则；不要引入 Sheng Wi-Fi/ADSP/GPU 构建依赖。[上游构建脚本](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/scripts/build-gop-legacy-display.py)。
3. 只读版本仅需当前 ABI 的 IOMemoryDescriptor/IODeviceMemory、IOMemoryMap、PE_parse_boot_argn 和有界日志/时钟接口。将 mode4 编译单元整个排除，避免即使运行mode1仍因为函数体存在而链接 `sheng_cpu_idle_mach`、线程、recount、触控和GPU符号。编译使用实际 Nova XNU tree，不按“iOS16”标签猜测开源内核版本。
4. 物理窗口映射成功不代表 EL1 MMIO 一定可读；首个记录应标明映射/读取进度，核对现有异常处理边界。无热路径刷屏、无无限重试；首次访问资格实验优先采用一次有界读取并解映射，周期日志只复述缓存。若以后要解释启动期间动态频率，再单独增加低频有界采样。把采样阶段和 raw monotonic ticks 一起写入现有持久日志，避免将同一缓存误认成多个时刻。
5. 对只读候选做对象反汇编/符号检查：证明性能窗口没有 store，尤其不包含 `wr32`、`voteDomain`、`voteL3`、`setLevel`；源码host测试使用伪 MMIO，检查有界解析和坏表拒绝。不需要先增加 XNU idle patch、SMP/AMP、L3 或用户态更改。

## SMP、GIC/IPI 与调度

Sheng 使用 `sheng-smp=1 armcpu-spi-ipi=1`；公共 DTSI 的 GIC distributor/redistributor是 `0x17100000` / `0x17180000`、redistributor stride `0x40000`、timebase 19.2 MHz、PSCI SMC。这些与 Nova 已有 ArmCpu支持及成功 ADT描述同类，因此不是发现了一个 Nova 缺少的全新 GIC驱动。[共享DTSI](https://github.com/UniXNU/UniXNU-sm8550-common/blob/23b2049fcd9e30356161def84231334fb55ecca9/dts/sm8550.dtsi)。

Nova ArmCpu 新鲜SHA256仍为 `3c87b3bd617be5582a6a24ff797c359497ee9feaa702ee883ecee4270db21c9f`，与已完成的成功镜像静态审计一致。它已经具有secondary启动、PSCI结果、逐核timer/SGI计数和软件SPI IPI路径。关键差别是实际成功 payload 没有启用SMP参数；board.toml里的参数不等于打包进镜像的参数。现有证据是boot-only选择，不是运行时逐核census。[既有SMP审计](<nova-root>/artifacts-nova/boot-speed-round2-nova/baseline-nova/smp-status-nova.md)；[本次源码哈希](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/nova-source-evidence-nova.json)。

上游 AMP 补丁明确增加 `__ARM_AMP__`、将BCM2837配置的MAX_CPU_CLUSTERS从1改2；CPU0–2标为E、CPU3–7标为P；Edge优先让前台bucket去P，阻止部分前台线程回E，并允许空闲P偷取E工作。Nova对应配置仍是单cluster，未包含这组补丁。Sheng 的两个调度cluster与三个频率domain不是同一个划分。[AMP补丁](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/patches/xnu/zzyf-sheng-amp.patch)；[DTS](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/dts/sheng.dts)。

先观察频率，再做独立SMP实验更容易解释。若做SMP，应沿用当前Nova成功envelope和同一诊断代码，从`cpus=2`开始；记录MPIDR/MIDR、processor注册、PSCI返回、每核timer/IPI持续进展及实际调度。不能只依据step7、gCPUsRunning(bool)、E1增长或DT八个节点宣布八核正常。已有Sol双核失败基线不同，不能直接归因，也不能忽略它。开启AMP/Edge应等SMP稳定、真实拓扑及ABI验证后另做，不与调频同时更改。

### mode4 的离线核问题

上游idle补丁在`cpu_to_processor(cpu)==NULL`时返回0；governor始终遍历固定8核，以`dt - idle_delta`计算busy，然后取domain内最大值。因此未注册核连续返回0时，该核被算为100%忙。在Nova当前boot-only选择下，domain0中未上线CPU1/2就足以让domain0一直被判定满载。这个结论是代码推导，并非真机观测。[idle补丁](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/patches/xnu/zzye-sheng-cpu-idle.patch)；[governor](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/package/CpuFreqProbe.cpp#L200)。

未来迁移时helper必须能区别“无processor/未在线”和“idle确实为0”，通过内核现有同步方式取有效CPU集合。刚上线/离线转换或idle计数倒退时重新建窗口，不能将过期初值算成负载。若尚无在线核，则跳过domain，不把零当忙。仅检查EPSS enable不足以过滤同domain内离线核；给所有CPU都返回0的伪计数也不可取。host测试需涵盖boot-only、部分在线、上线第一窗口、计数倒退、全idle和满载。

## 固件与 Boot→苹果 73秒 的边界

Sheng页面给出的 UniXNURamDiskDxe 是从固件HOB/FDT initrd范围发布payload块设备，板级内存扩展仍在UEFI。该文件没有给出“能消除73秒”的证据；Nova当前能进入XNU与Hello，也没有本研究发现的菜单缺项问题。不要为优化当前等待整包更换Sheng固件、内存预留、GOP或DTB。[交接源码](https://github.com/UniXNU/UniXNU/blob/8d157ee18ae9c4d518aba28cb3040a84e860dcd1/package/UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.c)。

若缺少可靠的UEFI→XNU分界，最小观测是同一monotonic时基下增加Boot菜单选中/内核handoff/内核首条记录三个时刻，与已有root mount、launchd、苹果、Hello对齐。必须先校准时基和hand-off起点，不能用E1数值当秒。若频率探针只在显示初始化之后挂接，它也不能解释之前所有73秒；挂接点应在能够安全映射MMIO后的早期阶段。

## 推荐开发项和验收顺序

| 顺序 | 独立开发项 | 本轮应回答的问题 | 通过信号 |
| --- | --- | --- | --- |
| 1 | 默认关闭的三domain EPSS只读探针 | 当前Nova启动时是否保持低频/低请求？停顿时CPU仍忙还是服务等待？ | 不含MMIO写入；表有界；raw/state/request可对齐；Hello保持 |
| 2 | 若1支持低频假设，再做仅domain0的受限非turbo vote | 单独提升当前boot domain是否缩短root/launchd/lsd阶段？ | 请求和状态均变化；同一计时起点/相同Data；多次对照超出波动 |
| 3 | 独立逐核census＋双核SMP | 次核是否实际进入可持续调度？并行是否缩短关键阶段？ | timer/IPI/调度均有进展；Hello保持；不是仅PSCI成功 |
| 4 | 部分在线感知的governor | 高低负载能否正确升降频且不误判离线核？ | host边界测试＋真机请求/状态/负载一致 |
| 5 | 真实拓扑＋AMP/Edge | 前台放在P核是否改善性能且不破坏启动？ | 先有SMP证据；分开评估coreduetd/用户态策略 |
| 6 | 独立L3实验或更早UEFI频率观测 | 内存/固件段是否仍是实际瓶颈？ | 有阶段时间证据后才做，避免同时改CPU与L3 |

不要把“服务等101秒”自动解释为低CPU频率。只读探针应与父代理的lsd/IPC时序一起看：若频率正常且大部分时间在wait，优先继续服务等待根因；若持续CPU忙且状态显著低于已验证LUT档，再推进第2项。

本报告新增文件均位于指定研究目录，未修改其他会话、构建树、设备状态或中央CHANGELOG。没有运行下载的host check，因此来源记录中的PASS不冒充本次验证。完成的验证是固定源码读取/哈希、Nova源码差异检索、魅族DTB结构解析和寄存器定义交叉核对。


## 附录：最小 mode1 集成点与异常边界

2026-10-30 补充。修改前已保存本研究目录内 `snapshot-nova/REPORT-before-mode1-integration-nova.md`；主报告之外未修改生产文件。以下是实现建议，尚未实现或上机。

### 建议独立 C 模块，BootLog 只做结果输出

以 Nova 已有 USB GCC readiness 模块的组织方式作为模板，而不是复制其硬件地址，也不执行上游完整 CpuFreqProbe：

- 独立的板级 `CpuFreqReadiness.c` / 常量头 / 小 archive，通过 `STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, ...)` 注册。这里已在 KMEM、OSLOG、THREAD_CALL 之后；不用新增 IOKit service/personality、XNU源码startup补丁或独立线程。具体挂接顺序应以本轮链接产物确认，不能只看源码宏。[现有USB单次探针](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeMeizuUsbProbe/UsbReadiness.c:47)；[startup顺序](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/kern/startup.h:65)。
- bootarg缺失或不等于严格的数值1时，在映射、日志和MMIO读取之前返回。板级选择、一次性guard、4KiB页面检查必须显式保留；这是魅族专用模块，不能用MMIO探测来猜运行设备。
- 用当前内核的`io_map(pa, 0x1000, VM_WIMG_IO, VM_PROT_READ, true)`映射一个domain，采完立即`ml_io_unmap`，再处理下一个。只使用对齐32位volatile load及合适的读取屏障；不要用memcpy/字节读取/64位读扩大访问语义。读取进度先存到内存标量，故障时有机会留下阶段证据，但不能保证panic前持久化。
- 首个访问资格候选可进一步只读boot domain0的enable/state/DCVS/request四个字，再单独决定是否扩展LUT和其他domain。如果一次覆盖全三域，硬上限应写入代码：3个enable＋最多6个state/DCVS＋8个每核request＋120个LUT字，共最多137次32位读取；遇到未启用或全ones的enable就跳过该domain的其余字。表终止条件可以减少次数，不能增加重试。
- 采样一次后保存不可变快照，包含状态、domain、窗口、各raw字、已完成读取数和采样tick；用明确发布顺序让autolog读到完整快照，不能仅用散乱volatile字段假装一致。周期日志只能输出这个缓存，并携带不变`sample_ticks`和`sample_id=1`。这能验证首次可达性及该时刻频率；不能证明lsd随后100秒一直处于同一频率。
- 若已有持久日志足以保存一次结果，直接一次输出更简单。确需重发时可在现有`meizu_autolog_thread`的scalar-summary位置调用小型dump函数，限次数/时间，不增加线程。不要在`native_blog_char`、IRQ/FIQ、E1重绘路径或framebuffer映射函数中做频率MMIO。[autolog调用点](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7942)；[现有有界日志模式](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:907)。
- 编译可参照现有USB构建器的`startup.o.json`解析、Mach-O/startup节、undefined-symbol白名单与archive审计，但仍需先按隔离规则核对路径，不能直接运行或改写该旧模块。该构建器实际检查的deployment为ARM64 iOS17.0，虽然Nova用户态/kext ABI目标是iOS16.7.16；这正是应沿用真实编译记录、避免按名称猜ABI的原因。[现有构建器](<nova-root>/unixnu-meizu20pro-nova/scripts/build-native-meizu-usb-probe.py:25)。

### 当前没有可直接复用的 MMIO 故障隔离

**现有可用路径的结论：只能限制软件读取范围和次数，不能承诺访问失败后继续启动。**

1. USB探针的`if (!base)`只是防御性检查。实际`osfmk/arm/io_map.c`在KMEM后使用`KMA_NOFAIL | KMA_PAGEABLE`分配，然后建pmap映射；没有测试目标寄存器是否可达。`VM_PROT_READ`限制CPU页表写权限，也不拦截设备拒绝读、外部abort或总线stall。[io_map实现](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm/io_map.c:88)。
2. 本份ARM64 XNU的`ml_probe_read`和`ml_probe_read_64`并未实现，两者都会直接调用panic。不能用它们封装新探针，也不能因为名字包含probe而认为有异常恢复。[loose_ends.c](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm64/loose_ends.c:841)。
3. `ml_nofault_copy`检查地址翻译和`pmap_valid_address`后走物理内存copy。它不提供MMIO外部abort恢复，也可能因目标不属于有效RAM而拒绝复制；不能用于本任务的寄存器读取。[machine_routines.c](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm64/machine_routines.c:1974)。
4. `handle_kernel_abort`的copyio recovery仅针对特定recover表和若干fault分支，非普通volatile MMIO读的通用保护；未分类内核abort最终panic。为了读几个寄存器新增全局fault recovery或更换异常向量属于另一项内核工作，不能悄悄并入最小mode1。[sleh.c](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm64/sleh.c:1886)。
5. 即使加入一次读取后的超时检查，也无法给尚未返回的总线load设可靠软件时限。DTB/上游Linux证明位置与预期语义，不能证明当前EL1/固件/hypervisor访问权限。故下一轮不能表述为“安全探测，失败自动跳过”：它仍可能panic/停机。一次有限读取是缩小暴露面，不是消除风险；是否上机应由主代理结合当时授权、回退镜像和设备状态判断。

为实现后的host验证准备的检查范围：禁用路径零映射/零读取；重复调用不二次采样；非4KiB配置拒绝；地址及偏移白名单；未启用/全ones跳过；读取总数上限；LUT边界及request越界；快照发布完整性；重复输出保留原sample时间。反汇编需确认对性能窗口无store，不能用“源码里没有wr32”代替二进制核查。这些都是待实施验收项，本研究没有运行这些测试。
