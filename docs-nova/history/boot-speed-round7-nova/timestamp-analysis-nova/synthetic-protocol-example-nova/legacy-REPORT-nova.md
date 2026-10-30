此报告读取派生正文；原始timestamp证据及字节映射见同目录source-map-nova.json。前缀时间未合入旧计量。

# 启动日志自动时间表

本报告只读取已有日志，不操作设备。

日志：`<nova-root>/artifacts-nova/boot-speed-round7-nova/timestamp-analysis-nova/synthetic-protocol-example-nova/autolog-unprefixed-nova.txt`
SHA256：`d617f0179ddcdc43e3aa37dda329d1fd932da41e6e44362bc70b259f7564e2a7`

固定 13 项中有 0/13 可自动输出秒数，但包含阶段age观测值、采样下界和采样窗口；真正具有起止事件时间戳的阶段耗时为 0 项。不能据此计算 Boot→Hello 的时间覆盖比例。

| 阶段／指标 | 自动结果 | 起点证据 | 终点证据 | 类型与边界 |
| --- | --- | --- | --- | --- |
| Boot 点击 → 苹果首次可见 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；日志没有 Boot 菜单点击和视觉苹果首次显示事件 |
| Boot 点击 → Hello 首次可见 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；日志没有 Boot 菜单点击和视觉 Hello 首次显示事件 |
| 系统根盘挂载 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| Union/SystemOS 挂载 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| Data 挂载 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| SpringBoard exec → 首次 Setup 捕获 | 未知 | 无时间锚 missing | L8 exec_capture_anchor_without_timestamp | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| launchd exec → early boot complete 记录 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；launchd exec 无时间；System Event 原始时钟未映射，不能跨域相减 |
| launchd 首个 boot-task 记录 → early boot complete 记录 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；只计算同一 System Event 域原始 tick 差；无该域频率，不给秒数，也不把相邻任务开始间隔当任务耗时 |
| SpringBoard 等待 launchd | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；最大观测 phase age；pending 为较早的锁内快照，age 在解锁后的输出时刻计算。不是严格等待下界，延迟误差上界未知；缺值不表示没有等待 |
| SpringBoard 等待 lsd | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；最大观测 phase age；pending 为较早的锁内快照，age 在解锁后的输出时刻计算。不是严格等待下界，延迟误差上界未知；缺值不表示没有等待 |
| 关联到主线程 owner 的 worker → lsd IPC 等待阶段 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；按同组round、SpringBoard pid/taskgen/main tid、ownerTID、worker tid和对端代际精确配对。仅worker phase_age在锁内同次采样；主线程owner另行观测，不表示主线程全程连续等待；缺值保持未知 |
| 首次 Setup 捕获 → 首次周期采样 | 未知 | L8 exec_capture_anchor_without_timestamp | 无时间锚 missing | 未知；仅代表 Setup 进程已存活到该次采样；不是 scene 完成，更不是 Hello 显示 |
| lsd 首次 → 最后一次 IPC 统计采样 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；这是日志采样覆盖窗口，不是 lsd 启动耗时或 Boot→Hello 时间 |

## 每项计量的原值与时钟

| 指标 | 时钟域 | 原值／单位 | 来源 |
| --- | --- | --- | --- |

## 日志可见事件

| 事件 | 行号 | 时间原值／单位／时钟 | 说明 |
| --- | --- | --- | --- |
| Setup 执行捕获／诊断开启 | L8 | 无时间戳 | exec_capture_anchor_without_timestamp |

## worker 等待链逐次身份配对

| 身份 pid/gen/main tid/worker tid/seq/peer/peer gen | 轮次 | 主线程 owner 行 | worker 行 | phase_age 原值 | op_age 原值 |
| --- | --- | --- | --- | --- | --- |

仅匹配到有效 lsd 身份且未倒退的组可进入 worker 阶段指标；op_age 不作为等待阶段耗时。主线程 owner 与 worker 分别采样，不外推全程依赖。

## 操作计量（不能相加当作启动阶段总时间）


## 外部人工校准

未提供外部校准。

## 完整性与解释边界

- Boot 菜单点击、苹果首次显示、Hello 首次显示都没有可验证日志事件。Setup exec、scene 请求和 framebuffer swap 均不能替代 Hello。
- 未给无时间戳事件插值；相邻日志行、heartbeat tick、E1 次数和 watchdog 配额不当作秒数。
- sbipc latest 的 pending 快照早于输出时计算的 age；显示的是观测值，不能给严格等待下界。worker 的 phase_age 使用锁内保存的采样时刻，语义不同。
- 不同 clock 域或不同频率不得相减。时间倒退／频率变化的采样流不产生区间结果。
- 根盘 rootio 为 CNTPCT，blog-status／cpufreq 为 CNTVCT；两者也不直接相减。
- JSON 保留逐事件／逐样本原值、单位、来源行与精度。时间起点不等于 Boot 菜单零点。
- 秒数保存为十进制字符串，换算最多30位有效数字；这不是观测精度。age_ms和cs4k为向下取整毫秒。
- 已识别计时记录未发现倒退或格式异常；这不证明采集覆盖完整启动过程。
