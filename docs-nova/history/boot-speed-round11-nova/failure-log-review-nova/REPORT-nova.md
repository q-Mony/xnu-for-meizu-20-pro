R11 失败日志与策略增量独立审阅：通过，无阻断项。

冻结源码：`kern_exec.c` 为 `62892df1cbfb216e2d5f847b26678f75012e590a080ff7a083303992778893d1`；`BootWatchdogPolicy.c` 为 `428e818dd4a6479a67f21d93aac1a36b325338e211860f60e888eae11149d4de`。测试后的实际文件与冻结哈希一致；Policy测试副本逐字节一致。

- 日志部分直接提取实际 logger、ring snapshot、首轮 prelude 和 policy 失败 caller，33/33项ASan/UBSan通过，其中包括原R7全部9项正常路径回归。普通限频、clean no-op、统计上报预算和正常路径不调用sync保持。
- force结果1..7分别覆盖。busy不夺取原持有者的资源；一次门闩在busy时也消耗。write及sync中的递归调用被挡住；取得的busy在所有出口释放。write失败不sync；sync失败不更新saved_total/write_ok；主policy errno在panic中保留。
- 强制快照最多保留64KiB，含最大前缀/header最多18个4KiB块。实际ring代码在未环绕、环绕及dropped饱和情况下返回正确尾部/header。只调用一次write API，成功write后最多sync一次。
- 实际prelude在首次snapshot前包含version11、invocation、warm参数和暖摘要。实际失败caller先输出已格式化的branch/op/errno与摘要，再force；panic含相同版本、原始错误和save结果。测试没有错误声称返回后的save值已存在于此前snapshot。
- Policy增量119项通过，其中93个实际调用边界各注入EIO，并让其后的cleanup再返回EACCES；最初errno和op保持。getter的四个int指针参数及const char*返回类型与caller匹配，空输出指针和重复调用重置也通过。
- 更新的独立mock使用真实XNU的互斥标志条件，同一真实356字节fixture在R10复现EINVAL22，在R11返回0。三处warm open仅保留O_NOFOLLOW_ANY。两种policy序列化、metadata/identity helper、目录权限/所有权允许条件与R10一致；已有inode不修改，cold已有文件仍拒绝。

本次没有扩大策略接收范围，没有修改生产、设备操作、挂载或完整内核构建。一次初始host运行失败源于新写的测试事件数组只能记32项，R7回归会写401次；已保存旧测试并把测试数组扩大到512，随后33项全部通过。初始失败不是生产代码失败，日志保留可追溯。

仍不能把修复EINVAL22当作已经解释屏幕上的17，也不能宣称R11已经提速。force在busy时会跳过；UFS锁等待没有deadline，所以尝试次数/字节上限不等于严格墙钟上限。最终设备结果仍需本轮候选实测。

机器结果：[integration-result-nova.json](<nova-root>/artifacts-nova/boot-speed-round11-nova/failure-log-review-nova/integration-result-nova.json)。日志测试：[33项结果](<nova-root>/artifacts-nova/boot-speed-round11-nova/failure-log-review-nova/run-20261029T220913573156Z-nova/result-nova.json)。Policy增量：[119项结果](<nova-root>/artifacts-nova/boot-speed-round11-nova/failure-log-review-nova/policy-delta-nova/result-nova.json)，以及[旧版本RED22](<nova-root>/artifacts-nova/boot-speed-round11-nova/failure-log-review-nova/policy-delta-nova/legacy-red-nova.log)。
