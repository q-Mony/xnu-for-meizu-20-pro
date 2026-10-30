# 接收前等待与Hello黑屏后续诊断方案（Sol，新增，无旧版）

2026-10-30，仅只读源码核对及方案；未改生产、未安装。与R18独立SHA候选分开，保持可对照性。

## 新确认的采样缺口

kern_exec.c:8028 meizu_sample_receiver 依赖 meizu_sb_ipc_receiver 返回的当前endpoint。R17在目标请求等候期endpoint为launchd pid1；实际backboardd pid55直到delivery才由life记录确认。单纯把 target_lsd 扩为 target_lsd || backboardd 仍会遗漏等待期。

kern_exec.c:8072–8086 的 workers rounds 仅 target_lsd=true执行，因此receiver-map出现backboardd也不意味着已有backboardd线程采样。R17等待期只见launchd receiver-map，约77秒开始该registry anchor unavailable，不能从无线程样本推断backboardd一直运行或一直阻塞。

下一次应从成功exec、精确/usr/libexec/backboardd路径验证后单独保留anchor和task_uniqueid，不能依赖当前IPC endpoint或可被逐出的通用registry。以exec时钟采0/5/15/25/35/45秒（共享全boot总轮数、replacement不重置）；每轮有capture_ticks/status/identity与截断计数。使用独立ref与try-lock、释放后打印，禁止stop task/读不稳定用户栈/持锁打印。main先取，最多64个线程，访问次数也必须封顶，不能只以成功采样数限制遍历。复制的是已有稳定TH_WAIT用户PC/LR状态；运行线程明确state-only，不能把旧寄存器当当前PC。应记录每轮task累计user/system CPU差及有效区间，CPU时间与墙钟不能混加。

thread.c:4468已有meizu_sb_thread_snapshot逻辑可参考，thread.c:4611已有worker采样；新增调用前仍需审查锁顺序、引用/exec替换/退出/busy/deadline所有路径，并用真实提取的状态/调度逻辑测试。输出用有界snprintf后single%s，沿用R17真实encoder预算验证，避免再引入log_encode断言。

已有backboardd静态报告的server创建0x10000a360、注册helper0x100023ee8、三处FOREVER group wait可与采样PC/LR离线对照。但这些unslid地址不能直接当运行PC，无正确映射则标unknown。不能为打时间戳直接patch私有二进制，REA目前不可用；本方案也不伪造服务ready。

## 黑屏仍缺的证据

NegroniFramebuffer.cpp:285只记录前16次layer提交，无法说明其后停止。记录必须区分swap调用、enabled layer、source lookup失败、rect/format拒绝、成功copy与总字节；稀疏间隔持续到至少10分钟，辅以每秒计数汇总和限次状态变化，不逐帧printf。

输入驱动编译关闭及实体buttons sink为空意味着无法用按键/点屏恢复来证明死机；也不能据此认定正常待机。先不盲开驱动。

BootLog仍可清背景、滚屏和写状态，同ABL framebuffer存在并行写入。下一诊断应记录GUI首次copy时间、控制台写入/清页/滚屏计数与last ticks，保留ring，避免直接关日志后宣称修复。实际panel电源/亮度未知，selector返回success不代表硬件动作已实现。

帧采样可区分提交黑像素与外部覆盖，须有坐标/stride/边界验证、合理采样开销；小块统计/摘要只能是样本，不能证明全画面或Hello。精确Hello需可信可见回调或有限完整图像快照离线识别；Boot起点需UEFI选择事件时钟传递。目前Setup armed102.125秒不是Boot→Hello。未完成这两端点不能再承诺免手工就有精确总时间。

## 实施顺序与验收

1. R18先只接暖Data SHA，局部正确性与ARM64构建封包，等待用户安装命令；实机改善只由同范围warm hash/import计时确认。
2. 独立下一诊断候选补backboardd exec-relative线程/CPU样本及显示/console低频计数；保持原行为，界定探针开销。验证身份/引用/轮次/busy/退出/替换/无时间倒退/encoder安全及production TU。
3. 运行证据证实具体依赖或覆盖机制后才修复36秒/黑屏。当前不选定UFS、digitizer、timeout或console为已证实根因。
