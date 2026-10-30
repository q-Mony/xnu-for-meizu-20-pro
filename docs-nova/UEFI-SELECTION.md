# UEFI 源码选择（uefi-sheng-nova）

本导出只保留 Meizu 20 Pro UEFI 构建实际引用的源码，避免把整个 4 GB 级
checkout（含 `Binaries`、`Build`、OpenSSL 测试数据和厂商二进制）复制进源码仓库。

## 包含

| 路径 | 原因 |
|---|---|
| `README.md`、`LICENSE`、`Building.md`、`Status.md`、`build_uefi.py`、`build_active_devices.sh`、`setup_env.sh`、`pip-requirements.txt`、`.gitignore` | 顶层构建/许可/说明 |
| `BootShim/` | 启动链源码（含 Nova 修改） |
| `Common/Mu/` | `DeviceBuild.py` PackagesPath；上游源码（测试 cert/bin 按策略排除） |
| `Common/Mu_OEM_Sample/` | `DeviceBuild.py` PackagesPath |
| `Platforms/Meizu/` | Meizu 20 Pro 平台源码（含 `RAW.inc`/`DXE.inc` 与 `meizu20pro.toml`） |
| `Silicon/Qualcomm/`、`Silicon/Silicium/` | `DeviceBuild.py` PackagesPath；`SiliciumPkg`、`SiliciumNullPkg` |
| `Silicium-ACPI/Common/`、`Silicium-ACPI/Platforms/Meizu/`、`Silicium-ACPI/Silicon/Qualcomm/` | `DeviceBuild.py` 的 PackagesPath |
| `Resources/Configs/`、`Resources/MuPatches/`、`Resources/Scripts/` | 板级配置/补丁/构建脚本 |

`OpenSSL` 子模块以固定 commit 的说明文件代替完整源码；`Mu_Basecore` 以固定
commit + 本地补丁 `provenance/patches/mu_basecore-local-changes.patch` 代替完整
checkout。**`prepare-source-nova.py` 不初始化这两者**；它只处理
`sources-nova/{xnu,libdispatch,libpthread}`。UEFI 依赖需按
`docs-nova/BUILD.md` 第 3 节手动准备：clean pinned checkout、应用 Mu 补丁、把
OpenSSL checkout 放到脚本期望路径。

## 排除

- `uefi-sheng-nova/Binaries/`：厂商/设备二进制，完全排除；`meizu20pro` 必须由
  用户从本地固件提取，禁止上传其内容。见 `docs-nova/BINARIES.md`。
- `uefi-sheng-nova/Build/`：EDK II 构建输出。
- `Mu_Basecore/` 完整源码：900 MB 级上游依赖，按固定 commit + patch 提供。
- `Silicon/Silicium/OpensslPkg/Library/OpensslLib/openssl/`：按固定 commit 提供。
- `Silicon/Silicium/OpensslPkg/Library/OpensslLib/OpensslGen/`：OpenSSL 生成源，
  可由固定 OpenSSL 源码重新生成，避免 23 MB 生成文件与 4 MB 级 asm。
- `Silicon/MediaTek`、`Silicon/Nvidia`、`Silicon/Samsung`、`Silicium-ACPI/Platforms/`
  的其他设备目录：非 Meizu 20 Pro 构建依赖。
- `Resources/DTBs/`、`Resources/Pictures/`、`Resources/ramdisk`：设备 DTB/媒体/
  ramdisk 二进制，不是源码。
- 所有 `*.aml`、`*.cer`、`*.pfx`、`*.pvk`、`*.bmp`、`*.efi`、`*.bin` 等二进制/
  测试证书材料：由策略按内容/扩展名排除。
- `.github/workflows/active_devices.yml` 与 `.github/dependabot.yml`：已重命名为
  `.disabled`，不会在初始推送时自动构建设备或创建外部更新。

## 构建时补齐

XNU/libdispatch/libpthread 由 helper 恢复：

```sh
python3 prepare-source-nova.py --fetch-references   # 显式联网拉取固定上游
# 或提供一个已固定在 provenance/upstream-pins-nova.json commit 的参考目录
python3 prepare-source-nova.py --reference-root /path/to/references
```

UEFI 依赖不经过 helper：`Mu_Basecore` 需在 clean pinned base 上应用本地补丁
（11 个文件）；`OpenSSL` 需 checkout 到
`67b5686b4419b4cb8caa502711c41815f5279751`。完整命令见
`docs-nova/BUILD.md` 第 3 节。`Binaries/meizu20pro` 不由本仓库提供。
