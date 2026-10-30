# 导出策略与排除规则

本仓库由 Nova 当前工作内容导出，**只含源码/文档/固定来源与可移植准备脚本**。

## 目录级

包含：`unixnu-main-nova/`（除 `out/`、`build/`、`.git/`、备份）、
`unixnu-meizu20pro-nova/`（除 `.cache/`、`build/`、`.git/`、备份）、
`uefi-sheng-nova/` 的源码子集、`sources-nova/`、`configs-nova/`、`tools/`、
`docs-nova/`、`tools-nova/history/`、`provenance/`。

排除：`inputs-nova/` 全部；`snapshot-nova/`；`logs-nova/` 原始日志；所有原始
`out/`、`build/`、`Build/` 输出（仅显式导出 `.config` 与
`owner-data-volume.h`）；`.git` 与嵌套仓库；`node_modules`；SDK/工具链/cache；
`uefi-sheng-nova/Binaries` 与 `Build`；`Resources/DTBs`、`Resources/Pictures`；
`Mu_Basecore` 完整 checkout、OpenSSL 完整 checkout、`OpensslGen` 生成源，以及非
Meizu 平台/非 Qualcomm/Silicium UEFI 源码。UEFI 选择详见 `docs-nova/UEFI-SELECTION.md`。

## 文件级拒绝

扩展名：`.img .ipsw .dmg .im4p .macho .o .a .dylib .efi .fd .fv .bin .mbn .fw
.dtb .dbr .bc .spv .metallib .zip .tar .gz .xz .zst .zstd .pyc .pdb .obj
.lib .dll .exe .so .pem .key .p12 .pfx .jks .keystore`。

备份后缀：`.bak .pre .old .orig .rej ~`（含 `.bak-*` 这类带描述后缀）。
`.env`、`local.toml`、SSH 私钥、token/凭据文件一律排除。
未知扩展名按内容嗅探；含 NUL 的二进制或伪装成无扩展名的二进制被拒绝。
单文件上限 50 MiB（实际导出无文件接近该上限）。

Markdown/文档用 SVG 与小尺寸 PNG/JPG 可保留；本轮只保留源码树内测试/文档图像，
不导入无关媒体。

## 扁平化与 Git

- 主仓库与 UEFI 的子模块按**普通源码目录**导出，不含嵌套 `.git`，因此没有
  gitlink；原子模块 URL/HEAD 记录在 `provenance/submodules-nova.json` 与
  `docs-nova/PROVENANCE.md`。
- 原 `.gitmodules` 不放入工作树（避免被误认作活动子模块），作为归属参考保存到
  `provenance/gitmodules/`。
- `.github/workflows` 中会触发自动部署/设备构建/外部写入的工作流已重命名为
  `.disabled`，不会在初始推送时运行；原文件同时保存在
  `provenance/disabled-workflows/`，说明见 `provenance/disabled-workflows/README.md`。

## 历史文档与工具归档

- `docs-nova/history/`：收录 `artifacts-nova/` 下作者 Markdown 报告/计划/README，
  保留原相对结构并做隐私替换。排除本导出子树、`TASK-nova.md` 智能体提示、
  第三方 `reference-sources`/`pinned-sources`/`upstream-*`/`sources-nova` 克隆、
  snapshot/failed-run 目录、dsh 事件/transcript/wrapper/config、原始日志/手机数据。
- `tools-nova/history/`：只收录有独立分析价值的作者 build/test/analysis 源码，
  包括 canonical R18 stream SHA 测试和 R18 runtime analysis。排除 `run-dsh-*`、
  设备安装/刷机/恢复编排、per-run 生成 fixture 与二进制。
- 历史脚本保留原始假设并明确标记为归档，不是可移植构建入口；本轮未执行其中任何
  硬件/设备/日志采集步骤。
- `provenance/history-sources-nova.json` 记录每个归档文件的原始与导出 SHA-256。
- 本仓库**不包含**全部原始证据、完整日志或全部第三方研究代码。

## 写入安全

`prepare-source-nova.py` 在第一次 `mkdir/copy/move/clone/fetch` 前检查所有目标与
祖先：`out`、`tree`、`tree/.cache`、`tree/build/xnu`、`.config`、owner 头、
`out/feeds` 与 cache 路径。任何目标或祖先是符号链接时立即失败，不跟随、不删除
链接，也不在 checkout 外写 marker；dirty cache 默认拒绝覆盖，显式
`--replace-cache` 才会移动到带时间戳备份。

## 实际计数

- 导出文件（不含 `.git`）：见 `provenance/inventory-nova.json` 的
  `summary.file_count` 与 `summary.total_bytes`。
- 历史文档/工具计数：见 `provenance/history-sources-nova.json` 的 `summary`。
- 策略拒绝：见 `provenance/export-policy-nova.json` 的 `excluded` 分类汇总。
- 最大文件、嵌套 Git、gitlink、symlink、secret 扫描结果见
  `provenance/validation-nova.json`。
