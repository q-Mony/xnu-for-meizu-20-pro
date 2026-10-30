# R11：R10 实际包参数与分支只读绑定

新文件，无旧版本。未操作设备、未重建、未修改 R10 源码或交付物；所有输出限定本目录。此报告属于静态诊断，不证明实机执行过哪一个 payload。

## 结论

1. **已排除 R10 镜像内 DarwinBoot 使用 256 字节参数区的假设。** 最终 TOML args 为 260 ASCII 字节，最后一个参数 `nova-data-warm=1` 从偏移 244 开始。实际 EFI 的 UniXNU 动态分配 UTF16 参数；DarwinBoot 清零并复制 1024 字节，在字符索引大于 1022 时拒绝，而非截断。XNU 的实际 arm_init 复制 1152 字节 boot_args 结构，PE_boot_args 使用相同 `+0x6c` CommandLine 偏移。
2. **NativeRamBlock 与 watchdog 使用同一个真实参数解析入口和同一 key 地址。** 两者在最终 payload 中直接 BL 到 `_PE_parse_boot_argn=0xfffffff00891491c`，key 均为 `0xfffffff00713ec85` (`nova-data-warm`)，输出长度均为 4。未经过不同导入 shim。该 wrapper 每次调用 `_PE_boot_args`，不消费全局解析游标。
3. **新 warm 分支确实已链接进 R10 payload。** watchdog 在参数 found 且值为 1 时，直接调用 `_watchdog_warm_policy=0xfffffff00895a96c`；否则进入旧 cold 分支。相关 policy 函数、PE parser、arm_init、PE_init_platform 与 R10 raw 精确一致，PE/arm_init 正式对象也与 raw 除重定位外一致。
4. **R9/R10 的 Darwin banner 完全相同，不能用于轮次鉴别。** 两者 banner 均为 Oct 29 03:08:52；LC_UUID 不同，R9 `e789b379972336b99ad44e10629c980c`、R10 `2867b49a7ab73bbf8b234cc7a1c3b71b`。这些是构建身份，不是卷身份。
5. **R10 warm 首次 open 的非法 flags 确实存在于 payload。** `_watchdog_warm_policy` 使用 `0x20100100`，实际 `_vn_open_auth` 检测 bit 29 与 `0x200100` 组合，跳至返回 EINVAL 22。与实现代理源代码审计相符。单独的 `failed17 @kern_exec.c:9753` 仍不能证明实机走过新 warm 分支。
6. **实际选择的配置/内核还需运行日志证明。** 实际 UniXNU 先尝试自身 DeviceHandle，再枚举 SimpleFileSystem 并使用首个成功读取的 `\EFI\UniXNU\unixnu.toml`。DarwinBoot 从该 Request.Volume 和 KernelPath 读取内核；本次未发现内核缓存回退，但静态包无法证明当次枚举选中了哪个卷。R11 应有独有运行标记与格式化分支/errno 日志。

## 证据

- R10 boot SHA256：`995a2e784c5cea35280f71171629b16e6fb431a699ca9bec363224c60a120c2f`。
- R10 payload SHA256：`ae3d54df5a2fd5df2f729bf3fb8d1067c2a1e0737925c99b51e098763f84b50a`。
- 实际固件 kernel 字段为 gzip 加 DTB 尾部，已在本目录副本解压；`firmware-extraction-nova.json`、`firmware-inflate-nova.json` 记录长度与哈希。
- UEFIExtract 对本地解压副本提示 volume alignment / no VTF；成功生成层级及全部 leaf dump。实际 DarwinBoot、UniXNU、UniXNUDxe PE 均与 Nova 继承 DEBUG EFI 完全相同，详见 `firmware-module-binding-nova.json`。这些旧构建映射仅作只读证据。
- `firmware-instruction-binding-nova.json` 对实际提取 PE 的关键机器码字逐条断言；`*-actual-nova.disassembly.txt` 留存完整反汇编。
- `payload-binding-nova.json` 保存正式对象/raw/payload 比较、调用目标、字符串与构建身份；各函数反汇编独立留存。
- `parser-probe-nova/` 使用实际 XNU bootargs.c 的逐字节副本，只替换外部头文件依赖为隔离 shim。完整 260 字节参数循环 10000 次，共 30000 次解析均 found/value=1；输入每轮字节不变，ASan/UBSan 通过。该实验验证源解析器，不替代实机身份。

## 可区分的日志

- R9 与 R10 都有 `meizu watchdog policy failed17 @kern_exec.c:9753` 这一通用 panic 来源，不能靠它区别。
- `warm directory`、`warm disabled.plist`、`warm disabled.migrated` 是 R10 独有失败阶段。
- `meizu watchdog policy: ready (volatile warm Data; verified policy entries preserved)` 是 R10 独有成功文本。
- `NOVA-DATAWARM` 已在 R9 存在，不能独立证明 R10。
- 当前 Darwin banner 也不能证明 R10；优先新增简短版本标识、参数 found/value、选中分支、阶段和 errno，并确保 panic 前持久化。
