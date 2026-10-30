# 第五轮最终二进制静态审计 — Nova

结果：**PASS_STATIC_BINDING**。新文件，无旧版本；只读审计，无构建、封装或设备操作。

- BootLog.o 与 archive 内成员逐字节一致；6 个 BootLog 函数及 autolog 线程的最终链接体在排除列出的 relocation 字段后匹配。
- IRQ/FIQ 实际 payload hook 分别进入 NativeBootLog，再经原 512/64 筛选进入同一个 periodic_status；stub 重放原指令并返回原入口 +4。
- 两个 gate 调用点均只有一组 LDAXR/STXR；CFG 不存在 STXR 后返回 LDAXR 的软件重试路径。向较低地址公共 RET 的分支不误称重试。BootLog 对象未引入外部 atomic/锁 helper。
- 实际机器码为 ISB/MRS CNTVCT → BL blog_status → ISB/MRS CNTVCT；计时位于绘制前后。autolog 真实 BL 调用新 dump，dump 释放 gate 后才 printf。
- 数据校验、UFS、IPC、栈观察、SHA wrapper/ARM64 ASM，以及触控/HID/ArmCpu 的所选整个对象与第五轮 build-before 逐字节相同。
- 所选功能 raw→payload 字节一致，非 kernel EFI 文件与第四轮相同，Android boot 除 ramdisk 及 v0 digest 外逐字节相同。

完整哈希、偏移、CFG 证据、对象清单和机器码见同目录 audit-nova.json 与 disassembly 文件。

此结论仅证明候选镜像静态绑定与输入保留；不证明真机启动加速。原有 console-wide volatile busy 的 SMP 边界仍在；periodic gate 只串行周期刷新及其汇总快照。
