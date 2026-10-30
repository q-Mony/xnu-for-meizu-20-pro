第四轮 launchd 消息及返回码复核（Nova，2026-10-30）

**结论：目前不能把这批计数解释成 launchd 业务反复失败，也不能准确命名两个 ID 的业务。** lsd 的聚合只保存“最终 Mach 返回码是否非零”，没有保存非零码的值，原日志中两个 lsd 明细都仍处于 WAIT。因此，2553 次非零返回的具体类别不能从现有落盘数据恢复。

但同一份日志另有非常具体的反证线索：Setup/Budd 历史中，17 次发送 `0x40000388` 后，同 generation、同线程的下一次接收均返回 `MACH_RCV_TOO_LARGE`，再下一次仅接收操作全部成功。51 次发送 `0x40000324` 后的下一次接收全部直接成功。这支持优先核对“接收缓冲不足后再接收”，而非先修改服务的失败重试逻辑。它仍不是 lsd 2553 次返回码的直接证据。

本报告新建，无旧版本；只读取 Nova 代码、日志及公开 Apple 源码，没有修改生产、其他报告或设备，没有执行下载脚本。

**输入及 lsd 证据**

输入：[第四轮 autolog](<nova-root>/logs-nova/boot-speed-round4-nova/candidate-run-nova/autolog-nova.txt)，1,453,771 字节，SHA-256 `ea8a091fabf4ecfca6f97465d347b539a7c10baa7806b3cc5ab3f5470f234ab0`。末尾自偏移 1,453,346 起为 425 个 NUL；本文引用的消息与计数记录均在该尾部之前。没有把尾部损坏或缺失的记录补成零值。

| lsd gen271 → pid1/gen4，mode3 | 最后可见 completed | 非零 Mach 返回次数 | sum_ns | 单次平均 |
|---|---:|---:|---:|---:|
| `1073742728 / 0x40000388` | 2553 | 2553 | 7,256,009,166 | 2.84215 ms |
| `1073742628 / 0x40000324` | 2555 | 0 | 11,198,328,854 | 4.38291 ms |

最终记录位于第 7611、7612 行。第 2387、2388 行已经分别累计到 2426、2428 次；它们所属 dump14 的 `age_ms=130396`，见第 2365 行。age 是相对 lsd 捕获时间，不是 Boot 菜单时间。

这两个桶的次数相近，只说明两种观察的出现量相近，不能建立逐请求“失败→fallback”的配对。约 7.256 秒和 11.198 秒都是 trap 观察 elapsed 合计，可能与其他线程/阶段重叠，不是可直接兑现的启动节省时间。`errors=0` 也只表示最终 Mach 返回成功，不证明 launchd 回复中的业务状态成功。

第 1919 行是 lsd 的 `0x40000388` 明细，第 2148 行是 `0x40000324` 明细；两者都是 `phase=4`（WAIT）、`recv=0 initial=0 result=0`。这些零值是尚未完成时的字段状态，不能覆盖聚合里的非零完成计数。

[统计完成函数](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:258) 把 `result != MACH_MSG_SUCCESS` 转成布尔计数，桶中没有按原始 `mach_msg_return_t` 分组。[既有 drain](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:1769) 仅打印 pending 的 SB latest；虽然内存中有 64 项环保存完成历史，这一函数并没有把 lsd 的那些完成记录写入 autolog。现有落盘聚合不能反推其原始码。

**同日志的 Setup/Budd 接收扩容证据**

按 `(gen, tid)` 分开处理 `setup-budd-ipc` 的 phase1（发送开始）与 phase3（接收开始）记录，保留原顺序并排除其中插入其他发送的情况，得到：

| 发送 ID | 已记录发送数 | 同线程下一接收 | 再下一步 |
|---|---:|---|---|
| `0x40000388` | 17 | 17/17 为 `id=0 result=0x10004004` | 17/17 都是 receive-only，`id=0x20000000 result=0` |
| `0x40000324` | 51 | 51/51 为 `id=0x20000000 result=0` | 随后继续其他发送；不需要上述额外接收 |

17 组包含 gen872、gen970，以及一组 gen446；它们是 Setup/Budd 历史，不是 lsd gen271。发送 phase2 与接收 phase4 也分别保留其返回结果。这里使用同线程的连续可见阶段作关联；MSB 的发送与接收分配不同 sequence，不能把它描述成拥有同一个全程请求 ID 的业务追踪。

一组完整例子是 gen872、tid1968：

