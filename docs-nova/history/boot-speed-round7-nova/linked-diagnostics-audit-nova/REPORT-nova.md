# 第七轮最终链接独立审计

新增报告，无旧版本。本子任务仅读取生产源码、对象、内核和镜像；没有构建、封装或操作设备。

最终结论：修正版 `PASS_STATIC_BINDING`，本审计未发现阻止继续真机测量的静态链接问题。它不代表已测得启动提速，也不代表真实设备上的日志写入、计时或启动稳定性已经通过。

正式证据目录为 `run-20261029T191925973579Z-nova/`：

- 构建：`build-result-20261029T191650465794Z-nova.json`，构建成功且 19 项源码起止哈希与当前源一致。
- raw SHA256：`2048a7352b341a66010911fcde791f7b17e7e68b2d463266d453c5a8aa491f74`。
- payload SHA256：`75cab5a197d104085f9d569f097acc66bea710f1257cc6375346fee0602f76f0`。
- boot SHA256：`96fc42236df897138936a2a9151735c96afdaeb9539786336f09d655da303d2c`。
- `audit-nova.json` SHA256：`6dc2f8a2c6b7214b9effbf62d1809683ac715884a959819a2d9a1fcc0d31d4a7`。

23 个生产对象函数与 raw 除重定位字外一致；28 个所选函数与最终 payload 一致，其中 bsd_init/load_init_program 的既定入口 hook 各排除 4 字节。另解析 28 条真实调用绑定，不以源码存在或符号表出现代替调用验证。

已确认：

- printbuf 的 P/L 两条实参路径到达真实 emitter；cs kill、abort、同步异常、boot-video 四类裸日志传入 R。
- emitter 内有实际 `ISB / MRS CNTVCT_EL0 / MRS CNTFRQ_EL0`，正常路径没有 printf、分配或锁调用；仅保留编译器栈保护失败处理。
- root_device_resolve/root_mount 调用次序保留，root_mount 返回经过 end 后才允许重试；SystemOS/Data 准备计时保留，Data end 在 keystore 通知之前。
- 校准的实际指令位置为 CNTVCT-before `0xfffffff0086a1050`、mach 调用 `0xfffffff0086a1054`、CNTVCT-after `0xfffffff0086a1060`、CNTFRQ `0xfffffff0086a1064`。
- `format-and-host-binding-nova.json` 进一步解析 autologio、clockcal、bootphase begin/end 的格式字符串地址到 x0，再到 printf 调用，中间没有其他 BL；匹配修正版 kern_exec 的 9 项 host 测试通过。
- 实际编译记录为 ARM64、DEVELOPMENT=1、XNU_BOOT_PROGRESS=0，未带 host counter fixture；console 的 enabled/disabled 测试与最终源码、头文件哈希一致，负控有效。
- 8 个 UFS/VerifiedRoot/SHA/CPU/IPC 对象与第六轮正式构建逐字节一致；VerifiedRoot 数据 SHA 与策略/RSA provider 调用保持，CPU 单次 MMIO 的只读窗口、调用和发布顺序重新通过机器码核验。

首个成功编译候选被审计拒绝：raw `21861405c37c149f1aca3e38d7c7e33645e1f40fc066384a7c738882e7cd1036` 的两处 `"isb; mrs ..."` 在 Apple ARM64 工具链中只产生 ISB，clockcal 的 CNTVCT 读取数量为 0，CNTFRQ 为 1。对象与 raw 一致，问题来自真实编译结果。拒绝证据为 `rejected-clockcal-audit-nova.json` 及对应函数机器码、顶层反汇编；这些旧证据保留，不代表最终版本。主代理将分号改为换行并重编后，本审计硬断言两次真实 CNTVCT 读取才通过。

复现最终审计命令（从 Nova 根执行，每次产生新证据子目录）：

```sh
python3 artifacts-nova/boot-speed-round7-nova/linked-diagnostics-audit-nova/audit-linked-timestamps-nova.py \
  --build-result artifacts-nova/boot-speed-round7-nova/build-result-20261029T191650465794Z-nova.json \
  --package artifacts-nova/boot-speed-round7-nova/packaging-nova/timestamp-candidate-final-nova
```

记录解释仍有限制：字符环允许并发交错，快照可能看到正在写入的字节；phase 时间包含 begin 日志的成本；内核时间零点不是 Boot 菜单点击；成功 exec 不能当作 Hello 首次显示。后续离线分析需要保留这些界限。
