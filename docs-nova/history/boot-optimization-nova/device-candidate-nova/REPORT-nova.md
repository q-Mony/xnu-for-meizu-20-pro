# Nova 候选首次运行：SpringBoard 与 Setup

**后段主要等待仍然存在：同一 SpringBoard 主线程串行等待 launchd、lsd 至少135.623秒，接近刷前137.636秒。** 用户报告 Boot→Apple约90秒、Hello在237秒出现；这些是外部观察。本报告不重复统计主代理负责的 `nova: cs4k` 阶段。

| 同线程等待 | 候选 PID26/TID254 | 刷前 PID25/TID253 |
|---|---|---|
| launchd | seq254，≥35.282 s，L1468 | ≥36.070 s |
| lsd | seq2248，≥100.341 s，L2003 | ≥101.566 s |
| 按序相加下界 | **135.623 s** | **137.636 s** |

只纳入完整 `phase=4 pending=1` 样本。launchd终点 PID1/gen4，lsd终点 PID97/gen271，与exec/receiver映射匹配。两段分别有4、10个年龄递增样本，顺序无重叠。差值−2.013秒是采样下界差，不是受控实验的精确提速；约100秒的lsd等待仍是下一阶段的重要延迟。

Setup L2285启动（PID249/TID2206，attempt1）。在其 age2.000、8.018、15.019秒，主线程重复采样为相同PC/LR/SP、相同event及 `hint=6`；本地 `block_hint.h:39` 定义该hint为semaphore等待。L3985–3988的专用探针记录 SELECT/ASSERT/BLOCK_CALL/OFFCORE，L4706在窗口截止时CLOSE；留存窗口未记录该对象的RETURN或SIGNAL事件。**这支持Setup主线程等待未解除，但尚未确定应该发信号的回调或线程。** `event=9` 是OFFCORE调度观察，不是budd发出semaphore signal。

L4047/L4113/L4172对应的 `sbipc setup-main seq47242` 均为 `phase=6 pending=0`、`status=completed`。最后的age13599 ms只是已完成IPC记录的年龄，不能称为“IPC又等了13.599秒”。

L4717确有Setup `scene-create` 20秒watchdog、`8BADF00D`终止。 032506及其他历史启动也有此类Setup watchdog；前后留存区间不等长，不能归为本次回归，也不能把刷前未记录它视为已修复。报告给出的全系统CPU20.310秒与应用CPU1.140秒（6%）不同，不能从100%总CPU推断Setup自身忙循环。L4090另有chronod 10秒watchdog。identityservices反复退出和poster扩展check-in timeout在刷前也有，不能只凭本次出现认定Nova引入了回归。

日志含16条完整可解析的framebuffer swap记录（序号1–18，7/8未保留为完整记录），其中9条位于Setup watchdog记录之后；说明后续显示路径仍有活动，不证明这些内容就是Hello。**用户观察到Hello成立，同时存在Setup场景创建稳定性问题。** 无同源呈现时间戳可排序Hello与watchdog，不把20秒直接加进237秒，也不因日志没有Hello文字而否认用户观察。

内核banner沿用03:08:52构建时间，但L256出现Nova专用标记；新镜像身份以主代理刷写及SHA读回记录为依据，不能据banner认作旧日志。正文SHA-256 `243b87f3669855c8d73d38b127b14fea6d9063edd9c82c039216ed43d33de4f3`。原始窗口、头、正文及capture哈希已核对；`bytes-total=173`恰好是boot-video合成前缀。无NUL，末尾完整换行，254条heartbeat从0单调到5060。持久化多块写仍非原子；console `dropped=0`不表示IPC历史完整，L4749明确记录 `sbipc overwritten=297517`。

E1约220000→300000仅记录为用户观察；此处没有对应采样间隔和同步轨迹，不据此判断崩溃或中断风暴。没有执行任何设备命令、修改源码或重跑构建；本目录为新建，备份声明在 `snapshot-nova/boot-optimization-nova/device-candidate-before-nova/`，中央CHANGELOG由主代理追加。

[候选原始文本](<nova-root>/logs-nova/device-boot-optimization-nova/candidate-run1-nova/autolog-nova.txt) · [结构化证据](<nova-root>/artifacts-nova/boot-optimization-nova/device-candidate-nova/comparison-nova.json) · [刷前对照](<nova-root>/artifacts-nova/boot-optimization-nova/device-baseline-nova/comparison-nova.json)

补充异常核对：留存记录未发现真正的kernel panic、明确签名bad-hash/mismatch或非零 `addfilesigs result`（记录到的24次结果均为0）。`gPanicBase`初始化提示、DumpPanic进程启动及APFS manifest-import警告在刷前也存在，均不能当作本次panic/签名失配。底层bad-hash计数未被dump，且部分错误打印受`cs_debug`控制，因此这里仅报告“未见记录”，不宣称内部错误计数必为零。
