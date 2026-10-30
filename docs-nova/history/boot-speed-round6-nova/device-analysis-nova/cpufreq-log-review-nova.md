# 第六轮 CPU 快照与日志完整性独立核查 — Nova

2026-10-30（Asia/Shanghai）。新文件，无旧版本。本项仅新增本报告；未修改生产源码、中央文档、其他报告或设备。来源为当前 Nova 源码、已钉住提交的 SM8550 实现和本轮离线日志；未执行上游脚本。

结论：第六轮成功得到一次 domain0 的四字只读快照，证明此次访问按预定路径返回；尚不能证明 CPU 全程低频，不能据此归因约三分钟启动。日志文件封装与捕获哈希核对通过：text_bytes 比 ring_total 多出的 173 字节全部来自合成 boot-video 前缀，不是丢失或长度错误。`dropped=0` 仅说明本次快照没有因 ring 容量而淘汰正文，不代表从 Boot 菜单到复位的所有事件均已保存。

## CPU 快照可以说明什么

输入为 [autolog 第 186 行](<nova-root>/logs-nova/boot-speed-round6-nova/candidate-run-nova/autolog-nova.txt:186)。全文件仅有一条 `nova: cpufreq`，phase 为 `autolog-once`。

| 字段 | 本轮数值 | 可以支持的结论 | 不能推出的结论 |
|---|---|---|---|
| status / reads / valid_mask | sampled / 4 / 0xf | 四次原始 load 均返回，经过 enabled 路径 | valid_mask 不是硬件健康、频率合理或可随意写寄存器的认证 |
| enable_raw | 0x00000003 | bit0=1，符合 Linux EPSS 的 domain enabled 检查 | bit1 的额外语义、所有 CPU 在线、整段启动正常 |
| state_raw | 0x2e000c57 | Linux EPSS 状态换算使用 low8=0x57=87 | 未验证输入时钟前不能声明实测 MHz；高位不按猜测解码 |
| dcvs_raw | 0x00009b30 | bit0=0；固定 Linux 实现不会据此启用 per-core request rows | 不能据其余位推断调频策略、负载或禁用全部动态调频 |
| request_index_raw | 0x0000000c | 性能请求索引 12；小于实现的静态 40 项上限 | 不是 12 MHz、不是 1.2 GHz；没有 LUT 不能知道该行频率或是否为有效末尾/boost 行 |
| MPIDR before / after | 0x81000000 / 0x81000000 | 两端 affinity 匹配，affinity_changed=0 | 读取 MPIDR 没有绑核，不能排除中间迁移；不是所有核心的采样 |
| begin / end / timer_hz | 501980442 / 501980824 / 19200000 | CNTVCT 采样窗口 382 ticks，即约 19.895833 微秒 | 19.2 MHz 不是 CPU 频率；raw tick 除频率不是 Boot 菜单开始后的时间 |

采样代码 [CpuFreqReadiness.c:163](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeMeizuCpuFreqProbe/CpuFreqReadiness.c:163) 在检查和映射之前取 begin，end 在解映射之后、printf 之前。因此 19.895833 微秒覆盖采样函数内这些步骤，不能当作四次 MMIO 的纯总线耗时，也不包含之后格式化日志和 UFS 持久写。valid_mask 在每次 load 返回后设置，定义见 [CpuFreqReadiness.h:26](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeMeizuCpuFreqProbe/CpuFreqReadiness.h:26)。这次实际访问已成功返回，不扩大为其他地址、时段或写操作的可用性证明。

本轮快照在 [autolog 调用点](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:8070) 发生，不是最早内核入口，更不是 Boot 菜单点击。getter 返回同一份不可变缓存，不能把以后引用 sample_id=1 的 CPU 记账行视为新的硬件频率采样。[实现注释与发布代码](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeMeizuCpuFreqProbe/CpuFreqReadiness.c:213) 也明确保留此边界。

## 固定一手实现支持的解码边界

