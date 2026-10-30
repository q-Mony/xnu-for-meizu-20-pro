第三轮启动优化：rootio 独立复核（Nova，2026-10-29）

这是新建报告，无旧文件。范围仅为第二、第三轮已采集日志与计时口径的独立核对；本复核没有修改生产代码、执行构建或操作设备。主分析与 CHANGELOG 由主代理统一维护。

第三轮实测 Boot→Hello 为 **212 秒（3 分 32 秒）**，相比第二轮的 245 秒减少 **33 秒，约 13.47%**。第三轮早期根文件数据校验耗时显著下降；同样 23 项 cs4k 任务的读取阶段合计减少约 23.8 秒，与苹果标志提前 24 秒的观察一致。苹果之后仍用 139 秒，剩余耗时不能继续全部归因于根文件 SHA 校验。

| 用户测量区间 | 第二轮 | 第三轮 | 本次减少 |
|---|---:|---:|---:|
| Boot 菜单开始→苹果标志 | 97 秒 | 73 秒 | 24 秒 |
| 苹果标志→Hello | 148 秒 | 139 秒 | 9 秒 |
| Boot 菜单开始→Hello | 245 秒 | 212 秒 | 33 秒 |

第三轮 Boot 开始时刻为 17:12:50。用户确认的 139 秒是苹果出现后到 Hello 的额外时间，不能当作整轮总时间。本表是单次人工计时比较；日志没有 Boot 菜单起点或 Hello 首帧的精确标记，也没有据此得出多次启动的稳定收益。

**证据与解析质量**

| 输入 | SHA-256 |
|---|---|
| [第二轮 autolog](<nova-root>/logs-nova/boot-speed-round2-nova/candidate-run-nova/autolog-nova.txt) | `283735861cf23fa8581a73728ee76961cd593a2658d2762bc4c9efe47af149aa` |
| [第三轮 autolog](<nova-root>/logs-nova/boot-speed-round3-nova/candidate-run-nova/autolog-nova.txt) | `57cbb9046f33237974d31bdef848ed537f446520d99ee869dc8c2c2277bc9633` |
| [第三轮原始窗口](<nova-root>/logs-nova/boot-speed-round3-nova/candidate-run-nova/autolog-window-nova.bin) | `7dd3f4a4326d1f64bb25396f6b0a200790877da9320c4f68a010d7ac55ccf6af` |

使用只读的 [严格 rootio 解析器](<nova-root>/artifacts-nova/boot-speed-round2-nova/analyze-rootio-nova.py) 复核，第二轮为 35 个快照，第三轮为 24 个快照；两者严格状态均为 clean，各为一个单调计数段，没有解析出的 malformed、duplicate 或 discontinuity，频率均为 19,200,000 Hz。两轮每个已记录快照的 `bad_hash` 均为 0。

`bad_hash=0` 表示这些快照中根文件数据校验失败计数为零，不能证明系统所有认证环节均成功，也不能证明日志没有缺失。各字段独立读取，快照不是事务；命令尝试与成功计数偶尔相差 1 可以来自采样时仍在进行的命令。

**相同早期 cs4k 工作量**

两轮各有 23 条 cs4k 记录，按顺序比较的 `(bytes, reads)` 元组完全相同，总计 2,724,626,432 字节（2598.40625 MiB）、10,403 次读取。第二轮记录位于第 257 至 510 行，第三轮位于第 256 至 465 行。邻近的 `dyld_shared_cache_arm64.xx` 打开记录和 `fcntl ADD` 支持这些记录属于早期共享缓存准备；数值元组相同本身不是文件内容完全相同的证明。

| 23 项记录的时间合计 | 第二轮 | 第三轮 | 减少 |
|---|---:|---:|---:|
| total_ms | 44,002 ms | 20,209 ms | 23,793 ms（54.07%） |
| read_ms | 35,538 ms | 11,761 ms | 23,777 ms（66.91%） |
| hash_ms | 8,435 ms | 8,418 ms | 17 ms（0.20%） |

源码 [ubc_subr.c](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/ubc_subr.c:5891) 在 `vn_rdwr` 前后累计 `read_ticks`，因此 `read_ms` 包含底层 VerifiedRoot 校验，不能解释成纯物理 UFS 读盘时间。随后独立的代码签名整页及子页哈希循环累计 `hash_ticks`；它与根文件校验是不同计时范围。本轮根文件校验加速主要反映在 cs4k 的 `read_ms` 中，单独的 `hash_ms` 基本未变。

当前 cs4k 读取块仍为 256 KiB。日志只打印成功、至少 16 MiB 的前 64 项工作，并将计时截断为整数毫秒。因此，上表是这些已记录调用的 elapsed 时间合计；不能把可能重叠的函数计时直接视为全局独占墙钟时间，也不能把约 23.8 秒与其他层计时再次相加。

**覆盖这些任务的早期 rootio 窗口**

分别采用首个 cs4k 之前的第一个快照，以及全部 23 项 cs4k 之后的第一个快照。第二轮为日志第 185→892 行，第三轮为第 200→878 行。它们覆盖近似相同阶段，也包括其他根文件读取，不是完全相同的工作量，更不是 Boot 或 Hello 边界。

