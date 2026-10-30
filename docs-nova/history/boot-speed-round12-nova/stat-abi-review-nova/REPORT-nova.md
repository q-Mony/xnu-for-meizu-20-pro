# R12 pre-exec stat ABI review — Nova

结论：**排除“PID 1 尚未 exec，因而 sys_fstat64 使用 32 位布局，copyin(sizeof stat64) 导致 op9”作为本次目录检查失败的直接解释。** 未修改生产代码、设备、挂载状态或目录允许条件。

## 运行时与来源

R11 回收日志 `logs-nova/boot-speed-round11-nova/candidate-run-nova/autolog-nova.txt` 的 SHA-256 为 `5f8fdb18993618314f12710fd1b9cb46ee130a90da5c9e62800efb890e19bdbd`。第 170 行 `meizu: is64=1`；第 171 行 scratch 为 `0x100000000`、大小 `0x10000`；第 237 行 data_prepare result=0；第 242/244 行 warm branch=2、op=9、errno=22。warmload digests verified=1。日志尚未记录目录 mode/uid/gid/created，不能断言具体哪一项不符。

`bsd_init.c:621` 给 kernproc 设置 P_LP64；`:1396` 从 kernproc clone PID 1；`kern_fork.c:703–728` 继承地址位数及 P_LP64/UT_LP64；`kern_proc.c:2006` 的 IS_64BIT_PROCESS 直接检查该标志。无需先 exec 才成为 LP64。R11 的实际 is64=1 与源码一致。

## 真实 ARM64 目标对象的字段布局

提取既有 R11 BootWatchdogPolicy-target-nova.o 与同构建 kern_descrip.o 的 DWARF，未用 macOS host struct 代替目标 ABI。脚本断言 policy stat64 与 syscall stat64 的所有字段偏移和总长完全一致。

| 结构 | sizeof | st_mode | st_nlink | st_ino | st_uid | st_gid | st_atimespec | st_size |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| policy stat64 | 144 | 4 | 6 | 8 | 16 | 20 | 32 | 96 |
| syscall stat64 | 144 | 4 | 6 | 8 | 16 | 20 | 32 | 96 |
| user64_stat64 | 144 | 4 | 6 | 8 | 16 | 20 | 32 | 96 |
| user32_stat64 | 112 | 4 | 6 | 8 | 16 | 20 | 28 | 64 |

mode_t → __darwin_mode_t → __uint16_t；nlink_t 也为 __uint16_t；ino64_t → __darwin_ino64_t → __uint64_t。两个 16 位字段占 4–7 字节，inode 在偏移 8，不存在由 inode 对齐挤移 uid/gid 的差别。

`sys_fstat64` 强制 isstat64=1，`fstat` 按传入 proc 的 IS_64BIT_PROCESS 选择 user64/user32。两个 munge 函数均按字段原值复制 mode/uid/gid，未修改属主。R11 正常选择 user64，输出 144 字节与 copyin 长度一致。即使反事实地选 user32，其 112 字节输出中的 mode/uid/gid 仍在相同偏移；读取额外尾部可令 times/size 等无效，却不能单凭布局差异改变 op9 使用的三字段。若存在任意内存破坏，则属于另一假设，不能再归因于此 ABI 差异。

## op9 精确含义

该路径已经完成 mkdir（0 或 EEXIST）、O_DIRECTORY|O_NOFOLLOW_ANY open、sys_fstat64、copyin。op9 仅要求目录类型、权限低 12 位精确 0755，及属主为 501:20、0:0，或“本次 mkdir 成功创建且为 0:20”。已有 0:20 会被拒绝。op9 发生在 disabled.plist/disabled.migrated 内容处理前，与文件内容、stat.st_size 或尾部差异无关。

## 按当前证据排序的可证伪假设

1. **运行时属主组合不满足条件。** 如已存在目录返回 0:20，created=0；离线 owners-enabled fixture 返回 501:20 尚不能替代运行时值。测量 raw uid、gid、created、mkdir errno；若为 501:20 或 0:0 则排除此项。不得据猜测加 allowlist。
2. **运行时权限位不满足精确 0755。** O_DIRECTORY 已成功，因此权限位不匹配比“它根本不是目录”更值得先核查。测量 raw mode（八进制及原值）；若 S_ISDIR 且 mode&07777=0755 则排除此项。
3. **stat 返回缓冲或打开对象异常，非 32/64 位布局错误。** 当前没有支持该项的直接证据，仅当上述值完全符合而 op9 仍失败时再查。先固定记录 op9 当次实际读取的 mode/uid/gid/created；如果它们已经解释失败，不扩展诊断。若全部符合却仍失败，再对同一次 fstat 的 scratch 原始前 28 字节、copyin 后值和 fd/vnode 身份做对照，区分缓冲变化、对象选错或诊断归属错误。

推荐下一轮只增加 mode/uid/gid/created/mkdirerrno 的有界诊断，保持现有失败行为及元数据规则。先确认拒绝的具体分项，再决定修复；本审计不构成设备通过或启动提速结论。

原始 DWARF、结构化字段表、相关源码段、R11 日志行和输入 SHA 均见 evidence-nova.json。