已重新核对 [cpu-register-reference-manifest](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/sources-nova/cpu-register-reference-manifest-nova.json) 的三个源码副本大小与 SHA256，全部一致。固定源提交为 `42f3b40c702abbc37c3a04c95af008cd283aeaf3`；其中 qcom-cpufreq-hw.c 的 SHA256 为 `ae071321d207bfd57efb479e0d1bff47ba3640884faf0013f808818cb3015a27`。以下结论来自该驱动的实际代码，不把 Sheng 的常数直接视为魅族真机频率测量。

- [EPSS 布局第 413 行](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/sources-nova/sm8550-mainline-nova/drivers/cpufreq/qcom-cpufreq-hw.c:413) 定义 enable 0x0、state 0x20、DCVS 0xb0、LUT 0x100、request 0x320、row stride 4、最多 40 项；本轮四个偏移与其一致。
- [初始化第 559 行](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/sources-nova/sm8550-mainline-nova/drivers/cpufreq/qcom-cpufreq-hw.c:559) 只用 enable bit0 判断启用，用 DCVS bit0 判断 per_core_dcvs。其余原始位在本报告中保持不解释。
- [request 读取第 147 行](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/sources-nova/sm8550-mainline-nova/drivers/cpufreq/qcom-cpufreq-hw.c:147) 明确表示 cpufreq core 请求值，读取索引后再查频率表。索引 12 在静态上界内，仍不能代替实际 LUT 行有效性检查。
- [LUT 解析第 241 行](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/sources-nova/sm8550-mainline-nova/drivers/cpufreq/qcom-cpufreq-hw.c:241) 需要 source、L、core-count、重复终止等信息；不同 source 使用 xo_rate 或 alternate clock 的派生值。本轮没有读取 LUT，故不能报告请求档位 MHz、最高非 turbo 档位或距上限的比例。
- [状态换算第 135 行](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/sources-nova/sm8550-mainline-nova/drivers/cpufreq/qcom-cpufreq-hw.c:135) 在 EPSS 没有 reg_current_vote 时使用 `(domain_state & 0xff) × xo_rate`。本轮只知道乘数为 87；没有请求频率就不能从状态与请求的差推断 throttling，更不能断言是过热。
- [时钟获取第 658 行](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/sources-nova/sm8550-mainline-nova/drivers/cpufreq/qcom-cpufreq-hw.c:658) 实际查询名为 xo / alternate 的 clock provider；alternate 还要除以 CLK_HW_DIV=2。[SM8550 DTSI 第 5981 行](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/sources-nova/sm8550-mainline-nova/arch/arm64/boot/dts/qcom/sm8550.dtsi:5981) 指向 bi_tcxo_div2 与 GCC_GPLL0，而非 CNTFRQ。

条件示例：若另行证实魅族 EPSS 的 xo 输入也是 19.2 MHz，则状态换算为 `87 × 19.2 MHz = 1670.4 MHz`。这个 19.2 MHz 假设来自 [固定 Sheng 实现的 kXoKHz](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/sources-nova/UniXNU-sheng-nova/package/CpuFreqProbe.cpp:66)，并不是因为本轮 CNTFRQ 恰好同值。当前 [魅族 DTB 证据](<nova-root>/artifacts-nova/sheng-research-nova/cpu-boot-nova/nova-dtb-cpu-evidence-nova.json:182) 证明窗口、compatible 和 xo/alternate 引用，但未完成完整 clock graph 频率确认。1670.4 MHz 只能标为条件推导的状态值，不能写成已测得 CPU 恒定运行频率，也不能据它确认或排除整段启动的 CPU 瓶颈。

## 三种时钟必须分开

CPU 探针 [probe_ticks](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeMeizuCpuFreqProbe/CpuFreqReadiness.c:74) 与 BootLog 状态读取 CNTVCT；[NativeUfs timerTicks](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1343) 读取 **CNTPCT**；IPC/任务 CPU 窗口使用 mach_absolute，其本地实现包含 cpu_base_timebase。不能仅因它们都按 19.2 MHz 换算且数值相近，就直接混减得到跨阶段时间。

前一份 anchor-feasibility 草稿把 rootio 标成 CNTVCT 属于误标；本次已向集成代理和独立审阅者指出，正确名称是 CNTPCT。本项遵守仅写本新报告的范围，未替其他任务修改该报告。跨时钟统一时间线需要配对采样/偏移校准；同一时钟内的时间差无需假定其 epoch 为菜单起点。

