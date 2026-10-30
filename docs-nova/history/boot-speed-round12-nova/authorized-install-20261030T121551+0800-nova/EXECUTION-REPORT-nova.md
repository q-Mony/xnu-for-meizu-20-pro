# R12 授权安装与启动 — 执行报告（dsh）

- 日期：2026-10-30
- 任务目录：`<nova-root>/artifacts-nova/boot-speed-round12-nova/authorized-install-20261030T121551+0800-nova`
- 授权：用户原话“安装”，仅限已审核 R12 候选 `c8addd613a564fc2…`（96 MiB）。
- 执行器：dsh，deepseek-official/deepseek-flash，reasoningEffort=high；未委派、未换模型。
- 最终验收：Sol。本报告只陈述已执行阶段与脚本产物，不把“发出启动指令”等同 Hello。

## 结论

本地复核、只读 check、export_preflight、manifest 授权门、install、start 六个阶段全部返回成功。
`boot_a` 已写入候选并通过整分区回读；`boot_b` 保持 `BASE_B` 未变；Fastboot 已确认 `current-slot: a` 并发出重启。
当前状态：**XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION**，即等待画面/Hello 确认，尚未确认进入 XNU。
未自动返回 Android，未声称修复 e22，未声称提速。后续画面确认由 Sol 询问用户。

## 阶段与产物

| 阶段 | 状态 | 产物（sha256 见 execution-result-nova.json） |
|---|---|---|
| 1 本地复核 | ALL_MATCH：29/29 source、27/27 verification artifact、boot_image sha 与 100663296 字节 | `<nova-root>/artifacts-nova/boot-speed-round12-nova/authorized-install-20261030T121551+0800-nova/LOCAL-VERIFICATION-nova.json` |
| 2 只读 check | READONLY_DIRECT_WRITE_PRECONDITIONS_CHECKED；设备未变更；活动槽 b | `<nova-root>/logs-nova/boot-speed-round12-nova/check-device-nova.json` |
| 2 暖输入预检 | EXPORT_PREFLIGHT_PASS；export `7e6cc19843c0ee28…`；GPT 与备份复核通过 | `<nova-root>/artifacts-nova/boot-speed-round12-nova/authorized-install-20261030T121551+0800-nova/EXPORT-PREFLIGHT-nova.json` |
| 3 授权与备份 | manifest 原样备份哈希 `6c276006…`；仅改 status/authorization/installation_allowed/requires_explicit_new_user_install_command 并追加授权记录路径 | `<nova-root>/snapshot-nova/before-r12-user-authorized-install-20261030T121700+0800-nova/delivery-manifest-nova.json`；`<nova-root>/artifacts-nova/boot-speed-round12-nova/authorized-install-20261030T121551+0800-nova/AUTHORIZATION-nova.json` |
| 4 install | INSTALLED_A_AND_FULL_READBACK_VERIFIED_IN_ANDROID_B；candidate_sha256 正确；full_partition_readback_match=true；boot_b_unchanged=true；临时镜像已删除 | `<nova-root>/logs-nova/boot-speed-round12-nova/install-device-nova.json` |
| 5 start | XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION；AndroidB/A/B 全量哈希/GPT/暖输入复检后，Fastboot set_active a 并 reboot | `<nova-root>/logs-nova/boot-speed-round12-nova/start-device-nova.json` |

## 设备证据摘要（不含串号）

- 安装前：slot b，`boot_a=67ce00ec…`（BASE_A），`boot_b=7df205f1…`（BASE_B）。
- 安装/启动记录：`boot_a=c8addd613a564fc2114d8920a5e2798bfeef8b66074577a4a0e3fced94368129`，`boot_b=7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2`。
- 允许的写入仅：暂存候选到 `/data/local/tmp`、写 inactive `boot_a`、A 槽选择与一次启动重启。未写 `boot_b`，未改 GPT/分区表，未改 unixnu 根盘/暖 Data。
- 失败：无。脚本阶段记录未覆写，重试保护保留（各 phase JSON 均为 `"x"` 独占创建）。

## 边界

未确认 Hello、未确认启动耗时；不声称 e22 修复或性能提升。未安装任何外部仓库代码；未更新 CHANGELOG（留待 Sol 验收后记录）。
