# HackiOS 图形、触控与用户态移植审查 — Nova

2026-10-29。新建报告，无旧文件。本轮只读源码与公开验收文档；未构建、未运行仓库脚本、未操作设备、未修改生产源码。CHANGELOG 由主代理统一记录。

**结论：可以吸收图形语义修复和验收方法，不能把一加 9 的 A660/DPU/触控驱动或用户态二进制直接搬到魅族 20 Pro。** 本地已存在 A740 的 NativeAdreno → Turnip → NativeMetal 主线；优先沿该主线核对魅族板级资源，移植上游已解决的问题和测试用例，比把整个 Redwood 栈接入现有系统更有依据。这里的“存在”指源码和 sheng 文档，不等于魅族已经启用或实机通过。[L1][L2][L3]

**固定输入与证据等级。** Kernel 仓库为 `b1ackm4x/hackios-xnu-kernel@8aefda9c133a19fad1c1083eb713480348f43d78`（下称 K）；独立 userland 为 `b1ackm4x/hackios-xnu-userland@3278f7f01796e74ecd634c0d348d2b255a37658f`（下称 U）。读取了 Nova 内研究副本；kernel sparse 恢复遇到网络失败后，对关键缺失文件用固定 commit 的 `raw.githubusercontent.com` URL 只读补查，未操作其 Git。所有外链均固定提交，不用浮动 `main`。

本报告区分：①实际源码机制；②维护者提供、绑定版本与工作负载的手机运行记录；③主机测试/编译；④尚未验证。第②类不是本轮独立复现；原始手机日志、镜像与完整 manifest 没有公开入 Git，不能据文档重算全部结果。测试程序名称、GPU 设备名称或 CI 通过不能单独证明硬件执行。[K1][K2][U1]

**首先要纠正两个版本陷阱。**

- K 已合并 kernel、userland、加载器及镜像工具；独立 U 不再是当前构建依赖。`docs/REPOSITORY.md` 写明单一 `source_commit`，旧 userland 仅保留历史。不能把 U 的旧动态库与 K 的新驱动随意配对。[K3]
- U 的 `userland/docs/lemonade-metal.md` 是 2026-10-24 的 88 项验收、同步 `commit`、CPU 拷贝和“120 Hz / direct / 联合运行待验”记录。K 的当前源码已包含异步 GPU 完成、DPU 单层直接显示、最多四层 BGRA CPU 合成、双缓冲和固定 120 Hz；K 的较新 `2026.10.27-base.2` 文档又补齐联合验收。K 的 DISPLAY/NATIVE-BENCH 文中仍有 10 月 25 日“新候选尚未联合”的旧状态，必须按日期和包哈希解释，不能拿它否定 10 月 27 日的记录，也不能拿后者外推每个 `main` 构建都已实机通过。[U1][K4][K5][K6]

**手机证据能支持到哪里。**