## 日志封装逐字节核查

输入：[capture-nova.json](<nova-root>/logs-nova/boot-speed-round6-nova/candidate-run-nova/capture-nova.json)、[原始窗口](<nova-root>/logs-nova/boot-speed-round6-nova/candidate-run-nova/autolog-window-nova.bin)、[提取正文](<nova-root>/logs-nova/boot-speed-round6-nova/candidate-run-nova/autolog-nova.txt)。捕获元数据时间为 2026-10-29 18:28:50.474589 UTC，即北京时间 2026-10-30 02:28:50.474589；它是读取保存时间，不是 Hello 时间。

| 检查 | 实算结果 |
|---|---|
| raw window | 33,554,432 字节；magic=MEIZULOG |
| header ring_total | 1,118,534 |
| header text_bytes / 实际文本大小 | 1,118,707 / 1,118,707 |
| 提取边界 | 文本逐字节等于 raw[4096:4096+text_bytes] |
| 合成前缀 | 173 字节 = 开头换行 1 + boot-video 行 172 |
| 扣除前缀的 ring 正文 | 1,118,534，恰好等于 ring_total |
| dropped / reserved | 0 / 0 |
| 文本 NUL | 0 |
| 文本行 | 6,226 行，末尾有换行 |
| header page padding | 32 至 4095 字节全部为零 |
| 本快照写入块数 | 275 个 4096 字节块 |
| 最后一块正文后的 padding | 3,597 字节，全部为零 |
| raw SHA256 | `1d2370afbeb4c49b3d68a5934ad2698a394fe055d552a9a3dd504e08b84a27e4`，与元数据一致 |
| text SHA256 | `367d82822557acd8967eb53f522a8c3d7db70dc268fc6fbb02f8a3337d882ad8`，与元数据一致 |

173 字节差异与生产 writer 完全对应：[kern_exec.c 第 7739 行](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7739) 调用 native_blog_boot_video_format，并在有前缀时额外加入一个换行；第 7746 行 `n=prefix+retained`，hdr.total=snapshot_total 而 hdr.bytes=n。[BootLog.c 第 230 行](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:230) 明确完整 boot-video 行固定为 172 字节。因此不能用 `text_bytes != ring_total` 判定日志损坏。

[read-autolog-nova.py 第 28 行](<nova-root>/tools/read-autolog-nova.py:28) 核对窗口长度和 magic，从 header bytes 指定范围提取文本。此次独立复算读取的是既有本地文件，没有执行该设备采集脚本。快照之外的 32 MiB 尾部空间不属于 header 指定的有效文本，不据其中内容追加旧日志。

## “完整”的实际范围与下一步

本轮 ring_total 小于 [8 MiB ring 容量](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/console/meizu_log.h:6)，retained=total，dropped=0；正文没有 NUL，结构和文件复制一致。可称“此次持久快照未见 ring 覆盖丢字节、提取截断或封装长度异常”。

仍不能称“完整捕获所有启动事件”：[writer 第 7674 行注释](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7674) 规定最短一秒写间隔，正常运行时复位仍可能丢最近约一秒加调度/I/O 延迟，writer stall/error 时没有墙钟上限，分块原地写也不具备掉电原子性。[ring snapshot](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/console/serial_general.c:165) 与并发写字符不构成原子事务，dropped 是容量淘汰计数，不是所有潜在缺失的检测器。固件菜单未进入此 ring，Hello 也没有已验证的语义时间点。

建议先把本次同 generation 的 lsd CPU 用时与墙钟等待分析合并：CPU 少而等待长时，继续追等待对象/服务重试；CPU 长时，再核实持续运行频率是否值得作为加速方向。单个 autolog 快照不能替代这一步。若后续要量化 CPU 请求/状态频率，下一项应是核实魅族 xo/alternate clock graph，再设计有界只读 LUT/阶段状态观测，分别标记请求值、状态换算值与采样时刻。本报告不建议直接写性能寄存器、不扩大到 L3 或打开完整 governor。
