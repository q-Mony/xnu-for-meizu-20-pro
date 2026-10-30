R11 最小失败诊断合同（只读设计建议，尚未实现）。

目的：下一次 policy 失败时，同时判别实际运行版本、warm 参数是否读到、选择的分支、具体失败系统调用、errno，以及本次日志写入是否被跳过或失败。它不改变策略成功条件，不吞掉17或22，也不改变正常启动的日志频率。

建议集中在 `BootWatchdogPolicy.c` 与现有 `kern_exec.c` 日志实现/调用点；不扩展通用 panic 行为，不修改 UFS 读路径或正常日志窗口布局。

1. 定义本候选独有、在 manifest 绑定的只读标识，例如 `nova-r11-policy-v1`。同一标识用于首次 autolog 启动前的运行时行、policy 入口/失败行和最终 panic。版本字符串不是最终镜像自哈希；交付记录将该标识与实际包哈希关联。
2. 记录 warm 参数的 present/value 和真正选中的 `disabled / preflight / cold / warm` 分支。失败阶段要细到 `mkdir / directory.open / directory.stat / policy.create / existing.pre_stat / existing.open / existing.read / existing.eof / existing.post_stat / existing.path_stat / close`，并附 policy 或 marker 文件种类。保留最先失败的阶段和 errno，清理失败单列，不能被最后一次 close 覆盖。
3. 仅 policy_error 非零时，在正常 PID1 上下文、调用 panic 之前，先输出已经格式化的 `build, branch, op, errno` 和参数值。然后调用现有 `native_ufs_log_read_stats()`，使 rootio、warmsource、warmload 及摘要在此次 snapshot 之前进入 ring。已核对该函数只读软件计数和架构计数器，不访问 MMIO、不分配内存、不持有 transport lock。
4. 执行专用 `force_once`，返回可区分的结果。保留原子 busy 获取；最小实现只 try 一次，busy 就返回 `SKIPPED_BUSY`，不自旋、睡眠或重试。成功取得 busy 才消耗这次强制尝试，且所有退出路径均释放 busy。只跳过一秒 rate gate，保留日志开关、窗口范围、容量检查和原 UFS transport 锁、超时与出错处理。
5. 建议强制失败快照只保留最后64KiB的 ring 内容，正常8MiB容量不变。已有 `meizu_log_snapshot(out, cap, &total)` 支持 tail cap；header.total仍是ring累计值，header.bytes与dropped按实际retained计算。含4KiB header及至多4KiB前缀，总计最多18个4KiB块=72KiB，小于当前128KiB transport上限，因此最多一个 WRITE(10)。本次 policy/warm/版本诊断必须先打印，确保处在尾部。
6. 返回至少区分 `WRITTEN / SKIPPED_BUSY / SKIPPED_ONCE / NO_WINDOW / WRITE_FAILED`。更新原尝试时间/保存计数，使紧接着的普通 panic flush 不会绕过正常节流重复写入。最终 panic 使用同一标识及 `branch, op, errno, force_result`，即使持久日志被跳过，屏幕也能给出明确分支及结果。不得只在写入后打印诊断而期待本次 snapshot包含它。

持久性和边界必须说清楚：现有 `native_ufs_log_write` 没有 FUA，也没有 cache sync。`WRITTEN` 只表示现有写命令返回成功。若要求使用 `SAVED` 表示已要求介质落盘，应在写成功后最多调用一次现有 `native_ufs_log_sync()` 并单独记录结果；失败为 `SYNC_FAILED`。这样无需新写窗口或改 UFS 协议，但会多一个现有 SYNCHRONIZE CACHE 命令。不能在未 sync 时宣称断电持久保证。

上述64KiB方案的单个 WRITE(10) 既有超时为2秒；现有 sync 命令超时为10秒。但 `gHost.lock` 的 IOLockLock 等待没有 deadline，所以这些是命令超时和尝试次数边界，不是严格墙钟上限。最小方案保留锁语义；若之后需要严格墙钟限制，必须单独设计 transport try-lock，不能直接绕过锁。在 busy 分支立即panic时，也不能保证该次诊断已持久化；这正是 `SKIPPED_BUSY` 和可见panic标识必须保留的原因。

最小验证集合：

- 普通 dump 在一秒内仍节流；force 在同样条件下恰好尝试一次写入。
- busy时返回跳过且无I/O，不改变原持锁者；递归/再次force不重复写。
- force成功、open/capacity/write失败的每条退出路径都释放busy，并返回不同结果。
- 实际用于header的文本包含本次 `build/branch/op/errno` 和warm摘要；模拟超过64KiB时仍包含末尾标记，header与dropped正确。
- policy主错误不被诊断写入错误或close错误替代；最终panic能显示两者。
- 新host模型必须复制XNU的flag互斥规则：`O_NOFOLLOW_ANY | O_NOFOLLOW`拒绝22；实际合法组合通过。目标编译只能验证声明/类型，不能替代此语义测试。
- 设备验收读取header有效范围；确认R11独有标识、实际参数分支、准确失败op/errno和写入结果，而非使用旧raw尾部补全证据。

实施权由父代理分配；本报告没有修改生产代码。
