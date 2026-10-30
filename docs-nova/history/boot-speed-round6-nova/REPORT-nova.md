# 第六轮启动诊断 — Nova

状态：**用户已确认到达 Hello，日志已回收并分析，设备最后确认在 Android B**。A 槽为第六轮正式候选、整分区回读一致，B 原哈希保持。本轮没有人工总计时，Boot/Hello 视觉锚点尚未加入，不能报告总启动秒数或宣称提速。第五轮人工累计时间为苹果 73.03 秒、Hello 220.56 秒。

## 本轮结果

- 22 对同代 lsd CPU/IPC 快照全部有效。在桌面主线程相同锁等待位置的重复观察内侧，90.283 秒窗口内 lsd 累计 CPU 为 30.520 秒，约 33.805% 单核等效。其余时间不能直接当睡眠或全系统空闲，下一项区分依赖阻塞与调度排队。
- 本轮依赖链为桌面主线程等工作线程、工作线程等 lsd。该工作线程同一 IPC 等待阶段的锁内采样年龄至少 36.498 秒；旧“主线程直接等 lsd”指标缺失不代表等待消失。
- 按日志顺序包围上述观察的根盘独立窗口为 111.649 秒，read/verify/copy 累计 1.796/0.280/0.027 秒。绘制独立窗口 110.324 秒内累计 0.545 秒、零滚屏增量。这些窗口不能相加或冒充 Boot→Hello。
- 同工作量 23 次 cs4k 为 19.371 秒，上轮 19.356 秒；读盘与哈希性能相近。一次 CPU 四寄存器采样成功，但没有足够证据报告全程 CPU MHz。
- 本份日志没有 Setup watchdog，仍有一条 chronod watchdog。捕获时间与历史不同，不据此宣称 watchdog 已修复。

详细复算、源码语义及限制见 [真机分析报告](<nova-root>/artifacts-nova/boot-speed-round6-nova/device-analysis-nova/REPORT-nova.md)。日志封装和哈希一致；多出的 173 字节是正常前缀，dropped/NUL 都为零。此结论不保证复位前尚未刷盘的最后事件也已保留。

## 本轮测量

- 现有 autolog 线程只采样一次 boot domain0 EPSS，精确启动参数 `nova-cpufreq-probe=1` 才允许访问。只读固定四个寄存器；enable 无效时只读一个。输出原值、采样时刻与时钟频率，不把 timer_hz 或请求索引冒充 CPU MHz。
- 将既有 lsd IPC 快照与同代 lsd task 的累计 CPU 记账配对，保留原 10 秒间隔、31 次上限。记录采样前后时刻、有效状态及 user/system ticks。忙、缺记录、换代都输出明确状态，不伪造零 CPU。
- 未改变 CPU 调频策略、SMP、L3、Data、根盘读/校验、服务或 watchdog 策略。

累计 CPU 能帮助区分大量执行和大量等待；较低 CPU 使用量还不能单独区分同步睡眠与调度排队。CPU 频率原值只代表单次采样，后续如需频率需结合正确 LUT 解释。MMIO 成功映射不保证总线读取可完成，软件次数上限不是硬件超时恢复。

## 验证

CPU 实际生产函数 48 项 ASan/UBSan 测试和 2 个负对照通过。lsd 实际函数 24 项场景分别在 precise=0/1 下通过，4 个负对照有效；autolog 与 IPC 的宏未定义、0、1 三种状态均通过。实际 shell 模式块的 14 项测试验证开启/关闭、缓存失效和错误配置拒绝。

完整内核构建、prelink、PAN 和固定封装通过。最终二进制核对确认 autolog→CPU 探针及 IPC→CPU 记账→recount 的实际调用；5 个对象函数绑定、16 个保留对象以及 raw→payload 函数字节均通过。固件、DTB、ADT、trustcache、Data、继承 AVB 尾均保留，启动参数只追加诊断开关。

第一次候选 `22ccd2d6…` 因 IPC 调用被条件编译去掉而被最终审计拒绝，**没有刷入**。已用独立 IPC 对象编译开关修复，并通过实际调用核验；该旧包继续禁止使用。

| 正式产物 | SHA256 |
| --- | --- |
| boot | `a2114a6acbfe136b3793d1ec2cbf80c4ff7358c444cde9b344f0704479b5bd1e` |
| raw kernel | `9a5d91cda01888313607128a26d9b2e5d2f8ceb485b8a67dc9a2e6e4c601c47d` |
| payload kernel | `28b5299bcf853c1972e78044d8a7ab80f61da664972f1129a731c833a4cc5865` |

正式镜像：`packaging-nova/cpufreq-candidate-final-nova/boot-cpufreq-candidate-nova.img`。完整证据见 `delivery-manifest-nova.json`、`linked-diagnostics-audit-nova/REPORT-nova.md` 与 `independent-final-hook-review-nova.md`。

## 自动阶段时间

按用户要求，已完成 [第六轮自动时间表](<nova-root>/artifacts-nova/boot-timeline-nova/round6-final-nova/REPORT-nova.md)。真实时间、阶段年龄观测、采样下界和窗口分别标记；没有时间戳的阶段保持未知，不用相邻日志插值。Boot 菜单点击、苹果和 Hello 首次画面尚无可信锚点，Setup exec 与首 swap 不能替代它们。本轮无需人工分段计时。

复核同时修正旧 sbipc 指标解释：pending 在锁内复制，age 在解锁后重新取时钟计算，故旧 launchd/lsd 最大 age 只能叫观测值，不能严格称“≥等待秒数”。本轮 worker 的 phase_age 使用锁内保存时刻，具有不同语义。数字未改，正式第五轮修正版见 `../boot-timeline-nova/round5-final-v2-nova/`。

设备阶段记录见 `../../logs-nova/boot-speed-round6-nova/candidate-start-nova.json` 和 `postrun-device-state-nova.json`。02:27 核对用户返回的 Fastboot 后切 Android B，02:28 核对 A/B 并完成日志读取。主机重启与读取时刻都不是 Boot 菜单点击或 Hello 首帧时刻。本次分析未再次操作设备或修改生产源码。
