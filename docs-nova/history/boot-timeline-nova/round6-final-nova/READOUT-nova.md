# 第六轮 v2 读数说明

本文件补充自动报告，不改原始日志。输入 SHA256 为 `367d82822557acd8967eb53f522a8c3d7db70dc268fc6fbb02f8a3337d882ad8`。

直接 `sbipc latest` 的 launchd 阶段 age 最大观测为 27.660 秒；pending 快照与 age 计算不同步，因此没有标为严格等待下界。直接 lsd 指标未知，不能解释为没有等待。

新增 worker 项独立保存三组匹配：主线程 ulock 行 L1679、L1783、L1939 与 worker 行 L1682、L1786、L1942，对应 round=1/2/3。固定身份为 SpringBoard pid=25、gen=52、main tid=252、owner/worker tid=1166、seq=2383、peer=96、peer gen=270。phase_age_ms 原值为 5898、16088、36498。

worker 的 phase_age 使用锁内保存的同次 pending 采样时刻，可给该 worker IPC 等待阶段 ≥ 36.498 秒的采样下界。主线程 owner 是另一时刻的观测，不能据此报告主线程整段连续等待的时长；op_age 也不替代 phase_age。每一对的 sb-workers、ulocks、header 来源行均保留在 JSON。

Setup 首次周期采样时存活 ≥ 2.028 秒，lsd 统计采样窗口为 210.659173281 秒，两者都不是 Hello 出现时刻。本轮无人工总计时，Boot→苹果和 Boot→Hello 均未知，未从日志总长度推算。

固定 13 项中 4 项有秒数，但真正具有起止事件时间戳的阶段耗时仍为 0 项，不能计算 Boot→Hello 的墙钟覆盖比例。L1450 的 exec-map 捕获上限通知作为完整性限制保留。v1 的 `round6-evidence-nova/` 仅是历史结果，不再作为当前结论。
