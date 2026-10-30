# Nova 启动日志时间线工具

新文件，无旧版本。此工具只读取已保存的文本日志，输出 JSON 和中文 Markdown；不联系设备，也不修改生产源码。Python 标准库，无第三方依赖。

## 使用

工作目录固定为 `<nova-root>`。为每次分析指定一个尚不存在的输出目录；工具拒绝覆盖目录或符号链接，也拒绝输出到 Nova 以外。

```sh
PYTHONDONTWRITEBYTECODE=1 python3 artifacts-nova/boot-timeline-nova/boot-timeline-nova.py \
  --log logs-nova/boot-speed-round5-nova/candidate-run-nova/autolog-nova.txt \
  --output artifacts-nova/boot-timeline-nova/round5-replay-new-nova \
  --external-calibration logs-nova/boot-speed-round5-nova/user-observation-nova.json
```

输出为 `timeline-nova.json` 和 `REPORT-nova.md`。如果不提供 `--external-calibration`，人工计时保持缺失，不从日志总长度补填。再次运行时须换一个新目录。

```sh
PYTHONDONTWRITEBYTECODE=1 python3 artifacts-nova/boot-timeline-nova/test_boot_timeline_nova.py
```

31 项测试直接导入实际解析器，并运行真实 CLI 检查已有输出拒绝覆盖。包括 ns/us/ms/tick、没有频率、CNTPCT/CNTVCT/Mach/System Event 跨域相减拒绝、时钟频率变化、计数倒退、代际切换、缺失/重复起点、末行截断、坏行不能桥接、Setup 不能替代 Hello，以及固定 SHA256 的第五、六轮原始日志回放。worker 链新增真实三对样本、错代、错 owner、错 round、缺 main 身份和年龄倒退测试。坏 cs4k 记录会抑制不完整的总计。测试只验证离线证据处理，不验证设备启动提速。

## 计量契约

- JSON 每个事件/采样保留来源行、原始数值、单位、时钟域、频率（若可知）和证据精度；秒数使用最多 30 位有效数字的十进制字符串，不能把换算位数解释成观测精度。
- `rootio` 的 `timerTicks()` 来自 `NativeUfs.cpp` 中 `CNTPCT_EL0`；`blog-status` 和 `cpufreq` 使用 `CNTVCT`。两者频率相同也不跨域相减。
- `lsdipc-trap` 只用记录自身的 `absolute_ns`，保留原始 `ticks`；不借硬件计时器频率转换 Mach 原值。
- `System Event` 的原始字段保留为独立、不透明的时钟域。当前没有验证它到秒的映射，只输出同域 tick 差。
- `sbipc latest age_ms` 在输出时计算，pending 状态是此前的锁内快照；二者不同步，因此只报告阶段 age 观测值，不能作为严格等待下界，延迟误差上界未知。Setup 的 age 先计算、随后检查该代线程 active，可报告进程存活采样下界。
- `sbworkeripc phase_age_ms` 使用锁内保存的采样时刻和 pending 行，是 worker IPC 等待阶段的采样年龄下界。`op_age_ms` 是整个操作的年龄，不能替代等待阶段。工具按同组 round、SpringBoard exec 的 pid/taskgen/main tid、主线程 ownerTID、worker pid/taskgen/tid、owner 标志和对端 pid/gen 精确关联；主线程 owner 另行采样，因此不外推主线程整段连续等待。
- 不向无时间戳事件插值。Boot 菜单点击、苹果首次可见、Hello 首次可见都没有可验证的日志锚点。Setup exec、scene-create 请求、watchdog 配额和 framebuffer swap 都不等于 Hello。
- 首条汇总采样不是 Boot 零点；日志最后一条也不等于 Hello。Heartbeat/E1 是计数，不换算成秒。
- 采样窗口、代码签名各次操作耗时之和、根盘累计计数差不能互相相加组成 Boot 总时长。不能由这些数据认定 UFS 是唯一瓶颈。
- 识别到坏的计时记录会使该 schema 家族的区间/合计失效，避免跨过未知计数重置。末尾无换行的记录按可能截断处理，不参与计算；之前完整的记录仍可保留为已观察到的窗口。

## 覆盖率和第五轮示范

当前结果使用 `round5-final-v2-nova/` 和 `round6-final-nova/`，由 schema v2 工具生成。第五轮原始日志 SHA256 为 `cb61b6a8838e6648c508225e4ef6a52cdec3eccad7bc7230e40e9f4d1b682659`。

v2 增加了独立 worker 项，固定清单从 12 项变为 13 项，并非墙钟时间分桶。第五轮为 8 项未知、1 项原始 tick 差、2 项阶段 age 观测值、1 项存活采样下界、1 项采样窗口。第六轮为 8 项未知、1 项原始 tick 差、1 项阶段 age 观测值、2 项采样下界、1 项采样窗口。两轮均有 4/13 能输出秒数，真正具有起止事件时间戳的阶段耗时为 0 项；Boot→Hello 时间覆盖百分比未知。

第五轮自动结果：SpringBoard 等 launchd 的最大阶段 age 观测为 36.278 秒、等 lsd 为 101.427 秒（均非严格下界）；Setup 首次周期采样时已存活 ≥ 2.002 秒；lsd 的两个统计采样相距 260.643033073 秒。最后两项不代表 Hello 或启动完成。人工确认的苹果 73.03 秒、Hello 220.56 秒独立保存在外部校准栏，不与日志时钟合并。日志 L1391 的 exec-map 捕获上限通知也保留为完整性限制。

`round5-first-check-nova/` 为保留的开发草稿，rootio 曾误标 CNTVCT、exec-map capture-limit 曾误作坏行。`round5-evidence-nova/`、`round5-final-nova/`、`round6-evidence-nova/` 是旧的 v1 结果，把直接 sbipc age 过强地标成下界，且未纳入 worker 项。**这些旧目录不得作为当前结论来源**，均留存未覆盖。

## 追溯与边界

`provenance-v2-nova.json` 记录当前工具、测试、输入、输出和备份路径；`tests-v2-result-nova.json` 记录本会话实际运行的 31 项测试结果、命令和 SHA256。测试先在原实现观察到失败，再修正重跑。旧版 `provenance-nova.json`、`replay-provenance-nova.json` 和 24 项测试目录保留为历史证据。

第六轮 `round6-final-nova/` 未提供人工总时间，Boot→Hello 继续未知。直接 lsd 指标未知，但三组有效 owner→worker→lsd 记录给出 worker 等待阶段采样下界 36.498 秒；不能解释为主线程全程连续等待，也不能把直接指标缺失当作等待消失。CPU 占比解释由其他分析负责，此工具仅保留支持的 CPU 探针时间点。

工具不证明日志从 Boot 到 Hello 全覆盖，也不证明没有环形覆盖。图像里是否真正显示苹果或 Hello 必须有另外的视觉证据和经过验证的时间同步。本工具没有完成设备侧启动性能修复。
