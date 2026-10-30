# Disabled GitHub workflows

- `unixnu-main-nova/.github/workflows/docs.yml.disabled` ← `unixnu-main-nova/.github/workflows/docs.yml`: 旧 docs workflow 在 push 时部署 GitHub Pages（external write）。已改名 .disabled。
- `unixnu-main-nova/.github/workflows/macos.yml.disabled` ← `unixnu-main-nova/.github/workflows/macos.yml`: 旧 macOS build workflow 接受外部 device repository/URL 并可上传产物；已改名 .disabled。
- `uefi-sheng-nova/.github/workflows/active_devices.yml.disabled` ← `uefi-sheng-nova/.github/workflows/active_devices.yml`: 旧 active_devices workflow 在 push/PR 时构建并上传设备固件；已改名 .disabled。
- `uefi-sheng-nova/.github/dependabot.yml.disabled` ← `uefi-sheng-nova/.github/dependabot.yml`: dependabot 会创建自动 PR/外部更新；已改名 .disabled。

原文件同时保存在本目录。初始推送不会触发这些工作流。
