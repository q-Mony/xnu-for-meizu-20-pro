# R8：启动时间、Data capture 与稳定性证据

2026-10-30。新文件，无旧版本。仅离线分析已保存的 R7/R8 日志；未操作设备、生产源码、旧解析结果或中央文档。

**R8 的 Data capture 成功；主要慢段仍在用户态启动阶段。** SpringBoard 成功 exec 捕获到首次 Setup armed 相隔 **161.579416303 秒**，R7 同指标为 152.523992969 秒。用户已确认看到 Hello，但本轮没有人工秒表总值或 Hello 首帧时间戳，不能据此报告 Boot→Hello 提速或退化。

R8 同时出现三次 Setup scene-create watchdog、一次 SpringBoard fault/crash 和随后重启。它们必须作为稳定性问题保留，不能只记录最后看到 Hello 就算无错误。

原件：[R8 日志](<nova-root>/logs-nova/boot-speed-round8-nova/candidate-run-nova/autolog-nova.txt)，SHA256 `dcf50ef3eaf2dc02157f62f5f04b752be9e5b3b29b40c881b5ab73ec3c9f81c9`。下面行号均指原始日志。

## 相同锚点对照

| 指标 | R7 秒 | R8 秒 | R8−R7 秒 | R8 行号 |
| --- | ---: | ---: | ---: | --- |
| root_device_resolve | 7.385559479 | 7.369095052 | -0.016464427 | L102→137 |
| root_mount | 0.018272812 | 0.018130312 | -0.000142500 | L138→157 |
| systemos_prepare | 0.155145729 | 0.155215990 | +0.000070261 | L178→212 |
| data_prepare | 0.005490990 | 0.005677448 | +0.000186458 | L213→228 |
| launchd 成功 exec→SpringBoard 成功 exec | 21.108157239 | 21.102992395 | -0.005164844 | L240→707 |
| SpringBoard 成功 exec→首次 Setup armed | 152.523992969 | 161.579416303 | +9.055423334 | L707→2756 |
| lsd 成功 exec→首次 Setup armed | 146.035655573 | 157.792883542 | +11.757227969 | L888→2756 |
| launchd 成功 exec→首次 Setup armed | 173.632150208 | 182.682408698 | +9.050258490 | L240→2756 |

四个阶段均 result=0，记录包含阶段起始日志成本。exec/armed 是内核捕获点，不是第一条用户指令、服务 ready 或画面可见。表中区间可能互相包含，不相加成启动预算。小数位是复算结果，不代表物理测量精度。

全部差值来自记录内部的 Mach 时间。R8 328 对显式 ticks/ns、R7 305 对全部满足 `ns=floor(ticks×10^9/19,200,000)`，所以本报告对 Mach 持续时长的换算有内部证据。没有将 CNTVCT 日志前缀减去 Mach capture_ns，也没有将绝对值写成从 Boot 菜单计时。L102 的 inline phase 由已有 R7 解析器按原始 P 前缀边界处理。

root_mount 短只说明被这对探针包围的挂载阶段短，不能推论后续全部磁盘访问都快。长用户态区间也不能直接归为 UFS 等待。本轮仍没有足够证据把 UFS 定为主要原因。

## Data capture

| 证据 | R8 结果 |
| --- | --- |
| 配置 | L211：post-Setup，delay_seconds=90，restore=0 |
| 触发 | L10471：capture_id=4013558640，since_setup_ns=90048936510 |
| ID 绑定 | 与 L2756 **第一次** Setup capture_ticks 相同；不是后两次 Setup |
| 结果与返回 | L10472–10473：status=0，header_uncertain=0，restore=0 |
| 数据大小 | logical=536870912，encoded=231147936，extents=152 |
| 整体捕获时长 | end_ticks−start_ticks=59,845,478，约 3.116951979 秒 |
| 冻结时长 | frozen_ticks=59,840,894，约 3.116713229 秒 |

触发记录位于首次 Setup 捕获后 **90.048936510 秒**；capture 内部 start 在首次 Setup 后约 **90.049005052 秒**。脚本验证一次配置、一次 fired、一次 result、一次 return、同 ID、源顺序、至少 90 秒及 frozen≤total。实际导出字节和文件系统有效性由父代理独立核对；日志中的成功状态不替代磁盘内容验证。

没有 Hello 首帧锚点，因此不能把“Setup 后 90 秒”写成“Hello 后 90 秒”，也不能从 Boot→Hello 中机械扣除这 3.12 秒。此候选在启动后增加了一次实际冻结，下一轮速度比较应明确 capture 是否开启。

## 应用注册与容器操作

按成功 exec 的 pid/taskgen 对齐身份，未用 bucket 编号代替身份。R8 lsd 为 pid97/gen272（L888），containermanagerd 为 pid63/gen234（L833），launchd 为 pid1/gen4（L240）。R7 容器服务虽也是 pid63，但 gen208；脚本分别核对。

首次 Setup 之前最后一个完整采样为 R8 dump16（L2677），R7 dump15（L2686）。两者并非同一 dump 序号，下面比较的是各轮首次 Setup 之前的边界快照。

