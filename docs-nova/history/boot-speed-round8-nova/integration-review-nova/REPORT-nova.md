# R8 capture-only 集成独立审核

新文件，无旧版本。审核者仅写本目录内报告、抽取测试、host可执行文件和结果；没有编辑生产文件、构建整kernel、挂载/恢复快照或操作设备。其他代理正在同一工作区工作，未回退其改动。

**结论：所审冻结实现和离线reader未发现阻断项。** 36项实际函数抽取host测试通过，C++编码器→Python reader→既有SparseDisk导入互通通过。结论只覆盖当前capture-only范围，不证明真实设备捕获、APFS一致性或启动提速。

## 固定输入

| 文件 | SHA256 |
|---|---|
| XNU `bsd/kern/kern_exec.c` | `9723d2e7d8104ac9c29702db785c2787ceedf49a552743f466892828c9537bc8` |
| `NativeRamBlock.cpp` | `a0a1ba36f6d73ac11dc9e8348bd71be45339c09356c7c99369f202b9f517e58f` |
| `WarmCapture.h` | `4c1faa1c54d47a6d39b5c58ee1c152017f3e24e34e0053350a687469b5e2c9a2` |
| `read-data-capture-nova.py` | `5b8dfb6d96c1956b37dcbaaa1e16a086096136a88e3b37ef7c39889538fb824f` |
| `SparseDisk.h` | `5ab6516488b7daaef88e788fffef17195e7d2f74746da2206c6d9ed3a63c8c74` |

NativeRamBlock变更相对 `snapshot-nova/boot-speed-round8-nova/data-capture-before-nova/NativeRamBlock-before-nova.cpp` 审阅，避免把既有seed诊断行误归R8。`git diff --check`无问题。

## 已执行的独立测试

全部从Nova根目录执行；被测函数由脚本从真实生产源码原文提取，不手工重写。host使用ASan+UBSan。

| 命令 | 结果 | 证据 |
|---|---|---|
| `python3 artifacts-nova/boot-speed-round8-nova/integration-review-nova/test-capture-trigger-nova.py` | 14/14 PASS | `trigger-test-results-nova.json` |
| `python3 artifacts-nova/boot-speed-round8-nova/integration-review-nova/test-capture-wrapper-nova.py` | 22/22 PASS | `wrapper-test-results-nova.json` |
| `python3 artifacts-nova/boot-speed-round8-nova/integration-review-nova/check-capture-interoperability-nova.py` | PASS_FORMAT_INTEROPERABILITY_NOT_RUNTIME | `capture-interoperability-results-nova.json` |

触发器测试详见 `trigger-review-nova.md`。实际运行 `meizu_setup_capture`、`nova_data_capture_poll`、完整autolog函数的一圈。验证默认关闭、错误参数、无Setup、90秒边界、时钟回退、锁忙、成功/失败均仅一次、后继Setup不推迟首次anchor、真实路径身份过滤和无锁调用。Setup引用20秒后退出不影响独立anchor。

wrapper测试实际运行 `warmCaptureEnabled`、`NativeRamBlock::captureWarm` 和外层C入口，并调用真实 `WarmCapture.h`；IOKit/UFS环境用带顺序断言的host stub。覆盖disabled/wrongarg/wrongpartition/noID、无选中实例/歧义、分配失败/无gate、offline/unattached/Data错误、mode1/2/dense/isBig/direct/overlay/persist-state拒绝、export容量/GPT拒绝、成功与再次调用。验证分配/释放与摘要输出在gate外，UFS调用在gate内，锁获取释放平衡，拒绝路径不写介质。

测试适配中的warnings禁用仅用于host中可见定义的weak全局地址、CommonCrypto deprecated声明及提取的旧函数未使用局部；没有修改被测函数。真正IOKit实例生命周期、scheduler和硬件DMA不由host stub模拟。

Data作者的独立生产头测试已读代码及结果，未重复运行已通过的大套件：147 captures、1,173,279 assertions，包含每次write/sync故障、半写、永久失败、最后flush失败但介质已呈现commit、布局边界、随机稀疏内容与SourceChanged。`host-tests-nova.log` 为PASS；目标对象编译日志为空且作者报告ARM64目标编译通过。此处不将host通过替代最终kernel链接/封装门禁。

## 格式互通

