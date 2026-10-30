# 第六轮最终静态绑定审计 — Nova

结果：**PASS_STATIC_BINDING**。新增文件，无旧版。审计未构建内核、封装或操作设备。

审计输入为成功构建 `build-result-20261029T180917125947Z-nova.json` 和正式封装 `packaging-nova/cpufreq-candidate-final-nova`。

| 产物 | SHA256 |
|---|---|
| 原始内核 | `9a5d91cda01888313607128a26d9b2e5d2f8ceb485b8a67dc9a2e6e4c601c47d` |
| Payload 内核 | `28b5299bcf853c1972e78044d8a7ab80f61da664972f1129a731c833a4cc5865` |
| Boot 镜像 | `a2114a6acbfe136b3793d1ec2cbf80c4ff7358c444cde9b344f0704479b5bd1e` |

- CPU 对象与 archive 成员逐字节相同，6 个外部依赖均在白名单，无自动 STARTUP 入口。5 个关键对象函数在排除列明 relocation 字段后与最终内核匹配；16 个诊断/原功能函数 raw→payload 字节相同。
- 实际 autolog→CPU 探针 `BL` 位于 `0xfffffff0086a6d5c`；IPC→CPU 计时 `BL` 位于 `0xfffffff00821db14`。CPU 计时实际调用 recount 两个 API、引用/释放线程、前后两次查询 task 活动状态；注册表解锁支配 recount 调用。
- `mach_msg.o.json` 确认实际编译有唯一 `XNU_MEIZU_LSD_CPU_TIMING=1`，并绑定最终源对应的 host 测试；未依赖 BSD 对象专属宏。新 archive 出现在实际链接配方中，构建脚本和 MakeInc.kernel 的重链依赖接线匹配。
- 严格参数计数/值门控在控制流上支配 `io_map`。映射参数解析为 `0x17d91000`、4 KiB、Device、只读、可移除。映射返回值不被改写；唯一四个窗口 LDR 偏移为 `0`、`0x20`、`0xb0`、`0x320`，各有 DMB OSHLD，窗口区零 store；无控制流重复 mapping 或某次 MMIO load。enable 全 ones 或未启用时跳过其余三读。
- 快照写入位于 STLR 发布之前；getter 的 LDAR 与发布 STLR 解析到同一个 `snapshot_ready` 地址。原子认领在 MMIO 前有编译器生成的 LDAXR/STLXR 重试，审计不声称整个函数无循环。
- BootLog、Root/UFS、sys_ulock、SHA、HID、触控、ArmCpu 的 16 个保留对象与本轮 build-before 快照逐字节相同。
- 构建前/后/审计时源哈希一致。相对第五轮，EFI 只允许内核和唯一 Boot args 后缀变化；其它 EFI、固件、DTB、boot 外围及继承 AVB 尾一致。最终 boot 的 ramdisk 与提取 payload 镜像哈希绑定。

首个候选曾缺少 IPC→CPU 调用，审计当时阻断，部分证据保存在 `first-candidate-blocked-preflight-nova.json`。其结果不用于本次 PASS；最终镜像使用修正后的独立编译开关并重新构建、封装、审计。

本审计只证明静态绑定和约束，不证明真机执行、启动速度提升或总线读取可恢复。一次 EPSS 快照不代表整个启动期间频率；recount 是已累计 CPU 时间，与 IPC 快照不原子同步。MMIO 无重试也不能解决总线 stall/abort。继承 AVB 尾不代表新签名。

完整检查、函数地址、哈希和限制见 `audit-nova.json`；机器码见同目录五份 disassembly 文件。
