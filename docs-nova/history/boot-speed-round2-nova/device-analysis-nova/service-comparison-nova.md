# Nova 第二轮候选：签名转换、服务等待与用户计时对照

2026-10-29。本文件及配套 JSON 为新文件，无旧文件；仅离线分析，未修改生产代码、计划、CHANGELOG 或操作设备。

**签名转换局部缩短了约 5.5 秒，但最主要的 lsd 等待仍超过 103 秒。用户本轮 245 秒到 Hello 的观察没有被日志否定；日志也没有同源 Boot/Hello 首帧标记，不能声称已独立验证了精确 245 秒。** 相比首轮 237 秒，本轮增加 8 秒；相比同一旧镜像重复轮 190 秒，本轮增加 55 秒。已计时的签名阶段没有变慢，整体差值来源尚未定位。

新日志：[autolog-nova.txt](<nova-root>/logs-nova/boot-speed-round2-nova/candidate-run-nova/autolog-nova.txt)，SHA256 `283735861cf23fa8581a73728ee76961cd593a2658d2762bc4c9efe47af149aa`，与两份基线不同。完整样本、行号、计时夹取窗口与上下文符号匹配见 [service-comparison-nova.json](<nova-root>/artifacts-nova/boot-speed-round2-nova/device-analysis-nova/service-comparison-nova.json)。rootio 数值总分析由主任务负责，见 [rootio-analysis-nova.json](<nova-root>/artifacts-nova/boot-speed-round2-nova/device-analysis-nova/rootio-analysis-nova.json)。

## 三轮对照

| 项目 | 首轮旧候选 | 相同旧镜像重复轮 | 本轮新候选 |
| --- | ---: | ---: | ---: |
| 用户 Boot→Apple | 约 90 s | 约 80 s | 97 s |
| 用户 Boot→Hello | 237 s | 190 s | 245 s |
| 用户 Apple→Hello | 约 147 s | 约 110 s | 148 s |
| 23 次大文件签名转换总计 | 49.418 s | 49.485 s | **44.002 s** |
| 其中 read | 40.967 s | 41.027 s | **35.538 s** |
| 其中 hash | 8.429 s | 8.431 s | 8.435 s |
| SB 等 launchd 最大已观测年龄 | 35.282 s | 35.481 s | ≥28.281 s |
| 随后 SB 等 lsd 最大已观测年龄 | 100.341 s | 100.796 s | **≥103.673 s** |
| 同线程两次串行等待下界 | 135.623 s | 136.277 s | **131.954 s** |

本轮用户起点为 **16:30:50（Asia/Shanghai）**；按其耗时换算，Apple 约 16:32:27、Hello 约 16:34:55。这些是外部观察及其算术换算，不是内核日志自动识别的帧时间。

23 次转换的文件大小顺序、总字节数 **2,724,626,432**、转换层读取调用数 **10,403**，三轮完全一致。本轮相较两基线总计分别缩短 5.416/5.483 秒，约 11%；几乎全部来自 read 缩短 5.429/5.489 秒，hash 基本不变。这支持本轮读取路径获得局部收益，尚不能把一次对照当稳定均值。转换层 `reads` 不是 UFS 命令数；它没有变化，不表示底层批读没有生效。

当前 `cs4k` 探针仅统计成功且至少 16 MiB 的转换，23 条都在首个 shared-region mapping 之前（新日志 L257–L510，mapping L511）。不能把这些行当所有启动读取，也不能把其 read 时间与 rootio 内层读/验证时间再次累加。L247–L438 留存的 24 条签名注册结果全部为 0；未见 `meizu: PANIC` 记录。

## 服务等待没有被这次批读解决

本轮 SpringBoard PID 26 / TID 253：

- seq 260 指向 launchd PID 1 / generation 4，send ID 6001009；L1033、L1317、L1426 观测年龄 7.876、18.077、28.281 秒。
- 随后 seq 2270 指向 lsd PID 96 / generation 269；L1552–L2115 共 11 个递增样本，最终 103.673 秒。它是 receive-only，`send_id=0`，前置发送没有完整保留。

本轮 launchd 只落入 3 个约 10 秒间隔的样本，旧轮为 4 个，采样相位也不同。因此 **28.281 与 35 秒的最大年龄差不能解读成 launchd 精确快了 7 秒**；同理，串行下界少 3.7–4.3 秒不是完整服务耗时的确定改善。

lsd 仍有与前轮一致的初始化链：

- L987–L994：主线程 TID 430 匹配 `_LSPreferences`、`_LSDatabaseClean`、`LSDatabaseBuilder createAndSeedLocalDatabase:`。
- L1055–L1056：TID 436 匹配 `LAUNCH_SERVICES_CLIENTS_ARE_WAITING_FOR_THE_DATABASE_TO_FINISH_SEEDING` 与 `LSDatabaseBlockingFetchClient`。
- L1589、L1739–L1745、L1816：TID 438 匹配应用目录扫描、MIContainer 容器查询、LaunchServices 扫描可执行 bundle。
- L1535 的 TID 438 确有一条指向 containermanagerd 的 pending 请求；后面 round 2/3/4 的 pending 集合为 0，且 TID 438 多次是 running 样本（L1627、L1708、L1891）。**不能说同一条容器 IPC 一直卡着 103 秒。**
- L2133 出现 LaunchServices `.csstore` mmap，L2157 SpringBoard 序列前进；数据库/容器初始化仍是下一轮需要拆分的工作。