| 调用类别 | R7 完成数 / errors | R8 完成数 / errors | R7 累计 trap 秒 | R8 累计 trap 秒 | R8 行号 |
| --- | --- | --- | ---: | ---: | --- |
| 容器发送，mode1 / id1073741824 | 3838 / 0 | 3838 / 0 | 0.128182187 | 0.136848333 | L2702 |
| 容器接收，mode2 / id0 | 3838 / 0 | 3838 / 0 | 41.724101250 | 34.993612031 | L2703 |
| launchd，mode3 / id1073742728 | 2550 / 2550 | 2550 / 2550 | 8.368242083 | 11.351849427 | L2699 |
| launchd，mode3 / id1073742628 | 2552 / 0 | 2552 / 0 | 8.923515208 | 10.182863333 | L2700 |

R8 Setup 后首个采样的容器计数仍各 3838（L4285–4286），R7 也相同（L4584–4585），因此计数在首次 Setup 两侧得到包围。R8 保留的全部容器 bucket 中未见非零 errors，也未保存 lsd/containermanagerd 的 xr 退出记录。这里的 errors 是 Mach trap 返回分类，不等同业务失败；`2550/2550` 两轮相同，不能认定为新失败。诊断 ring 有丢失，未见不代表从未发生。

trap 累计时长可以跨线程重叠，不能相加为 wall time，也不能和 CPU 时间相加。容器累计时长缩短约 6.73 秒，而 SB→首次 Setup 反而长约 9.06 秒，进一步说明不能用这一累计量独立解释整体速度。

**被统计的容器工作量并未减少。** 结合既有符号化中应用注册/容器查询路径，后续“保留完成初始化后的 Data”对照应验证这些调用是否减少，以及 SB→Setup 是否同时缩短；本轮计数本身不证明 3838 次请求全部冗余。

## 超时、崩溃与比较边界

| R8 来源行 | 证据 |
| --- | --- |
| L2756、L4686 | Setup pid242/gen914，首次 armed 后 `code=8badf00d`，scene-create watchdog，allowance 10 秒 |
| L5358、L7825 | Setup pid257/gen971，第二次 armed 后同类 watchdog，allowance 20 秒 |
| L8069、L10531 | Setup pid269/gen1021，第三次 armed 后同类 watchdog，allowance 20 秒 |
| L10604 | SpringBoard pid25/gen53/tid252，明确 `sbfault exc=1 code=1` |
| L10605、L10607 | SpringBoard `xr rv=11 ns=2 code=b`，随后明确 `Process[25] crashed: SpringBoard` |
| L10706–10707 | 再次请求 SpringBoard，返回 pid282，error=0 / exec_done=1 |

前两次 Setup watchdog 在 capture-fired L10471 **之前**，无法由这次实际导出写入或冻结解释。第三次 watchdog 及 SpringBoard 崩溃在 capture-return 之后；先后顺序不足以证明捕获导致崩溃。

R7 保留文本只有一次 Setup armed；未见相同的 Setup watchdog 或 SpringBoard fault/crash。按各轮同种 CNTVCT 日志前缀比较，R7 覆盖首次 Setup 后约 **35.952014427 秒**；R8 首次 Setup watchdog 在 **12.421447917 秒**，第二次 Setup armed 在 **14.940935 秒**。因此 R7 已覆盖首次失败和重启的对应相对时段，却没有保存相同记录，属于需要关注的稳定性差异。R8 第三次 Setup、后续 SpringBoard 崩溃超出 R7 保留时长，不能完整比较。R7 的 chronod 已有 scene-create watchdog，不应把所有 watchdog 一概称为新问题。

R7 L6124 的 setup-retire reason=deadline 是探针采样窗口结束，不作为 Setup 退出证据。本报告不据缺少后续 exec-map 推断无重启：R7 L1470 与 R8 L1420 均已达到 capture-limit=256。

## 完整性与复现

R8 原件 2,839,307 字节，NUL=0，capture 元数据 text hash/length 匹配，console ring dropped=0。R7 解析器得到 11,159 个有效标签、四对阶段、零 issues。源映射、去前缀正文、原字节副本均另存；旧解析器仅导入 analyze，未运行其 main，也禁止写 pycache。

console 完整性不代表 IPC ring 完整。R8 IPC dropped/overwritten 在 L10518 达到 328,619，L10718 降为 2,801，随后到 L11139 达到 169,033；脚本保留为两个计数区段，不跨 reset 相加。reset 之后的邻近日志已显示 SpringBoard pid282，但不把异步计数与任务身份强行作原子绑定。缺失事件仍不可用于证明未发生。

复算脚本：[analyze-round8-final-nova.py](<nova-root>/artifacts-nova/boot-speed-round8-nova/timing-analysis-nova/analyze-round8-final-nova.py)。默认输出已存在；复算时传入该 timing-analysis-nova 目录下的**新**输出子目录，脚本拒绝覆盖：

```sh
python3 -B artifacts-nova/boot-speed-round8-nova/timing-analysis-nova/analyze-round8-final-nova.py --output artifacts-nova/boot-speed-round8-nova/timing-analysis-nova/replay-2-nova
```

最终机器结果：[analysis-nova.json](<nova-root>/artifacts-nova/boot-speed-round8-nova/timing-analysis-nova/analysis-final-nova/analysis-nova.json)；输入及输出哈希：[provenance-nova.json](<nova-root>/artifacts-nova/boot-speed-round8-nova/timing-analysis-nova/analysis-final-nova/provenance-nova.json)。初稿脚本及 analysis-output-nova 保留，最终脚本修正 capture-limit 记录选择并补充进程事件对照，未覆盖初稿文件；中央变更记录由父代理统一追加。
