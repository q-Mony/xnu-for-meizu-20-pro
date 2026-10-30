# Boot/Hello端点实现边界（Sol，只读源码核查，新增无旧版）

尚未实施，不属于R18 SHA镜像。现有内核clockcal只能对齐内核mach/CNTVCT，不能补回菜单选中时刻。Boot计时不应使用应用entry、内核entry、Setup armed或人工回复时间代替。

已找到两个明确源码位置：
- package/UniXNUPkg/Library/UniXNULib/Main.c:148，最终确认完成、调用UniXNUBootEntry前，是UniXNU菜单最终选中的候选起点。若用户平时从UEFI BDS的Boot选项开始计时，这个点仍晚于该起点，不能自动等同；需先核对实际UI层级。
- drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c:525，当前loader明确拒绝Request->DeviceTree非空；实际读kernel.macho.adt，并在AdtNode验证及最终memory-map中修改预留chosen属性。因此不能只把时间加到UniXNU准备的FDT就以为传到了XNU。计时方案需显式走loader支持的通道，或扩协议revision，旧结构不可越界读写；ADT增加预留固定大小chosen属性，按现有bounds/duplicate/path验证规则写入。UEFI和XNU均记录counter种类、frequency、有效位/版本，验证频率一致、单调和范围，不能默认EL切换前后同epoch。

Hello首帧端点仍无源码回调：一般swap只证明提交，纯非黑像素也不是Hello。低频像素样本/摘要用于黑屏诊断，不能命名hello_first_visible。需要可信的Setup可见状态回调，或保存有限画面供离线识别；后一方案须先设计独立、已授权写入区域/容量/失败退出，不得挤占当前32MiB日志窗口或改固定warm export/cold seed，避免破坏可恢复性。当前不自动改镜像空间布局。

验收：缺起点、无可信Hello识别、时钟不连续、快照失败一律unknown；只报告有定义范围的内核阶段时间。仍保留用户确认已到Hello作为体验观察，不把回复时间当呈现时刻。
