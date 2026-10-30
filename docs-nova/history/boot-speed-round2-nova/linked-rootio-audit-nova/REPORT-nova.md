# Root I/O 实际链接审计 — Nova

2026-10-29。新文件，无旧版本。只读源码、编译记录、对象、archive 和内核；只在本目录创建审计文件，未改生产、CHANGELOG 或设备。本报告区分已阻断的初包与修正包。

**修正后的 raw kernel `3f590ddaeaef16c0b7a0fe98895e9d70279a23ba571ed53b95bcc8b1f7edf885` 通过本次静态接入审计。** heartbeat 真实调用统计函数，统计真实调用 `printf`，128 KiB 批量读取与 16 KiB 逐块认证代码均在实际链接内核中。没有发现使用旧 `kern_exec`/NativeUfs 代码的阻断项。此结论不等于已经真机提速或已经取得运行日志。

## 两轮结果

| 输入 | 结果 |
| --- | --- |
| 初轮 raw `83f011e0…6728b`、镜像 `055cbf95…f4eba` | **BLOCK**：统计函数存在，但 heartbeat 无调用；统计仍调用 `kprintf`。主代理收到阻断后修正并重编，未将本报告作为初包放行依据 |
| 修正 raw `3f590dda…df885` | **PASS**：`_meizu_autolog_dump → _native_ufs_log_read_stats → _printf` 的真实 AArch64 BL 均已解码核验 |
| 修正后最终提取 payload `55a3b5a2d2a9d6a11b150109cb039ad930749a3ef85662fa2ab2bebdb7fb8a35` | 三个关键函数（heartbeat、stats、ufsReadWindow）与修正 raw 逐字节相同，包含已链接重定位后的指令；两级 BL 调用仍存在 |

初包 raw 在修正构建后已被替换。本次先在它尚存在时读取并检查，无 heartbeat→stats 的 BL；随后从保留的初包 payload 再次独立检查，结果一致。旧包证据：[old-payload-audit-nova.json](<nova-root>/artifacts-nova/boot-speed-round2-nova/linked-rootio-audit-nova/blocked-83f011-nova/old-payload-audit-nova.json)。它明确记录的是保留 payload，未冒充仍可读取的旧 raw。

旧包构建时 `kern_exec` 确实重新编译：对象 SHA 从原备份 `3d275440…891d` 变为 `1a8a5270…d111`，mtime 为 16:17:19。但对象中的 `meizu_autolog_dump` 仍为原来的 688 字节，body SHA 都是 `fa982e7435e27d9f0a98c251a8e53d297d59c64493f8f811d5c08f64e50ba3fd`，且对象没有 `_native_ufs_log_read_stats` 符号引用。`XNU_NATIVE_UFS` 条件块被编译掉；整体对象变化不能替代调用接入证据。

## 修正轮源码与实际对象

[build-result-rootio-fixed-nova.json](<nova-root>/artifacts-nova/boot-speed-round2-nova/build-result-rootio-fixed-nova.json) 记录构建 16:22:23–16:23:09（Asia/Shanghai），退出码 0，前后源码哈希不变。本次重算相符：

| 输入 | SHA256 |
| --- | --- |
| RootWindowRead.h | `4f6d615d6e31b90b4dc804211cfd13d7d8778d44aeb2580446dfa4d6dd7b1888` |
| NativeUfs.cpp | `f719119ec580a68d1d4817bd01290e7cd0e0e171b782220a9b696647a4d1ee83` |
| kern_exec.c | `da6059318757f7cd1e42c60eb2a913669cb928944ebe8abf49682e1b19408ba1` |
| NativeUfs.o | `c2fa5a1729cd18107b8ffd8630160ffbf1c3a1fd5a2175f872ffedcb727bba1f` |
| libNativeUfs.a | `5466c73c09f21bd45b82a3de5c648e51747f19c1c4e1f69959ea282308827aa7` |
| kern_exec.o | `7c2d9fb24a076cfe2e2f71c4d3dd3d9e0517056f342d2bbde05a7a0ef50a77d2` |

`NativeUfs.o` mtime 16:22:53，`kern_exec.o` mtime 16:23:02；[当前 kernel.log:1240](<nova-root>/unixnu-main-nova/out/meizu20pro/logs/kernel.log:1240) 有 `CC kern_exec.o`。它已不是 bootstrap 恢复时 mtime=1、SHA `3d275440…891d` 的旧对象。新的 heartbeat body 为 736 字节，SHA `d5f99d6f74b385869e9905189ba035c0e80ae8889de13daaea49600a5675067e`；对象有 `_native_ufs_log_read_stats` 未定义引用。