| 能力 | 公开的一手记录 | 可以得出的结论与边界 |
|---|---|---|
| GLES/Metal 实际渲染 | 10 月 24 日 FD660 GLES 像素回读、26 项 IOSurface、15 项 raster、逐步增加的 Metal feature 检查；同一 iOS metallib 在 Apple M4 上作对照 | 有设备输出与像素/缓冲内容检查的维护者记录。U 的 88 项不是最终功能总数；Apple2 capability table 也不是 Metal family 全面合规。[U1][K4] |
| 双缓冲原生 Metal | 10 月 25 日同程序、同 shader、1125×1260、379 MHz；关闭流水线约 60.60/60.47 fps，最终候选约 116.46/116.61 fps；P95 8.66/9.21 ms，三段 4,379 帧 | 是固定测试和约 15 秒分段的完成率；仍有长帧，不是所有应用稳定 120 fps，不是 ProMotion，源读回不是面板光学摄影。[K5] |
| 新运行包联合验收 | `2026.10.27-base.2`，指定内核/root 哈希，180 秒：显示 36、网络 61、Metal 103、MPS 2、CA 4；2,773 次 DPU 传输，零回退/失败；另有无诊断 90 秒 743 次 DPU 运行与正常停止 | 对这个包有比单测更强的手机组合证据。该次未重新测 UFO，不可把旧网页分数移植过来。一次复用旧 RAM loader 的失败及冷启动规避也被记录，长期稳定性仍未验收。[K6] |
| Safari 高刷 | 10 月 25 日 base.4 无网页观测探针时 DPU 实际完成 105.272 fps，页面标签可显示 120 | 已解除原生偏好导致的固定 60 fps 策略；页面标签不是硬件完成率，未达到稳定 120 fps。[K6] |
| AIR 编译覆盖 | U 报告：456 模块 reader 成功，321 translate/ir3，135 `air.visible` 不支持 | 这是编译覆盖，不是 GPU 正确性。报告自己明确“reaching ir3 means compiled, not correct”；不能写成“iOS 所有 shader 已支持”。[U2] |
| 可交付性 | K README 说明公开下载仍是 CI 通用组件实验版，完整用户准备工具和已验收公开运行版本尚未交付 | 公开源码/组件有研究价值，但没有一个可直接给另一台魅族启动的合格整包。[K1] |

**A660 → A740 的具体不可直接复用点。**

| 层面 | K 的实际绑定 | Nova 的差异与移植要求 |
|---|---|---|
| 平台、GPU identity | `select_gpu_platform()` 只认 `/arm-io/device_type` 的 `lemonade` 或 `redwood`；一加分支 `chip_id=0x06060001`、GMEM=1536 KiB、highest bank bit=16 | 更名不足以适配 A740。必须使用魅族真实 GPU/UBWC/GMEM/OPP/资源信息；不能伪装成 lemonade 让代码越过保护。[K7] |
| 用户态 ↔ 内核 ABI | `RedwoodGPU` 把 Linux msm request 编号作为 external-method selector：GEM new `0x42`、submit `0x46`、wait `0x47`，另有 IOSurface `0x7e`；libdrm shim 宣称 msm 1.7，buffer handle 也是 map memory type | `NativeAdrenoABI.h` 明确是自定义 version=1 固定记录，selector 1…16，**不是 ioctl ABI**。直接放入上游 Mesa/Metal 动态库不能连通现有 NativeAdreno；需适配层，或重写它的 KMD 接口和 IOSurface import/缓存/fence 协议。[K8][U3][L4] |
| 寄存器与命令流 | A660 路径调用 `a6xx_cp_init`，8 个 payload、mask `0x2f`；寄存器和 GMU/clock/resource 表按 SM8350/原 redwood 分支写死 | 本地 A740 CP init 使用 A7XX thread control、7 payload、不同 mask 与 power-up reglist。A740 ring 还有 BR/BV 双线程 framing、`CP_EVENT_WRITE7`、CACHE_FLUSH7/INVALIDATE7 与上下文重置。即便部分寄存器偏移相同，也不能复制 A660 PM4 包或启动次序。[K9][L5][L6] |
| 固件与安全加载 | 启动必须有 `a660_gmu.bin`、`a660_sqe.fw`、`a660_zap.mdt`、`a660_zap.b02`；zap 由 TrustZone 验签/加载；缺文件明确回退软件 | 这些不是 A740 固件。需要魅族/SM8550 合法取得、与其安全固件匹配的 GMU/SQE/必要安全加载流程；本地弱默认固件大小为零会拒绝运行，也不能将 sheng 内嵌固件当作魅族已验证输入。[K10][L7] |
| GMU / 电源 | 固定 GMU 地址、HFI queue 与启动配置；性能投票在 `lemonade_speeds` 和 speed bin 分支，关停需停止 GMU、RPMh votes、总线事务后撤 SMMU | GMU firmware/HFI、GPUCC/GCC、RPMh/RSCC、OPP/speed-bin 是板级启动的一部分；不得仅换 GPU 名称或设同样频率。A740 需要自己的 startup/teardown 和故障恢复证据。[K11][L1] |
| SMMU / DMA | GPU SMMU 基址 `0x03da0000`；该实现固定 3 级 4 KiB、39-bit VA；GPU stream 0、GMU stream 5；`SMMU_STREAM_MASK=0x400`、TCR2 36-bit PA | 4 KiB 页这一共性不代表 SID、CB、ASID、VA/PA bits、缓存模式或 hypervisor 权限一致。按魅族 DT/现场寄存器建立资源表，沿本地 ArmSmmu4K 做映射/撤销/TLB/退出验证；不能照搬 context 或 mask。[K12][L4] |
| Shader | AIR→NIR 的语义转换可与具体 GPU 相对分离；最后由 Gallium/Freedreno 与 IR3 针对 screen 编译，当前开发依赖 Mesa 25.0.7 | `.metallib` 中的 AIR 可作为前端输入；Apple GPU machine code、A660 编译结果、PM4 state、shader cache 都不能当作 A740 可执行产物。需对 A740 重新生成 shader/state，并验证 NIR/IR3 ABI 和 Mesa 版本。现有主线是 AIR→SPIR-V→NIR→IR3，AirNir 是另一个离线入口。[U4][L1] |

