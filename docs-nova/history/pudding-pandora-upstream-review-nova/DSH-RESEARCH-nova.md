# pudding-ios / data-a-live 对 Nova 的可用性核查

## 结论

两仓都不是“给小米 17 开发的 iOS 模拟器/虚拟机”，而是**在小米 17 与小米 17 Pro 真机上裸机运行 iPhone X 的 iOS 16.7.16**：自编 xnu-8796、4 KiB 页、ABL `fastboot boot` + 自写 iBoot shim，不使用 UEFI、虚拟机或 Linux 宿主兼容层。`pudding-ios` 是小米 17（pudding，SM8850）的**增量仓**，主体补丁/驱动来自 `data-a-live`；`data-a-live` 是小米 17 Pro（pandora，SM8850）的主移植仓。

最新 `pandora_sink` 是替代崩溃/被禁 daemon 的兼容 stub，只避免请求无限排队或忙重试，**不是真实服务，也不能作为 Nova 的 lsd 修复或提速证据**。

对 Nova（魅族 20 Pro/M2391，SM8550）而言：**本次检查没有找到能直接修复暖 Data e22/b2/op9 的代码，也没有找到已证明缩短 Boot→Hello 的可搬补丁。** 可借鉴的是少量测量方法、4K 代码签名批量读取的等价测试，以及一个需先验证三簇拓扑的 CPU 调度方向。两仓的显示/触控/GPU/电源参数与 Nova 不同代，不能直接套用。

## 1. 机型、形态与固定提交

| 仓 | 目标设备/SoC | 形态 | 固定提交 | 最新提交（Asia/Shanghai） | 最新实际差异 |
|---|---|---|---|---|---|
| `yechenan/pudding-ios` | 小米 17，25113PN0EC，pudding，SM8850，ARM64 | 增量补丁+脚本+文档；构建时拉 `data-a-live` 固定 `81b71b1`，再叠本仓 0241–0252 | `836d4c879a417b588224e6fa594dd85914af457d` | 2026-10-30 09:32:49，`sync docs (audio handoff + status)` | **空提交**；前一提交 `d1ea2be` 仅新增 `docs/AUDIO_HANDOFF.md`（47 行文档）；最近代码提交 `7c24750` 只改 `xnu/patches/0252`（+11/−1） |
| `ShueiTsuki/data-a-live` | 小米 17 Pro，pandora，SM8850，ARM64 | 主移植仓：内核补丁、驱动、Turnip/Metal 工具、启动器 | `5fc9d2fb7acbe2ae9694891db42d49a44f6acb35` | 2026-10-30 04:29:12，`pandora_sink holds XPC messages a second; Turnip logs second finishes of a BO` | 2 个代码/诊断文件，+207/−6：`tools/sink/pandora_sink.c`、新增 `tools/turnip/pandora_bocheck_finish.py` |

