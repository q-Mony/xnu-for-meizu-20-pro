# Nova 第三轮：签名转换与服务等待对照

2026-10-29。两个报告文件均为新文件，无旧版本；仅离线分析，未操作设备、修改生产代码、计划或 CHANGELOG。

**本轮用户已明确：苹果标志前 73 秒，苹果出现后再等 139 秒，总计 212 秒（3 分 32 秒）。比第二轮 245 秒快 33 秒，约 13.47%。** 原先将 139 秒解释为总时长的记录已经由主代理保存旧版并更正；本报告使用澄清后的数值。

Boot 为 2026-10-29 17:12:50，Apple 约 17:14:03，Hello 约 17:16:22，时区 Asia/Shanghai。这些是用户计时及算术换算，日志未自动识别 Hello 首帧。

| 项目 | 第二轮 | 第三轮 | 可得结论 |
|---|---:|---:|---|
| 用户 Boot→Apple | 97 s | 73 s | 少 24 s |
| 用户 Apple→Hello | 148 s | 139 s | 少 9 s |
| 用户 Boot→Hello | 245 s | 212 s | 少 33 s |
| 23 次大文件签名转换 total | 44.002 s | 20.209 s | 少 23.793 s |
| 其中 read | 35.538 s | 11.761 s | 少 23.777 s |
| 其中 hash | 8.435 s | 8.418 s | 基本不变 |
| SB 等 launchd 最大已观测年龄 | ≥28.281 s | ≥35.644 s | 采样下界，不能认定慢了 7.363 s |
| SB 等 lsd 最大已观测年龄 | ≥103.673 s | ≥101.036 s | 长等待仍在，不能认定精确快了 2.637 s |
| 同线程串行等待下界 | ≥131.954 s | ≥136.680 s | 不是完整服务耗时，也不是整机时间差 |

签名转换的 23 条记录按顺序的字节数和读取次数完全对齐，总共 **2,724,626,432 字节、10,403 次转换层读取**。新日志 L256–L465，全部在首次 shared-region mapping L466 之前。已记录的 24 个签名注册结果全部为 0，没有 `meizu: PANIC` 记录。

这项同工作量测量支持读取及其内部校验路径获得约 23.8 秒收益，与苹果出现前少 24 秒相互印证。`cs4k read` 包含内部根盘读取与校验，不能再和 rootio 内层耗时累加；`cs4k reads` 也不是 UFS 命令数。其探针只覆盖成功且至少 16 MiB 的转换，因此不能代表启动所有读取。整机少 33 秒中的其余部分暂不强行归因。

本轮 SpringBoard PID 25 / TID 252 的服务依赖仍然很长：

- seq 261 指向 launchd PID 1 / generation 4，send ID 6001009。L935、L1195、L1348、L1419 的年龄分别为 5.057、15.243、25.448、35.644 秒。第二轮只有 3 个样本，本轮有 4 个，采样相位不同。
- 随后 seq 2322 指向 lsd PID 97 / generation 272。L1532–L1955 共 10 个递增样本，最后为 101.036 秒。这是 receive-only 样本，send ID 为 0，不能据此重建原始请求的完整发送过程。
- lsd 主线程 TID 435 在 L963–L970 与 `_LSPreferences`、`_LSDatabaseClean`、`LSDatabaseBuilder createAndSeedLocalDatabase:` 相匹配；TID 439 在 L982–L983 等待数据库 seeding；TID 437 在 L1391、L1627 落入应用目录扫描，L1090–L1095 落入容器查询。
- L1544、L1654 中 TID 437 两次指向 containermanagerd PID 63 / generation 230，但请求序号分别为 4592、9774；L1763 已是另一序号 20407，目标未知。L1462、L2110 又是可运行的 state-only 样本。因此不能说同一个容器 RPC 一直卡了 101 秒。
- L2014 出现 LaunchServices `.csstore` mmap，L2018 的 SpringBoard receiver 已推进到 backboardd。仅凭队列 qcount=0、未知的 continuity 和存在覆盖的内部 IPC 环形记录，不能证明某条消息出队，也不能统计全部请求。

