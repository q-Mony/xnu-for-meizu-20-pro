# Boot → Hello 耗时分析（Nova，2026-10-29）

日志支持的主要延迟是：**launchd 启动前同步转换 dyld 缓存签名，随后 SpringBoard 主线程串行等待 launchd 和 LaunchServices，最后 Setup 仍有超时重启。** 这些阶段足以解释分钟级启动；现有日志缺少选择 Boot 和 Hello 首帧的同源时间戳，不能精确复原用户观察到的 300 秒。

本次只读历史日志、冻结构建物及 Nova 副本源码。新增文件仅在 Nova；没有修改功能源码、构建、刷机或操作设备。

## 证据范围与来源

- Astra 的 [REPORT.md]([local-home]/...) 引用 `032506` 基线。未在已检查的 Astra 目录发现另一份独立原始启动日志；主机编译日志不能代替手机启动日志。
- 主分析日志：[autolog-20261029-032506.txt]([local-home]/...)。SHA-256 `e5ee7199ec234746ee60d2244554dab3b5c33ab05f23c073e370cdb103983bf2`。
- 实际报告过 Hello 的对照：[autolog-20261028-151829.txt]([local-home]/...)，用户观察记录见 [hello-151829-analysis.md]([local-home]/...)。它和 032506 属于不同构建/启动，不互相借用精确时间或二进制身份。
- 另外比较 20261028-214724、20261029-022743；四份文本哈希各不相同。`snapshot/sol/autolog-20261029-035508.txt` 与 032506 完全相同，未当作另一次启动。
- 冻结 boot 镜像 SHA-256 `4fcf20feb1639c19fac0331c3ea4180ba0d0f302078ed2bde88f04c5434a1ac1`；其已提取 kernel SHA-256 `9a4212834eb4ac1575d1d47c91802471f833eb7e268d8b351a5913cbe90636ae`。本次重新计算文件哈希；镜像、提取物与 032506 的关联沿用 [历史取证报告]([local-home]/...)，未重新提取镜像或读取当前设备。

## 1. 前段：同步转换整个共享缓存的签名

032506 第 243 行开始打开 `dyld_shared_cache_arm64`；第 594 行才进入共享区映射，参数 `files=44 maps=52`。这段中 launchd 反复停在 `fcntl(F_ADDFILESIGS...)` 的签名注册调用。

例如 `.01`：

- 第 251 行 `addfilesigs pid=1 cmd=97 size=229376 off=114409472`。
- 第 252～258 行穿过 heartbeat 20、40、60、80；第 260 行才返回成功。
- 较小的相邻分片迅速返回；多个上百 MB 分片重复类似过程。
- 签名日志打印额度是 24 对；第 24 对以后不再打印成功信息，不代表后续转换停止。

**延迟已定位到 `ubc_cs_blob_add()` 内部。** [kern_descrip.c:4034](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_descrip.c:4034) 的起始打印发生在签名 blob 读取之后，起止打印之间调用该函数。因此这里的长间隔不能仅解释为读取那个 224 KiB 签名文件块。

冻结构建启用了 `XNU_NATIVE_CS_HASH_CACHE=1`。[ubc_subr.c:4877](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/ubc_subr.c:4877) 同步调用 `cs_native4k_prepare()`，其 [完整循环](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/ubc_subr.c:5805) 会：

1. 按 CodeDirectory 的 `codeLimit`，逐 16 KiB 读完已签名正文。
2. 计算并验证原始 16 KiB 摘要。
3. 对同一缓冲区再逐 4 KiB 计算摘要，供当前 4 KiB 内核使用。
4. 全部验证完，才发布转换结果并返回调用者。

因此当前兼容实现把整份正文的读取及两轮摘要计算放在启动关键路径上。该循环本身是串行的。

