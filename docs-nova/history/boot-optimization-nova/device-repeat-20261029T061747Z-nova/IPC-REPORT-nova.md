# Nova 第二轮 IPC 与 Setup 对照

**用户已确认两轮都从 Boot 菜单开始计时：237秒→190秒，观察到的减少47秒成立；日志中的两段主要服务等待没有缩短。** 同一SpringBoard主线程的launchd、lsd串行等待下界为136.277秒，上一轮为135.623秒。

| 等待对象 | 第二轮 PID26/TID253 | 第一轮 PID26/TID254 |
|---|---|---|
| launchd | seq260，≥35.481 s，L1466 | ≥35.282 s |
| lsd | seq1980，≥100.796 s，L1989 | ≥100.341 s |
| 按序合计下界 | **136.277 s** | **135.623 s** |

只计算完整 `phase=4 pending=1` 样本。launchd终点PID1/gen4，lsd终点PID97/gen272；exec/receiver映射一致，4个与10个年龄递增样本发生在同一主线程且先后不重叠。差值 **+0.654秒** 是采样下界差，不能解释用户报告总时间减少47秒，也不是精确阶段耗时回归。

两轮刷写/读回记录中的镜像SHA相同：`2336c87cc0c71f810d3967c8601124075e381f8efebd67834b3526124a3e1dab`。用户进一步明确：“**上轮也是从 Boot 菜单开始**”。因此两轮用户观察的起点均为Boot菜单，237→190秒、减少47秒的报告差值成立。本轮明确时间为2026-10-29 **14:21:50→14:25:00（Asia/Shanghai）=190秒**，Apple约80秒。

主机重启命令为14:21:04.731463：到本轮用户Boot起点45.268537秒，到用户Hello观察235.268537秒（约3分55秒）。这是**主机重启起点的另一口径**，不替换或推翻用户的190秒；本轮45.268537秒间隔不能用来断言两轮计时混用。用户钟表与主机时钟未独立验证同步，上述两数只是记录间算术差。签名转换阶段及主要IPC等待基本未变，现有分段证据仍不能解释47秒具体减少在哪个阶段，因果归属保持未知。

Setup第一轮在L2193启动（PID238/TID2050）。age2.000秒仍有42ms的pending IPC；age8.046/15.014秒主线程显示semaphore等待（hint6），最后IPC已为`phase6/pending0`。L4743出现已知的20秒scene-create watchdog/8BADF00D，应用CPU1.481秒（7%），不能将100%全系统CPU认作Setup忙循环。L4818启动第二次Setup（PID265/TID2413）；age8.037/15.035秒再次采到semaphore等待，但本段未保留其后续结果。已完成IPC记录的age11425/9927ms不能算作仍在等回复。

两轮均保留 **一次Setup20秒watchdog、一次chronod10秒watchdog**；本轮chronod位于L4097。本轮多保留了第二次Setup，日志到tick5320、6641行；上一轮到tick5060、4776行。区间不等长，不依据次数或总字节判定回归、修复或吞吐变化。identityservices重复退出、poster扩展check-in timeout在上一轮已有。Hello由用户确认，日志中的swap不能提供Hello首帧内容/同源时间，无法确定Hello与watchdog的先后因果。

**留存记录未见真正kernel panic、明确签名bad-hash/mismatch或非零addfilesigs结果；记录到的24次结果全为0。** `gPanicBase`初始化提示、DumpPanic进程启动、APFS manifest-import警告与旧轮相同，不能当作本次panic或签名失配。内部bad-hash计数未dump、部分打印受`cs_debug`限制，因此结论限于未见错误记录。

原始32MiB窗口、正文与capture哈希已验证；正文SHA `da158d93768f3199bc2be69e7282544081a94d608545418fbbcc3f09b95bfa85`。无NUL、完整换行；`bytes−total=173`由boot-video合成前缀解释；267条heartbeat从0单调到5320。持久化仍非原子，console `dropped=0`不表示IPC历史完整，L6600记录 `overwritten=291991`。不得将tick换成精确秒表、采集时间当启动时间，或把watchdog20秒再叠加到190秒。

现有解析器 [analyze-boot-nova.py](<nova-root>/artifacts-nova/boot-latency-nova/analyze-boot-nova.py) 已先审阅，函数只读输入、返回数据，CLI仅打印stdout，不覆盖报告；本轮用`python3 -B`导入。本报告和 [ipc-comparison-nova.json](<nova-root>/artifacts-nova/boot-optimization-nova/device-repeat-20261029T061747Z-nova/ipc-comparison-nova.json) 最初为新建；本次按用户确认修订前，两份完整旧版已分别备份到 `snapshot-nova/boot-optimization-nova/repeat-20261029T061747Z-nova/` 下的 `*.before-boot-origin-confirmation-nova.*`。仅修改这两份报告及其备份，未写CHANGELOG，未操作设备。签名阶段汇总由主代理负责，本报告未重复计算。

输入：[第二轮日志](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt)；对照：[第一轮报告数据](<nova-root>/artifacts-nova/boot-optimization-nova/device-candidate-nova/comparison-nova.json)；计时依据：[用户确认](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/user-observation-confirmed-nova.json)、[主机启动记录](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-start-nova.json)。

计时起点补充证据：[用户明确确认](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/timing-origin-confirmation-nova.json)。
