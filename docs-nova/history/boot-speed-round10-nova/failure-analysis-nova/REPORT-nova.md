R10 失败日志：仍然只有首份快照，无法从正文证明执行的是 R10 策略分支。

正文 SHA256 为 `4da5795493ab13556ed2d569295ac033f12702ad111559d6c63f212bf0ee5fc7`，24,627 字节、192 行。最后时间戳为 **35.067279219 秒**，停在 `blog-status dump=1` 后的 `cpufreq sample_id=1 phase=autolog-once`。这是体系计数器时间轴位置，不是用户从 Boot 菜单计时的用时。

- header 的 ring total=24,454、bytes=24,627、dropped=0。差值 173 字节仍是合成的 boot-video 前缀。正文完全等于 header 指定的有效范围。
- 本轮快照含 header 共 8 块，结束于 offset 32768。后面的 33,521,664 字节与 R9、R8 的原始窗口逐字节相同；其中的旧 heartbeat/rootio 等记录不能当成本轮事件。
- Data 的 512 MiB 设备仍选择 xid112、descriptor7，primary xid17，与同一份 R8 暖快照相符。没有保存 warm status/digest、Data owner 准备结果或 policy 阶段回执。
- root_device_resolve 耗时 16.258931302 秒，root_mount 耗时 0.018343646 秒。systemos_prepare 在35.064711666秒开始，但没有保存结束记录。没有用户态或 Hello 锚点，不能评价本轮提速。

R9、R10 正文并非同一份字节：计数器、VM地址和引导统计都有变化，支持“新一次运行”的判断。但 R8/R9/R10 的 Darwin 版本字符串完全相同，当前正文也没有 R10 独有的 runtime/build/branch 标记。地址变化和 wired pages 相差一页不能作为新策略代码已执行的证据。父代理报告 A 分区完整回读匹配 `995a2e784c5cea35280f71171629b16e6fb431a699ca9bec363224c60a120c2f`、B 未改变；本审阅者未操作设备，包/启动链核查由另一代理负责。

当前源码能解释保存缺口：autolog 先 snapshot/write，之后才打印 autologio、rootio、warmsource、warmload 和 heartbeat。`meizu_autolog_flush()` 只是普通 dump，仍受 busy 和“上次尝试结束后至少一秒”的限制。全局 `panic()` wrapper 确实调用这个普通 flush，但它输出的是 `printf("meizu: PANIC %s", str)`，所以只存未展开的格式字符串，不能替代已经格式化的实际 errno 记录。

没有 panic 时刻、flush 返回状态或后续 I/O 结果，因此不能证明此次确切原因是一秒限频。busy 跳过、限频跳过及后续未完成等在当前日志里无法区分。

需更正此前 R10 独立审阅的接口兼容结论：实现代理发现、此处已只读核对，XNU `vfs_vnops.c:411` 明确拒绝 `O_NOFOLLOW_ANY` 与 `O_NOFOLLOW` 同用，返回 EINVAL=22。R10 目录 open 和两个 openat 形式均包含该组合。此前 host shim 把这个组合当成合法输入，因此 3,933 项主机测试和真实头文件编译通过，没有覆盖这条内核运行时规则。原报告作为历史证据保留，其“API兼容通过”结论已被此发现取代。

这项确定缺陷本身预期是 22，不能解释父代理转述的屏幕 errno17。当前正文没有保存 panic，也不能证明 cold/warm 的实际分支；应先补齐下面的诊断合同，避免再凭同一个17推断原因。

R11 最小诊断建议见 [r11-diagnostic-contract-nova.md](<nova-root>/artifacts-nova/boot-speed-round10-nova/failure-analysis-nova/r11-diagnostic-contract-nova.md)。本轮只创建报告和本地分析器，没有改生产、刷机、挂载或构建。

机器证据：[evidence-nova.json](<nova-root>/artifacts-nova/boot-speed-round10-nova/failure-analysis-nova/evidence-nova.json)。
