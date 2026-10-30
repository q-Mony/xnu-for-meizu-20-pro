# R11 真机失败结果 — Nova

新文件，无旧版。第十一轮于2026-10-30 06:20在Android B内写入非活动A并完整回读，06:21:35确认Fastboot A后发出重启。用户报告 `nova-r11 policy fail e=22 b=2 op=9 save=1 @kern_exec.c:9822`，未到Hello。06:24:58确认Fastboot A后切B，06:26核对完整A/B并回收日志。

本轮第一份有效日志确有version11/pre-data，selector为warm=1；R8暖加载status0、两项固定SHA verified1，Data挂载及prepare返回0。失败为warm目录类型/权限/uid/gid谓词检查，不是本轮open flags拒绝。保存前的完整失败文字已在本轮有效ring回读；屏幕报告save1为保存函数返回后的独立证据。

日志没有原始目录属性，因此尚不能判断谓词哪一项不匹配；不将host fixture或猜测作为放宽依据。下一轮先核对stat ABI、APFS属性映射及目录准备逻辑，必要时只加入实际字段诊断。手机当前Android B，不重复要求Fastboot/再次读取R11。暖Data启动性能仍未获得有效Hello对照。
