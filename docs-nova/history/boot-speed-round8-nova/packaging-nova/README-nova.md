# R8 Data capture 封装入口

2026-10-30。新文件，无旧版本。本分支只编写封装工具并做主机检查；没有构建内核、封装可刷候选或操作设备。生产代码和候选构建由父代理负责，未修改历史 R6/R7 packer。

`pack-capture-candidate-nova.py` 使用固定 R7 `timestamp-candidate-final-nova` 作为逐字节基线，只允许替换 `EFI/UniXNU/Payloads/kernel.macho`，以及在 `EFI/UniXNU/unixnu.toml` 的原 Boot args 最后追加一次 **` nova-data-capture=1`**。它直接从固定 R7 boot 提取 FAT 字节，不重新制造固件、分区或 Data seed。

输入核验已通过：R7 boot SHA256 为 `96fc42236df897138936a2a9151735c96afdaeb9539786336f09d655da303d2c`；boot 为 100,663,296 字节（96 MiB），FAT 为 94,371,840 字节（90 MiB）。固定报告 SHA256 为 `51f85f4ec975d922d3852a29eb0405bcce0bcd799909124bea05b52b32e5a89e`。

运行路径均须解析后位于 Nova；输出必须是本 R8 `packaging-nova/` 下从未存在的新目录。只读检查命令：

```sh
python3 <nova-root>/artifacts-nova/boot-speed-round8-nova/packaging-nova/pack-capture-candidate-nova.py --check-inputs-only
```

父代理在最终 kernel 构建和独立审计完成后使用以下入口，运行时仍需确认此 kernel 是选择的最终产物：

```sh
python3 <nova-root>/artifacts-nova/boot-speed-round8-nova/packaging-nova/pack-capture-candidate-nova.py \
  --kernel <nova-root>/unixnu-main-nova/out/meizu20pro/kernel/kernel.macho \
  --output <nova-root>/artifacts-nova/boot-speed-round8-nova/packaging-nova/capture-candidate-final-nova
```

封装守卫：

- 固定 R7、R6、初代封装 helper 的 SHA，固定 R6/R7 boot/report/完整 EFI 文件集合，固定入口补丁源、异常源及 PAN 校验工具。只调用经读取审计的 helper；不运行历史 main，不复制历史环境输入，不访问网络。
- 原始内核必须是未入口打补丁的 prelinked Mach-O。入口补丁只作用于输出副本一次；核验分支目标、PAN，并记录改前改后 SHA。源内核完成前后必须相同。
- 原始和已补丁内核都要求 `_native_ram_block_capture_warm`、`_nova_data_capture_poll`、两项 R7 phase 符号、两项 CPU probe 符号已定义。缺失任一则拒绝。
- 原始与已补丁内核都用完整 `nm` 和 `nm --defined-only` 结果检查 **`_boot_log_export_put` 未定义**。若链接了强或弱定义都拒绝；允许原始符号表中留有未定义弱导入，但会把实际记录写入报告。不能以“源中 weak 声明”或“猜测不会调用”替代这个检查。
- 保存原始完整符号表、defined-only 表，以及 raw/payload 两份 capture/export 符号 JSON。此处符号审计证明链接定义存在/不存在；触发调用点和运行时行为仍由生产审计与设备证据确认。
- Boot entry 身份、默认条目、原来全部字节保留；拒绝任何已存在的 capture 参数（包括 0、空值、重复）、任何原有参数重名、已有 warm/persist/reset/restore 参数；拒绝转义/多行 args、非 ASCII、超出含 NUL 的 1024 字节上限。
- FAT 修改后重新提取完整 EFI，文件名集合和内容 hash 必须仅有 kernel/TOML 两项变化；ADT、trustcache、Data512 seed 另做逐字节比较。Data512 的 DBRDISK1 头和逻辑长度 512 MiB 不变。
- 96 MiB boot、90 MiB FAT、固件、Android header 除 ID 字段、DTB、尾部及继承 AVB 字节保持不变，重算并验证 Android v0 SHA1 ID。不产生新的 AVB 签名。
- 任一生成阶段失败会写 `DO_NOT_FLASH-round8-packaging-incomplete-nova.txt`，失败目录不能重用。正式 PASS 仍明确 `hardware_tested=false`、`device_accessed=false`。

`--self-test-only` 只允许输出名以 `DO_NOT_FLASH-` 开头；它可接受旧内核缺少本轮符号，用于离线封装流程演练，但仍拒绝 `boot_log_export_put` 已定义，且始终写 `DO_NOT_FLASH.txt`。本分支没有运行这个封装演练，也没有生成镜像。不能把它当正式候选旁路。

已完成验证：

- 10 项真实配置转换/符号拒绝测试通过（包含多个子用例）：精确追加、重复/已有参数、warm/persist、Boot 身份、字符/长度边界、CRLF 保留、强/弱 GPU 写者定义拒绝、旧内核定义拒绝。证据 `config-symbol-tests-nova.json`。
- 4 项实际 CLI 拒绝通过：缺参、只读模式混用、输出根目录、固定旧内核。全部没有创建候选输出目录。证据 `cli-rejection-checks-nova.json`。
- 固定输入 `--check-inputs-only` 实际返回 `INPUTS_VERIFIED`。未执行入口补丁/PAN/mcopy 生成路径；最终新 kernel 到位后需运行一次实际封装并检查其 `evidence-nova/verification-nova.json`。

新增文件和哈希见 `PROVENANCE-nova.json`。本分支未修改已有文件，不需要改前备份；中心变更记录由父代理统一更新。
