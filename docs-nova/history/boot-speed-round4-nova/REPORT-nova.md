# Nova S03：定位 lsd 启动长等待（第四轮）

2026-10-29，新文件，无旧版本。状态：实现、host测试、构建、封装与独立审阅通过；一次真机到达Hello，总计212秒，第四轮日志已取回，设备留在Android B。此诊断轮未改善用户总计时。

第三轮数据SHA优化已证实同样23次cs4k从44.002降为20.209秒，但用户确认Boot→Hello仍212秒，苹果之后139秒。SpringBoard本轮对launchd和lsd的串行等待下界仍35.644+101.036秒。当前工作优先定位后者，未改变Data初始化、服务业务逻辑、超时或watchdog策略。

## 假设与可证伪信号

1. 数据库/应用/容器扫描包含大量短请求：如果成立，lsd已完成trap计数大、典型时长短，累计耗时显著；需要结合线程和阶段解释并发，不能把sum直接当wall。
2. 少数请求被对端长期阻塞：如果成立，少量长桶/最大耗时较大，pending op_age随时间增加，peer/message ID帮助定位对端；receive-only和endpoint未解析必须标未知，不能伪造历史请求。
3. 现有可缺页用户栈采样放大等待：如果成立，获准backtrace_user、cookie、printf的累计/最大耗时显著；若20余次采样总计很小，这个假设应降级。

## 测量范围

- 新增仅lsd的有界已完成Mach trap统计，begin起点独立保存，第一次pending→completed时记账；保留generation、错误次数、容量溢出信息。它衡量trap elapsed，可能包括调度/等待/内核处理，不等于业务RPC往返或服务执行CPU时间。
- 原pending行添加操作年龄与phase年龄，区分同一操作停顿和阶段更新。
- 计量既有lsd backtrace观察体，保持原本16线程/32trace预算和执行行为；进行中样本不当作已完成。
- 由原单autolog线程触发dump，每10秒、最多31次、首次capture起300秒截止；无逐请求输出。保留原rootio与SB等待记录。

## 基线和回退

保持第三轮完整数据校验、256KiB上层读取/128KiB底层合并、原bootargs、固件、ADT、trust cache、Data seed与服务语义。基线boot SHA256：`8ba3c90fe9078d4b5da04359a266596bdfb5a08026c19871065aee7e31533894`。

源文件修改前备份：`snapshot-nova/boot-speed-round4-nova/source-before-nova/`；第三轮build/kernel/log的独立APFS副本：同轮 `build-before-nova/`。仅使用固定已验证封装替换kernel，不改root分区或Android B内容。

## 验收

需要生产函数级host测试验证重复finish、nested、generation、计数/桶上限、输出节流与未完成样本；完成XNU构建/PAN/封装核验及独立审阅后再刷。设备成功到Hello后回收日志，用这些计数选择下一项实际优化。诊断版本不预先承诺提速。

## 实现与验证结果

生产仅修改mach_msg.c、sys_ulock.c和kern_exec.c三份文件。IPC真实生产生命周期10组ASan/UBSan场景通过；栈观察计量实际提取函数12组ASan/UBSan场景通过，含pthread进行中快照。内核构建/prelink/PAN成功，构建前后源码hash一致。最终raw/payload的11个相关函数逐字节一致，7个对象函数与raw除已记录重定位外一致，autolog真实BL调用两个dump已核实。根盘三个对象与第三轮逐字节一致，原policy/data SHA provider路由保持。

候选boot SHA256：`1b05a1b8f83ade84cd5348fa71216b80fb49b6868a7d738a9eaf2be068da33a9`；raw：`1a9b6c826881e52d4f484c200ffb6cdceeb1eb2a02079630275ac38a68d2361c`；payload：`7387a49f94fbbd29d2d6f06a4021f67aa3ed18733e9a499515a1a678d8764814`。固定封装的非kernel EFI、固件、bootargs、ADT、trust cache与Data seed不变。

