# R8 稳定性：Setup 回调等待与 SpringBoard 解码故障

2026-10-30。新增离线分析文件；未改生产代码、设备、旧报告或中央文档。

**这组发现不阻断暖 Data 的对照实验，但不能把暖启动实验当成稳定性已修复。** R8 有真实的 Setup watchdog：主线程在 `-[ProximitySetupController needsToResume]` 发出 `hasConnection:` 查询后无限等待回调。另一个独立故障是 SpringBoard 在 `libate.dylib` 的 `DecodeRow` 内发生读取异常。现有证据没有把这两条路径接到 lsd 全量重建上。

下一独立定位点是 `hasConnection:` 的异步回调完成链；不能通过放宽/缩短 watchdog 或无条件返回成功掩盖它。暖 Data 实验继续比较注册工作量、SB→Setup、Boot→Hello，并把 Setup/SpringBoard 超时和重启一并列为稳定性结果。

## 与旧轮次的差异

| 轮次 | Setup | SpringBoard | 能比较的窗口 |
| --- | --- | --- | --- |
| R6 | L2598 一次 armed；未见 Setup watchdog；L5909 是探针 deadline | 未见明确 fault/crash 或再次 spawn | 无时间戳前缀、无 Setup capture_ns；诊断保留到 age_us=20021467，不能制造精确 Setup→日志结束时间 |
| R7 | L2762 一次 armed；未见 Setup watchdog；L6124 是探针 deadline | 未见明确 fault/crash 或再次 spawn | 首次 Setup 日志前缀后约35.952秒 |
| R8 | L2756/5358/8069 三次 armed；L4686/7825/10531 三次 scene-create watchdog | L10604 fault、L10605退出、L10607明确crash，L10707重启pid282 | 首次Setup前缀后约212.632秒 |

R8 首次 Setup watchdog 在同种 CNTVCT 前缀相对指标的12.421秒，第二次 armed 在14.941秒，位于R7保留的对应相对窗口；R7没有保存同类事件。因此这是值得关注的稳定性差异。R8第二次watchdog在38.253秒、第三次99.693秒、SB fault107.207秒，超出R7日志窗口，不能据R7缺记录断言那些问题不会发生。R6/R7/R8都有chronod的scene-create watchdog，不能把所有同名错误称为新问题。

上述是不同轮次保留日志的对照，不是对R8改动的因果证明。console dropped=0不代表IPC诊断ring完整，exec-map也达到256条上限。`setup-retire reason=deadline`仅表示20秒采样窗口结束，不是进程退出。

## 两份真实崩溃报告的绑定

父代理从R8只读Data快照提供的两份 `.ips` 已复制到本目录 `crash-inputs-nova/`，并固定SHA256；后续分析不依赖Data挂载。

| 文件 | SHA256 | 与日志对应 |
| --- | --- | --- |
| Setup-1969-12-31-160341.0006.ips | `cf56fc59c2885139018a52dc9c2eaa0db2f0d590da7f648de899bacbeb42c0f7` | pid242，L4686，10秒allowance |
| Setup-1969-12-31-160407.0008.ips | `a7b25bd3a2e77d7d3effa938d379e3bffd1559bde4d90bb7e46ea549e4777976` | pid257，L7825，20秒allowance |

两份均为build20H392、bug_type509，termination code2343432205（0x8BADF00D）；PID、退出码、watchdog描述均与原日志一致。文件名和报告内1969年日期来自设备时钟，**不是分析或刷机的本地日期**。

两份stackshot都有实际共享缓存UUID `8f970452-c9e3-3e88-b114-feb1864bc9e0`，与已挂载同版本cache匹配；Setup UUID `9b4d9b5a-97c8-38d0-a692-dc686fa7bb4b` 也与源可执行文件匹配。R8日志L485 header为0x1a9a14000，slide为0x29a14000；没有套用R7 slide。IPS共享缓存帧按其S类镜像的未滑动基址加offset定位，独立于日志运行时地址。

符号化同时核对局部/导出符号及LC_FUNCTION_STARTS。总计1468个查询有精确函数匹配；45个最近符号未能确认所属函数，保留为未确认名称，不用于结论；64个共享缓存外地址保留原始位置。这里的统计包含重复查询，不是独立函数数。

## Setup 的确切等待路径

两次watchdog的主线程分别为2133、2343，均为 `com.apple.main-thread`、TH_WAIT；stackshot的waitInfo明确显示等待**本进程所属信号量**。相同调用链为：

```text
BuddySceneDelegate scene:willConnectToSession:options:
  → SetupController runWithScene:
  → SetupController _createNavInAirplaneMode:
  → ProximitySetupController needsToResume
  → dispatch_semaphore_wait(..., DISPATCH_TIME_FOREVER)
  → semaphore_wait_trap
```

共享库帧由真实stackshot直接符号化；Setup自身四帧分别为image offset `0x67c18`、`0xaf50`、`0xa094`、`0xafc9c`。单独的UUID匹配ObjC解析核对方法IMP和函数起点；返回地址减4后，调用点分别为 `runWithScene:`、`_createNavInAirplaneMode:`、`needsToResume`、`dispatch_semaphore_wait`。细节见 `setup-methods-nova/` 的工具与报告。

`needsToResume` 创建初值0的信号量，向 `self.client` 发 `hasConnection:` 并传入block，然后无限等待。block先向捕获的self发 `_setConnected:`，再对同一捕获的信号量调用 `dispatch_semaphore_signal`。该连接关系由实际指令、selector和indirect-symbol stub绑定确认，**不是凭方法名推测**。