输入是作者真实 `WarmCapture.h` 生成的256MiB fixture，SHA256 `6dfacf1522a27e2ad1b519e3c3d06199adbbc607bff023e0cc0901cc83d95e7b`。用fixture自己的seed/id生成的ack文件明确命名为 `synthetic-fixture-ack-not-runtime-nova.txt`，**不是真机成功日志**。

父任务的实际Python `decode()`接受header+payload；随后实际生产 `ramPackedLayout`/`ramSparseImport` 导入，512MiB逻辑盘的131072页逐页与fixture规定的内容比较。结果为3个extent、83个非零页、encoded340048字节。Python与C++重建逻辑盘SHA256均为：

`c1546acc5a28e44764b89308c65e76eb40268b83d4def7a2ebd619c26e0bf06d`

静态核对：NVDCAP1 NUL结尾、v1、4096字节header、state1/flags1、payload offset4096、seed/hash字段64/96/128、reserved零值、最后padding零值均一致。reader先清零128..159再重算headerSHA，与encoder构造次序一致。cold seed绑定是完整逻辑盘hash，和NativeRamBlock启动时的hash语义一致；不是错误地拿packed文件hash代替逻辑盘hash。

reader要求完整256MiB、严格extent长度/对齐/不重叠/全量消费、header/payloadSHA、固定cold seed与唯一成功trigger-return。仅导出新目录，不mount/restore；输出明确 `clean_filesystem_proven=false`。这些检查不证明APFS或SQLite事务一致。

## 生产差异审核

- bootarg+分区名限制开启；仅seed选择且开关开启才retain。无效/重复选择释放局部引用；单一全局pin维持Data实例生存期。CAS发布已使用ACQ_REL/ACQUIRE；不同实例会置ambiguous并拒绝capture。永久pin是启用实验时的boot生命周期引用，不会在默认关闭路径新增。
- capture确认online、ready、storeAttached、Data成功，拒绝mode1/2、direct、overlay、isBig和非sparse形态。只使用既有export API，没有改GPT/分区布局、UFS写窗口、signed root、Data seed或恢复路径。
- stage与header为同一预分配buffer中不相交的范围，分配在gate前；同一gate涵盖plan及整个流式导出。沿既有gate→gHost锁顺序，无持VFS/Setup/日志锁调用。释放gate之后才更新摘要/IOFree/IOLog。
- plan限制512MiB逻辑盘、256MiB窗口并扣除外层header；每条extent和数据大小64位计算有上界。stream额外限制不能写过plan的encoded长度，完整长度/数量再比对；最后一块填零。正常调用源稳定由gate提供，SourceChanged检查不是绕开gate的替代品。
- 提交顺序为invalid header+sync→payload+sync→commit header+sync。失败尽力revoke，若介质继续拒绝则标headerUncertain；最后commit/flush失败即使头物理可见也返回非0，reader要求匹配成功日志。保留这一“不能保证失败后磁盘头必无效”的真实语义。
- GPU导出互斥依靠候选最终符号门禁：禁止已定义`_boot_log_export_put`，同时要求capture/poll有定义；仅每条UFS命令锁不能替代这个门禁。当前R7基线没有GPU writer定义，R8最终仍须检查。

## 仍需保留的限制和后续门禁

1. **无整次capture的总墙钟deadline。** 512MiB扫描/256MiB输出是容量界，单条WRITE可达2秒、SYNC可达10秒；累计可能明显阻塞Data。正常情况下记下start/end/frozen_ticks，不能把容量上界称为固定时限。本轮先延迟90秒再一次性capture，且不测试restore；这个已知实验限制应在设备测量说明中保留。
2. Setup成功exec后90秒不是Hello完成证明；真实Boot→Hello数据不应被推导为这个marker。没有成功真实capture与完整日志前，不能宣称已提取暖Data。
3. `weak_import`允许空引用检测，但默认Darwin链接仍拒绝完全缺失provider。host空符号测试显式允许唯一未解析符号才验证-1路径；最终kernel不可因此放宽capture/poll必须已定义的门禁。
4. 设备层仍需父任务完成最终kernel/包的符号与内容校验、分区和回滚检查。此审核未执行设备动作；测试通过不是刷入结果。

上述边界不构成当前capture-only实现的代码阻断。后续若启用restore、持久化mode或更改capture锁范围，需重新审核。
