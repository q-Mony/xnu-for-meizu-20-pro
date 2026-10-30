# R7 根盘与自动日志成本核验

新文件，无旧版。只读本轮日志与源码；未操作设备、未改生产代码或主 parser。

本轮证据支持把下一步重点放在用户态服务等待。已测量的根盘传输、块认证和自动日志快照／写盘成本，无法解释服务区间里的百秒停留；但这不等于排除整个 UFS 路径，因为请求排队、`windowLock` 等待及全部初始化并未纳入这些计数。

## 输入与交叉验证

原件 [autolog-nova.txt](<nova-root>/logs-nova/boot-speed-round7-nova/candidate-run-nova/autolog-nova.txt)，1,483,736 bytes，SHA256 `01e6b73d77676d6436c3d898dc9671ce464bcb422d6d8ffb92ed0b735147420a`。文本／32 MiB 原始窗口哈希均与 capture 元数据一致；dropped=0、NUL=0。捕获时间不是 Hello 时刻。

[独立分析脚本](<nova-root>/artifacts-nova/boot-speed-round7-nova/device-analysis-nova/storage-logio-nova/analyze-storage-logio-nova.py) 从完整 P 前缀边界读取协议记录，保留原始行号与字节偏移，恢复 L102 内嵌的 root_device begin。与 `round7-final-analysis-nova` 交叉核验通过：6270 个完整前缀、8 个 phase 事件、21 个 autologio 样本、23 次 cs4k 总量及 rootio 首末增量均一致；主 parser issues=0。全部累计字段单调，所有保留的 bad_hash、clock_errors 均为0。

可重放命令（cwd 为 Nova）：

```sh
python3 artifacts-nova/boot-speed-round7-nova/device-analysis-nova/storage-logio-nova/analyze-storage-logio-nova.py
```

机器可读全记录、原始值、外围采样与校验结果见 [storage-logio-analysis-nova.json](<nova-root>/artifacts-nova/boot-speed-round7-nova/device-analysis-nova/storage-logio-nova/storage-logio-analysis-nova.json)。没有 Boot 菜单、苹果首次可见与 Hello 首次可见的精确日志锚点，因此本报告不推断本轮 Boot→Hello 耗时或提速比例。

## 四个明确阶段

仅按同一 phase/id 内部的 `mach_absolute` ns 相减；包含 begin 打印成本。

| 阶段 | 原始行 | 耗时（秒） | 区间内成本采样 |
| --- | --- | ---: | --- |
| root_device_resolve | 102→137 | 7.385559479 | 无 |
| root_mount | 138→157 | 0.018272812 | 无 |
| systemos_prepare | 178→211 | 0.155145729 | autologio L207；rootio L209 |
| data_prepare | 212→227 | 0.005490990 | 无 |

root_device 内 L105→106 的相邻 CNTVCT 标签间隔为5.879495052秒：从 `ktriage_register_subsystem_strings` 日志到 `AppleFSCompressionTypeZlib load succeeded` 日志。不能据此把间隔归给前一个函数，也不能认定期间无日志字节。该7.386秒是后续早期路径优化项，但缺少边界 rootio 差值，当前无法归因给 UFS。

## 成本总量

| 计量 | 最后记录或范围 | 测得的累计成本 |
| --- | --- | --- |
| rootio | L6186 | 读取7.485871667秒；块认证4.259832604秒；拷贝0.635666198秒 |
| cs4k | L265–474，23次 | total 19.344秒，其中 vn_rdwr 10.911秒、hash 8.405秒 |
| autologio | L6206，201次写盘均成功 | attempt 0.377318854秒，其中 copy 0.015657239秒、write 0.360893125秒；最大单次write 0.040944322秒 |

rootio 最终35717个命令均成功，实际读取2,934,652,928 bytes，请求／拷贝2,663,285,760 bytes，bad_hash=0。L209→6186 的 CNTPCT 采样窗口201.556781458秒，对应增量读取7.398256510秒、认证4.250652708秒、拷贝0.634892969秒。L2358 的 cmds=32707、ok_cmds=32706 是逐字段非原子快照，随后恢复相等，不能仅凭该行判定失败。

cs4k 记录2,724,626,432 bytes、10403次读取，全部完成于上述四个 phase 之后。它只报告成功且至少16 MiB的转换，并有64条配额；毫秒是向下取整后求和，不能代表所有签名工作。`vn_rdwr` 内含文件系统、底层读取、验证、拷贝及等待，因此与 rootio 有重叠。

autologio 总写入82,518,016 bytes。copy/write 是 attempt 的组成部分，三者不能相加；attempt 也不包含随后 report/clockcal 输出、全部诊断格式化／屏幕刷新等成本。因此可说“测得的快照与写盘不到0.4秒”，不能说“全部日志成本不到0.4秒”。

## 长等待附近的成本增量

按同一 pending 身份的首末观察行，分别选其前后最近的累计采样。以下是日志顺序上的外围采样窗口，不是精确服务区间或严格上界；每种计量只在自身时钟域相减。

| 服务 pending 观察 | rootio 外围窗口 | 读取／认证／拷贝增量（秒） | autologio 外围窗口 | attempt／write增量（秒） |
| --- | --- | --- | --- | --- |
| launchd，L965→1569，age 4486→35090ms | L934→1743，40.638252448秒 | 1.242668906 / 0.276798021 / 0.035480573 | L962→1756，40.791532187秒 | 0.011706771 / 0.009882605 |
| lsd，L1759→2463，age 8462→90060ms | L1743→2523，91.353281354秒 | 0.920148646 / 0.211463802 / 0.021062656 | L1756→2646，91.806618490秒 | 0.059091667 / 0.052700104 |

lsd 身份为 pid97/taskgen272，等待方 pid26/tid253/seq2514。此长区间没有 cs4k 完成记录，但不能由此排除小文件或尚未完成的签名工作。pending age 的采样早于该行输出，90.060秒是观察值，不作为严格等待下界。

## 覆盖范围与下一步

- [NativeUfs.cpp:1343](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1343) 的计时源是 CNTPCT；read/verify/copy 分别包住 [hostRead4k](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1721)、`vr_verify_block`、`buffer->writeBytes`。它与日志前缀的 CNTVCT、phase 的 Mach ns 分开处理。
- [RootWindowRead.h:29](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootWindowRead.h:29) 在 `ops.read` 前取得 windowLock，因此锁等待不在 read_ticks 内。根盘计数也未覆盖所有初始化和上层排队。
- [ubc_subr.c:5893](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/ubc_subr.c:5893) 用 Mach 时间包住 `vn_rdwr`；cs4k 的门槛／配额见5954行起。
- [kern_exec.c:7765](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c:7765) 的自动日志 attempt 在获得busy后开始，节流／clean跳过及后续report不计入。

优先继续 lsd/launchd 的执行、调度与 IPC 回复链分析，用同一实例身份连接 CPU、阻塞与请求进展；root_device 的7.386秒保留为次级项。目前没有证据把长启动主要归因于已计量的UFS传输或自动日志写盘，也没有依据承诺继续放大读取块能消除百秒等待。以上组件有重叠，不能相加拼成开机总预算。
