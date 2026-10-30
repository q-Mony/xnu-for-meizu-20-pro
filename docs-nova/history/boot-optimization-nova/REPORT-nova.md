# Nova 启动优化

候选已在同一手机两次成功到达 Hello。用户实测 Boot→Hello 分别为3分57秒（237秒）和3分10秒（190秒），并明确两轮均从 Boot 菜单开始，第二轮少47秒。本次复测使用完全相同镜像；签名转换及主要服务等待基本不变，尚不能确定47秒差异的具体来源。此前约5分钟为用户近似基线，未进行重复配对统计。

改动针对历史日志中 launchd 加载 dyld shared cache 时的同步签名转换：保留完整原签名验证，减少读取次数及重复转换，并加入局部 ARM64 SHA-256 加速。源码、构建与产物都在 Nova，未修改 Sol/Astra。

## 已实现

1. 同一 vnode 的完整签名/策略检查通过后，复用已验证 native4k 缓存。锁外准备后重新取得 UBC 并完整检查并发发布；临时 blob 与返回缓存分别认证。
2. 首次转换每次最多读取 256KiB；每个原 16KiB 摘要通过后，才从同一缓冲区生成 4KiB 摘要。尾块、截断 SHA256、I/O/分配失败均保留正确语义。
3. 仅此转换路径调用硬件 SHA256。每批至多 256 blocks（16KiB），禁抢占期间读取当前 CPU SHA2 能力；不支持时恢复抢占后使用原标量实现。独立汇编保存使用的 17 个向量寄存器。
4. 新日志 `nova: cs4k bytes=… reads=… total_ms=… read_ms=… hash_ms=…`，用于后续真机区分读取和计算时间。

## 验证

| 检查 | 结果 |
|---|---|
| 真实 prepare 函数、摘要/尾块/篡改/错误清理 | ASan/UBSan 通过 |
| 16MiB+1 的读取次数 | 1025 → 65，所有摘要一致 |
| 相同签名注册两次的转换次数 | 2 → 1；旧版优化断言 RED，新版 GREEN |
| 完整注册函数 | 基线/候选各 26 功能与资源用例通过；7 个模拟篡改拒绝 |
| SHA 真实汇编及标量代码 | 5480 摘要用例、144 特性/分批用例通过；q0–q31 保持 |
| 本机 64MiB 哈希基准 | 0.327255s → 0.030114s，约 10.87×；不是手机启动成绩 |
| Nova 构建、prelink、PAN | 通过 |
| 最终链接映像 | 新调用链可达；独立汇编与已测对象除核实的重定位外一致 |
| 固定基线打包 | 通过；固件/DTB/启动参数/ADT/trustcache/Data512 内容保持 |

未声称运行真实硬件 PAC 或在 Mac 上执行内核特权抢占/特征探测；相关 host 测试使用模型。实际 ARM64 SHA 指令及向量寄存器测试在 Mac 上运行。

## 候选与来源

- 镜像：`packaging-nova/candidate-nova/boot-optimized-candidate-nova.img`（100,663,296 字节）。
- 镜像 SHA256：`2336c87cc0c71f810d3967c8601124075e381f8efebd67834b3526124a3e1dab`。
- raw prelink：`142e3a63fb4314ac53826333ed0abe22c95f8c1fed83eb11989ba932a5ecb10a`。
- payload 内核：`6e2f63e577e2fa32f5beaf3c380b03b65ad0db15fb36bebd8d91c07760e0fe30`。
- 补丁：`boot-optimization-nova.patch`；构建证据：`build-verification-nova.json`；最终代码审计：`final-binary-audit-nova/audit-nova.json`。
- 冻结 XNU 文件清单：5206 文件一致、4 修改、2 新增。另有隔离时误移的测试 Prefix.pch，原样备份仍在；未发现其他源码差异。该比较不代表全部外部编译依赖相同。
- AVB 尾部按历史原型保留，没有重新生成 AVB 签名。
- 构建先遇到沙箱 sysctl 限制，再遇到中断留下的3个缺失对象；从本次改前备份恢复以供符号清点并设为旧时间，最终 make 已重新编译全部3个对象。6个优化源文件构建前后哈希完全一致。

## 首轮真机测量结果

用户已明确确认设备空闲并授权 Nova 独占、刷入及测量。当前 Android B 槽与 boot_a 备份均已核实；通过 fastboot 只写 boot_a，返回 B 槽读回整分区，SHA256 与候选完全一致，boot_b 哈希不变。