解释限制：保留原有按成功exec的完整vnode basename为lsd进行识别，并非完整路径认证。bt dump短暂占用已有非阻塞admission gate，有可能使恰好同时尝试的调试trace跳过；不能称为零干扰。多槽统计非原子快照，累计elapsed可能并发或包含抢占；没有测量业务服务CPU时间。IPC统计在首次300秒预算到期后的首个可用心跳可输出一次final摘要，不延长收集或重置额度。host模型不替代手机验证。

## 已进入设备诊断

独立审阅 `independent-review-nova.md` 未发现阻断。2026-10-29 17:50:58完成boot_a刷入，17:54:30从Android B整分区回读A与候选一致、B保持原hash；17:54:42.419选择A并发出重启。一次前置枚举校验失败时流程在任何后续切槽前停止，重新只读确认唯一预期设备后继续；未绕过设备身份校验。

用户已报告本轮计时，详见下节。第三轮已验证镜像保留为回退；详细设备操作在 `logs-nova/boot-speed-round4-nova/`。


## 真机结果与日志分析（2026-10-30更新）

用户报告2026-10-29 23:42:00从Boot菜单开始，76秒出现苹果，随后136秒到Hello：总计212秒（3分32秒），与第三轮相同。依据用户确认已返回Fastboot，23:49核对预期设备/A槽后切回Android B；23:52整分区核验boot_a仍与第四轮候选一致、boot_b哈希不变。该结果只证明一次到达Hello；日志仍有两个Setup scene-create watchdog和一个chronod watchdog，S03尚未解决。

原始日志1,453,771字节，SHA256 `ea8a091fabf4ecfca6f97465d347b539a7c10baa7806b3cc5ab3f5470f234ab0`。dropped=0，但末尾独立行含425个NUL，未删除或拼接修复。24条rootio、22组IPC快照及9条栈计量记录严格解析成功；这不证明整份日志完整无损。

| 观察 | 结果 | 含义 |
| --- | --- | --- |
| 相同23项cs4k | total20.442秒、read11.997秒、hash8.417秒 | 第三轮为20.209/11.761/8.418秒，读取与SHA收益保持 |
| 早期根盘认证 | verify3.627289秒，bad_hash=0 | 第三轮3.632708秒；未观察到认证回归 |
| SpringBoard等待 | 同线程launchd≥35.421秒、lsd≥97.565秒 | 串行采样下界132.986秒，不能当完整独占预算 |
| 包围lsd等待的101.616秒窗口 | 根盘read1.627591秒、verify0.244891秒、copy0.024225秒 | 加大读取已不是剩余百秒等待的主要方向 |
| 主等待内部80.191秒 | 完成24,101次trap，全部<1秒 | 支持大量短调用持续执行；不能排除未完成调用、用户态工作或调度等待 |
| lsd已获准栈观察 | 16次，总约1.820469毫秒 | 所计量的backtrace/cookie/printf不是分钟级瓶颈 |

IPC观察到大量容器与launchd交互。两个launchd ID在内部窗口各完成2052次，其中0x40000388全为非零trap返回；当前未保存lsd具体返回码，不能当成业务失败。其他Setup/Budd记录存在MACH_RCV_TOO_LARGE之后正常接收的路径，只能用来提示扩容可能性，不能外推lsd全量码。32桶中14桶被不同内核ID占据，overflow保留总量但限制服务归因。

当前优先评估重复Data初始化和其他未计量的诊断显示开销。未经验证，不跳过数据库初始化、不伪造服务成功、不直接启用Data持久化。若再补IPC证据，应定向记录返回码并为服务保留聚合容量，避免泛增日志。尚无第五轮镜像或剩余等待修复结果。

证据：`logs-nova/boot-speed-round4-nova/user-observation-nova.json`、`postrun-device-state-nova.json`、`candidate-run-nova/`；本轮 `device-analysis-nova/` 中的log-quality、read-path-comparison、service-rootio-windows、lsdipc-analysis、lsdbt-analysis报告及JSON。旧版文档已保存到本轮snapshot。