`RedwoodGPU.cpp` 还明确当前等待采用轮询、各 client buffer 共享同一 GPU 地址空间。其资源引用、超时和停止顺序值得参考，但不能把这理解成已实现与 Nova 相同的进程隔离/队列/调度模型。[K8][L4]

**最值得重用的 Metal/GLES 内容。**

1. AIR 语义与像素回归：half derivative 先升 32-bit 再收窄、half varying 在阶段接口用 32-bit slot、语义 varying 链接、helper invocation、typed image store、Stencil8 取样、flat 首顶点语义、blend constant、clear-only pass、资源持有、MSAA color resolve。这些对应上游明确失败→修复→像素对照过程，适合变成 Nova 前端/translator 的对照用例，不能仅移植 feature flag。[K4][U1]
2. 大常量 ring buffer 的重复上传优化：`uniform_extents()` 从专门化 NIR 推导实际 UBO 读取上界；动态地址保留完整范围、未读资源不上传。上游同工作负载上传量由约 120–200 MB 到 40–43 KB。适合比较本地 metal2vulkan/Turnip 路径的常量计划，移植算法前先核实本地是否已有同类优化。[K4][K13]
3. Metal `device` buffer 小偏移：当前 AIR 后端将 SSBO 基址向硬件对齐，利用额外 UBO bias 修正 load/store/atomic，避免复制破坏别名和写回。它不是单纯放宽 align 检查；需要配套 shader lowering、绑定表和跨 draw 生命周期。[K13]
4. 异步提交的正确边界：K 的 `commit` 已通过 `rwa_batch_submit` 落实 GPU 提交，串行 fence queue 在 `rwa_completion_wait` 后才完成、回调和归还名额；AIR 在 context 锁内 flush+零超时落实 deferred submit，锁外等真实 screen fence。U 文档中“commit remains synchronous”和 K 源码旧注释不能覆盖这一实际代码。可借用状态机和失败用例，但需改接 NativeAdreno 的 fence 与 IOSurface 所有权。[K14]
5. EAGL 桥：将 iOS CA 的 EAGLContext/private function table 转接 GL 调用，处理 IOSurface、BGRA、坐标和 framebuffer fetch；大体处于 API/合成语义层，可借用测试和转换逻辑。其私有函数表仍按目标 iOS 库布局生成，且底层共享面依赖 RedwoodDRM 特殊接口，不能把整个 `eagl.c` 当成平台无关 OpenGLES 库。[U5][U3]