以上符号通过 PC 减本轮 slide `0x5fa8000`，与只读历史符号表的 unslid 地址精确匹配；slide 与当前 `nf` 和 dispatch 地址一致。没有重新校验本轮 dyld cache UUID，故只作为上下文匹配；未采用误导性的 nearest kernel stub 符号名。

接收队列虽然多次 qcount=0，但 cookie/epoch 改变且 `continuity=unknown`，仍不能证明 SB 那一条消息已出队。launchd registry 在 L1331 起不可用，不能因此认定 launchd 停止运行。

## 与新内核计数器的时序是否相容

新日志首条 rootio 在 L185，counter=454720267，hz=19200000。以下均以**这一条观察为 0**，用事件前后的 rootio 行夹取范围；它们不是 Boot 秒数，也不是这些事件自身携带的精确时间戳。并发日志和采样到打印的延迟可能影响夹取边界。

| 事件及日志位置 | 相对首条 rootio 的观察范围 |
| --- | ---: |
| 最后 cs4k / 首个 shared mapping，L510/L511 | +41.507～49.514 s |
| 首个已记录 framebuffer swap，L1504 | +79.960～90.100 s |
| SB 转入 lsd 依赖，L1523 | +79.960～90.100 s |
| `.csstore` / SB 序列前进，L2133/L2157 | +191.637～201.788 s |
| 第一次 Setup arm，L2276 | +201.788～211.975 s |
| 第一次 Setup watchdog，L4722 | +232.383～242.457 s |
| 第二次 Setup arm，L4836 | +262.883～273.143 s |
| 第三次 Setup arm，L7094 | +293.488～303.743 s |

这条内部时轴确实显示了数分钟的初始化过程，与用户的多分钟启动量级相容；它不能精确复算 245 秒，也不能把首个 swap 自动叫作 Apple 或 Hello。读取工作已缩短，而 lsd 仍占据长时间窗口，这比“存储还没一次读完”更直接地解释了为何只调批量没有把整体启动降到很短。

本轮日志还保留了 3 次 Setup 启动及 3 次 20 秒 scene-create watchdog。首轮 Setup 在 age 2.030 秒仍是 running 样本，到 age 8.022/15.014 秒已变成相同 PC/LR/SP、hint=6 的 semaphore 等待（L4089/L4161），随后 L4722 被 watchdog 终止。相似问题旧日志已有，不能仅凭出现 watchdog 认定本轮新引入。

**不能把三次 20 秒直接加到首次 Hello 上。** 第二、三次 Setup 启动在首条 rootio 后已超过 262/293 秒。若用户 245 秒确属此轮，且日志顺序对应正常执行顺序，则首条 rootio 又发生在 Boot 之后，这两次启动应处于首次 Hello 之后；它们解释后续稳定性问题，不能解释首次 Hello 前的全部等待。第一次 watchdog 与用户首次 Hello 的精确先后仍缺同源标记，保留未知。

## 下一轮要回答的具体问题

优先将 **lsd seeding 的重复初始化、应用/容器扫描、短 RPC 累积和线程实际执行**拆开计时，确认大约 100 秒消耗在哪条依赖；继续追踪 Setup 主线程等待的 semaphore 应由哪个工人或回调完成。现有证据不足以支持跳过 seeding、删除服务或缩短 watchdog。

本轮已证实的是“指定签名转换阶段快了约 5.5 秒，后段长等待仍在”。97 秒 Apple、245 秒 Hello 与旧轮的差值，尚无完整同源阶段时间能归因；此前相同旧镜像的 237/190 秒差异也仍未被内部计时解释。不要把 rootio ticks/hz 当 Boot 起点、把 heartbeat tick 当秒，或把 E1 当读取进度。

## 复核入口

基线原始日志分别是 [首轮](<nova-root>/logs-nova/device-boot-optimization-nova/candidate-run1-nova/autolog-nova.txt)（SHA `243b87f3669855c8d73d38b127b14fea6d9063edd9c82c039216ed43d33de4f3`）和 [重复轮](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt)（SHA `da158d93768f3199bc2be69e7282544081a94d608545418fbbcc3f09b95bfa85`）。服务等待复用 Nova 本地解析器；`nova: cs4k` 在完整行中搜索 marker 后解析五个十进制字段，校验每条字段齐全，再按顺序/字节数匹配，避免丢失行前前缀。

```sh
python3 -B artifacts-nova/boot-latency-nova/analyze-boot-nova.py \
  logs-nova/device-boot-optimization-nova/candidate-run1-nova/autolog-nova.txt \
  logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt \
  logs-nova/boot-speed-round2-nova/candidate-run-nova/autolog-nova.txt
```

该命令从 `<nova-root>` 执行，只读输入，输出到 stdout。配套 JSON 记录了用于本报告的逐条 cs4k 数据、等待样本和区间，未混用旧测量作为新轮结果。
