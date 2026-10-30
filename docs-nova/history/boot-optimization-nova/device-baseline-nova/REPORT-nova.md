# 刷前 Astra 日志与 032506 对照（Nova）

**两项主要瓶颈一致：共享缓存签名注册的长阶段仍存在，随后 SpringBoard 同一主线程串行等待 launchd 与 lsd 至少 137.636 秒。** 此报告分析的是刷 Nova 前留存日志，不是 05:42:59 启动的 Nova 候选日志，不能用于宣布 Nova 优化结果。

新建本报告及 JSON，无旧文件；只读源日志，没有设备命令、源码修改或构建。新文件记录在 `snapshot-nova/boot-optimization-nova/device-baseline-before-nova/new-files-nova.json`，中央 CHANGELOG 由主代理追加。

| 观测 | 刷前记录 | 032506 |
|---|---|---|
| 内核构建 banner | 2026-10-29 04:30:15 CST，L3 | 2026-10-29 03:08:52 CST，L3 |
| dyld 首次打开 → shared-region map | L243 → L594 | L243 → L594 |
| map 前最后 heartbeat | tick1940，L591 | tick1940，L591 |
| 记录到的签名注册起止对 | 24 对成功，其中12对跨 heartbeat | 相同 |
| SpringBoard 主线程 | PID25 / TID253 | PID26 / TID254 |
| 等 launchd 下界 | 36.070 s，seq258，L1580 | 35.296 s，seq261，L1568 |
| 等 lsd 下界 | 101.566 s，seq2196，L2082 | 100.830 s，seq2376，L2053 |
| 同线程按序等待合计下界 | **137.636 s** | **136.126 s** |
| Setup arm | 1 次，L2296，PID245 / TID2115 | 3 次 |
| Setup scene-create watchdog | 留存区间未见 | 2 次，各20 s |

签名阶段例子完全对应：`dyld_shared_cache_arm64.01` 在 L251 注册、经过 tick20/40/60/80 后于 L260 返回0；L594 才进入 `srmas2 files=44 maps=52`。它与已定位的同步原始签名校验及4KiB转换路径相符，但本份日志没有独立 I/O/hash 计时，不把 tick1940解释为精确97秒，也不沿用旧二进制的 PC 符号映射到这个新构建。

SB 等待只纳入 `phase=4 pending=1`。seq258 的4个连续样本年龄增长至36070 ms，终点 PID1/gen4（launchd）；之后 seq2196 的10个样本增长至101566 ms，终点 PID96/gen270（lsd）。两组都属于 PID25/TID253，先后无重叠，所以可加下界。相对032506多1.510秒只表示样本下界差；构建、参数、捕获区间并非受控对照，不能据此评价变慢或变快。

记录中 L1600起有16条 framebuffer swap；L2296 arm Setup，L2922/L4043/L4664分别保留 Setup age2028/8016/15018 ms 样本。没有直接 Hello 像素或首帧时间标记。未见 Setup watchdog 不等于稳定性已修复；L4721的10秒 watchdog属于 **chronod**。同样，未见Hello文字不等于启动失败。

日志头 `total=976384`、`bytes=976557` 相差173字节，**恰好等于开头空行与 `[boot-video raw]` 合成前缀的长度**。已核对原始32MiB窗口的头、解码正文及 capture JSON 哈希；当前writer的 `total=snapshot_total` 不含前缀，`bytes=prefix+retained` 含前缀，符合这个差值。不能把173字节差直接当作撕裂证据。留存正文无NUL、以完整换行结束，301条heartbeat从0单调到6000，没有倒退。

上述检查仍不能证明持久化多块写具有原子性或包含整次启动，报告只使用完整可解析事件。`captured_utc` 是主机采集时间，banner是编译时间，均不作为启动时间。刷前a85498…镜像关联沿用主代理设备记录；console banner本身不足以唯一确认镜像。历史 Astra manifest说明其关闭touch/HID，与032506并非相同配置。

输入：[刷前日志](<nova-root>/logs-nova/device-boot-optimization-nova/before-candidate-nova/autolog-nova.txt)（SHA-256 `a19012c1b0e6e048bd54a76251ccb53781cf2e0f2f5e1c0fcd595b4f82c31601`）；[032506]([local-home]/...)（SHA-256 `e5ee7199ec234746ee60d2244554dab3b5c33ab05f23c073e370cdb103983bf2`）。机器可读的行号、连续等待样本与边界见 [comparison-nova.json](<nova-root>/artifacts-nova/boot-optimization-nova/device-baseline-nova/comparison-nova.json)。解析复用已审阅的本地 `artifacts-nova/boot-latency-nova/analyze-boot-nova.py`，未执行任何上层脚本。