上述符号匹配使用本轮 slide `0x48f0000`，与 L658 的 `nf` 一致；按 unslid PC 精确匹配历史符号记录。未重新认证 dyld cache UUID，只作为上下文证据，未使用不可靠的远距离最近符号。

内部时间窗以下列本轮首条 rootio（L200，ticks=470840840，hz=19200000）为零点。它不是 Boot；事件由相邻采样行夹取，可能受并发打印和采样至打印延迟影响。

| 事件 | 第二轮相对首条 rootio | 第三轮相对首条 rootio |
|---|---:|---:|
| 最后 cs4k / 首次 shared mapping | 41.507–49.514 s | 20.587–28.526 s（L465/L466） |
| 首次已记录 framebuffer swap | 79.960–90.100 s | 59.067–69.219 s（L1439） |
| 首次 `.csstore` mmap | 191.637–201.788 s | 170.696–180.847 s（L2014） |
| 首次 Setup arm | 201.788–211.975 s | 180.847–191.100 s（L2156） |

这些阶段整体约提前 21 秒，支持前段读取收益向后传递；不表示每个服务本身都快了 21 秒。212 秒总耗时与该内部时轴量级相容，但缺同源 Boot/Hello 标记，不能独立精确复算。不得把首个 swap 自动叫作 Apple 或 Hello。

本轮留存 **1 次 Setup arm，没有 Setup scene-create watchdog**。首次 Setup 的 2.045 秒样本是 state-only；8.036 秒是 hint=6 的 semaphore 等待；15.000 秒已恢复为 state-only（L2845、L3885、L4536），不同于第二轮 8 秒和 15 秒停在相同等待状态。它是本轮观察到的改善，尚不能宣称该稳定性问题永久修复。

L3929 另有 **chronod 的 10 秒 scene-create watchdog**，夹在首条 rootio 后 191.100–201.225 秒。它不是 Setup 的 watchdog，也没有 Hello 首帧时间可用于严格判断其发生在首次 Hello 前还是后。不能把它的 10 秒额度直接加到首次 Hello 时间。第二轮的 3 次 Setup watchdog 也不能整体计入第一帧预算。

**S03 最窄的下一步：只测清 lsd seeding/应用扫描这约 100 秒。** 用同一单调时钟记录该阶段的成对开始/结束，以及 lsd→containermanagerd 的每次请求完成耗时，按 task generation、TID、sequence 关联，输出有上限的分段耗时和请求次数。优先复用现有 IPC 探针并避免通用环形记录覆盖掉这一条链；保留现有 rootio 计数。验收是一次成功启动能区分数据库/扫描执行、多个容器 RPC、线程等待各占多少，再针对证实的最大项改动。现有证据不支持跳过 seeding、删除服务或调整 watchdog。

新日志 SHA-256：`57cbb9046f33237974d31bdef848ed537f446520d99ee869dc8c2c2277bc9633`。第二轮：`283735861cf23fa8581a73728ee76961cd593a2658d2762bc4c9efe47af149aa`。主代理报告 autolog dropped=0，本分析确认 NUL=0；header 的 ring_total=977206 与正文 977379 相差 173 字节，由 rootio 审核处理，本文不声称日志绝对完整。内部 SB IPC 记录的 overwritten 是另一层数据损失，不能与 autolog dropped=0 混淆。

逐条转换、服务样本、时间夹取窗口、符号匹配与限制保存在 [service-comparison-nova.json](<nova-root>/artifacts-nova/boot-speed-round3-nova/device-analysis-nova/service-comparison-nova.json)。使用现有只读 `analyze-boot-nova.py` 解析服务等待，额外以完整字段名解析 cs4k/rootio，未使用 heartbeat tick 或 E1 代替秒数。两轮各一次测量不是稳定均值。
