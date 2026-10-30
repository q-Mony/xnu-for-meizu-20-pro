# 启动日志自动时间表

本报告只读取已有日志，不操作设备。

日志：`<nova-root>/logs-nova/boot-speed-round5-nova/candidate-run-nova/autolog-nova.txt`
SHA256：`cb61b6a8838e6648c508225e4ef6a52cdec3eccad7bc7230e40e9f4d1b682659`

固定 13 项中有 4/13 可自动输出秒数，但包含阶段age观测值、采样下界和采样窗口；真正具有起止事件时间戳的阶段耗时为 0 项。不能据此计算 Boot→Hello 的时间覆盖比例。

| 阶段／指标 | 自动结果 | 起点证据 | 终点证据 | 类型与边界 |
| --- | --- | --- | --- | --- |
| Boot 点击 → 苹果首次可见 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；日志没有 Boot 菜单点击和视觉苹果首次显示事件 |
| Boot 点击 → Hello 首次可见 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；日志没有 Boot 菜单点击和视觉 Hello 首次显示事件 |
| 系统根盘挂载 | 未知 | L139 first_observed_untimed_marker | L156 first_observed_untimed_marker | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| Union/SystemOS 挂载 | 未知 | L180 first_observed_untimed_marker | L193 first_observed_untimed_marker | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| Data 挂载 | 未知 | L212 first_observed_untimed_marker | L216 first_observed_untimed_marker | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| SpringBoard exec → 首次 Setup 捕获 | 未知 | L692 successful_exec_capture_without_timestamp | L2680 exec_capture_anchor_without_timestamp | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| launchd exec → early boot complete 记录 | 未知 | L228 successful_exec_capture_without_timestamp | L689 timestamped_log_event_not_task_completion | 未知；launchd exec 无时间；System Event 原始时钟未映射，不能跨域相减 |
| launchd 首个 boot-task 记录 → early boot complete 记录 | 15650525 ticks（秒数未知） | L512 timestamped_log_event_not_task_completion | L689 timestamped_log_event_not_task_completion | 同域原始 tick 差；只计算同一 System Event 域原始 tick 差；无该域频率，不给秒数，也不把相邻任务开始间隔当任务耗时 |
| SpringBoard 等待 launchd | 36.278 秒 | 无时间锚 phase_start_not_timestamped | L1560 earlier_pending_snapshot_with_print_time_phase_age | 阶段age观测值（非严格下界）；最大观测 phase age；pending 为较早的锁内快照，age 在解锁后的输出时刻计算。不是严格等待下界，延迟误差上界未知；缺值不表示没有等待 |
| SpringBoard 等待 lsd | 101.427 秒 | 无时间锚 phase_start_not_timestamped | L2429 earlier_pending_snapshot_with_print_time_phase_age | 阶段age观测值（非严格下界）；最大观测 phase age；pending 为较早的锁内快照，age 在解锁后的输出时刻计算。不是严格等待下界，延迟误差上界未知；缺值不表示没有等待 |
| 关联到主线程 owner 的 worker → lsd IPC 等待阶段 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；按同组round、SpringBoard pid/taskgen/main tid、ownerTID、worker tid和对端代际精确配对。仅worker phase_age在锁内同次采样；主线程owner另行观测，不表示主线程全程连续等待；缺值保持未知 |
| 首次 Setup 捕获 → 首次周期采样 | ≥ 2.002 秒 | L2680 exec_capture_anchor_without_timestamp | L3531 periodic_observation | 采样下界；仅代表 Setup 进程已存活到该次采样；不是 scene 完成，更不是 Hello 显示 |
| lsd 首次 → 最后一次 IPC 统计采样 | 260.643033073 秒 | L877 periodic_snapshot | L10343 periodic_snapshot | 采样窗口；这是日志采样覆盖窗口，不是 lsd 启动耗时或 Boot→Hello 时间 |

## 每项计量的原值与时钟

| 指标 | 时钟域 | 原值／单位 | 来源 |
| --- | --- | --- | --- |
| launchd 首个 boot-task 记录 → early boot complete 记录 | `system_event_raw:launchd` | 15650525 ticks | L512 → L689 |
| SpringBoard 等待 launchd | `mach_absolute_elapsed:sb_wait:25:252:261` | 36278 ms | L950, L1211, L1424, L1560 |
| SpringBoard 等待 lsd | `mach_absolute_elapsed:sb_wait:25:252:2226` | 101427 ms | L1708, L1788, L1871, L1962, L2030, L2122, L2201, L2278, L2346, L2429 |
| 首次 Setup 捕获 → 首次周期采样 | `setup_exec_relative:244:919:1` | 2002 ms | L2680 → L3531 |
| lsd 首次 → 最后一次 IPC 统计采样 | `mach_absolute` | 260643033073 ns | L877 → L10343 |

## 日志可见事件