当前 archive 抽取的 `NativeUfs.o` 与独立对象逐字节相同。链接参数对 Nova 的 `libNativeUfs.a` 使用 `force_load`。136 个可按名称识别的 NativeUfs text 函数，以及 kern_exec 的 heartbeat，在对象和修正 raw 之间全部非重定位指令字一致。对象中 8 个 `_OUTLINED_FUNCTION_N` 本地名字可能与其他对象重名，未用名字字典强行认定其身份；排除项已记录。所有 ARM64 重定位字另列，未声称全文件链接重定位/数据指针均已独立认证。

完整新旧源码/对象哈希、mtime、编译入口和逐函数比较：[audit-nova.json](<nova-root>/artifacts-nova/boot-speed-round2-nova/linked-rootio-audit-nova/fixed-3f590d-verified-nova/audit-nova.json)。本次不再做独立重编，依据是本轮编译记录、源码哈希、对象差异、archive 成员一致性及实际链接指令比较。

## 实际机器码

[调用链证据](<nova-root>/artifacts-nova/boot-speed-round2-nova/linked-rootio-audit-nova/fixed-3f590d-verified-nova/callchain-nova.json)：

- heartbeat 地址 `0xfffffff00869f798`；`0xfffffff00869f9cc` 的 `0x940a83d2` 是 BL，目标为 stats `0xfffffff008940914`。
- stats 中 `0xfffffff0089409b0` 的 `0x97e467d2` 是 BL，目标为 `_printf`；没有直跳/BL 到 `_kprintf`。函数读取 `CNTFRQ_EL0`、`CNTPCT_EL0` 和累计软件计数器。
- 新调用仍在 heartbeat 的低频分支内，源码每 200 次已获得 busy 的 dump 调用触发；调用数不是固定时间单位。

[ufsReadWindow 反汇编](<nova-root>/artifacts-nova/boot-speed-round2-nova/linked-rootio-audit-nova/fixed-3f590d-verified-nova/batch-nova.disassembly.txt) 中：

- `0xfffffff008941b1c` 装入 `0x20000`，`...1b74` 以它截断待覆盖字节数，`...1b98` 再检查物理读取不超过 128 KiB。之后将字节数右移 12 位作为 4 KiB block 数传到 `hostRead4k`。
- `...1bd0` 真实调用 `hostRead4k`，前后读取 CNTPCT 并以 `ldxr/stxr` 原子累计 read ticks；命令数、成功命令数、物理字节数也有更新。
- `...1c28` 每轮认证偏移增加 `0x4000`，`...1c38–1c40` 将每个认证块长度限制为 16 KiB；`...1c6c` 真实调用 `vr_verify_block`。尾块使用实际剩余长度，4 KiB 传输填充不进入该长度。
- `...1c8c` 在认证失败时进入错误路径；只有整批认证循环成功退出后才到 `...1cfc` 的 `IOMemoryDescriptor::writeBytes` 虚调用。计时还覆盖 verify/copy，并累计认证与复制字节数。
- 新 batch 函数为 928 字节，原备份为 616 字节，内容不同；不是把旧 16 KiB 一次读取函数继续链接进来。

完整请求入口也与对象匹配，包含请求数/请求字节数计数；正常完成或出错后的 completion 调用仍在 `ufsReadWindow` 返回并释放 window lock 后。更完整的边界/失败语义测试由并行测试代理负责，本审计不重复宣称已覆盖全部运行情形。

最终包对应证据：[final-payload-callchain-nova.json](<nova-root>/artifacts-nova/boot-speed-round2-nova/linked-rootio-audit-nova/fixed-3f590d-verified-nova/final-payload-callchain-nova.json)。父代理另负责 PAN、SHA 汇编、打包 envelope、分区状态和设备验证。本报告不替代这些检查，也不证明实际请求一定达到 128 KiB；真实批量分布和耗时需从下一轮运行日志读取。

## 可复查入口

使用 [audit-linked-rootio-targeted-nova.py](<nova-root>/artifacts-nova/boot-speed-round2-nova/linked-rootio-audit-nova/audit-linked-rootio-targeted-nova.py) 指定新输出目录名和 kernel SHA。它调用同目录基础脚本，排除无法按名字唯一识别的编译器 outlined helper，保存反汇编并对两级 BL 单独解码。基础审计首次尝试遇到重名 outlined helper 时失败，未出具 PASS；修正后的审计范围及限制如上。新建输出目录禁止覆盖已有结果。

这是静态构建/接入 PASS，仍需要真机日志确认实际执行、存盘和启动用时；未将编译成功或函数存在当作性能改善。
