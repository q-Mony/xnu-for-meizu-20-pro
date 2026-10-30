# R13 失败日志回读报告（只读）

状态：`CAPTURED_R13_FAILURE_AT_DISABLED_MIGRATED_GID99_AWAIT_SOL_REVIEW`。用户确认 Fastboot 后仅做 A→B 回退与日志回读；未刷写、未改源、未安装/启动新镜像。

设备：Android B；boot_a R13 `9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2`；boot_b 原 `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2`；GPT/暖 export `7e6cc1…` 未变；`device_mutated_this_task=false`。证据 `post-failure-device-nova.json` `617663…`。

捕获：32MiB 窗口，ring `33922`，text `34095B`，dropped `0`，NUL `0`，251 行；raw `dd7c89…`，text `2e5eb0…`。

日志：242 行目录 `mode=040755 uid=99 gid=99 nlink=5 dev=16777221 ino=100`；243 行 R13 `policy-owner-compat version=13` 已出现；244 行 `disabled.migrated mode=0100644 uid=0 gid=99 size=0 nlink=1`；245 行 `op=12 stage=warm disabled.migrated errno=22 branch=2`；246 行 `FAILED`；247 行 `save=attempting`。`disabled.plist` 无失败 metadata，未观测其 owner，不能假设 99。R13 的 99:99 目录兼容已生效。

断言复核：`watchdog_file_metadata`（140-147）对 marker 要求 regular、uid0、nlink1、0644、gid 0/20、size0；观测仅 `gid=99` 不满足，其余均通过。180 行 `before.st_dev != parent->st_dev` 因 `||` 短路未执行；文件 dev 未打印、未观测；identity/content/EOF 未到达。type/mode/owner/content 检查不得放宽。

屏幕与日志：`e=22 b=2 op=12 stage` 一致；`save=1` 仅来自用户屏幕，日志只有 `save=attempting`，无完成行；`act_set init ast=804200 reason=200`、caller `0xfffffff0086a2904`、`kern_exec.c:9822` 不在本 32MiB 窗口。

暖/数据：`warmload status=0 verified=1`，payload/logical 与固定 R8 值一致；`data mount rw error=0` 且 `data_prepare result=0`。

结论：R13 目录修复通过后，失败前移到已存在 `disabled.migrated` 的 `gid=99`；硬件启动失败（用户屏幕），未到 Hello，未测提速，APFS 99 来源仍未证明，stat ABI 假设已排除。

最小建议（未实施）：仅在现有 99:99 目录兼容路径允许 marker `gid=99`，保留其余全部文件断言与 `st_dev` 检查；补 host 测试覆盖 99/20/0、created、mixed、`st_dev` 与 non-marker 99，并显式观测 dev。证据 `evidence-nova.json` `9f42ae…`；等待 Sol，不安装下一步。
