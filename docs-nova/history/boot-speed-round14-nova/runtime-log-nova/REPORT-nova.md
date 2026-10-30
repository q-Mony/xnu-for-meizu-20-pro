# R14 运行日志分析修正（Sol Review）

修正版；原版备份于 `snapshot-nova/boot-speed-round14-nova/runtime-log-nova/before-review-correction/`。旧报告遗漏 first Setup，并把 launchd exec→first spawn 的 19.796s 当作最大剩余启动区间；本版以 **first Setup** 为启动区间终点重算。

用户“进了”确认 R14 Hello 仍为独立定性事实；Setup armed **不是** rendered Hello/Hello first frame，日志也无 Hello first-frame 锚点。不得由日志末尾或主机重启时间推导 Boot 菜单时长。

## 实际日志与时钟校验
- R14 文本 `dfeb9005406a5f8bec9a003a2953258cee25b25fb444d17100de40101ef01f02`；R7 `01e6b73d77676d6436c3d898dc9671ce464bcb422d6d8ffb92ed0b735147420a`；R8 `dcf50ef3eaf2dc02157f62f5f04b752be9e5b3b29b40c881b5ab73ec3c9f81c9`。行号均按各自 autolog 文本。
- Mach capture 行：R14 launchd L256 `capture_ticks=624604521/capture_ns=32531485468`；SpringBoard L730 `1029625136/53626309166`；lsd L908 `1108331117/57725579010`；first Setup L1621 `1916221829/99803220260`。
- R7/R8 用同域 capture：R7 launchd L239 `506245164/26366935625`、SB L702 `911521783/47475092864`、lsd L893 `1036097861/53963430260`、Setup L2762 `3839982448/199999085833`；R8 launchd L240 `506056393/26357103802`、SB L707 `911233847/47460096197`、lsd L888 `983935276/51246628958`、Setup L2756 `4013558640/209039512500`。
- 所有比较锚点均验证 `capture_ns == floor(capture_ticks*1e9/19200000)`；bootphase `ns` 同样验证。决定性时长只用 Mach capture，不混用 CNTVCT 前缀。

## 四个 bootphase（R14，Mach）
| phase | begin 行/ticks/ns | end 行/ticks/ns | 时长 s |
|---|---|---|---|
| root_device_resolve | L102 303268814 / 15795250729 | L138 615499986 / 32057290937 | **16.262040208** |
| root_mount | L139 615500223 / 32057303281 | L158 615851407 / 32075594114 | 0.018290833 |
| systemos_prepare | L184 620979445 / 32342679427 | L220 623964333 / 32498142343 | 0.155462916 |
| data_prepare | L221 623964643 / 32498158489 | L237 624080343 / 32504184531 | 0.006026042 |

R8 rootresolve 为 `25889822239-18520727187=7.369095052s`；R7 为 `7.385559479s`。R14 rootresolve 较 R8 多 **8.892945156s**。

## 同域 Mach 区间（秒）
| 区间 | R14 | R7 | R8 |
|---|---:|---:|---:|
| SB -> first Setup | **46.176911094** | 152.523992969 | 161.579416303 |
| launchd -> SB | 21.094823698 | 21.108157239 | 21.102992395 |
| launchd -> first Setup | 67.271734792 | 173.632150208 | 182.682408698 |
| lsd -> first Setup | 42.077641250 | 146.035655573 | 157.792883542 |

R14 SB→Setup 相对 R8 缩短 **115.402505209s（-71.4215%）**，相对 R7 缩短 **106.347081875s（-69.7248%）**。这是单次运行、非同一 warm/capture 配置下的同锚点观察，支持“运行时内部观测段缩短”，不证明全部启动或 Hello 首帧，也不作单一因果归因。

## 启动区间重排
- 以 first Setup 为终点，最大高阶启动区间是 **SB->first Setup 46.177s**，不是 19.796s。
- `launchd exec -> 首个 boot spawn(fsck)` 的 **19.796054115s** 仍成立，但只是嵌套在 SB->Setup 内的具体静默段。
- first Setup 之后的日志尾部到 L5298 为 **67.930022865s**，属日志保留/UI 运行，不是启动区间；旧 timestamps 的 67.853s 是 first backboardd receiver→last log 的采样尾，也不得当作启动。
- 日志无 Boot 菜单墙钟；用户未给时长，因此 **Boot→Hello 总时长仍未测量**。

## Warmload 内部字段（重叠，禁止求和）
R14 L181：start→end `(612629348-415889972)/19200000=` **10.2468425s**；read `0.331257917s`、hash `8.667086354s`、import `9.286926406s`、publish `31.907680729s`。这些字段可能重叠；不得相加，也不得据此声称 read 主导。

## 稳定性/完整性检查
- 保留窗口未发现实际 panic/crash/watchdog 事件行；仅见 DumpPanic L907、ReportCrash L1390 的 exec-map 与 policy ready L246，不能据此断言发生过或未发生崩溃。
- 缺席只证明**此捕获窗口**无相应行，不提供全局稳定性保证。
- autolog ring `dropped=0/NUL=0` 与诊断 `sbipc loss dropped=119317` 是不同层：前者是日志环完整性，后者是诊断计数器。

## 剩余目标（仅假设）
1. SB->first Setup 仍约 46s；2. launchd 首个 spawn 前静默约 20s；3. warmload hash 约 8.7s。下一步只应量化这些区间，不实施修改、不访问设备。

## 绑定
时间戳表 `timestamps-nova.json` `91c89132af26ad571406af963690bead7e0b598d034ee99107150cdcdd633ace`；原始 log、恢复/install 记录均保留。REPORT/evidence 的最终哈希见 review-result 与 changelog。