这不只是源码猜测：第 592 行实际采样 PC `fffffff0083859f0`，在上述冻结 kernel 中解析为 `_ccsha256_ltc_compress+0x17c`；历史采样的另外两点分别是同一函数 `+0x440`、`+0x344`。一个预链接区 PC 尚未命名，未强行归因。源码 `ubc_subr.c` 和 `kern_descrip.c` 的 SHA-256 均与冻结 manifest 一致。

**时间量级：**032506 在映射前已有 tick1940，映射后 tick1960；按周期线程名义每次 50 ms，大致是百秒量级。实际 Hello 对照 151829 对应 tick1340，名义约 67 秒。`tick` 是 dump 调用计数，有显式 flush、I/O 和调度影响，不能把这些数字当作精确秒表或不同构建间的严格性能差值。可以确认该阶段长期占用启动路径，不能证明其中 CPU 与存储 I/O 各占多少。

## 2. 后段：SpringBoard 串行等了至少 136.126 秒

032506 中同一 SpringBoard PID26/TID254：

| 等待对象 | 请求序列 | 最后捕获的 pending age | 证据 |
|---|---:|---:|---|
| launchd | 261 | **35.296 秒** | [第 1568 行]([local-home]/...)，phase4/pending1，终点 PID1/gen4 |
| lsd | 2376 | **100.830 秒** | [第 2053 行]([local-home]/...)，phase4/pending1，终点 PID97/gen272 |

两段请求按顺序发生在同一主线程，因而可以合计下界 **136.126 秒（2 分 16 秒）**。这不是把不同后台进程的并发时间重复相加。`age_ms` 来自 [mach_msg.c:1445](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:1445) 的绝对时钟差；这里只纳入 phase4 且 pending1 的记录，不把已完成请求的“年龄”计成等待。

独立历史日志复核：

| 提取文件标识 | 等 launchd 下界 | 等 lsd 下界 | 同线程串行合计下界 |
|---|---:|---:|---:|
| 20261028-151829（用户确认 Hello） | 26.736 s | 103.352 s | 130.088 s |
| 20261028-214724 | 35.241 s | 100.380 s | 135.621 s |
| 20261029-022743 | 27.987 s | 103.440 s | 131.427 s |
| 20261029-032506（Astra 基线） | 35.296 s | 100.830 s | 136.126 s |

所以 100 秒左右的 lsd 等待在这些记录中是重复现象。不同日志含不同观测补丁，表格证明重复出现，不是严格的同配置性能实验。

### lsd 在等什么

当前日志的用户栈去掉共享缓存 slide 后，与已保存的 [144210 符号报告]([local-home]/...) 有精确地址匹配：

- 第 1154～1165 行：`_LSPreferences`、`_LSDatabaseClean`、`LSDatabaseBuilder createAndSeedLocalDatabase:`，以及首次启动容器清理路径。
- 第 1187～1188 行：`LAUNCH_SERVICES_CLIENTS_ARE_WAITING_FOR_THE_DATABASE_TO_FINISH_SEEDING` / `LSDatabaseBlockingFetchClient`。
- 第 1655、1694～1697 行：扫描应用目录、汇总 LaunchServices 数据库、查询容器的路径。

这些支持“**应用数据库尚在初始化/扫描，客户端等待它就绪**”。符号匹配复用了历史符号资料，没有重新验证当前缓存 UUID；未匹配地址仍未知，系统调用 stub 的邻近符号未当作真实函数调用。

同次启动的原始 IPC 证据进一步显示：第 1609～1621 行 lsd TID439 发往 `containermanagerd`；第 1792 行它另有请求等待 PID83/gen215，该身份在第 933 行已确认为 `usermanagerd`。第 2057 行仍有 lsd worker 的 semaphore 等待。

**不能据此认定 containermanagerd 单独卡住了 100 秒。** 容器队列样本有 `selection-changed`、`continuity=unknown`，lsd 的后续依赖也发生变化。launchd 那段 35 秒具体卡在哪个处理操作，当前样本同样不足。最终锁 owner、服务回调或工作线程准入原因仍需后续区分。

## 3. Setup 还会超时重启，但不能全部算进首个 Hello

