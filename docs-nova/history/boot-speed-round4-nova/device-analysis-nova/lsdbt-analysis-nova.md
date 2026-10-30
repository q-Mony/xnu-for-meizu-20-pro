# 第四轮 lsd 栈观察耗时复核 — Nova

2026-10-29。新报告，无旧版本；只读分析，不修改生产代码、不操作设备。

结论：本轮所计量的 lsd 栈观察整体不是主要启动瓶颈。最终 generation=271 的 16 次获准采样全部完成；累计内核整数计时为 1 ms，即 [1,2) ms，单次最大为 0 ms，即 <1 ms。按本板 19.2 MHz 时基换算，34953 ticks 约 1.820469 ms，最大 3009 ticks 约 0.156719 ms，均值约 0.113779 ms；精细换算与内核毫秒输出一致，但本报告没有另做最终 payload 的时基审计。

这只降级 backtrace_user → cookie → printf 这一已测代码段，不能据总 Boot→Hello 212 秒排除全部诊断开销。计时不包含 admission 检查、统计发布、dump 本身、其他 IPC probe、持久日志及 IRQ/FIQ framebuffer 绘制。累计 elapsed 包含被抢占的可能，不是独占 CPU 时间；多槽汇总非同时快照，不能当启动墙钟预算相加。

## 原始证据与质量

- 日志：`logs-nova/boot-speed-round4-nova/candidate-run-nova/autolog-nova.txt`，SHA256 `ea8a091fabf4ecfca6f97465d347b539a7c10baa7806b3cc5ab3f5470f234ab0`。
- 日志共 1453771 字节，capture dropped=0，但含 425 个 NUL，全部位于 L7642 的单独尾行。因此不声称整份日志完整无损。解析直接读取原始 bytes，未删除 NUL、未拼接字段。
- 9 条 `nova: lsdbt-time` 汇总位于 L1117–2500，全部通过完整行格式检查，无 NUL、字段缺失或重复。16 条 trace header 位于 L962–2419，序列连续 1…16；204 条返回 PC 齐全，每条 header 声明的帧数与所见 frame 索引精确相符。
- L875 捕获 `/usr/libexec/lsd`，generation=271；所有采样与汇总属于这一代。最终 L2500：admitted=16 completed=16 pending=0 busy_slots=0，total_ticks=34953 max_ticks=3009。

## 预算与一致性

| 检查 | 本轮观察 | 结论 |
| --- | --- | --- |
| 完成数与原trace | 每次汇总completed均等于此前所见trace header数 | 没有把进行中trace伪计为完成的迹象 |
| 全局trace预算 | 16 / 32 | 未触顶 |
| 线程槽 | 8 / 16 | 未触顶 |
| 每线程/每opcode | 最大2 / 2 | 符合既有上限 |
| 每次帧数 | 最大16 / 16 | 全部error=0；truncated反映有界帧截断 |
| 汇总输出 | 9 / 31 | 没有无界输出迹象 |
| 累计计数/总时长/最大时长 | 同代单调，所有汇总pending=busy=0 | 没有代次混合或累加回退 |

10秒节流、首次capture后300秒截止、最多31条变化输出由生产源码与真实函数host测试覆盖。本轮汇总不携带自身时间戳，邻近rootio只能夹出粗时间窗，因此不能仅凭这9条日志独立证明每次10秒节流和300秒截止曾在真机触发。每条汇总的前后rootio行及原计数已保存到同名JSON，未把rootio时间当Boot起点。

## 后续诊断含义

当前约100秒的 lsd 等待，应优先沿已完成 Mach trap 聚合、容器扫描及服务依赖继续定位；没有依据为了提速关闭这一有界栈观察。其16次实测总成本不足2毫秒，量级上不解释分钟级等待。

持久日志仍可能在调用线程同步写UFS并与root读取竞争；屏幕状态仍可能在IRQ/FIQ中同步绘制。两者本轮都没有相应累计计时，维持“待测假设”，不能由栈观察成本小推导为已排除。
