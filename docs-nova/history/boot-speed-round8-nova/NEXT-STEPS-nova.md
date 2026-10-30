# R8已回收，继续入口

2026-10-30。用户本轮Fastboot确认已处理，04:29切回Android B并核对A/B。手机当前Android B，**不要再次执行R8待Hello/待Fastboot/回收流程**。

R8 boot A `0a0cff5e8154d80af23f863dd31acf4b3b9645408a1ee826261bd012d9e5409f`；B原值`7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2`。后续设备动作仍需要新鲜核对身份、槽位、A/B和GPT，持续用户授权有效，不重复问刷机权限。

回收完成：log SHA dcf50ef3eaf2dc02157f62f5f04b752be9e5b3b29b40c881b5ab73ec3c9f81c9；export SHA 7e6cc19843c0ee28eb8f1891a7d53e074688743902ef9ccf01b20d1a48e57140。完整证据见delivery-manifest-nova.json与filesystem-check-nova/REPORT-nova.md。reader原validation中的NOT_FILESYSTEM_VALIDATED是当时的分阶段状态，后续独立fsck-result返回0，不覆盖原始reader输出。

时间分析：SB exec→首次Setup armed161.579416303秒（R7 152.523992969秒）；root resolve7.369秒，mount0.018秒；容器请求仍3838。Setup不等于Hello首帧，不能报Boot→Hello实测提速。

捕获：Setup后90.049秒开始，冻结3.116713229秒，status0/header_uncertain0。fsck_apfs -n通过，LS csstore约10.4MB、MCM SQLite完整性检查通过且558个登记条目。只读目录枚举有权限遗漏，报告保留限制。snapshot不是应用事务一致性保证，未自动恢复。

当前本地快照可能仍挂载在filesystem-check-nova/mounted-data-nova，使用hdiutil info按captured-data-raw-nova.img backing查询实时设备号后才可卸载。其他会话的挂载保持不动。结束应detach本镜像并复核raw SHA。

接下来：
1. cpu_probe_impl比较外部export的UEFI预加载与内核pre-publication方案。不能在APFS探测后替换整盘，不能在matching线程等root media，不能用依赖更晚launchd的log_open。单次try-open若无法确保命中就不能充当有效warm test。
2. 选最小确定性实现，保持mode0、Data UUID和OS/SystemOS验证，明确expected payload/logical hashes与回退边界。不得盲开mode1/2或扩大boot分区。
3. lsd_cpu_accounting继续Setup两份真实ips和SB DecodeRow fault分析；R8前两次Setup超时发生采集前，不能归因3.1秒保存冻结。
4. 做一次真正暖Data实机对照，验证warm loaded日志/实际Data hash、请求工作量、SB→Setup时长及稳定性；没有实测前不宣称修复。

R8生产源冻结；下一轮变更先备份到snapshot-nova，输出新目录和新清单。既有export完整备份及R8成功快照均保留。开发计划P02/S03仍进行中。
