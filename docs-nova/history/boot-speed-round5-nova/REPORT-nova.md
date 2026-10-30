# Nova 第五轮：调试状态区刷新限频

2026-10-30。新文件，无旧版。状态：生产实现、内核构建/prelink/PAN和固定封装完成；19场景host测试及4个负对照、最终链接静态审计和独立审阅全部通过，已刷入并整分区回读一致，真机到Hello且日志分析完成。用户确认Boot→Apple73.03秒、Boot→Hello220.56秒；相对第四轮212秒多8.56秒，单次未见提速。设备已返回Android B。

第四轮Boot→Hello为212秒，lsd等待下界97.565秒。已测根盘读取/校验和lsd栈采样不是剩余百秒等待的主要解释。本轮只检验中断入口同步绘制状态区的开销，不预先认定它是主因。

实际1080×2400/stride4320几何下，每次状态刷新仅清五行就写686,080字节，另加文字。原IRQ每512次、FIQ每64次分别触发；现保留这两个筛选及所有计数/ELR/init采样，并增加共用1秒上限，从上次绘制结束起算。首次周期绘制立即进行，KV首次状态、检查点与kprintf行为保留。屏幕E1为EL1中断计数，并非耗时或启动百分比，刷新下降后不能以屏幕数值更新速度判断内核进度。

周期统计使用CNTVCT/CNTFRQ同源差值。独立一次weak-CAS try-gate保护预算和计量，忙时立即跳过；这是周期路径的门限，不是对旧console全局volatile busy协议的SMP修复。hz无效、逆向时间、频率变化会记录或停止相应观察，不等待。计量周期绘制次数、总tick、最大tick、限频次数和时钟异常；真实滚屏另计次数，未计滚屏耗时。初次KV绘制不计入周期耗时。

现有autolog线程每10秒最多输出一条统计，最多31条，300秒预算到期后允许一条final。复制统计后释放gate再printf，不在中断里打印。总tick并非独占CPU预算，不能与其他嵌套计时直接相加。

源代码和前次build已备份到snapshot-nova/boot-speed-round5-nova。候选沿用固定封装，仅替换kernel；第四轮镜像保持为回退。不改Data持久化、服务业务、SHA/根盘读取、bootargs或Setup。


最终候选boot SHA256：`d4f3fd3e33a97365268ad5714b25fc9614a797bcce264468f240baf5c020e25e`；raw `c358ee9cf6737d01816eb279f6f8507ff67680c764db2851c8d39083f25c6372`；payload `bdf7c71c0c41813b1e201b2d4f21976c1f047ea5de81fb9e5b670ef888f071fe`。封装验证所有非kernel EFI、固件/DTB、bootargs、ADT、trust cache和Data512原样保留。

审阅中已修复报告时钟回退与final间隔边界；final也要求距上一条至少10秒。绘制计时CNTVCT读取带编译器memory clobber，紧贴实际绘制函数前后；这不是面板完成时间，未添加DSB或改变现有像素缓存策略。


验证结果：ASan/UBSan下19个真实生产函数场景全部通过，4个负对照按预期拒绝；最终链接核验7个对象函数、21个raw/payload函数及15个保持不变的原对象。弱CAS无回跳重试，实际计时指令紧贴绘制调用，autolog调用/释放gate后printf均核实。独立审阅无新增阻断。固定设备流程只刷boot_a，Android B整分区回读验证后才选择A启动。新增设备脚本只在本清单ready时允许执行，逐阶段重核设备身份/分区哈希。


设备执行：2026-10-30 00:42:27完成boot_a刷入；00:43:36从Android B核对完整A分区匹配候选、B仍为原hash；00:44:23.233确认Fastboot A后发出重启。所有步骤先检查匿名设备标记和实际分区，未写boot_b、root或Data。启动记录中的原current_slot字段是前置Android哈希核验时的B，具体切A及确认记录见actions；另存candidate-start-nova.json明确preflight B/最后确认A。

## 真机结果与归因边界

用户确认2026-10-30 00:44:38从Boot菜单开始，苹果累计73.03秒，Hello累计220.56秒（苹果后147.53秒）。Hello钟表00:48:18.560为推算值，未用主机重启时刻替代Boot起点。单次比第四轮多8.56秒，不足以认定稳定回退。

00:54:56核对Fastboot A后切回B；00:56:17回读确认boot_a仍为第五轮，boot_b为原值，00:56:20取完保留日志。原始正文2,085,571字节，SHA256 `cb61b6a8838e6648c508225e4ef6a52cdec3eccad7bc7230e40e9f4d1b682659`；dropped=0、NUL=0。30条rootio和30条status严格解析通过，所有已保存rootio快照bad_hash为0。

| 指标 | 第五轮 | 解释 |
| --- | --- | --- |
| 相同23项cs4k | total19.356 / read10.929 / hash8.402秒 | 2,724,626,432字节、10,403次读取，与第四轮同序同工作量；total少1.086秒 |
| SB等待launchd | ≥36.278秒 | 采样下界 |
| SB等待lsd | ≥101.427秒 | 同线程串行采样下界合计137.705秒，长等待仍存在 |
| lsd可见样本周围rootio窗口 | 101.520秒内read1.190 / verify0.230 / copy0.022秒 | L1688→2568；该窗口内已计量的根盘read/verify/copy不能解释主要等待 |
| lsd可见样本周围状态窗口 | 100.247秒内70次绘制、0.508804秒；滚屏增量0 | L1692→2556；此候选周期绘制不是百秒瓶颈 |
| 最后保留的status快照 | 197次绘制共1.428093秒，最大7.317448毫秒；clock_errors=0 | dump30、final=0，不是终止final；全捕获覆盖Hello之后 |
| Setup异常 | 保存到3次scene-create watchdog，额度均20秒 | 不能相加成首个Hello启动预算；本日志未找到chronod watchdog，不代表问题已修复 |

状态窗口包围首次和末次可见pending行，不保证涵盖请求真实起点；其100.247秒小于最大等待年龄101.427秒，因此不能冒充完整等待区间。旧版本没有绘制耗时计量，不能将新值当作节省量；滚屏仅计次数，未测耗时。各计时有重叠，不能相加成Boot预算。

本轮排查到此收口：暂停继续调低状态刷新频率。主要问题仍在服务准备/执行路径。下一步结合Sheng固定源码核对CPU当前频率与Data初始化复用，先做能够区分原因的窄实验，不直接启用八核、Data持久化或禁用服务。

证据：`device-analysis-nova/run-comparison-nova.json`、`status-timing-analysis-nova.json`、`rootio-analysis-nova.json`、`legacy-boot-analysis-nova.json`；设备及用户原文在 `logs-nova/boot-speed-round5-nova/`。离线复放脚本 `device-analysis-nova/compare-run-nova.py` 校验固定日志哈希、23项工作量和统计来源后成功运行；已有输出需先备份才允许重跑。