| 原日志行 | 观察 |
|---|---|
| 2707–2708 | 发送 `0x40000388` 到 pid1/gen4，发送返回 0，reply endpoint 编号 10。 |
| 2748–2749 | 同线程接收返回 `0x10004004`，options=`0x8380000403104207`；低位包含 SEND、RECEIVE、RCV_LARGE。 |
| 2750–2751 | 同线程下一次仅接收成功，options=`0x8380000403100202`，reply ID=`0x20000000`，接收 endpoint 为 10。 |
| 2752–2753 | 再发一次 `0x40000388`；第 2806–2809 行再次出现 TOO_LARGE→receive-only 成功。 |
| 2810–2813 | 随后发送 `0x40000324`，直接收到 `0x20000000`，发送和接收返回均为 0。 |

[本地 message.h](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/mach/message.h:1368) 将 `0x10004004` 定义为 `MACH_RCV_TOO_LARGE`，表示接收缓冲不足以容纳 inline data；`0x10004003` 则是 `MACH_RCV_TIMED_OUT`。两者不能混为同一业务错误。[选项定义](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/mach/message.h:991) 中 SEND、RECEIVE、RCV_LARGE 分别为 1、2、4。

[mach_msg_receive_results](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:2007) 的 TOO_LARGE 分支明确：当 `MACH64_RCV_LARGE` 生效时，消息尚未从队列取出，只得到需要的大小和可选接收名；随后 [mach_msg_receive_too_large](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c:1841) 回填所需尺寸。第 2905 行的 special-reply 清理分支也特意保留这种状态供下次接收。

因此，这组实际观察符合内核支持的“先报告尺寸、再取消息”流程。日志没有记录原接收容量和所需容量，尚不能判定是正常消息大小分布、用户态缓冲策略、错误的大小报告，还是本机移植问题；也不能仅凭此建议直接增大某个全局缓冲区。

Nova 本地 [libdispatch 的接收代码](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/libdispatch-work/src/mach.c:824) 也包含遇到 TOO_LARGE 后按返回尺寸分配、调整选项并重试的实现。它说明这类返回可用于正常恢复，但不是当前 libxpc/launchd 两个业务 ID 的实现证明。

**消息 ID 语义核对的边界**

在 Nova 的活动源码、feeds、kits 与本轮 inputs/cache 中，未找到这两个十进制/十六进制 ID 的定义，也未找到目标版本的 libxpc/launchd MIG 源码。现有 `audit-target-xpc-endpoints.py` 是历史静态分析脚本，会调用其他写文件的脚本；其硬编码的 `unixnu-main-nova/.cache/firmware/target-volumes/...` 输入在本工作区不存在，本次没有运行它。

已只读获取 Apple 官方 `launchd/main/src/job.defs`（5516 字节），来源为 `https://raw.githubusercontent.com/apple-oss-distributions/launchd/main/src/job.defs`。该定义从 `subsystem job 400` 开始，含 `look_up2` 等旧 MIG 接口，但没有本次两个 ID；没有将其中旧接口按偏移猜配到 804/904。公开检索接口没有返回可用结果，后续 GitHub API 查询返回 403 rate limit；这不构成“公开资料不存在”的证明。

`0x40000388 - 0x40000000 = 904`、`0x40000324 - 0x40000000 = 804` 只是数值分解。差值 100 也不能证明一方是另一方的 MIG reply：本轮它们都被观察为发往 launchd 的 send ID，而可见接收成功消息的 ID 是 `0x20000000`。本报告不为它们指定未经验证的业务名称。

**最窄、可验证的下一步**

1. 在现有 lsd 的两 ID 聚合点保留有限的精确最终 `mach_msg_return_t` 分组，以及 send 是否成功、receive 初始码与最终码。继续使用已有 generation、peer pid/gen、完成去重和时间预算；输出只包含标量。首先确认 2553 次是否全为 TOO_LARGE，还是混有超时、无效目的地或拷贝错误。
2. 如果为 TOO_LARGE，只在该分支采样原 `recv_msg_size`、所需 `ith_msize`/辅助区尺寸及相关选项；对同 generation/tid 的下一次 receive-only 记录结果和耗时。若中间发生新 send、换代或超过短关联窗口，则明确标记未关联，不沿用旧请求。固定样本上限，避免逐次 2500 多条打印。
3. 从结果判断是否存在重复扩容或异常尺寸，再选择缓冲策略或内核大小报告的修复。业务名称仍需目标版本 libxpc/launchd 二进制的已确认函数/分派定义来映射；精确返回码的验证不应等待未经证实的业务命名。

该方案首先回答“非零到底是什么”，不需要更改 launchd 服务行为、跳过初始化或按未知 ID 加特殊成功返回。当前证据不足以把这两个桶确认为剩余启动延迟的根因。