032506 第 2162 行才 arm 第一轮 Setup；第 [4575 行]([local-home]/...) 和第 6958 行各有一次 `scene-create` 超过 **20.00 秒**，被 watchdog 以 `8BADF00D` 终止；第三轮只保留了部分记录。

实际 Hello 的 151829 日志共有六次 Setup scene-create watchdog（首轮 10 秒，后五轮 20 秒）。这说明画面出现与 Setup 成功稳定进入界面不是同一件事。

日志没有 Hello 首帧时刻，不能直接把这些 20 秒全部叠到首屏时间。Framebuffer 的 swap 日志也没有时间戳，且打印发生在实际拷贝之前，不能把某一条 swap 当作 Hello 已显示。

## 4. Astra 的单并行度发现如何解释

Astra 的 [报告]([local-home]/...) 和 [active accounting]([local-home]/...) 表明：冻结镜像的启动参数缺少 `sheng-smp`，实际工作队列缓存并行度为 1；Setup 有 constrained 请求准入被拒绝的观测。这可能放大计算和服务回调的排队延迟。

但并行度缓存值不等于全过程在线 CPU 数；没有记录各核心在线的完整序列，也没有受控对照证明开 SMP 后能缩短这两段等待。尤其签名转换循环自身是串行的，不能把“八核上线”直接折算为八倍提速。

## 时间戳限制与下一步优先级

冻结 ADT SHA-256 `e619e34a7ba160ee427c2d401bd86e90777f61d6295b2f8d641bc24b108a1654`，8 个 CPU 节点 `timebase-frequency` 均为 19,200,000 Hz。`[System Event]` 打印的是原始 `mach_continuous_time()` ticks，见 [system_event_log.c:60](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/os/system_event_log.c:60)。首个 fsck 事件到 `Early boot complete` 仅差 **1.032922 秒**。其绝对时钟坐标约 130.49/131.52 秒不能直接当作选择 Boot 后的时间。

另外：文件名时间是日志提取时间；`vm_swap_create_file failed @26 secs` 的 26 是调用前采样；300 秒诊断保留窗口不是 300 秒启动 sleep。这些均未用于虚构总时长。

建议后续优化按证据排序：

1. **先测签名转换的读耗时与 hash 耗时，并核查重复转换。** 这是已定位到函数和实际采样 PC 的启动长路径；改动必须保留原签名校验与 4 KiB 页验证语义。
2. **再测 lsd 数据库初始化到 SpringBoard 收到回复的链路。** 区分数据库扫描、容器/用户服务响应、回调线程调度；它每轮稳定贡献约 100 秒等待。
3. **把 Setup 的 20 秒 watchdog 作为另一个交互稳定性问题处理。** 结合 Astra 并行度证据做有计时和在线拓扑记录的验证，而非直接假定 SMP 已解决。

复测时需要同源时钟标记 Boot、转换入口/出口、首个 Hello 实际提交或呈现时刻，才能验证总时间下降多少。本次任务仅完成离线诊断，不宣称已修复。

## 可复查交付物

- [analyze-boot-nova.py](<nova-root>/artifacts-nova/boot-latency-nova/analyze-boot-nova.py)：仅解析历史文件，已运行成功；默认校验关键等待与日志去重。
- [measurements-nova.json](<nova-root>/artifacts-nova/boot-latency-nova/measurements-nova.json)：四份日志的哈希、行号、请求序列、等待样本和签名操作配对。
- [provenance-nova.json](<nova-root>/artifacts-nova/boot-latency-nova/provenance-nova.json)：5 份源码与冻结 manifest 一致、4 份冻结构建物哈希、内核符号与采样偏移。

复查命令（只输出 JSON）：

```sh
python3 <nova-root>/artifacts-nova/boot-latency-nova/analyze-boot-nova.py
```

该脚本是可重复的日志测量，不是实际启动性能回归测试。没有操作手机，也没有用模拟测试替代真实 Boot→Hello 验证。
