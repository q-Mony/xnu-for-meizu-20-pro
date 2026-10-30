# 启动时间表与第六轮关键计量独立复核 — Nova

2026-10-30。新文件，无旧版本。只读复核已有日志、解析器、测试记录及本地计时源码；唯一既有文件修改是修正 anchor-feasibility-nova.md 的 rootio 时钟标注，修改前已备份。未改生产源码、未构建、未操作设备。中央 CHANGELOG 由父代理统一更新。

结论：最终 v2 时间表的关键数字、时钟域隔离及当前第五/六轮身份配对通过独立复核。审阅中发现的 rootio 时钟误标、坏 cs4k 记录未使合计失效、sbipc latest 年龄被误称严格等待下界均已处理。Boot 菜单、苹果首次可见、Hello 首次可见仍无可靠日志时间锚；这项审阅不证明端到端启动提速。

## 正式输入绑定

以下路径均相对 Nova 根；此前 round5-evidence、round5-final 和 round6-evidence 保留为历史草稿，正式解释使用本表 v2/final 结果。

| 输入/结果 | SHA256 |
|---|---|
| logs-nova/boot-speed-round5-nova/candidate-run-nova/autolog-nova.txt | cb61b6a8838e6648c508225e4ef6a52cdec3eccad7bc7230e40e9f4d1b682659 |
| logs-nova/boot-speed-round6-nova/candidate-run-nova/autolog-nova.txt | 367d82822557acd8967eb53f522a8c3d7db70dc268fc6fbb02f8a3337d882ad8 |
| artifacts-nova/boot-timeline-nova/boot-timeline-nova.py | bb0f3137325c86a995e790d09c16e386bc340ff80fe4c0db7ceb5ab712425297 |
| artifacts-nova/boot-timeline-nova/test_boot_timeline_nova.py | ac82d3a59fea68f7c1b622d9578b2e1ebef067861a850bc2f84b057766410d18 |
| artifacts-nova/boot-timeline-nova/round5-final-v2-nova/timeline-nova.json | 853a03ccefe911e354881b60da7c13a1f6598ccacc181c985639df7841c33c44 |
| artifacts-nova/boot-timeline-nova/round5-final-v2-nova/REPORT-nova.md | ba52abaae20ffe5370b69c2d18097539c60f0f7206ab2f92931841281c55cd6c |
| artifacts-nova/boot-timeline-nova/round6-final-nova/timeline-nova.json | a0c4008c9c03696b118d174d404270452febaaff958062823f4d7d96459a4d83 |
| artifacts-nova/boot-timeline-nova/round6-final-nova/REPORT-nova.md | 94d171ca31b09bbd8a64204fd2586aaa0d38bad9ac0a94153d1177d42f13a5b6 |
| artifacts-nova/boot-timeline-nova/tests-v2-result-nova.json | 5214cf6eb35ae49ef6a5c88efda9669b6df16757cb62ba6f186e405a721219f3 |
| artifacts-nova/boot-speed-round6-nova/device-analysis-nova/analysis-final-nova.json | 4d68888098b7e4f15619ed68309178428700b54d5b49312b817a38891b2f6289 |

独立复算使用 Python 标准库的正则及 Decimal，直接从 raw 行取值，未调用被审解析器的计算函数。25 项原始值与报表数字断言通过；最终 v2 的年龄类型、13 项分母及未知完整阶段另作语义断言。已阅读 worker 保存的 31 项通过记录与新增测试源码，未重复运行其测试。父代理 CPU 解析的 22 对快照、21 个间隔和五项拒绝负例记录也已阅读；以下关键数字另从 raw 独立复算。

## 从原始行复算

以下 L 行号分别对应各轮 autolog，秒数仅保留便于阅读的位数；JSON 保留原值。多位小数表示算术结果，不代表视觉或采样精度。

