# R15 运行时报告（Nova）

## 结论
R15 诊断已到达 Hello（用户确认“进hello了”），随后出现转圈；本次仅只读取证，未改行为、未刷机，不声称提速。上一轮报告生成由 Sol 在无工具活动时终止，属任务终止，不是设备故障；此前记录均保留未改。

## SpringBoard 崩溃
- L5941 pid25 异常（exc=1, code=1, pc=0x215a71a4c）；L5942 exit rv=11。
- 当次 check_np 头为 0x1c1e34000（L502），slide=0x41e34000；故障 PC 未滑动值 0x1d3c3da4c，属 /usr/lib/libate.dylib（UUID d185fd86…）。这只定位故障模块，不证明非法指针来源。
- L5964 “Too many corpses” 是崩溃现场创建的次级诊断，不是首因；L5974 pid233 成功重启。窗口内未见 panic，内核在崩溃后继续记录。
- 无精确转圈时刻，崩溃/重启与转圈现象一致，但不能证明二者时间点对齐。17 条 crashed 记录不等于 17 个不同服务。dropped=0/NUL=0 不能排除旧共享 IPC 环曾被覆盖。

## 36.33 秒显示查询
seq204/taskgen53/tid253：L898 start、L1612 finish；duration 697618183 ticks ÷19200000Hz = 36.334280364583336s，clock_error=0、result=0、reply=6001109。仅 1 条保留行，无溢出/不匹配/时钟无效；16 个 start PC，err=0、trunc=1。L902 frame3 0x1cfafbad4 − slide = 0x18dcc7ad4，位于已知 _BKSDisplayGetMainScreenInfo mach_msg 调用返回点，故运行期显示查询已确认；接收方内部等待原因仍未证明。发送端 endpoint 为 launchd 不等于 launchd 原生协议。

## 锚点
launchd 35256729375ns（L256）→ SpringBoard 56324788229ns（L730）→ Setup armed 103486883958ns（L1707）；间隔 21.068058854s、47.162095729s、合计 68.230154583s。R14 对应 21.094823698/46.176911094/67.271734792s。不据此声称提速；Boot→Hello 总时长未知，Setup armed 非首帧。

## 下一步
有界验证接收方就绪/回复时序（36.33s 背后）与 libate 非法访问的指针来源；暂不打补丁、不安装。
