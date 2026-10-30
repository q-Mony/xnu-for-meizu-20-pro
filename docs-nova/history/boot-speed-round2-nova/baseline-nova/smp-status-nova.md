# Nova 成功启动基线的 SMP 状态

2026-10-29。新文件，无旧文件。本次只核查本地源码、成功候选及历史记录；未修改生产源码、启动配置或 CHANGELOG，未构建、未操作设备。主代理统一记录本轮变更。

**当前成功候选的二进制和启动参数选择 boot-only 路径，未启用 ArmCpu 的 secondary probe。** 这有新鲜二进制证据，不只是检查 board.toml。现有运行日志缺少逐核状态和在线 CPU 计数，不能进一步声称“实测全程只有一核”。也不能把补上 SMP 参数当作已经验证的提速修复：历史存在双核候选未到 Hello 的真机记录。

## 当前成功镜像的静态证据

核查的是已到 Hello 的 `2336c87c…e1dab` 候选，其 payload kernel SHA256 为 `6e2f63e577e2fa32f5beaf3c380b03b65ad0db15fb36bebd8d91c07760e0fe30`。镜像、提取内容、固定输入的完整身份见 [baseline-nova.json](<nova-root>/artifacts-nova/boot-speed-round2-nova/baseline-nova/baseline-nova.json)。

| 核查项 | 本次结果 |
| --- | --- |
| 当前/分阶段 ArmCpu.cpp | 两者相同，SHA256 `3c87b3bd617be5582a6a24ff797c359497ee9feaa702ee883ecee4270db21c9f`，也与历史已审计源码相同；分阶段路径解析回 Nova 内源码 |
| 当前 ArmCpu.o | SHA256 `89e7cce5d0ce25492c44443669dcad39f682af0771580e852c2ce9f50e18c8d3` |
| 当前 libArmCpu.a | SHA256 `047b4f2caf49a435382774fd910ddc3aff5d817c4fa639b2250ba7197df8f3d3`；抽取出的成员与当前 ArmCpu.o 逐字节相同 |
| 对象与成功内核 | 对象的 49 个 text 函数，在冻结 raw kernel 和成功 payload 中的全部非重定位指令字相同；重定位字逐项记录为排除项，不声称已经验证全部链接重定位或数据指针 |
| 成功 payload bootargs | 没有 `sheng-smp`、`armcpu-spi-ipi`、`armcpu-diagnostics` |
| 当前 board.toml | 有上述三个参数；不能据此推断成功包启用了这些参数 |
| 成功 payload ADT | cpu0–7，reg/affinity `0x000` 至 `0x700`；cpu0 `running`，其余 `waiting`；19.2 MHz timebase；PSCI `smc`；GICv3、redistributor stride `0x40000` |

对象和 archive 的哈希与旧历史审计产物不同，不能直接沿用旧产物身份；上表以本次字节比较为依据。ADT 的 state、八个节点及 compatible 文本描述的是启动输入，不是运行时在线核数或对真实大小核拓扑的完整鉴定。

直接反汇编当前成功 payload 的 `ArmCpu::probe`：

- `0xfffffff008938864–0xfffffff008938874` 比较 boot CPU 与当前 CPU。
- `0xfffffff008938898` 将 `smp` 默认置零；随后调用 `PE_parse_boot_argn`。
- `adrp/add` 解析出的参数字符串虚拟地址 `0xfffffff00713b459` 已通过 Mach-O 节区映射读取，字节确认为 `sheng-smp`。
- `0xfffffff0089388b4` 检查结果为零时跳到 `0xfffffff008938830` 返回空指针。因此缺参数时不为非 boot CPU 注册该驱动。

对应源码：[ArmCpu.cpp:175](<nova-root>/unixnu-main-nova/drivers/ArmCpu/ArmCpu.cpp:175)。本次新增 [audit-smp-nova.py](<nova-root>/artifacts-nova/boot-speed-round2-nova/baseline-nova/audit-smp-nova.py)、[smp-evidence-nova.json](<nova-root>/artifacts-nova/boot-speed-round2-nova/baseline-nova/smp-evidence-nova.json)、[probe 反汇编](<nova-root>/artifacts-nova/boot-speed-round2-nova/baseline-nova/smp-probe-nova.disassembly.txt)、[参数地址核验](<nova-root>/artifacts-nova/boot-speed-round2-nova/baseline-nova/smp-gate-resolved-nova.json)。脚本已运行，49 个函数在两份内核的检查均通过；未运行任何历史快照脚本。

