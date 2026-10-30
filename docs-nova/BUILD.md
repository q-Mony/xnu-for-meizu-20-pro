# 构建与准备（source-only）

## 前提

- macOS + Xcode/Command Line Tools（系统编译器与 SDK 只读使用）。
- `git`、`python3`、`cmake`、`ninja`、`unifdef`、`ditto`（macOS 自带）等。
- 仅在本 checkout 内构建；不要指向任何其他会话的可写源码目录。

## 1. 检查快照与配置

```sh
python3 prepare-source-nova.py --dry-run
python3 prepare-source-nova.py --verify-only
```

`--dry-run` 不写文件、不联网。`--verify-only` 校验 `sources-nova/*` 的清单与
`configs-nova/meizu20pro-r18.config` 是否存在。

## 2. 准备构建树（两种模式）

### A. 已有本地上游参考 checkout（推荐，避免联网）

把 `refer/xnu`、`refer/libdispatch`、`refer/libpthread` 指向已固定到
`provenance/upstream-pins-nova.json` 中 commit 的 checkout，然后：

```sh
python3 prepare-source-nova.py --reference-root /path/to/references
```

### B. 显式允许联网拉取上游开源依赖

```sh
python3 prepare-source-nova.py --fetch-references
```

只从 `provenance/upstream-pins-nova.json` 记录的公开 upstream URL 拉取固定 commit；
不会下载 Apple 专有 kernelcache/SDK，也不会复制厂商二进制。

准备脚本会：

1. 只写入本 checkout 的 `unixnu-main-nova/out/meizu20pro/tree/`；
2. 在 `refer/` 建立固定 commit 的 base Git 元数据（新 clone，不复用其他仓库对象）；
3. 把 `sources-nova/xnu` 恢复到 `.cache/xnu-work`，把
   `sources-nova/libdispatch` / `libpthread` 恢复到对应 `.cache/*-work`；
4. 将 `configs-nova/meizu20pro-r18.config` 选为 `out/meizu20pro/.config`；
5. 放置 `configs-nova/owner-data-volume.h` 到 `tree/build/xnu/owner-data-volume.h`。

脚本不会执行 `git clean`，不会删除快照，也不会覆盖非本 checkout 路径。

## 3. 构建

```sh
./unixnu-nova build kernel -j8
```

`build-xnu-4k-baseline.sh` 会基于已恢复的 `.cache/xnu-work` 工作树继续应用
`patches/xnu/` 中尚未应用的补丁；**不要**先运行会重置该目录的通用 clean/fetch。

> 网络边界：`prepare-source-nova.py` 本身默认离线，只有 `--fetch-references`
> 才拉取它负责的三个固定 reference。正常 CLI 构建在缺少其他 feed（如
> libplatform/AvailabilityVersions/corecrypto/IOStorageFamily/edk2）或
> `.cache/sources/*.json` 头文件元数据时，仍可能按既有脚本自行访问上游；
> 本轮未做“完全离线构建”验证。该行为不会覆盖已恢复的 Nova XNU 源码。
当前 `patches/xnu/` 只保留 `zzzz-park-marker.patch`，其余早期补丁已包含在
`sources-nova/xnu` 快照中。

## 4. 必须由用户自备的输入（不分发）

- 原始 Apple kernelcache（用于 prelink / 符号）；
- 设备固件、DTB、ADT、trust cache / owner-signed root 策略输入；
- 设备暖 Data 分区内容或 data seed；
- GPU addon、Wi-Fi MAC 等私有输入；
- 厂商 `uefi-sheng-nova/Binaries/meizu20pro`（按原 `scripts/import-meizu-uefi.py`
  或既有提取脚本从用户本地固件提取）。

缺少上述输入时，内核可编译但完整 boot/payload/开机流程不能保证；本仓库
**不声称** fresh clone 一条命令即可复现开机。

## 5. UEFI 依赖

- `Common/Mu`、`Common/Mu_OEM_Sample`、`Silicium-ACPI` 以源码形式包含；
- `Mu_Basecore` 以固定 commit + 本地补丁
  `provenance/patches/mu_basecore-local-changes.patch` 提供；
- `OpenSSL` 以固定 commit `67b5686b4419b4cb8caa502711c41815f5279751` 提供；
- `Binaries/meizu20pro` 被完全排除，需本地提取，禁止上传其内容。

## 6. 验证边界

本次导出只做了源码级校验、策略测试与准备脚本 dry-run；没有执行完整构建、
没有连接设备、没有刷机。构建脚本中依赖硬件或原始固件/日志的步骤不在本轮验证范围。