应保留的未完成边界：完整 texture view、heaps/events、nonuniform threadgroups、depth resolve、全部 private SPI、全应用兼容和全格式覆盖。K 当前源码 `supportsSampleCount:` 仍只报告 1，texture-view 入口返回 nil，event signal/wait 入口没有实现操作；特定 2/4 样本颜色测试通过不等于所有格式/深度解析支持。Feature profile Apple2 是为 MPS/discovery 满足特定调用而选择的能力表，不是完整 Apple2 认证。[U1][U6][K4][K20]

**显示：可以移植所有权规则，不能照抄 DPU 或面板参数。**

K 的最新设计是单层兼容 BGRA IOSurface 等 GPU fence 后直接映射 DPU，QSEED 缩放到 1080×2400；多层最多四层，由 CPU 合成到两个内核自有 1125×2436 BGRA 表面，然后与 DPU 传输并行。它不是 DPU 多-plane 硬件合成，也不是所有路径零拷贝；特定渲染仍包含 GPU blit，不兼容布局可回退 GOP。[K4]

完成条件同时包含 PP0 完成、传输空闲、硬件 frame count、无 fault/underrun；随后撤映射、TLB sync 才释放直接扫描源。`lemonade_display.h` 的接口返回值清楚区分 `1=transferred+unmapped`、`0=CPU fallback safe`、`-1=retain source`。这一“提交/GPU完成/DPU完成/源释放分别管理”的契约，连同错误不伪造完成、退出先排空 GPU 调用和显示的设计，很适合审查 Nova 现有显示后端。[K4][K15]

硬件部分绑定 DPU 7.0/SM8350，`MDSS=0xae00000`、`MDP=0xae01000`、DISPCC、INTF1、SPI83/intid115、TE read-pointer bit2、两个 VIG/mixer、AMB655X 的原厂 60/120 Hz 命令；QSEED preload 实机采用水平5/垂直3。驱动按 1125×2436→1080×2400 几何硬编码。魅族显示/面板/DSC/DSI timing、DPU revision、IRQ、SMMU SID/CB 和 GOP handoff 必须单独核定；即使某些基址相同也不能推断兼容。[K16][K4]

当前 Nova `meizu20pro_defconfig` 明确 `CONFIG_XNU_NATIVE_MOBILE_DISPLAY=n`、`CONFIG_XNU_NEGRONI_DISPLAY=y`。上游 `MiniIOMobileFramebuffer/MiniIOSurface` 与本地保留原 Apple kext/NativeMetal/Negroni 的对象关系不同，因此最先可做的是接口和所有权对照，而不是替换内核显示类。[L3][K8][K15]

**触控：一加协议不可复用为魅族驱动。**

K 的一加硬件是 **Samsung S6SY792**，I²C `0x48`，SDA20/SCL21/reset22/IRQ23/analog-enable74；实现含开漏 GPIO I²C 和仅允许的易失 scan/event 命令。魅族本地是 **Goodix Berlin D**，GENI I²C@`0xa90000`、地址`0x5d`、SDA44/SCL45/reset24/IRQ25、1440×3200 坐标及不同 RPMh 供电。不存在把一加驱动换名后直接驱动魅族的依据。[K17][L8]

上游通用事件队列 `/dev/redwood-touch` 使用 `{time,type,finger,x,y}`，坐标0…65535，由注入 backboardd 的代码合成 IOHIDVirtualServiceClient 事件；适合借用事件序列、触点释放、边缘手势和录制回放测试。它的 HID private prototypes 明确针对20F66。Nova 板级实现设计为把触摸 digitizer 发布成原 IOHIDDevice，并钉住20H392 IOHIDFamily ABI；但实际选中 `.config` 已注释掉 `CONFIG_XNU_MEIZU_TOUCH=y`，这里不声称当前触控可用。若改为上游虚拟 HID 路线，须重新设计服务发现/事件身份，不能重复发布两个输入源。[U7][K18][L9][L10]

**iOS 与 init/launchd：是另一套完整 bring-up 架构。**

