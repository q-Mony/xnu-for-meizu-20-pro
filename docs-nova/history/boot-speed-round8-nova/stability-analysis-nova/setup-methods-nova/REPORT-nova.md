# Setup 主线程 semaphore 等待：准确方法与回调路径

2026-10-30。新文件，无旧版本。只读同版 Setup，未执行、修改、重签或部署该二进制；仅新增本分析目录内文件。

**已准确定位到 `-[ProximitySetupController needsToResume]`：它通过 `self.client` 发出 `hasConnection:` 回调请求，然后同步无限等待 semaphore。静态回调先调用 `_setConnected:`，再向同一个 semaphore 发信号。** 这里是邻近设备恢复检查的具体路径，不能把别的线程上看到的语言列表预热直接认作信号来源。

输入 `/Volumes/SydneyUpdate20H392.D22D221OS/Applications/Setup.app/Setup`，UUID `9b4d9b5a97c838d0a692dc686fa7bb4b`，SHA256 `2841aff2cf811c90f1becf4f1bf4a741551376227f79e8af8e57bcfffe553a05`，未滑动 `__TEXT` 基址 `0x100000000`。脚本对 UUID 与 SHA256 均作断言。

| 栈中返回地址偏移 | 返回地址减 4 的调用点 | 所属方法 | 方法 IMP / LC_FUNCTION_STARTS 半开边界 | 调用点目标 |
| --- | --- | --- | --- | --- |
| `0xafc9c` | `0x1000afc98` | `-[ProximitySetupController needsToResume]` | `[0x1000afbf8, 0x1000afccc)` | `_dispatch_semaphore_wait` |
| `0xa094` | `0x10000a090` | `-[SetupController _createNavInAirplaneMode:]` | `[0x100009a10, 0x10000a320)` | ObjC selector `needsToResume` |
| `0xaf50` | `0x10000af4c` | `-[SetupController runWithScene:]` | `[0x10000a744, 0x10000b4b0)` | ObjC selector `_createNavInAirplaneMode:` |
| `0x67c18` | `0x100067c14` | `-[BuddySceneDelegate scene:willConnectToSession:options:]` | `[0x1000679c0, 0x100067ccc)` | ObjC selector `runWithScene:` |

方法名来自 Objective-C method list 的 IMP 与 LC_FUNCTION_STARTS **完全一致**，没有用“最近的符号”跨函数归属。请求的四个调用点都在对应半开区间内。

`needsToResume` 的反汇编连接证据：

- `0x1000afc10` 与 `0x1000afc1c` 先查询 `hasConnection`、`hasResumed`，允许提前返回。
- 进入等待分支后，`0x1000afc2c` 令 `x0=0`，`0x1000afc30` 调 `_dispatch_semaphore_create`；返回对象保存在 `x20`。
- `0x1000afc38–0x1000afc44` 对 self 调 `client` 并保留返回对象于 `x21`。
- `0x1000afc60` 将 block invoke 设置为 `0x1000afccc`；`0x1000afc74` 把 self 与 semaphore 分别写进 block 的 `+0x20`、`+0x28`。`0x1000afc78` 保留 semaphore，随后仍保存在 `x20`。
- `0x1000afc80` 设置 `x2=sp`（block），`0x1000afc84` 设置接收者 `x0=x21`（client），`0x1000afc88` 经 ObjC stub 调用 `hasConnection:`。
- `0x1000afc90` 设置 `x0=x20`，`0x1000afc94` 设置 `x1=-1`，`0x1000afc98` 调 `_dispatch_semaphore_wait`。这条路径没有有限超时。
- block invoke `[0x1000afccc,0x1000afcf8)` 没有本地符号或 ObjC IMP 名称，因此保留地址称呼。其 `0x1000afcdc` 取 block `+0x20` 的 self，`0x1000afce0` 把回调参数放入 `x2`，`0x1000afce4` 调 `_setConnected:`。随后 `0x1000afce8` 取 block `+0x28` 的 semaphore，`0x1000afcf4` 尾调用 `_dispatch_semaphore_signal`。

导入绑定已做双重核对：`__stubs` 的 indirect-symbol table 与 stub 指令实际访问 GOT 上的 chained bind 一致。create / signal / wait 分别是 stub `0x1000f100c` / `0x1000f1018` / `0x1000f1024`，对应 GOT `0x10017cab8` / `0x10017cac0` / `0x10017cac8`。忽略 llvm-objdump 输出中错误的 `_unlink+大偏移` 注释。

解析边界：使用本地 Apple `EXTERNAL_HEADERS/mach-o/fixup-chains.h` 的定义解码，实际两段链均为 `DYLD_CHAINED_PTR_64_OFFSET=6`，16 KiB 页、4 字节链步长。共解出 34,938 个 fixup、301 个 class、5,740 个 ObjC 方法、6,740 个 function starts；5,740 个方法的 IMP 全部精确命中 function starts。small method 的相对偏移按各字段自身地址计算，name 间接 selector 再经 fixup 解引用。未支持的布局直接拒绝，未执行 dyld。

**最小下一定位点：** 检查 `self.client` 的 `hasConnection:` 请求进入、完成回调进入、`_setConnected:` 返回、semaphore signal 四个边界，使用同次请求身份与单调时间。静态代码已证明“谁负责发信号”的回调路径；它还没有证明这次运行由哪个线程执行、回调是否排队却未调度、client 服务为何没有及时完成。不能由本报告断定 UFS、语言预热、蓝牙故障或死锁，也不能把无限等待直接改成跳过恢复检查就称为修复。

可复算工具：`decode-setup-methods-nova.py`，输出固定到一个必须不存在的新目录 `decoded-v1-nova`，拒绝覆盖历史结果。结果包括 `analysis-nova.json`、全部 `methods-nova.json` / `fixups-nova.json` / `objc-stubs-nova.json`、五个函数原始反汇编与 `provenance-nova.json`。补充交叉验证在 `decoded-v1-nova/stub-binding-validation-nova.json`。

本报告与工具均为新文件，无旧文件备份需求；请由父代理将新文件范围追加到中央 `CHANGELOG-nova.md`。
