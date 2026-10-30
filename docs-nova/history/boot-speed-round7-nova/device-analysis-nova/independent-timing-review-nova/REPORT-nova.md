# R7 真机日志独立计时复核

新文件，无旧版本。本次只读原始采集、首次解析结果和已构建源码；未操作设备、修改生产代码、覆盖主解析器或中央文档。数字由本目录 `verify-r7-timing-nova.py` 直接读取原始 bytes 独立计算，没有 import 主解析器。

## 结论

原始时间戳和主解析器的 6,269 个相邻间隔均核对一致；21 组时钟校准有效。首次解析有一处遗漏：root_device_resolve 的 begin 位于上一条未换行输出之后，仍有独立完整 P 前缀和完整字段，并未丢失或损坏。它应与 end 配成 **7.385559479 秒**。已向主代理报告并独立审阅修复；首次分析保留，新结果另存。

四阶段均 result=0。三个 prepare/mount 调用很短，不能因此推断整个启动与 UFS 无关。设备解析、挂载后的读盘、用户态初始化和服务等待均有独立边界。

## 原件与完整性

原件 `logs-nova/boot-speed-round7-nova/candidate-run-nova/autolog-nova.txt` 共 1,483,736 字节，SHA256 `01e6b73d77676d6436c3d898dc9671ce464bcb422d6d8ffb92ed0b735147420a`。派生正文 SHA256 为 `002c16c2e9d7695978be0e127464007cb88b35c602fdd7b913bda0fbf8a325ba`。

直接解码 `autolog-window-nova.bin` 的 MEIZULOG header 并核对 payload：ring total=1,483,563，bytes=1,483,736，dropped=0，reserved=0；payload 与文本逐字节相同，差值 173 字节来自附加 boot-video 前缀。ring 未绕回，原件无 NUL，末尾有 LF。主 source-map 共 12,541 片，连续覆盖原件和派生正文；每个 copy 逐字节相同，每个去除 token 正好 56 字节。原件副本也相同。

6,270 个完整 token（6,244 P / 15 R / 11 L）全部频率为 19,200,000 Hz；没有零频率、未知/损坏 token、tick 倒退或频率切换。有 10 条物理行含多个标签、15 个 inline 标签；前缀完整不表示正文整条原子。两个无标签非空物理行分别是额外 boot-video 摘要与 L1342 的裸正文，不给它们继承前一时间。

**console ring 的 dropped=0 不表示所有诊断无丢失。** L6256 明确记录 `meizu: sbipc loss dropped=267031 overwritten=267031`，属于独立的 sbipc 队列。未写入 ring 的早期输出、截取之后的尾部及无插桩路径，也不能由 dropped=0 证明已被捕获。

## 四阶段独立配对

只用记录内的 Mach ns 相减，不拿 CNTVCT 前缀代替。begin 输出成本在阶段内，表中是经过时间而非 CPU 时间。

| 阶段 / id | 原件行号 | begin ns | end ns | 差值秒 | 真实源码边界 |
| --- | --- | --- | --- | --- | --- |
| root_device_resolve / 1 | L102 → L137 | 18,513,990,833 | 25,899,550,312 | 7.385559479 | bsd_init 的 setconf() |
| root_mount / 2 | L138 → L157 | 25,899,562,760 | 25,917,835,572 | 0.018272812 | vfs_mountroot()，不含其后认证工作 |
| systemos_prepare / 3 | L178 → L211 | 26,184,683,489 | 26,339,829,218 | 0.155145729 | 完整 native_owner_cache_prepare() |
| data_prepare / 4 | L212 → L227 | 26,339,847,135 | 26,345,338,125 | 0.005490990 | 完整 native_owner_data_prepare()，不含后续 AKS notify |

L102 前半是 `skip attaching fsw to lo0 using legacy TX model`，随后出现第二个完整 `k=P` 标签，从其 payload 起到 LF 是完整 `nova: bootphase ... id=1 edge=begin ... result=0`。独立审核只以这个真实 P token 划分 invocation 起点，没有在任意裸正文中搜索后强行恢复。首次 parser 去前缀后对整物理行 fullmatch，因而错误标为 malformed；问题在解析边界，而非内核记录缺失。

## 相邻 gap 与 clockcal

所有相邻记录的原始 tick 差、频率、行号和排序逐项与首次 parser JSON 一致。最大 gap 是 L105 → L106 的 112,886,305 ticks / 19,200,000 Hz = **5.879495052083333… 秒**，上下文是 `ktriage_register_subsystem_strings` 后到压缩模块 load succeeded。它位于 root_device_resolve 内，但不能独立归为前一行函数或压缩模块耗时。

后两大 gap：L471 → L472 为 1.0365421875 秒；L469 → L470 为 1.0356028125 秒。后段 heartbeat/探针持续输出，所以小 gap 不排除服务或某条线程在同一期间持续等待。不能把 gap 榜当成完整函数耗时榜，或据此排除百秒等待。

21 组 clockcal 的频率均为 19.2 MHz，均满足本次采样 `before ≤ mach_ticks ≤ after`；bracket 只有 2 或 3 ticks，即约 104.167 或 156.25 ns。记录的 Mach ns 与本次 tick 换算一致，每组计算出的局部 CNTVCT−Mach 偏移范围都包含 0。这说明这些采样点局部兼容，**不把时钟原点定义成 Boot 菜单、不作全局精确映射、不映射 NativeUfs 的 CNTPCT**。

首 tag 计数值换算为 17.827035625 秒，末 tag 为 235.95113390625 秒，跨度 218.12409828125 秒。这只是本段保存日志首末标签范围，不是 Boot→Hello。Hello 已由外部用户观测确认，但本报告没有以该观察建立精确日志锚点，Setup exec/首帧均不能替代 Hello 首次可见。

## 主 parser 修复独立审阅

修复后源 SHA256：`b6bd188f3ec6b191b187bd42feb5553b2bd181a0d97a696df385fc4ecddee9af`。已读相对 `snapshot-nova/round7-parser-inline-phase-20261029T193326016291Z-nova/` 的差异、测试增量及 `tests-inline-20261029T193518790113Z-nova` 的 29 项通过记录；未重复执行测试。

`framed_phase` 直接用 raw 有效 P token 的结束偏移到 LF/CRLF 做严格 fullmatch；不在去前缀正文中搜索子串。内部或尾部插入 token、多个 phase、坏前缀、零 Hz、L/R、quoted 内容、尾部杂字、缺 LF 均不能被拼接修复。原生无前缀完整 phase 行仍支持。L102 的 source map 现在指向第二个 P 之后的实际 payload。新增真实 R7 回归与负例覆盖这些边界，没有发现阻断性误归因，可由主代理产生 final 输出。

## 复核产物与边界

- `independent-evidence-nova.json`：8 条原始 phase、4 个时长、21 组 clockcal、top 20 gaps、ring header 与源哈希。
- `mapping-and-bounds-nova.json`：原件/派生文件与 12,541 个映射片段逐字节核对。
- `raw-excerpts-nova.txt`：带原件行号的关键片段。
- `verify-r7-timing-nova.py`：独立核算脚本，成功退出 0。首次运行仅因本审核脚本使用默认 Decimal 精度而触发过严容差；改为显式 50 位上下文后通过。旧脚本保存在 `snapshot-nova/round7-independent-review-decimal-context-nova/`，主 parser 和原始数据均未因此改动。

首次输出 `round7-first-analysis-nova` 的 root_device_resolve unknown 不能用于认定 begin 缺失。最终主输出若就绪，会在本目录另存数值对照证据，保留此次首次分析记录。
