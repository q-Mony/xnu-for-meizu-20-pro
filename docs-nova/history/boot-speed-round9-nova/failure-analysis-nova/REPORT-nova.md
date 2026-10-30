# R9 失败日志只读分析 — Nova

2026-10-30。新增报告，无旧版。只读本地输入，未操作设备、挂载镜像、修改源码或总文档。

**结论：暖 Data 已被使用的旁证很强，但完整暖恢复成功回执没有保存下来。本轮不能判断启动提速，也不能从这份日志直接确认屏幕 panic 原因。** 严格分析器返回 `WARM_LOAD_NOT_PROVEN`，表示证据不完整，不表示观察到了恢复失败。

## 可直接核对的日志事实

输入正文 SHA256 为 `5669b70e738003500df4addec1cc3e6acf0f1943e7de028e1cc10f9230c3d3c6`，24,627 字节、192 行。最后一条是首轮 autolog 的 cpufreq sample，cntvct=672865862、频率19,200,000，时间轴位置 **35.045096979 秒**。这不是用户 Boot 菜单计时。

- 第123–127行：512 MiB Data（131,072个4 KiB块，当前设备名 disk2）选择 `largest xid 112, best xid 112 @ 7`，并报告 `reloading after unclean unmount, checkpoint xid 112, superblock xid 17`。
- 第129行：NativeUfs 已发布 OS。结合本次交付源码的 gate，这是目标 Data IOMedia 曾满足发布条件的间接证据；它不等于 `/private/var` 挂载或后续 owner 准备成功。
- root_device_resolve 已完成，耗时 **16.270509115秒**；R8同一口径为7.369095052秒，增加8.901414063秒。此区间包含异步准备与等待，缺少 warm分项回执，不能把差值全归为UFS读取或恢复CPU耗时。
- root_mount 已成功，耗时 **0.018452136秒**。
- systemos_prepare 在35.042473333秒开始，只有begin，没有end。正文最后保存的是其开始后约2.6毫秒的状态；不能据此断言该阶段随后失败。
- 正文没有 warm status、warm digest、warmsource、data_prepare成功、launchd/SpringBoard/Setup锚点或panic。BootWatchdogPolicy errno17来自主代理转述的用户屏幕信息；屏幕没有uptime，本报告不补造时间或panic全文。

## 暖/冷字节区别

直接读取已交付 cold packed seed与R8 captured raw，核对文件SHA后查看primary NXSB及其声明的descriptor area，未挂载或修复。

| 元数据 | 交付冷seed | R8暖raw / R9运行日志 |
| --- | --- | --- |
| primary block0 xid | 17 | 17；整块SHA与冷seed相同 |
| descriptor area | base1118，8块 | 相同 |
| descriptor index1 | xid17 | xid109 |
| index3/5/7 | 无对应新NXSB | xid110/111/112 |
| 运行时选中 | R8冷启动选17@1 | R9选112@7 |

暖raw的index7位于逻辑block1125，恰与R9的112@7对应；`@7`是descriptor索引，不是逻辑block7。暖raw全文件SHA仍为固定 `372549a9fcb74ba062fbd871a370199c7d32657cfa4e436040c120b6dbfcbd43`。交付cold packed SHA为 `0f1a60ef3e23e355cec56d6a860db5606e2da14cba51660318422efdd99f7c46`。

这支持“已加载捕获的暖内容”这一推断；primary17与checkpoint112的不一致本来就存在于block cut，unclean提示本身不能作为UFS读坏的证据。当前R9代码只有通过完整header/payload/logical校验才交换候选，但完整运行时SHA回执未保存，因此仍不把本轮升级为严格暖启动验收通过。

## ring_total与text_bytes没有矛盾

MEIZULOG header记录 `total=24454, bytes=24627, dropped=0, reserved=0`。差值 **173字节**恰为写入器额外添加的一个换行与172字节boot-video记录。total只计console ring，bytes包含合成前缀；这不是173字节丢失或计数溢出。

正文严格等于raw窗口 `[4096, 4096+24627)`。header加正文向4 KiB对齐后结束于offset32768，共8块，末尾padding全零。

**offset32768之后的33,521,664字节与R8回收的raw窗口完全相同。** 它们是继承尾数据，其中仍有5,467,088个非零字节。R8窗口自身还可能继承更早数据，因此只能说“不是本轮新快照”，不能全部认作R8本次运行事件。全文扫raw搜到的12,826个`meizu: hb`和1,282个`nova: rootio`均在该旧尾部，不能拿来填补R9时间线。当前header范围之外没有可用的R9补充panic或warm回执。

## 为什么warm summary没进保存日志

已核对日志相关源码SHA与本次delivery manifest一致：

1. autolog循环先输出blog-status与cpufreq sample。
2. `meizu_autolog_dump` 调用 `meizu_log_snapshot`，构造header并把当前正文写到UFS。
3. 写入之后的heartbeat分支才调用 `native_ufs_log_read_stats`，后者输出rootio、warmsource与warmload状态/摘要，再输出hb。
4. 这些后续输出需要下一次成功snapshot才进入持久日志。

本次保存尾部恰好是首个cpufreq sample，与“首份快照未包含其后才打印的warm summary”一致。因此summary缺失不能证明warm未执行；`dropped=0`也只表示这份console snapshot未发生ring截断，不代表记录一直覆盖到屏幕panic。

为何没有留下后续snapshot，当前输入不能区分：panic过早、尚未到下一次允许写入、写入未完成或其他终止原因。源码顺序可以解释证据缺口，不能替代缺失的后续事件。

## 交付与后续

- `evidence-nova.json`：最终结构化证据及输入/源码SHA；以此文件为准。
- `log-excerpts-nova.txt`：带原始行号和时间轴位置的摘录。
- `parser-output-nova/`：复用固定R7时间戳parser的R9严格结果，未修改parser或旧输出。
- `analyze-failure-evidence-nova.py`：仅本地只读复现；直接字节检查不等于完整APFS结构验证。

本轮不产生Boot→Hello或SB→Setup性能结论。下一轮需要先处理另一路正在核查的BootWatchdogPolicy现有文件兼容问题，并保留warm状态/实际摘要及进入用户空间后的日志，再判断初始化工作是否减少。