`pudding-ios` 的 README 明确写“只包含机型专属增量”，上游 `data-a-live` 固定为 0240 的 `81b71b1`（[README.md:7](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/README.md#L7)、[fetch-upstream.sh:10](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/scripts/fetch-upstream.sh#L10)）。因此它的驱动主体不是本仓代码，且**构建停留在上游 0240，早于文档里提到的 0262/0293 等轮次**；本仓补丁也只到 0252。这个时间差是复现其后期“GPU 合成/音频”声明时首先要核对的。

`data-a-live` 的启动链是 `fastboot boot` 一个 boot header v4 镜像，`shim/loader` 代替 iBoot 拷贝内核段、放 ADT/boot_args 后跳转（[loader.c:2](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/shim/loader/loader.c#L2)）。这不是 UEFI 启动；Nova 的 UEFI 链、分区与 A/B 约束不能与之互换。

## 2. 宣传进度与源码证据

本次检查以源码和作者文档内嵌日志为主，未独立复现；在本次所查 tracked 文件中未发现与这些文档分离的独立原始日志或屏幕证据，也未穷尽作者仓库历史和站外附件，因此不能据此断言作者没有这类数据。运行时结论仍只能标“作者报告/文档内嵌日志、未独立复现”。`pudding` 的 `hello` 是自检页（EL1、FB 色条，116 s 后重启；[PUDDING_HELLO.md:5](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/docs/PUDDING_HELLO.md#L5)），不是 iOS 的 Hello/Setup。其 iOS 首次 present 在 835.254 s，37 ms 后 `exec Setup`（[PUDDING_STATUS.md:1241](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/docs/PUDDING_STATUS.md#L1241)）；作者后来把 835 s 从“lsd 建表”更正为 `assetsd` 阻塞（[PUDDING_STATUS.md:1474](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/docs/PUDDING_STATUS.md#L1474)）。`data-a-live` 声称 2026-10-22 首次出锁屏、GPU 合成约 110 fps，但仓内无对应原始日志，属作者报告。

| 能力 | pudding-ios | data-a-live |
|---|---|---|
| 显示 | 无本仓实现，继承上游 `SweetFramebuffer`；作者日志称 1220×2656/120 Hz，未独立复现 | `pandora_dpu.c` 与显示链已接入构建；作者报告可用，无原始日志 |
| GPU | 继承上游；作者报告后期 GPU 合成 49–81 fps（[PUDDING_STATUS.md:1497](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/docs/PUDDING_STATUS.md#L1497)），本仓无 GPU 代码 | `PandoraAdrenoGpu.cpp` + Turnip/NativeMetal 已接线；作者报告可用，无原始日志 |
| 触控 | 继承上游 `pandora_touch.c`；作者文档称可用，但当前启动参数是 `pandora_touch=0`（[PUDDING_BOOT.md:42](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/docs/PUDDING_BOOT.md#L42)），按轮次不一致 | `pandora_touch.c` 已加入 `bsd/conf/files`（[0130:9](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/xnu/patches/0130-pandora-volup-tlmm-and-touch.patch#L9)）；作者报告可用 |
| USB | 本次未发现数据/USB 日志实现；日志走 UFS 固定文件 | `pandora_usb.c` 串口**源码/编译接线存在**（[0249:9](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/xnu/patches/0249-usb-serial.patch#L9)）；**端到端串口运行未确认**，不能写成已可实时 USB 日志；README 明说数据功能没做（[README.md:23](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/README.md#L23)） |
| 电池/充电 | 文档列为“等系统起来”，本仓无实现，状态未知 | 电量计读数接 SweetBattery 由作者报告；充电控制未验证（[README.md:21](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/README.md#L21)） |
| WiFi/蓝牙 | 本次未发现 WiFi/蓝牙驱动；Setup 卡在无 WiFi/无 USB 数据，文档计划 | 本次未发现蓝牙控制器驱动；WiFi 仅调查。README 第 24 行只列 Wi-Fi、蜂窝、相机、背屏，**不能拿来直接证明蓝牙缺失**（[README.md:24](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/README.md#L24)） |
| 音频 | `0252` 已把音频设备/ADSP/功放代码编入；作者报告硬件链通、VirtualAudio 不给路由（[AUDIO_HANDOFF.md:5](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/docs/AUDIO_HANDOFF.md#L5)），即“代码已接线、无声音” | ADSP/功放代码在仓；作者报告链路推进到功放，仍属未独立复现 |

`pudding` 的 README 状态“卡在 exec 签名校验”已落后于其 `PUDDING_STATUS` 的 SpringBoard/Setup 记录；而 `PUDDING_STATUS` 又引用了本仓不存在的 0262+ 补丁。引用其结论时必须同时给“文档轮次”和“仓内可构建版本”，不能把两者当成同一二进制。

## 3. 可借鉴项（不凑数）

### data-a-live

**（1）`pandora_sink`：替代 daemon 的兼容 stub（最新 HEAD 重点）。** 内核 `0330` 在 `exec_activate_image()` 里给被 `sweetcrashstop` 停止的程序换上 `/usr/libexec/pandora_sink`（`sweetsink=2` 时也覆盖被 `sweetnoexec` 拦下的程序），保留原 argv/env，条件是文件存在且 sink 自身未被停（[0330:9](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/xnu/patches/0330-crash-stop-sink.patch#L9)、[0330:61](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/xnu/patches/0330-crash-stop-sink.patch#L61)）。sink 按 `XPC_SERVICE_NAME`/`argv[0]` 在 launchd plist 找 job，认领其全部 Mach 服务；带 reply port 的 MIG 请求立即回 `MIG_BAD_ID`，非 MIG 的 XPC 消息先持有 `HOLD_NS`=1 秒（最多 256 条，最旧先丢）再销毁（[pandora_sink.c:1](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/tools/sink/pandora_sink.c#L1)、[pandora_sink.c:35](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/tools/sink/pandora_sink.c#L35)、[pandora_sink.c:245](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/tools/sink/pandora_sink.c#L245)、[pandora_sink.c:251](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/tools/sink/pandora_sink.c#L251)、[pandora_sink.c:263](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/tools/sink/pandora_sink.c#L263)）。注释记载：立即丢弃曾使 CoreTelephony 客户端反复重连重试，sink 占 63% CPU、多个客户端 40–60%、机身 45°C；改为一秒持有后约每秒重试一次（[pandora_sink.c:36](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/tools/sink/pandora_sink.c#L36)）。

这是**兼容 stub，不是真实服务**：没有实现任何 MIG/XPC 方法，只避免服务缺失时请求永久排队或忙重试。它不能用来解决 Nova 的 lsd 等待，也不能证明 Boot 提速；更不能据此笼统关闭服务。源码/用户态安装接线存在，端到端运行只有作者注释和提交说明，未独立复现。它依赖 iOS launchd 的 Mach/XPC 语义与 `sweetcrashstop`，与 SoC 无关，但与用户态版本强相关；Nova 未发现等价实现。最小离线核对：只读对照 R7/R8 日志里的等待端口与 launchd plist 服务名，不实施 stub、不上机。

**（2）4K 代码签名 1 MiB 批量读 + ARMv8 SHA 测试（合并）。** `cs_native4k_prepare()` 把每次 16 KiB 读改为 1 MiB 缓冲、仍在 16 KiB 边界逐块校验（[0127:10](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/xnu/patches/0127-pandora-cs-native4k-1mib-reads.patch#L10)）；作者称 43 个共享缓存文件读取次数 166777→2630，且逐字节等价（[PLAN.md:239](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/PLAN.md#L239)）。等价测试从补丁后的 `ubc_subr.c` 抽取同一函数、在真实 16K 签名文件上比较 4K digest，并验证损坏文件同样被拒（[cs_native4k_equiv.c:2](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/tools/tests/cs_native4k_equiv.c#L2)）。SHA 侧把压缩函数在 `PANDORA_SHA256_CE_TEST` 下编成用户态程序，用 qemu-aarch64 + Python hashlib 比对（[0211:38](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/xnu/patches/0211-pandora-sha256-ce.patch#L38)、[0211:50](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/xnu/patches/0211-pandora-sha256-ce.patch#L50)），生产接线是在 `corecrypto_sha2.c` 切换 `SHA256_DI()` 并把新文件加入 `osfmk/conf/files.arm64`（[0211:27](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/xnu/patches/0211-pandora-sha256-ce.patch#L27)）。这些都是内核源码/构建清单接线，没有额外运行参数。

本机已有 256 KiB 批量读和 `read_ms/hash_ms` 计时（[ubc_subr.c:5878](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/ubc_subr.c:5878)、[ubc_subr.c:5959](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/ubc_subr.c:5959)），也已有 ARM64 SHA-256 汇编入口（[corecrypto_sha2.c:175](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c:175)）。R7 的 cs4k 合计是 19.344 秒、读 10.911 秒、hash 8.405 秒（[analysis-run-nova.txt:19](<nova-root>/artifacts-nova/boot-speed-round7-nova/device-analysis-nova/storage-logio-nova/analysis-run-nova.txt:19)），但这是**累计函数计量，不等于独立阶段墙钟**；不能拿主机 `read_ms` 去和手机 744 ms 比较，也不能据此断言 1 MiB“不会改变 3–4 分钟”。能说的只是：**目前没有证据表明 256 KiB→1 MiB 能消除分钟级等待**。最小离线验证仍是 0105/0127 的 digest 等价 + 读取次数比较；仅作为未来备选，不作为本轮优先后续动作。

**（3）双簇 AMP 的 P-spill 机制——只能作为待验证方向，不作为直接修复。** 作者在 `MAX_CPU_CLUSTERS=2` 下把 6 个 Oryon 性能核当 E、2 个超大核当 P，并在 `thread_group_init` 回调里对所有新线程组推荐 `CLUSTER_TYPE_P`，让 UI/FG 优先 P、忙时溢到 E，UT/BG 留 E（[0320:19](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/xnu/patches/0320-amp-pspill.patch#L19)、[pandora_perfctl.c:233](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/wsl/pandora_perfctl.c#L233)）。其生效前提被写死为 `num_clusters==2` 且簇类型恰为 E|P（[pandora_perfctl.c:262](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/wsl/pandora_perfctl.c#L262)）；设备树也必须用 `mkadt_pandora.py --amp` 标簇。作者报告 P 核从闲置变为 55–83% 忙（PLAN post15）。

本机 `board_config.h` 仍走 BCM2837 的 `MAX_CPU_CLUSTERS=1`（[board_config.h:138](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/pexpert/pexpert/arm64/board_config.h:138)），没有 P-spill。SM8550 是 1+4+3 三簇，不是 6+2；直接照搬会因 `num_clusters==2` 断言与簇类型不匹配而不生效，且需补设备树 cluster-type。**这是可能的调度收益点，但在没有本机启动阶段每核占用/频率证据前，不能声称提速，也不应当成 e22 修复。** 最小离线验证：只从现有启动日志统计 8 核在 Boot→Hello 各阶段的占用与频率，确认 P 簇是否闲置；若未闲置就停止，不改设备树、不改 `board_config.h`。

### pudding-ios

**（1）屏幕缩略图时间线，补 Nova P02 缺的视觉首帧锚点。** `0250` 每 N 秒把当前帧缩成 60×130 ASCII 缩略图，带 uptime 标签写入内核日志（[0250:7](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/xnu/patches/0250-pandora-screen-thumbs.patch#L7)），线程由 `pandora_thumbs_init()` 启动（[0250:51](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/xnu/patches/0250-pandora-screen-thumbs.patch#L51)）。配套 `run_report.py` 解析 `SWEET THUMB`、`SWEET FB: swap`、`display on/off`、`exec`、`CRASH`，输出首/末 present 与屏幕变化时间线（[run_report.py:6](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/tools/run_report.py#L6)）。构建接线是把 `pandora_thumbs_init()` 加进 `pandora_selftest.c` 的初始化路径；启动参数 `pandora_thumbs_start/every/until`。

本机没有等价的低帧率屏幕指纹；P02 仍缺 Boot/苹果/Hello 视觉锚点。这个方案不依赖 SM8850 寄存器，只依赖“把 framebuffer 读出来做缩略图”的显示驱动能力，Nova 有显示驱动，理论上可移植。代价是周期性读帧/缩放，必须默认关闭、只在诊断启动参数下开。最小离线验证：用已有启动日志手工统计 `present` 间隔，确认加入缩略图后同一轮的首帧时间戳可重复、且不改变 present 节奏。

**（2）日志式 HFS 根卷与“脏卷不是慢，是数据损坏”的判据——只作为反面/对照。** `pudding` 的 835 s 首次 present 最终归因于 `assetsd` 被脏的非日志根卷拖到 832.96 s 崩溃；改成日志式后硬复位 j2/j3 为 48 s，日志重放约 0.2 s（[PUDDING_STATUS.md:1472](https://github.com/yechenan/pudding-ios/blob/836d4c879a417b588224e6fa594dd85914af457d/docs/PUDDING_STATUS.md#L1472)）。仓内有 `hfs_journal_check.py`、`hfs_set_journal_bit.py` 等检查工具。它说明：**在归因启动慢之前，先证明卷是 journaled/clean，否则会把文件系统损坏误判为调度或用户态慢。** 但 Nova 是 APFS 暖 Data/快照路径，不是 HFS+ 固定文件，这些脚本和 8 字节卷头修补不能套用；只能借鉴“先记录卷状态再比较启动阶段”的实验纪律；最小离线验证也只是只读解析现有镜像卷头的 journaled/clean 位并对照启动日志，不写卷、不修卷。故不计为直接提速项。

`pudding` 的 `0247/0248` fsync/verify 也属同类：解决的是“写入返回成功但 HFS 日志仍旧”的持久化问题，Nova 的暖 Data 有独立快照/校验链，不能作为 e22 的替代。

## 4. 不可直接套用

- **启动链与分区**：两仓用 ABL `fastboot boot` + boot header v4 + iBoot shim；Nova 走 UEFI。`data-a-live` 的根是 userdata 内固定 16 GiB 文件（[README.md:15](https://github.com/ShueiTsuki/data-a-live/blob/5fc9d2fb7acbe2ae9694891db42d49a44f6acb35/README.md#L15)），另有“切 64 GB ios 分区”的备选；Nova 必须保留 Android B，不能采用对方的分区/安装器或清数据流程。
- **显示/触控/按键**：1220×2656 视频模式、`splash_region 0xFC800000`、Synaptics TCM SPI、PMK8850 按键，都是 SM8850 机器实测值；Nova 的面板、触控、PMIC 不同，寄存器/时序/坐标参数不可复制。
- **GPU**：Adreno 840 + 该仓 Turnip/NativeMetal/metal2vulkan 版本；Nova 是 A740，内核 ABI、SMMU/BO 生命周期和用户态版本不同，不能整包替换。
- **CPU**：6+2 Oryon 双簇与 Nova 1+4+3 三簇不同；`0320` 的 `num_clusters==2` 是硬门槛，不能把“P-spill”当成通用开关。
- **文件系统**：HFS+ 根镜像、journal 位修补、`clear_dirty_bit` 类操作不适用于 Nova 的 APFS Data/快照；不要用对方“大版本用户态 + 整体根镜像”替换来换启动时间。
- **4K 签名**：原理相同，但对方补丁带 `PANDORA` 宏、3.3 GB 缓存和自身函数布局；Nova 已有 256 KiB 批量读、SHA 加速和计时，不能覆盖现有实现。

## 5. 与上次 m2381 相比

上次 m2381 审的是 UEFI/设备仓库，最新提交只是文档或 board 注释，结论是“有电池协议/服务裁剪参考、无 e22 修复”；这两个仓是**同代 SM8850 的真实 4K 页 iOS 移植，给出了更明确的 cs4k 批量读、SHA 测试和双簇调度实现**，但同样没有 e22 补丁或已测的 Boot→Hello 提速，且设备相关性更强、更不能直接搬。

## 6. 唯一建议的本地离线后续行动

**离线把 Nova 已有 R7/R8 等待日志中的端口/服务/调用者/等待年龄整理成依赖清单，并与 launchd 服务配置、`data-a-live` sink 对应的问题类型对照。** 只做只读日志与源码归属，不实施 stub、不改策略、不上机。证据：R7 分析里 lsd endpoint 的等待 age 从 8.462 s 递增到 **90.060 s**（[analysis-run-nova.txt:240](<nova-root>/artifacts-nova/boot-speed-round7-nova/device-analysis-nova/storage-logio-nova/analysis-run-nova.txt:240)），只能视为等待观察，不等于 Hello；同报告 cs4k 的 19.344 s/读 10.911 s/hash 8.405 s 是累计函数计量，不等于阶段墙钟（[analysis-run-nova.txt:19](<nova-root>/artifacts-nova/boot-speed-round7-nova/device-analysis-nova/storage-logio-nova/analysis-run-nova.txt:19)）。

验收标准：每个结论都必须同时给本机日志行和源码位置；区分“服务缺失”“崩溃后重启”“服务已在运行但无回复”三类；优先定位最长 lsd 等待的调用双方和具体请求；没有证据的一律留 unknown；**不得用对方 CommCenter 的故事推断本机同根因**。256 KiB/1 MiB 等价测试只作为未来备选，不作为本轮优先后续动作。此步骤不实施 stub、不改策略、不上机。

## 输入范围与限制

只读研究：浅克隆 depth 6 的两个固定提交；未拉 submodule/LFS；未执行任何外部脚本/二进制；未构建、未安装、未接触设备；未使用 GitHub API（本次 403，提交时间与差异来自公开 `git log/show`）。本次检查以源码和作者文档内嵌日志为主，未独立复现；在本次所查 tracked 文件中未发现与文档分离的独立原始运行日志/截图，未穷尽仓库历史或站外附件，不能据此断言不存在。所有“可用/帧率/秒数”均为作者报告或文档内嵌日志，未独立复现；缺失子模块/未提交轮次的实现不能称为已检查。Nova 的 e22/b2/op9 与 mode/uid/gid 结论沿用 `artifacts-nova/boot-speed-round12-nova/REPORT-nova.md` 与 `artifacts-nova/boot-speed-round12-nova/stat-abi-review-nova/REPORT-nova.md`；**e22 仍是 R11 后暖 Data 候选的首个已知阻塞，stat32/64 偏移假设已排除，R12 仅增加日志且已构建未安装**，本次未重开该 ABI 假设。
