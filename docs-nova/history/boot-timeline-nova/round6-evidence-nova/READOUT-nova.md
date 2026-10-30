# 第六轮自动时间表的解释边界

本轮输入 SHA256：`367d82822557acd8967eb53f522a8c3d7db70dc268fc6fbb02f8a3337d882ad8`。这是只读补充，未改写冻结的工具、自动 JSON 或报告。

自动表中 `sb_wait_lsd` 为未知，表示当前解析器没有找到可用于这一指标的 SpringBoard 主线程 `sbipc latest` 样本，**不表示 lsd 等待消失**。本轮日志存在另一种等待链：SpringBoard 主线程等待 worker；该 worker 的 IPC 对端为 lsd。

| 记录 | 来源行 | 原始身份与字段 |
| --- | --- | --- |
| 主线程 ulock | L1679、L1783、L1939 | pid=25、taskgen=52、tid=252、ownerTID=1166、pending=1、owner=observed-at-wait |
| worker IPC 第一次可见采样 | L1682 | pid=25、taskgen=52、tid=1166、seq=2383、phase=4、peer=96、peer_gen=270、owner=1、phase_age_ms=5898、op_age_ms=5898 |
| 同一 worker IPC 后续采样 | L1786 | 同一身份/seq/对端，phase_age_ms=16088、op_age_ms=16088 |
| 同一 worker IPC 最后可见采样 | L1942 | 同一身份/seq/对端，phase_age_ms=36498、op_age_ms=36498 |

这些是独立于旧主线程指标的原始证据，尚未接入自动阶段计算。没有用相邻周期采样给它们插入绝对时间，也没有据此推算 Hello。owner 是等待当时观察到的关联，不能无条件延伸到未观测区间。

当前工具自动输出：launchd 等待下界 27.660 秒（L1048、L1290、L1457）；Setup 首次周期采样时存活下界 2.028 秒（L2598、L4071）；lsd IPC 统计采样窗口 210.659173281 秒（L869、L6192）。后两项都不是 Boot→Hello 的阶段耗时。

本轮没有人工总时间，`external_calibration=null`；Boot 菜单点击、苹果首次可见、Hello 首次可见及其耗时均保持未知。L1450 记录 exec-map 捕获上限为 256、槽数为 128，日志没有某条事件不能证明事件没有发生。12 项指标只有 3 项能输出秒数，真正的起止阶段秒数为 0 项，不能报告墙钟时间覆盖率。