## 为什么 board 与成功包不同

历史报告已找到直接原因：[setup-active-accounting/REPORT.md:33]([local-home]/...) 记录历史打包器 `setup-selection-provenance/packaging/reproduce.py` 第 21 行定义不含 SMP 参数的固定 `BOOTARGS`，第 84 行用它覆盖 board 配置。Nova 后续候选继续保留这个已验收 envelope。

这是当时固定已验收输入的打包做法。现有证据不能将它描述成“因 SMP 故障而主动关核”，也不能描述成违反原任务的误操作。历史 [meizu-smp-bootargs-fix/REPORT.md]([local-home]/...) 曾准备过独立参数修正，但其停止记录是 `STOPPED_FOR_ASTRA_ISOLATION`，不是硬件验证失败；那次交付本身未刷机。

## 运行证据和历史失败边界

Nova 成功日志没有逐核 ArmCpu/PSCI/MPIDR/在线 CPU inventory。[chronod 的 watchdog 统计](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt:4103) 和 [Setup 的 watchdog 统计](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt:4749) 中的 100% CPU，不足以证明全程只有一个 CPU 在线。`workqueue parallelism=1` 是缓存/策略值，也不能替代运行时 census。

[Sol 的双核实测报告]([local-home]/...) 记录：以 Astra 触控基线 `daf133a0…ce9d4` 为基础，仅替换 payload TOML，追加 `sheng-smp=1 armcpu-spi-ipi=1 cpus=2`；候选 SHA256 `cbbb0312ff9559b2fd10a3677b37eb7094612077e4e98e0f5d5e433b04479a19`。读回确认后启动，用户反馈未进入 Hello、IRQ/E1 快速增长。

该轮日志已经进入用户态，有 launchd 和 SpringBoard 请求，不能说停在 UFS。其触控为 4 ms 轮询，基线也不是当前 Nova 触控关闭候选。日志没有 CPU1/逐核 IPI、timer 或在线数，不能确认 secondary 调度正常，也不能把失败直接归因于 SMP。E1 和 init 线程被 IRQ 打断次数不等于在线 CPU 数；报告中的 heartbeat tick 换算没有在本次重新校准，不能用作精确秒表。

## 后续 SMP 实验应独立进行

本轮 RootWindowRead 批量大小实验继续固定成功 payload 参数。完成其同基线测量后，再决定是否引入 SMP 诊断。不能承诺开核必然消除 launchd/lsd 的等待。

1. 在同一 Nova 基线添加有界、低频观测，再作开关对照。保存 `machine_info.logical_cpu`、`processor_avail_count`、workqueue 首次缓存值及每核 startup step、PSCI 返回、预期/实际 MPIDR、timer/IPI 递增量。对照需要相同诊断代码、相同触控/Data/显示状态。
2. 首个开核对照可限制 `cpus=2`。当前 [machine_routines.c:1008](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm64/machine_routines.c:1008) 已有 `cpus` 参数解析，[1045 行起](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm64/machine_routines.c:1045) 限制加入 topology 的 CPU 数并保留 boot CPU；以后仍需核对实际候选和运行计数。不得同时携带冲突的 `cpumask`。
3. `gCPUsRunning` 是 bool，不是核数；`armcpu_registered`/`armcpu_gic_ready` 是标志。step7 只说明驱动初始化到达相应阶段，不能替代后续调度/IPI 进展。现有 volatile 总计数在 SMP 并发下可能丢失增量，跨字段观测也不是一致快照；新增观测须检查写入/发布契约。
4. `armcpu-self-ipi=1` 是另一项可选配置，不能自动追加。若出现 secondary 初始化后不调度，应单独检查首次 IPI 与 `SIGPdisabled` 清除路径。不要在 IRQ、调度或 processor-start 热路径密集输出日志。
5. 不直接开放八核，不硬改 workqueue 并行度为 8，不同时引入 AMP/大小核拓扑、触控或 GPU 变化。通过标准是保留 Hello、逐核 timer/IPI 持续进展、调度可用与启动耗时改善，不能仅以 PSCI 成功或 DT 节点数量判定。

本报告及旁边的审计脚本/JSON/反汇编均为新增文件，无旧版本；其他会话和历史快照只读。本次未改生产代码、配置、计划或 CHANGELOG，未调用设备工具。
