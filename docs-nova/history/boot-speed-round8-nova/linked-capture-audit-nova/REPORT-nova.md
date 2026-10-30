# R8 capture 最终链接核验

2026-10-30。新文件，无旧版本。本任务只读已完成构建及封装输入，所有证据写到新目录；未构建、未改生产代码、未操作设备。

**结果：`PASS_CAPTURE_STATIC_BINDING`。** 正式候选的采集入口和触发器已链接，实际调用链可达指定 UFS 导出接口；选中的 20 个采集/诊断函数共 15,700 字节，从 raw 到 payload 地址与机器码完全一致，没有排除入口字节。

最终证据为 `final-capture-audit-nova/audit-nova.json`，SHA256 **`91f0ad90f74de415bdf36483170d3428ffa8a64d3a06253ef9687b9c043a9afe`**。父代理已将该 JSON 纳入交付清单，后续补充另存文件，没有原地改写它。

| 对象 | SHA256 |
| --- | --- |
| raw kernel | `e84172cd115af78d814069c160e5201aa20da1ffb2095acea7667b6f181e7da6` |
| payload kernel | `3e180240e6fd595c7206af85839ff5411f298fe504d311ae4b7742ff913ddc6e` |
| boot image | `0a0cff5e8154d80af23f863dd31acf4b3b9645408a1ee826261bd012d9e5409f` |
| `NativeRamBlock.o` | `9051768621309be5522de0c84a0b8d629d91df1ec8d80a0c575915317f276770` |
| `kern_exec.o` | `e9c409065cd2fe26ed5da9609cd74d7821b0f4a5ca837609e5d8a30075e68378` |

核验绑定 `build-result-20261029T201353327311Z-nova.json` 的成功构建和稳定源清单；复核全部当前源哈希、实际编译 recipe 中使用的源码副本和 `WarmCapture.h`、ARM64/DEVELOPMENT/XNU_BOOT_PROGRESS 宏、NativeRamBlock archive 成员和实际 link 输入。复用 R7/R6/R3/R5 的读取器及反汇编 helper 前分别固定哈希，只导入函数，没有执行旧审计 main。

实际原始符号和反汇编显示：

| 调用 | 原始内核中的调用点 |
| --- | --- |
| `meizu_autolog_thread → nova_data_capture_poll` | `0xfffffff0086a717c`，BL |
| `nova_data_capture_poll → native_ram_block_capture_warm` | `0xfffffff0086a7584`，BL |
| `native_ram_block_capture_warm → warmcapture::capture<…>` | `0xfffffff00894f1f8`，BL |
| `warmcapture::capture<…> → native_ufs_export_write` | `0xfffffff00895004c`，BL |
| `warmcapture::capture<…> → native_ufs_log_sync` | `0xfffffff008950054`，BL |
| `native_ram_block_capture_warm → native_ufs_store_gpt_check` | `0xfffffff00894f194`，BL |

`NativeRamBlock::captureWarm` 没有单独发出的函数符号：对应逻辑被编译器内联/合并到采集代码中。这里没有补造一个独立的中间调用。NativeRamBlock.o 的 4 个采集相关函数与 kern_exec.o 的 4 个函数已按非重定位字节绑定到最终 raw；上述实际链接调用点另外核验。UFS 三个 sink 及读盘统计函数也绑定到实际、保留不变的 NativeUfs.o。

raw 与 payload 都实际执行完整 `nm`/`nm --defined-only` 检查，**没有 `_boot_log_export_put` 定义**；本次 raw 完整 nm 中甚至没有这个符号条目。该结论来自成品符号表，并非根据 weak_import 声明猜测。完整 raw 符号表、payload 相关符号和每个选中函数反汇编均随最终 JSON 保存。

R7 保留检查通过：

- 14 个诊断、UFS、VerifiedRoot、SHA、CPU probe 等对象逐字节等于已审计的 R7 快照，快照本身也核对 R7 固定报告哈希。18 份既有源文件 SHA 不变。
- 原有 autolog、IPC/回溯/CPU 统计、BootLog、UFS 日志读写与 SHA 调用仍存在于实际链接内核；没有因新增触发器丢失。
- `kern_exec.o` 整体因新增触发器而变化；补充证据 `retained-kernexec-functions-nova.json` 单独证明 `meizu_autolog_dump`（1,484 字节）和 `nova_lsd_cpu_timing_dump`（680 字节）的 R7/R8 对象函数体、规范化后的重定位目标，以及函数源码均完全相同。补充 JSON SHA256 为 `5b56ccddbb6eaba6b869daec3e9e597493b1e4393a97e97d01ddbec114ef5196`。
- 14 个触发器主机测试用例的 PASS 结果绑定到最终 kern_exec.c SHA `9723d2e7d8104ac9c29702db785c2787ceedf49a552743f466892828c9537bc8`。
- 本审计的 8 个 ARM64 调用图正反控制通过：覆盖已内联/独立函数、返回后的不可达代码、跳过路径、未知寄存器分支、未授权遍历的中间函数等。它不把只出现在未执行路径上的 BL 或未知间接调用算作证据。

本次结论只完成静态交付检查，不代表设备已成功触发采集、导出数据可作为干净文件系统恢复、已经显示 Hello 或已经提速。条件控制流检查保守遍历条件分支，不证明所有运行时条件能够同时满足；寄存器间接目标不作推断。对象绑定列出并排除重定位字，但关键采集调用另由最终机器码核对；raw→payload 的 20 个函数比较没有排除任何字节。

复核入口是 `audit-linked-capture-nova.py`，必须选择明确的 build-result 和新输出目录；正式成品审计还必须传 `--package`。`raw-audit-first-nova` 是此前仅 raw 的成功记录，最终交付以 `final-capture-audit-nova` 为准。新增脚本的唯一改前备份为 `snapshot-nova/round8-capture-audit-method-selector-nova/`，用于精确区分 `captureWarm` 类方法与名称中含此字串的模板实例；各文件及输出哈希见 `PROVENANCE-nova.json`。中心变更记录由父代理统一追加。