真实stackshot还看到另一条TH_RUN工作线程在 `+[IPLanguageListManager preheat] → regionalVariantLanguagesForSystemLanguages → regionalVariantLanguagesForBaseLanguage → NSLocale` 路径：第一次是NSDictionary写入/locale解析，第二次是语言标准化及内存释放。它只是同一时刻的另一个工作线程，**没有证据它负责上述hasConnection回调或信号量signal**，不能把语言预热当成已确认根因。

日志探针进一步限定等待结果：

| 轮次 | 首个无限等待与完成证据 |
| --- | --- |
| R6 | L4287–4290选中并下核；L4650同进程tid2057发SIGNAL_BEGIN；L4651主线程RETURN result0，elapsed13.040246秒；L4763 SIGNAL_END |
| R7 | L4456–4459选中并下核；L4854同进程tid2102发SIGNAL_BEGIN；L4855主线程RETURN result0，elapsed13.040155秒；L4931 SIGNAL_END |
| R8 attempt1 | L4504–4507选中并下核；L4696 RETURN result14（KERN_ABORTED），elapsed9.301448秒，紧随watchdog终止；窗口内未记录SIGNAL_BEGIN/END |
| R8 attempt2 | L7173–7176选中并下核；L7813到20.027496秒关闭探针；此窗口内没有signal或成功return |
| R8 attempt3 | L9836–9839选中并下核；L10463到20.044150秒关闭探针；此窗口内没有signal或成功return |

事件编号依据 `sync_sema.c` 的枚举：1SELECT、2ASSERT、3BLOCK_CALL、4RETURN、5SIGNAL_BEGIN、6SIGNAL_END、7DESTROY、8CLOSE、9OFFCORE。event9记录的是把目标线程调度下核时的当前执行者，**不是信号发送者**。R8后两次20秒窗口之外未被探针覆盖，不能把窗口内无signal扩展为终身无回调。R6/R7的13.04秒是被选中等待的测量，不能拿来代表整个SB→Setup的150多秒。

实际回调为何迟到/未返回，尚未定位。最小下一步是在已确认的 `hasConnection:` 调用和其block完成边界记录少量起止/返回状态，并识别client的服务对端；对照同一等待在冷、暖Data下是否正常完成。应保留合法“有连接/无连接/查询失败”的区分，修复完成通知或依赖初始化，而非无条件宣告成功。

## 与 lsd 重建、捕获及 SpringBoard 的关系

两次Setup watchdog的同一stackshot中，lsd主线程439和containermanagerd主线程351在服务run loop；此前大量扫描工作的lsd worker448已处于workq等待。主线程的明确等待对象是Setup自身信号量，没有直接出现指向lsd或容器服务的阻塞链。因此现在不能将Setup watchdog解释为仍在执行那3838次容器调用；也不能排除更间接的系统负载影响。

前两次Setup watchdog发生在实际Data capture触发之前，不能由其实际写入/冻结引起。第三次watchdog和SpringBoard fault在capture返回后，时间相邻仍不证明因果。

SpringBoard L10604的运行时PC `0x1fd651a4c` 对应未滑动地址 `0x1d3c3da4c`，`/usr/lib/libate.dylib` 中 `DecodeRow(void*, unsigned long)+132`。镜像UUID `d185fd86-5348-34f7-8233-f8b565d3f1ba`，函数起点 `0x1d3c3d9c8` 与LC_FUNCTION_STARTS精确一致。故障指令原字节 `8003c03d`，反汇编为：

```asm
ldr q0, [x28]
```

日志报告far/sub `0x1047cd000`，紧接SpringBoard退出和明确crash。当前只有PC/LR等寄存器摘要，没有完整x28上下文或实际资源路径；也没有捕获后的完整SpringBoard crash report。因此只能确认解码函数发生读取异常，不能判断是坏资源、越界、映射错误或其他内存破坏，更不能将其等同UFS问题。导出快照早于该崩溃，不含其报告符合时间顺序，不证明CrashReporter失效。

暖Data只针对初始化成果复用。它有可能改变上述工作出现的时序，但当前没有依据承诺修复hasConnection等待或DecodeRow故障。父代理已独立验证导出/文件系统/缓存；本报告没有发现必须撤销暖Data机制试验的证据。新试验必须明确记录这些故障是否重现，不能仅凭最终Hello确认隐藏重启。

## 复现与交付范围

固定输入、逐线程符号、原始偏移、未确认名称和实际指令均在 [analysis-nova.json](<nova-root>/artifacts-nova/boot-speed-round8-nova/stability-analysis-nova/evidence-nova/analysis-nova.json)，SHA256 `59fc852824a7761ce0c5d52e6d07af5c9ed09bd30134333b7f3e139f89a1ec2d`。

[analyze-stability-nova.py](<nova-root>/artifacts-nova/boot-speed-round8-nova/stability-analysis-nova/analyze-stability-nova.py) 固定核验R6/R7/R8、两份ips及旧只读符号解析模块SHA；不执行旧main、不写旧pycache、输出拒绝覆盖。复算可传本目录下一个新的 `--output` 子目录。源映射、运行时UUID绑定、函数/导出核验与fault opcode检查实际执行通过。

本目录均为新增研究文件；设备、生产树与中央变更记录未修改。仅凭已有采样无法证明整个慢启动的唯一原因，本轮收口于可验证的下一独立修复点。