HackiOS 输入为 **iPhone X iOS16.5 build20F66、arm64 dyld cache**；旧 U 的 fetch 文件和 K 的 private hook 注释相互印证。Nova `board.toml` 当前目标是 **iOS16.7.16 build20H392**，实际 candidate-run2 日志第3行却明确是 **Darwin Kernel Version23.6.0**。配置目标、运行内核和实际 cache/root 必须分别绑定核验，不能说现有镜像就是一套纯16.7.16环境；本地 README 的17.7.11旧描述同样不能替代配置与运行日志。[U8][K18][L9][L11]

实际启动链：kernel 将 `/sbin/launchd` 的 stage0 作为初始程序→stage0 把第二 RAM disk `/dev/md1` 只读挂到 `/redwood`→exec `/redwood/sbin/init`→准备可写 `/private/var`、恢复内容、注册 injection、跑 `etc/rc`→以 `exec /sbin/launchd.ios` 保持 PID1 交给 Apple launchd。release 模式保留 setup，编译掉诊断 workloads。普通 `rc.jobs` 由自有 init 的简易解析器执行，不是 shell。[K19][U9]

另有可选 `root_apfs` 路线：以 md0 flashboot 起步，借自有 `bindfs`、`kern.apfs_synthetic`、`kern.redwood_switch_root` 将只读 iOS flash 卷和 cryptex 切成根；`kern.exec_inject` 注入 backboardd/SpringBoard 等。保留 `rd=md0` 会让 launchd 跳过真实设备部分 boot jobs。当前 base 文档表明日常包仍是 RAM 会话、设置不持久化。[K19][K6]

因此只复制 stage0/init 并不能补齐本地启动：它要求相应 filesystem/sysctl/注入支持、双 RAM disk 布局、自有服务清单、MiniIOSurface/RedwoodGPU/HID 接口和20F66私有 framework ABI。Nova 现有 `OWNER_ROOT_TRUST / OWNER_CACHE_MOUNT / OWNER_DATA_MOUNT / OWNER_UNION_SYSTEMOS` 与原 launchd 路线需要逐项对照，不能简单把上游 mount/root-switch/禁用 jobs 变成默认。[L3][K19]

最直观的版本绑定证据是 K `inject.c`：20F66 的显示模式 parser 地址 `0x188529ea0+slide`、固定前后指令序列、CA display/EAGL vtable 地址，以及 `_MTLDevice` 对 Metal306.6.4 的假设。即使它会校验指令后拒绝不匹配，拒绝后的功能也不会凭空出现；16.7.16需要重新做 ABI/符号/指令识别与服务发现验证。不能把存在 guard 当成跨版本兼容。[K18][U6]

**建议的接入顺序（本轮未实施）。**

先锁定 Nova 实际 kernel/cache/root 组合和魅族 A740/显示/触控资源表，再围绕现有 NativeAdreno/NativeMetal/Negroni 路线挑选 AIR/资源/fence 测试。优先逐像素对照 half、导数、varying、unaligned SSBO、clear-only、资源提前 release 和异步 completion，随后审查显示源释放与退出排空；最后才考虑 EAGL 或 AIR/Gallium 作为额外后端。每个模块都需目标硬件输出与版本绑定记录，不将一加测试数、CPU 软件回退或 sheng 文档转算成魅族成功。

**固定提交引用。**

