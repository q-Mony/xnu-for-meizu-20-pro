此报告读取派生正文；原始timestamp证据及字节映射见同目录source-map-nova.json。前缀时间未合入旧计量。

# 启动日志自动时间表

本报告只读取已有日志，不操作设备。

日志：`<nova-root>/artifacts-nova/boot-speed-round7-nova/timestamp-analysis-nova/round7-first-analysis-nova/autolog-unprefixed-nova.txt`
SHA256：`002c16c2e9d7695978be0e127464007cb88b35c602fdd7b913bda0fbf8a325ba`

固定 13 项中有 4/13 可自动输出秒数，但包含阶段age观测值、采样下界和采样窗口；真正具有起止事件时间戳的阶段耗时为 0 项。不能据此计算 Boot→Hello 的时间覆盖比例。

| 阶段／指标 | 自动结果 | 起点证据 | 终点证据 | 类型与边界 |
| --- | --- | --- | --- | --- |
| Boot 点击 → 苹果首次可见 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；日志没有 Boot 菜单点击和视觉苹果首次显示事件 |
| Boot 点击 → Hello 首次可见 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；日志没有 Boot 菜单点击和视觉 Hello 首次显示事件 |
| 系统根盘挂载 | 未知 | L142 first_observed_untimed_marker | L160 first_observed_untimed_marker | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| Union/SystemOS 挂载 | 未知 | L185 first_observed_untimed_marker | L199 first_observed_untimed_marker | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| Data 挂载 | 未知 | L222 first_observed_untimed_marker | L226 first_observed_untimed_marker | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| SpringBoard exec → 首次 Setup 捕获 | 未知 | L702 successful_exec_capture_without_timestamp | L2762 exec_capture_anchor_without_timestamp | 未知；事件存在时只表示该行记录；未带时间戳，不借相邻采样时间插值 |
| launchd exec → early boot complete 记录 | 未知 | L239 successful_exec_capture_without_timestamp | L713 timestamped_log_event_not_task_completion | 未知；launchd exec 无时间；System Event 原始时钟未映射，不能跨域相减 |
| launchd 首个 boot-task 记录 → early boot complete 记录 | 19763781 ticks（秒数未知） | L525 timestamped_log_event_not_task_completion | L713 timestamped_log_event_not_task_completion | 同域原始 tick 差；只计算同一 System Event 域原始 tick 差；无该域频率，不给秒数，也不把相邻任务开始间隔当任务耗时 |
| SpringBoard 等待 launchd | 35.09 秒 | 无时间锚 phase_start_not_timestamped | L1569 earlier_pending_snapshot_with_print_time_phase_age | 阶段age观测值（非严格下界）；最大观测 phase age；pending 为较早的锁内快照，age 在解锁后的输出时刻计算。不是严格等待下界，延迟误差上界未知；缺值不表示没有等待 |
| SpringBoard 等待 lsd | 90.06 秒 | 无时间锚 phase_start_not_timestamped | L2463 earlier_pending_snapshot_with_print_time_phase_age | 阶段age观测值（非严格下界）；最大观测 phase age；pending 为较早的锁内快照，age 在解锁后的输出时刻计算。不是严格等待下界，延迟误差上界未知；缺值不表示没有等待 |
| 关联到主线程 owner 的 worker → lsd IPC 等待阶段 | 未知 | 无时间锚 missing | 无时间锚 missing | 未知；按同组round、SpringBoard pid/taskgen/main tid、ownerTID、worker tid和对端代际精确配对。仅worker phase_age在锁内同次采样；主线程owner另行观测，不表示主线程全程连续等待；缺值保持未知 |
| 首次 Setup 捕获 → 首次周期采样 | ≥ 2.014 秒 | L2762 exec_capture_anchor_without_timestamp | L3419 periodic_observation | 采样下界；仅代表 Setup 进程已存活到该次采样；不是 scene 完成，更不是 Hello 显示 |
| lsd 首次 → 最后一次 IPC 统计采样 | 180.482403907 秒 | L895 periodic_snapshot | L6218 periodic_snapshot | 采样窗口；这是日志采样覆盖窗口，不是 lsd 启动耗时或 Boot→Hello 时间 |

## 每项计量的原值与时钟

| 指标 | 时钟域 | 原值／单位 | 来源 |
| --- | --- | --- | --- |
| launchd 首个 boot-task 记录 → early boot complete 记录 | `system_event_raw:launchd` | 19763781 ticks | L525 → L713 |
| SpringBoard 等待 launchd | `mach_absolute_elapsed:sb_wait:26:253:258` | 35090 ms | L965, L1251, L1485, L1569 |
| SpringBoard 等待 lsd | `mach_absolute_elapsed:sb_wait:26:253:2514` | 90060 ms | L1759, L1868, L1942, L2045, L2124, L2197, L2298, L2368, L2463 |
| 首次 Setup 捕获 → 首次周期采样 | `setup_exec_relative:231:885:1` | 2014 ms | L2762 → L3419 |
| lsd 首次 → 最后一次 IPC 统计采样 | `mach_absolute` | 180482403907 ns | L895 → L6218 |