| 快照差值 | 第二轮 | 第三轮 |
|---|---:|---:|
| 观察窗口 elapsed | 49.514205313 秒 | 28.526300365 秒 |
| verify_bytes（此处也等于 physical） | 2,526,396,416 字节 | 2,528,362,496 字节 |
| verified 单元数 | 154,199 | 154,319 |
| read | 3.864599271 秒 | 3.951586198 秒 |
| verify | 27.845666927 秒 | 3.632708385 秒 |
| copy | 0.563138698 秒 | 0.560132135 秒 |
| cmds / ok_cmds | 21,208 / 21,208 | 21,252 / 21,252 |
| requested / copied | 2,488,782,848 / 2,488,782,848 字节 | 2,489,815,040 / 2,489,815,040 字节 |

第三轮校验字节比第二轮多 1,966,080 字节（约 0.07782%）。校验计时减少约 **86.95%**，约为原来的 1/7.67；按工作量归一化，11.55729→1.50658 ms/MiB，减少约 **86.96%**。底层 read 与 copy 基本稳定，支持实际改善集中在根文件校验阶段的判断。这些数值与主代理的 [compact summary](<nova-root>/artifacts-nova/boot-speed-round3-nova/device-analysis-nova/rootio-compact-summary-nova.json) 一致。

rootio 计时是包围相应调用的累计 elapsed 时间，可能包含抢占或中断，不是独占 CPU 使用时间。不同层计时存在包含关系，各字段又不是原子快照，不能通过简单相加解释整段 Boot→Hello。

**整份日志的首末快照不用于比较启动时间**

| 整份日志的可见快照差值 | 第二轮，第 185→9494 行 | 第三轮，第 200→5436 行 |
|---|---:|---:|
| elapsed | 344.546164896 秒 | 231.854825521 秒 |
| read | 11.009777865 秒 | 8.572116458 秒 |
| verify | 33.473511198 秒 | 4.292677969 秒 |
| copy | 0.940904010 秒 | 0.628797865 秒 |
| verify_bytes | 2,953,445,376 字节 | 2,940,977,152 字节 |

这些窗口延伸到 Hello 之后，而且停止采集的时刻不同。首个计数快照不是全零起点，最后一个也不是 Hello 首帧。不能用两个整份日志的 elapsed 或累计阶段计时相减，宣称 Boot→Hello 获得相同幅度的提升。

**173 字节的 capture 差额符合写入格式**

第三轮 [capture 元数据](<nova-root>/logs-nova/boot-speed-round3-nova/candidate-run-nova/capture-nova.json) 记录 `ring_total=977206`、`text_bytes=977379`，差额为 173 字节；`dropped=0`、`reserved=0`、`nul_bytes=0`。原始窗口大小为 33,554,432 字节。

独立按 `<8sQQII>` 解码原始头部得到 magic `MEIZULOG` 和上述字段。提取的文本逐字节等于 `raw[4096:4096+977379]`。`Darwin Kernel Version` 前恰有 173 字节：一个前导换行加一行 `[boot-video raw] ...`；其余正文恰为 977,206 字节。

[BootLog.c](<nova-root>/unixnu-meizu20pro-nova/drivers/NativeBootLog/BootLog.c:177) 的 formatter 明确只生成完整的 172 字节 boot-video 行。[kern_exec.c](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7647) 在它前面再补一个换行，将 `hdr.total` 设为 console ring 总量，而 `hdr.bytes` 设为合成前缀加保留的 ring 内容。因此这个差额是预期格式，不是由截断、损坏或漏读造成的证据。[读取脚本](<nova-root>/tools/read-autolog-nova.py:29) 按 `hdr.bytes` 从 4096 偏移提取文本，与上述格式一致。

`dropped=0` 只说明该快照报告的 ring 内容没有因保留容量截断；此轮 ring 总量也小于 8 MiB。它不能证明整个启动过程的输出均完整且一致地落盘：

- [写入节流说明](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7582) 明确规定上一次尝试结束后最多每秒一次完整写入。复位可能丢失末尾约一秒再加调度、I/O 延迟；写入停滞或失败时没有固定缺失上限，多块原地写入也不是掉电原子的。
- [console ring 写入与快照](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/console/serial_general.c:165) 先 relaxed 原子递增总量，再写字符；快照 relaxed 读取总量并无锁复制，不能保证与并发写入构成一致的逐字符或逐行事务。
- 严格 rootio 解析通过、NUL 字节为零，是已采集数据的质量证据，不是“完整无损采集”的证明。

本次证据支持保留第三轮根文件 SHA 加速改动；其早期效果明确，但不足以解释全部剩余 212 秒。后续应围绕苹果标志到 Hello 的 139 秒补充可对齐的阶段标记与等待证据，再选择下一项优化。继续扩大读取块或把所有内容一次性读取，都不能仅凭本轮数据认定会消除该阶段的主要延迟。