2026-10-29 05:42:59（Asia/Shanghai）确认切 A 成功并发出启动命令。该时刻不是 Boot 菜单开始或 Hello 首帧的时间。用户观察苹果标志约在90秒出现，Hello在237秒出现，因此苹果标志后约147秒。E1约220000→300000仅记录为用户观察，未转换成墙钟时间。

新持久化日志中有23条Nova计时，全部在首次共享区域映射前：处理2,724,626,432字节，读取10,403次，合计49.418秒，其中读取40.967秒、摘要计算8.429秒。读取已占这部分约83%；这些计时只覆盖成功且至少16MiB的转换，不能说覆盖全部启动。首次共享区域映射前最后heartbeat从刷前1940变为940，但heartbeat是调用计数，不能直接当精确秒数。

SpringBoard同一主线程仍串行等待launchd至少35.282秒、lsd至少100.341秒，总下界135.623秒；刷前对应137.636秒。该后段瓶颈未明显改善，差值是采样下界差，不作为精确收益。

候选有Setup scene-create 20秒watchdog与chronod watchdog记录。类似Setup问题在更早历史日志已存在，但当前日志没有可与Hello首帧同步的时间戳，不能确定本次watchdog与Hello的先后，也不能凭此宣称本轮引入或解决了该问题。取得的文本没有kernel panic或摘要错误匹配，仍不代表所有日志历史完整。

日志取回后，最后核实手机留在Android B槽，boot_b哈希未变，A槽仍为本候选。相关证据：`device-hash-performance-nova.json`、`device-candidate-nova/REPORT-nova.md`、`logs-nova/device-boot-optimization-nova/user-observation-nova.json`及`device-final-state-nova.json`。

刷前 boot_a（Astra 当前镜像）已保存为 `snapshot-nova/boot-optimization-nova/device-before-nova/boot-a-before-verified-nova.img`，SHA256 `a85498e89528303d47f55dc1b57dc786a2e9fd719837fdea3f55442e48a25c07`。备份从整分区精确长度取出，手机端前后及本地三方哈希一致。首次原始流末尾含80字节dd统计，已保留并明确标记，不能把那个未裁剪文件用于回退。

启动前旧日志保存在 `logs-nova/device-boot-optimization-nova/before-candidate-nova/`，本轮日志在 `logs-nova/device-boot-optimization-nova/candidate-run1-nova/`，正文SHA256为 `243b87f3669855c8d73d38b127b14fea6d9063edd9c82c039216ed43d33de4f3`。Nova新日志读取器仅在Android+su下读unixnu最后32MiB，不自动切槽或重启。两份日志头的173字节差等于boot-video合成前缀；候选无NUL、持久化dropped=0，但IPC环有overwritten记录，不能据此声称完整保留所有IPC历史。

## 第二轮真机复测（2026-10-29 14:21）

用户再次要求刷入测试；重新刷入同一候选并在Android B整分区校验，通过后切A。用户确认14:21:50从Boot开始、14:25:00首次Hello，共190秒；苹果标志约80秒。首轮237秒也由用户明确确认同为Boot起点。

第二轮23次大文件签名转换总49.485秒（read41.027、hash8.431），输入总量和读取次数与首轮一致；SpringBoard串行等待launchd≥35.481秒、lsd≥100.796秒，合计下界136.277秒。主要已测阶段基本不变，不能据此定位报告总时间少47秒的原因。Setup与chronod watchdog仍在，未见明确panic或签名错误匹配。

主机14:21:04.731重启命令至用户报告Hello约235.269秒，是另一个计时起点；不能替代用户的Boot→Hello190秒。双方未独立同步钟表、没有自动首帧计时。本轮最终留在Android B，A候选保留、B哈希未变。详细结果见 `device-repeat-20261029T061747Z-nova/REPORT-nova.md`，操作与原始日志在Nova根目录的 `logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/`。

## 剩余瓶颈与测量边界

本轮真机已确认 SpringBoard 等 launchd/lsd 的串行等待仍在。本轮未修改 LaunchServices 数据持久化，也未跳过 Setup。内存 Data 种子导致每次初始化的成本仍可能存在。不能把 host 10.87×或 I/O 调用减少比例直接应用到整机五分钟启动。