## 日志可见事件

| 事件 | 行号 | 时间原值／单位／时钟 | 说明 |
| --- | --- | --- | --- |
| 系统根盘 mountroot 调用 | L142 | 无时间戳 | first_observed_untimed_marker |
| OS 根卷 ready | L160 | 无时间戳 | first_observed_untimed_marker |
| 自动日志线程启动记录 | L177 | 无时间戳 | first_observed_untimed_marker |
| Union/SystemOS 挂载进入 | L185 | 无时间戳 | first_observed_untimed_marker |
| CPU EPSS 一次性探针 | L191 | 502794934 ticks / `cntvct` | diagnostic_probe |
| Union/SystemOS 挂载返回成功 | L199 | 无时间戳 | first_observed_untimed_marker |
| Data 挂载请求 | L222 | 无时间戳 | first_observed_untimed_marker |
| Data 挂载返回成功 | L226 | 无时间戳 | first_observed_untimed_marker |
| launchd successful-exec 记录 | L239 | 无时间戳 | successful_exec_capture_without_timestamp |
| 首次观察到 dyld cache 打开 | L256 | 无时间戳 | first_observed_untimed_marker |
| 共享缓存映射进入 | L475 | 无时间戳 | first_observed_untimed_marker |
| launchd: fsck | L525 | 895710357 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: mount-phase-1 | L526 | 895715583 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: data-protection | L527 | 895720496 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: finish-obliteration | L528 | 895725395 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: detect-installed-roots | L529 | 895730343 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: commit-boot-mode | L530 | 895744279 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: restore-datapartition | L531 | 895753686 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: restore-datapartition | L532 | 895758559 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: mount-phase-2 | L533 | 895763414 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: init-with-data-volume | L534 | 895976077 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: MSUEarlyBootTask | L535 | 895981195 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: fips | L536 | 895986071 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: keybag | L537 | 895990894 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: usermanagerd | L538 | 895995729 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: dirs_cleaner | L539 | 896000788 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: xpcroleaccountd | L540 | 896461497 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: init_featureflags | L541 | 896474152 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: auearlyboot | L543 | 899235882 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: tzinit | L546 | 899744610 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: finish-restore | L696 | 910972680 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| SpringBoard successful-exec 记录 | L702 | 无时间戳 | successful_exec_capture_without_timestamp |
| launchd: finish-demo-restore | L708 | 914218613 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: sysstatuscheck | L709 | 914222440 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: prng_seedctl | L710 | 914224874 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: launchd_cache_loader | L711 | 914227299 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| launchd: boot | L713 | 915474138 ticks / `system_event_raw:launchd` | timestamped_log_event_not_task_completion |
| lsd successful-exec 记录 | L893 | 无时间戳 | successful_exec_capture_without_timestamp |
| 首次观察到 framebuffer swap（画面内容未知） | L1598 | 无时间戳 | first_observed_untimed_marker |
| Setup 执行捕获／诊断开启 | L2762 | 无时间戳 | exec_capture_anchor_without_timestamp |

## worker 等待链逐次身份配对

| 身份 pid/gen/main tid/worker tid/seq/peer/peer gen | 轮次 | 主线程 owner 行 | worker 行 | phase_age 原值 | op_age 原值 |
| --- | --- | --- | --- | --- | --- |

仅匹配到有效 lsd 身份且未倒退的组可进入 worker 阶段指标；op_age 不作为等待阶段耗时。主线程 owner 与 worker 分别采样，不外推全程依赖。

## 操作计量（不能相加当作启动阶段总时间）

- 代码签名：23 条完整记录，total 19.344 秒；读取 10.911 秒，hash 8.405 秒。原值和全部来源行见 JSON。
  逐次日志的毫秒取整之和；不是连续阶段墙钟时间，不能与其他组件相加拼成Boot总耗时
- 根盘计量窗口：201.556781458333333333333333333 秒（CNTPCT）；read/verify/copy 的累计增量分别为 7.39825651041666666666666666667 / 4.25065270833333333333333333333 / 0.63489296875 秒。
  驱动累计操作时间；不同操作可能并发，不作为阶段墙钟时间或与Mach域相减

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
- L1470：exec-map capture-limit=256 slots=128；exec-map 日志达到捕获上限；后续缺少记录不能证明没有 exec。
- 已识别计时记录未发现倒退或格式异常；这不证明采集覆盖完整启动过程。