| 指标 | 第五轮原值与结果 | 第六轮原值与结果 | 可接受的解释 |
|---|---|---|---|
| launchd 首个 boot-task → early boot complete | L512→689：858906136−843255611 = 15650525 ticks | L513→711：915663076−895428286 = 20234790 ticks | 同一 System Event 域的原始差；无已验证频率，秒数未知；非 fsck 单任务耗时 |
| lsd IPC 首末采样 | L877→10343：311741730312−51098697239 = 260643033073 ns = 260.643033073 s | L869→6192：261812478281−51153305000 = 210659173281 ns = 210.659173281 s | 同一 generation=270 的 mach_absolute 采样覆盖窗口；非 lsd 启动或 Boot→Hello |
| rootio 采样窗 | L201→10395：(6075880890−450900572)/19200000 = 292.967724896 s | L202→6166：(4962044680−502614350)/19200000 = 232.261996354 s | CNTPCT 独立采样窗，不能与 Mach/CNTVCT 原值混减 |
| rootio read 增量 | 201046217 ticks /19200000 = 10.471157135 s | 216094687 ticks /19200000 = 11.254931615 s | 驱动全局累计操作时间；不是纯介质时间 |
| rootio verify 增量 | 82058046 ticks /19200000 = 4.2738565625 s | 81595724 ticks /19200000 = 4.249777292 s | 不能与并发 CPU 或阶段墙钟简单相加 |
| rootio copy 增量 | 12380010 ticks /19200000 = 0.6447921875 s | 12077808 ticks /19200000 = 0.6290525 s | 同上 |
| 23 条 cs4k 合计 | total/read/hash = 19356/10929/8402 ms | total/read/hash = 19371/10935/8408 ms | 逐操作向下取整毫秒的和；非连续阶段总时长 |
| SB 等 launchd 的 phase age | L950/1211/1424/1560 最大 36278 ms | L1048/1290/1457 最大 27660 ms | pending 快照对应的输出时年龄；不是严格等待下界 |
| SB 主线程直接等 lsd 的 phase age | L1708…2429，同 seq=2226，最大101427 ms | 未捕获可匹配记录 | 第五轮是年龄观测值；第六轮未知不能解释成没有依赖 |
| 首次 Setup 捕获到周期观察 | L2680→3531，pid244/gen919/attempt1，age=2002 ms | L2598→4071，pid233/gen887/attempt1，age=2028 ms | 进程活跃观察下界；不表示 scene 完成或 Hello |

第五轮 cs4k 来自行 254,269,284,299,312,327,342,355,370,385,397,410,418,425,427,431,434,439,444,447,451,454,461；第六轮对应行全部加 1。独立遍历 23 行复算，未借用工具的累计值。

第五轮人工记录仍独立为 Boot→Apple 73.03 s、Boot→Hello 220.56 s、Apple→Hello 147.53 s，来源 SHA256 为 `9d9a1d4694f5200c3f640dcf56735475db8e3c7f20ef620d45c0e910280dc773`。这些秒表值未用于反推日志原点、匹配某个 swap 或补无时间戳事件。第六轮没有人工总计时。

## 年龄语义与 worker 关联修正

[meizu_ipc_record_locked](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:590) 在 phase 切换时保存 Mach when。普通 sbipc latest 在锁内复制 pending 记录后解锁，[meizu_ipc_print](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:1649) 再读取当前时钟计算 age。两次观察之间可能已完成或出现调度延迟，日志没有保存该延迟上界。因此最终结果采用 observed_pending_phase_age，删除原有“≥36.278/≥101.427/≥27.66”严格下界表述。数字保持不变。

worker 路径不同：[meizu_worker_ipc_snapshot](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:1694) 在同一诊断锁内保存 sample_now 并复制 pending rows；输出时用保存的 sample_now 计算 phase_age。hook 更新也使用该诊断锁。因而可以报告该诊断等待 phase 在采样时已持续至少相应的向下取整毫秒；不能把它理解为纯 CPU 睡眠时间。

第六轮独立核对三组 raw：

| round | sb-workers/ulocks/main owner/header/worker 行 | 身份与结果 |
|---|---|---|
| 1 | 1677/1678/1679/1681/1682 | SB pid25/gen52/main252，owner1166；worker1166/seq2383/phase4→peer96/gen270；phase_age=5898 ms |
| 2 | 1781/1782/1783/1785/1786 | 同身份；phase_age=16088 ms |
| 3 | 1937/1938/1939/1941/1942 | 同身份；phase_age=36498 ms |

lsd 的 pid96/gen270 来自本轮 exec-map L866；SB main pid25/gen52/tid252 来自 L686。最终工具按 round、pid/gen、主线程 tid、owner、worker tid、seq 及 peer/gen 关联，错代/错 owner/错轮/缺 main/回退拒绝测试已读。采用 phase_age，不把任意 op_age 当等待阶段；本轮二者数值恰好相等。主线程 owner 与 worker 分开采样，owner 还是 wait 时的历史观察值，因此只支持关联方向，不证明主线程全程持有同一条依赖或连续等待 36.498 s。