- [K1] [K README:9–54（运行范围、公开组件、未完成项）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/README.md#L9-L54)
- [K2] [K README:71（私有原始材料不提交）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/README.md#L71)
- [K3] [K docs/REPOSITORY.md:3–42（单仓库与导入历史）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/docs/REPOSITORY.md#L3-L42)
- [K4] [K docs/LEMONADE-DISPLAY.md:3–87（当前设计、边界和历史分界）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/docs/LEMONADE-DISPLAY.md#L3-L87)
- [K5] [K docs/LEMONADE-NATIVE-BENCH.md:3–60（相同负载A/B和证据范围）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/docs/LEMONADE-NATIVE-BENCH.md#L3-L60)
- [K6] [K docs/LEMONADE-BASE-120HZ.md:3–48（10月27日base.2）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/docs/LEMONADE-BASE-120HZ.md#L3-L48)；[同文件:73–112（Safari）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/docs/LEMONADE-BASE-120HZ.md#L73-L112)
- [K7] [K xnu/files/bsd/dev/redwood_adreno.c:116–143（平台、chip、GMEM）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/redwood_adreno.c#L116-L143)
- [K8] [K xnu/files/iokit/Kernel/RedwoodGPU.cpp:1–30,78–95（ABI、共享地址空间与轮询）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/iokit/Kernel/RedwoodGPU.cpp#L1-L95)
- [K9] [K redwood_adreno.c:2848–2858（A6XX CP init）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/redwood_adreno.c#L2848-L2858)
- [K10] [K redwood_adreno.c:4047–4070（必需A660固件与软件回退）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/redwood_adreno.c#L4047-L4070)；[同文件:1670–1725（ZAP验签）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/redwood_adreno.c#L1670-L1725)
- [K11] [K redwood_adreno.c:1908–2096（HFI/GMU）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/redwood_adreno.c#L1908-L2096)；[同文件:3010–3085（退出与总线事务）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/redwood_adreno.c#L3010-L3085)
- [K12] [K redwood_adreno.c:1073–1089（SMMU基址）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/redwood_adreno.c#L1073-L1089)；[同文件:1206–1244（页表、stream、地址位）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/redwood_adreno.c#L1206-L1244)
- [K13] [K tools/mesa/air/air_metal.c:351–405（SSBO bias lowering）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/tools/mesa/air/air_metal.c#L351-L405)；[同文件:5591–5627（UBO extents）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/tools/mesa/air/air_metal.c#L5591-L5627)
- [K14] [K userland/src/redwoodmetal.m:3936–4014（异步commit/finish）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/redwoodmetal.m#L3936-L4014)；[K tools/mesa/air/air_metal.c:3260–3320（提交与锁外等待）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/tools/mesa/air/air_metal.c#L3260-L3320)
- [K15] [K xnu/files/bsd/sys/lemonade_display.h:8–26（显示接口返回契约）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/sys/lemonade_display.h#L8-L26)
- [K16] [K xnu/files/bsd/dev/lemonade_display.c:1–59（DPU/面板绑定）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/lemonade_display.c#L1-L59)；[同文件:957–983（固定缩放几何）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/lemonade_display.c#L957-L983)
- [K17] [K xnu/files/bsd/dev/lemonade_touch.c:1–44,99–121（Samsung/布线/协议）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/xnu/files/bsd/dev/lemonade_touch.c#L1-L121)
- [K18] [K userland/src/inject.c:102–150（20F66 patch/vtable）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/inject.c#L102-L150)；[同文件:763–794（20F66 HID SPI）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/inject.c#L763-L794)
- [K19] [K userland/src/stage0.c:1–35](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/stage0.c#L1-L35)；[K init.c:1–14](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/init.c#L1-L14)；[init.c:116–137（flash-root协议）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/init.c#L116-L137)；[init.c:1344–1380,1435–1468（注入/release启动）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/init.c#L1344-L1468)
- [K20] [K redwoodmetal.m:2312–2325（保守sample count）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/redwoodmetal.m#L2312-L2325)；[同文件:2996–3028（texture views）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/redwoodmetal.m#L2996-L3028)；[同文件:3915–3916（events空实现）](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/8aefda9c133a19fad1c1083eb713480348f43d78/userland/src/redwoodmetal.m#L3915-L3916)
- [U1] [U userland/docs/lemonade-metal.md:3–79（旧验收与明确边界）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/userland/docs/lemonade-metal.md#L3-L79)
- [U2] [U docs/air-report.md:3–45（编译覆盖而非正确性）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/docs/air-report.md#L3-L45)
- [U3] [U tools/mesa/redwood-drm/redwood_drm.c:1–20,78–124,147–185（DRM与映射契约）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/tools/mesa/redwood-drm/redwood_drm.c#L1-L185)
- [U4] [U tools/mesa/air/air_metal.h:1–19（AIR/Mesa接口）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/tools/mesa/air/air_metal.h#L1-L19)；[U README:34,56–58（Mesa25.0.7）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/README.md#L34-L58)
- [U5] [U userland/src/eagl.c:1–59（EAGL/IOSurface桥）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/userland/src/eagl.c#L1-L59)
- [U6] [U userland/src/redwoodmetal.m:4–38（private ABI与RedwoodGPU）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/userland/src/redwoodmetal.m#L4-L38)；[同文件:1310–1350（featureProfile）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/userland/src/redwoodmetal.m#L1310-L1350)；[同文件:2312–2325（sample count）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/userland/src/redwoodmetal.m#L2312-L2325)
- [U7] [U userland/src/redwood_input.h:1–44（touch队列）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/userland/src/redwood_input.h#L1-L44)
- [U8] [U ios165/fetch.sh:2–27（16.5/20F66/arm64）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/ios165/fetch.sh#L2-L27)
- [U9] [U README:51–52（rc解析器）](https://github.com/b1ackm4x/hackios-xnu-userland/blob/3278f7f01796e74ecd634c0d348d2b255a37658f/README.md#L51-L52)

**Nova 本地引用（工作副本，非上游手机验证）。**

- [L1] [docs/drivers/native-adreno.md](<nova-root>/unixnu-main-nova/docs/drivers/native-adreno.md:3)（runtime/组件），[shader路径](<nova-root>/unixnu-main-nova/docs/drivers/native-adreno.md:88)。
- [L2] [NativeMetal/README.md](<nova-root>/unixnu-main-nova/drivers/NativeMetal/README.md:3)（明确是sheng硬件状态）。
- [L3] [meizu20pro_defconfig](<nova-root>/unixnu-meizu20pro-nova/configs/meizu20pro_defconfig:4)（A740未直接启用），[root/cache/data配置](<nova-root>/unixnu-meizu20pro-nova/configs/meizu20pro_defconfig:24)，[显示配置](<nova-root>/unixnu-meizu20pro-nova/configs/meizu20pro_defconfig:50)。
- [L4] [NativeAdrenoABI.h](<nova-root>/unixnu-main-nova/drivers/NativeAdreno/NativeAdrenoABI.h:6)（version/selector），[device info](<nova-root>/unixnu-main-nova/drivers/NativeAdreno/NativeAdrenoABI.h:127)。
- [L5] [NativeAdrenoCpInit.cpp](<nova-root>/unixnu-main-nova/drivers/NativeAdreno/NativeAdrenoCpInit.cpp:6)（A7XX CP启动）。
- [L6] [NativeAdrenoRing.cpp](<nova-root>/unixnu-main-nova/drivers/NativeAdreno/NativeAdrenoRing.cpp:24)（BR/BV、flush/invalidate、context）。
- [L7] [NativeAdrenoFirmwareDefaults.cpp](<nova-root>/unixnu-main-nova/drivers/NativeAdreno/NativeAdrenoFirmwareDefaults.cpp:2)（空弱默认）。
- [L8] [MeizuTouchBoard.h](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchBoard.h:8)（GENI/SMMU），[引脚](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchBoard.h:44)，[Goodix/坐标/供电](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchBoard.h:68)。
- [L9] [board.toml](<nova-root>/unixnu-meizu20pro-nova/board.toml:15)（16.7.16/20H392），[HID ABI绑定](<nova-root>/unixnu-meizu20pro-nova/board.toml:50)。
- [L10] [选中.config触控已禁用](<nova-root>/unixnu-main-nova/out/meizu20pro/.config:12)。
- [L11] [candidate-run2实际Darwin23.6.0](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt:3)。
