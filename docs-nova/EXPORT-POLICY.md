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

## 实际计数

- 导出文件（不含 `.git`）：见 `provenance/inventory-nova.json` 的
  `summary.file_count` 与 `summary.total_bytes`。
- 策略拒绝：见 `provenance/export-policy-nova.json` 的 `excluded` 分类汇总。
- 最大文件、嵌套 Git、gitlink、symlink、secret 扫描结果见
  `provenance/validation-nova.json`。
