# Nova rootio 离线分析工具

2026-10-29 创建。此说明、分析脚本和自测脚本均为新文件，无旧文件。仅离线分析；不操作手机，不修改驱动或既有日志。自测全部是合成数据，不是真机结果。

工具：[analyze-rootio-nova.py](<nova-root>/artifacts-nova/boot-speed-round2-nova/analyze-rootio-nova.py)。Python 3 标准库即可运行。输出 JSON 到 stdout，保留输入 SHA256、原始行号、首末快照、每个区段的首末增量及相邻增量。

从 Nova 目录运行，用实际回收日志替换 `LOG_PATH`；将 stdout 保存到一个新文件，避免覆盖旧证据：

```sh
python3 -B artifacts-nova/boot-speed-round2-nova/analyze-rootio-nova.py LOG_PATH
python3 -B artifacts-nova/boot-speed-round2-nova/analyze-rootio-nova.py --strict LOG_PATH
python3 -B artifacts-nova/boot-speed-round2-nova/test-analyze-rootio-nova.py
```

可同时传多个日志，始终分别分析，不跨文件计算差值。`--synthetic` 将输出明确标为 `synthetic_not_device`；默认标为 `provided_log_not_device_validated`，不会把任意输入自动认证为真机证据。

## 字段和计算

对应 [NativeUfs.cpp:1348](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1348) 的 `nova: rootio`。marker 可以位于带时间、内核标签等前缀的行中；前缀中的同名字段不参与解析，同一物理行有多个完整 marker 也逐个处理。所有 14 个已知字段必须唯一、完整且是有效无符号十进制；`hz > 0`。

| 输出 | 含义 |
| --- | --- |
| `delta` | 两个已观测快照间全部累计计数及 ticks 的差；不虚构初始零快照 |
| `elapsed_ms` | `delta.ticks × 1000 / hz`，仅这两个快照之间 |
| `read_ms` / `verify_ms` / `copy_ms` | 各自累计 ticks 的差按相同 hz 换算 |
| `average_successful_command_bytes` | `delta.physical / delta.ok_cmds`；分母为 0 时为 null |
| `average_requested_bytes` | `delta.requested / delta.requests` |
| `byte_coverage` | requested、成功 physical、verified、copied 字节及 verified units 的增量；含几项明确命名的计数比率 |

physical 只在成功 hostRead4k 后计数，失败请求耗时可能包含在 read_ticks 内。read_ticks 覆盖 hostRead4k 的 host lock 等待、同步轮询、cache maintenance 和输出复制，并非纯介质时间。request 在 windowLock 之前计数，copy 为 writeBytes 的实际返回字节。计时是全局累计 elapsed duration，可能并发或跨快照边界，不能当 CPU 时间，不能相加后声称是 Boot→Hello，也不输出误导性的“启动占比”。

覆盖量会包含重复读取、对齐和 padding，不是读取了镜像多少唯一字节；这些全局、分别加载的快照非事务一致。短区间 ratio 可能大于 1，平均命令长度也可能受边界不一致影响；工具不据此判定硬件或校验故障。`bad_hash` 保留真实计数及增量。

## 不连续和退出状态

- 相邻完全相同的快照：记录 duplicate issue，忽略重复行，不制造 0 时间区间。
- 重新出现更早的完整快照：标记 replay，关闭当前区段，不跨重放拼接计算。
- hz 改变、ticks 回退、同 ticks 不同计数、任一累计计数回退：开启独立区段，记录原因及回退字段；不擅自解释为 reboot、计数 wrap 或自动换算时钟。
- 缺字段、重复字段、非法值、溢出、hz=0：该行拒绝，切断区段。下一条有效行成为新的起点，不能跨坏记录算增量。
- 单快照区段的 `first_to_last` 为 null；没有跨区段总计。

默认只要每个输入具有可比较区间且没有坏记录，就返回 0；区段切分和重复仍完整呈现在 JSON 中。任一输入不可读、没有可比较区间或含坏记录返回 2，同时仍输出其余可用区段。`--strict` 在任何 issue（含重复/切段）时也返回 2。需读取 JSON 的 `status` 和 `issues`，不能只凭存在结果文件认定日志干净。

自测覆盖前缀长日志、首末/相邻增量、同一行多条记录、重复和重放、ticks/计数/hz 不连续、坏行断链、零分母、累计时长大于区间时长，以及 CLI 多文件隔离。所有 fixtures 由自测脚本构造并显式标为 synthetic。