## 第六轮 CPU 与窗口抽查

同代 lsd dump5（L1635）→dump14（L2348）：

- 墙钟差：(3486033575−1752605770)/19200000 = 90.2826981770833 s。
- 用户态记账：(363990353−37915336)/19200000 = 16.9830738020833 s。
- 内核态记账：(280889952−20977968)/19200000 = 13.5370825 s。
- 合计 30.5201563020833 s，单核等效占比 33.8050998899%。这是 lsd 全 task 已入账 CPU；不是全系统 CPU 使用率，不等于某个工作线程 CPU，也未强刷运行尾段。

SB 主线程 L1608→2427 的 11 条记录重复观察到相同 event/PC/LR、hint=10；不证明采样之间从未唤醒。上述 CPU 窗口是这些观察记录内侧的可比较快照，不命名为完整连续等待时长。

按日志顺序包围这些记录的 rootio L1563→2569 独立 CNTPCT 窗口为 111.649080364583 s；read/verify/copy 分别为 1.7960240625 / 0.279908125 / 0.0273427083333 s。blog-status L1566→2566 独立 CNTVCT 窗口为 110.324491302083 s，绘制累计 0.5449397395833 s。独立复算均与 analysis-final 一致。它们不是精确同边界的窗口，也不能与 CPU 或彼此相加生成启动预算。

这支持继续排查服务依赖和 CPU 分配，尚不能将 90.28−30.52 s 全部归为 lsd 阻塞、调度排队、全系统空闲或 UFS 硬件问题。

## 时钟、锚点与捕获边界

rootio 的 [timerTicks](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1343) 读取 CNTPCT；blog-status/cpufreq 读取 CNTVCT；Mach timebase 还可能包含软件偏移。频率相同不等于原点相同。System Event 无已验证频率；E1/hb 为计数而非秒。最终工具没有跨域混减，也没有借相邻有时间采样给无时间事件插值。

anchor-feasibility-nova.md 的旧 rootio CNTVCT 误标已修为 CNTPCT，并补充独立配对校准要求。备份位于 `snapshot-nova/timeline-independent-review-20261029T183908561466Z-nova/anchor-feasibility-nova.md`，旧 SHA256 `41a5fe7ac70796940a1a9c65cb841458ae6d99c98be13dba487dc01b0d4fdf48`；修正后 SHA256 `dd5f660f5a26f584f303699cfb972dd155088ca3d300c27c3d71d40a8a5b1f0e`。

此前 cs4k family() 使用了错误的 meizu 前缀，坏 nova: cs4k 行未抑制部分合计；worker 已修正前缀并增加拒绝用例。两份正常 raw 数字不受此修复影响。

最终固定 13 个指标中，两轮都只有 4/13 可输出秒数，包含年龄观测值、采样下界或窗口；真正有阶段起止时间戳的秒数为 0 项。4/13 不是启动时间覆盖率。Setup exec 与首 swap 都不是 Hello：前者可能仍在创建 scene，后者未验证画面内容及有效呈现；第五轮多个 Setup exec 后续仍有 scene watchdog，进一步排除这种替代。

两份 raw 均以换行结束；识别到第五轮 L1391、第六轮 L1450 的 exec-map capture-limit=256 slots=128。语法/计数无倒退不证明完整捕获；后续缺 exec、缺 watchdog 或缺直接 pending 不得据此认定事件未发生，持久日志也可能缺复位前尚未刷盘的尾段。

## 下一项最小诊断建议

保留现有 lsd CPU 采样，在同样的低频和总时限内，对 lsd 线程做有界的状态采样。复用 retained anchor、task/线程 try-lock、有限线程引用遍历；在复制调度状态时同时保存 mach tick，解锁后打印。带 taskgen/tid、busy/truncated 标记，区分 runnable_or_running、纯等待、过渡/未知，并记录等待类别。TH_RUN 不能单独区分正在执行与排队可运行。

把这些状态与同轮 lsd CPU 增量并列，先区分“多数线程在等锁/IPC”和“有可运行线程但 CPU 记账增长少”。采样分布不等于累计等待时长；若仍需精确排队时长，再核实已有 scheduler 计量或单独加入转换计量。完整自动 Boot→Hello 还需固件菜单及可信画面标记，不能靠扩大读取块或补解释替代证据。