| 事件 | 行号 | 时间原值／单位／时钟 | 说明 |
| --- | --- | --- | --- |
| 系统根盘 mountroot 调用 | L139 | 无时间戳 | first_observed_untimed_marker |
| OS 根卷 ready | L156 | 无时间戳 | first_observed_untimed_marker |
| 自动日志线程启动记录 | L173 | 无时间戳 | first_observed_untimed_marker |
| Union/SystemOS 挂载进入 | L180 | 无时间戳 | first_observed_untimed_marker |
| Union/SystemOS 挂载返回成功 | L193 | 无时间戳 | first_observed_untimed_marker |
| Data 挂载请求 | L212 | 无时间戳 | first_observed_untimed_marker |
| Data 挂载返回成功 | L216 | 无时间戳 | first_observed_untimed_marker |
| launchd successful-exec 记录 | L228 | 无时间戳 | successful_exec_capture_without_timestamp |
| 首次观察到 dyld cache 打开 | L245 | 无时间戳 | first_observed_untimed_marker |
| 共享缓存映射进入 | L462 | 无时间戳 | first_observed_untimed_marker |
| launchd: fsck | L512 | 843255611 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: mount-phase-1 | L513 | 843260835 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: data-protection | L514 | 843265690 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: finish-obliteration | L515 | 843270495 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: detect-installed-roots | L516 | 843545066 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: commit-boot-mode | L517 | 843559138 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: restore-datapartition | L518 | 843568550 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: restore-datapartition | L519 | 843573410 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: mount-phase-2 | L520 | 843578262 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: init-with-data-volume | L521 | 843583055 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: MSUEarlyBootTask | L522 | 843588055 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: fips | L523 | 843592871 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: keybag | L524 | 843597653 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: usermanagerd | L525 | 843602451 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: dirs_cleaner | L526 | 843607482 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: xpcroleaccountd | L527 | 843617427 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: init_featureflags | L528 | 844048461 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: auearlyboot | L530 | 846966098 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: tzinit | L533 | 847470955 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: finish-restore | L681 | 858542269 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: finish-demo-restore | L682 | 858546725 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: sysstatuscheck | L683 | 858549432 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: prng_seedctl | L687 | 858899373 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: launchd_cache_loader | L688 | 858901806 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: boot | L689 | 858906136 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| SpringBoard successful-exec 记录 | L692 | 无时间戳 | successful_exec_capture_without_timestamp |
| lsd successful-exec 记录 | L876 | 无时间戳 | successful_exec_capture_without_timestamp |
| 首次观察到 framebuffer swap（画面内容未知） | L1579 | 无时间戳 | first_observed_untimed_marker |
| Setup 执行捕获／诊断开启 | L2680 | 无时间戳 | exec_capture_anchor_without_timestamp |
| Setup 执行捕获／诊断开启 | L5500 | 无时间戳 | exec_capture_anchor_without_timestamp |
| Setup 执行捕获／诊断开启 | L7959 | 无时间戳 | exec_capture_anchor_without_timestamp |

## worker 等待链逐次身份配对

| 身份 pid/gen/main tid/worker tid/seq/peer/peer gen | 轮次 | 主线程 owner 行 | worker 行 | phase_age 原值 | op_age 原值 |
| --- | --- | --- | --- | --- | --- |

仅匹配到有效 lsd 身份且未倒退的组可进入 worker 阶段指标；op_age 不作为等待阶段耗时。主线程 owner 与 worker 分别采样，不外推全程依赖。

## 操作计量（不能相加当作启动阶段总时间）

- 代码签名：23 条完整记录，total 19.356 秒；读取 10.929 秒，hash 8.402 秒。原值和全部来源行见 JSON。
  逐次日志的毫秒取整之和；不是连续阶段墙钟时间，不能与其他组件相加拼成Boot总耗时
- 根盘计量窗口：292.967724895833333333333333333 秒（CNTPCT）；read/verify/copy 的累计增量分别为 10.4711571354166666666666666667 / 4.2738565625 / 0.6447921875 秒。
  驱动累计操作时间；不同操作可能并发，不作为阶段墙钟时间或与Mach域相减

## 外部人工校准

用户确认：Boot→苹果 73.03 秒；Boot→Hello 220.56 秒；苹果→Hello 147.53 秒。
用户人工确认的独立记录；不用于推导日志起点、补时间戳或匹配视觉帧
来源：`<nova-root>/logs-nova/boot-speed-round5-nova/user-observation-nova.json`；SHA256 `9d9a1d4694f5200c3f640dcf56735475db8e3c7f20ef620d45c0e910280dc773`。

## 完整性与解释边界

- Boot 菜单点击、苹果首次显示、Hello 首次显示都没有可验证日志事件。Setup exec、scene 请求和 framebuffer swap 均不能替代 Hello。
- 未给无时间戳事件插值；相邻日志行、heartbeat tick、E1 次数和 watchdog 配额不当作秒数。
- sbipc latest 的 pending 快照早于输出时计算的 age；显示的是观测值，不能给严格等待下界。worker 的 phase_age 使用锁内保存的采样时刻，语义不同。
- 不同 clock 域或不同频率不得相减。时间倒退／频率变化的采样流不产生区间结果。
- 根盘 rootio 为 CNTPCT，blog-status／cpufreq 为 CNTVCT；两者也不直接相减。
- JSON 保留逐事件／逐样本原值、单位、来源行与精度。时间起点不等于 Boot 菜单零点。
- 秒数保存为十进制字符串，换算最多30位有效数字；这不是观测精度。age_ms和cs4k为向下取整毫秒。
- L1391：exec-map capture-limit=256 slots=128；exec-map 日志达到捕获上限；后续缺少记录不能证明没有 exec。
- 已识别计时记录未发现倒退或格式异常；这不证明采集覆盖完整启动过程。
