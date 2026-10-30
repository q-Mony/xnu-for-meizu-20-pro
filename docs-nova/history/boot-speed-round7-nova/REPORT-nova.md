# 第七轮：内核时间戳诊断

状态：用户确认第七轮到达Hello，已回Android B取回并完成时间戳日志分析。

新增日志CNTVCT P/L/R时间戳；root_device_resolve、root_mount、systemos_prepare、data_prepare有mach_absolute起止与返回码。exec捕获点表示成功exec后的捕获，不冒认服务ready/Hello。autolog记录累计copy/write/attempt及clockcal取样区间，重叠成本不能相加成启动预算。

保留第六轮频率策略、SMP、根盘读取/认证、Data和watchdog行为；屏幕输出文字与启动参数不变。全部非kernel EFI及固件/header/DTB/AVB区间和R6相同。

正式boot SHA256 `96fc42236df897138936a2a9151735c96afdaeb9539786336f09d655da303d2c`；raw `2048a7352b341a66010911fcde791f7b17e7e68b2d463266d453c5a8aa491f74`；payload `75cab5a197d104085f9d569f097acc66bea710f1257cc6375346fee0602f76f0`。

首候选因clockcal汇编未生成CNTVCT读取而拒绝，未刷。修正版最终机器码确认两次CNTVCT读取夹住mach调用；23个对象→raw函数、28个raw→payload函数及28条调用绑定通过，8个既有对象与R6一致。console19项enabled/2项disabled及负控、autolog9项、parser26项通过。

原始日志与剥离前缀副本分别保存并附字节映射。报表区分明确阶段耗时与相邻有效timestamp标签间隔，后者不是前一函数耗时。旧R6回放保持无新增标签时间；真实R7耗时排名已完成，见下述结果。Boot点击、苹果首帧、Hello首帧仍无可信内核锚点，不用手工分段计时补造结果。

详细绑定见 delivery-manifest-nova.json。2026-10-30 03:22:27只刷boot_a，03:23:11在Android B确认整分区哈希与候选一致且B未变，03:23:39核对Fastboot A后重启。主机重启时刻不是Boot菜单点击时刻；03:28按用户Fastboot确认返回Android B，03:29核对A/B哈希并读取日志；已完成真实耗时排名，仍无Boot→Hello总时间或提速结论。


## 第七轮实测结果

[完整分析报告](<nova-root>/artifacts-nova/boot-speed-round7-nova/device-analysis-nova/synthesis-final-nova/REPORT-nova.md) · [带相对秒数的内核日志](<nova-root>/artifacts-nova/boot-speed-round7-nova/device-analysis-nova/kernel-relative-timestamps-nova.txt)

| 测量区间 | 耗时 |
| --- | ---: |
| SpringBoard → Setup 成功启动捕获 | 152.523993秒 |
| launchd → SpringBoard 成功启动捕获 | 21.108157秒 |
| 根设备查找 | 7.385559秒 |
| 根卷挂载调用 | 0.018273秒 |
| SystemOS准备 | 0.155146秒 |
| Data准备 | 0.005491秒 |

日志累计201次写盘成功，write0.360893秒；快照完整attempt0.377319秒（有重叠，不相加）。lsd长等待附近约91秒的rootio采样窗口中read增量仅0.920149秒、verify0.211464秒；该计量未覆盖所有UFS锁等待/排队，不能完全排除整个存储栈。

主要排查方向已收窄到SpringBoard等待lsd，以及lsd与containermanagerd/launchd的重复通信。R7 lsd在70.286144秒观察窗记账24.959823秒CPU；未记到CPU的其余时间不直接归因为睡眠或低频。重复容器操作在R6/R7均3838次，尚未证明冗余，下一步定位具体调用栈和请求类别后再修改行为。

保留6270个有效标签，四阶段完整。真实L102内嵌阶段begin触发离线parser边界修复；29项测试及独立原始核算通过，最终issues=0，未因此改内核或再次刷机。console dropped/NUL=0；sbipc诊断自身丢弃267031条，exec捕获达到256上限。Setup捕获不是Hello首帧，218.124秒首末标签覆盖不是Boot→Hello。原始日志SHA256为01e6b73d77676d6436c3d898dc9671ce464bcb422d6d8ffb92ed0b735147420a。
