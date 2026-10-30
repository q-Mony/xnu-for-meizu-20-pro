# 构建与准备（source-only）

## 前提

- macOS + Xcode/Command Line Tools（系统编译器与 SDK 只读使用）。
- `git`、`python3`、`cmake`、`ninja`、`unifdef`、`ditto`（macOS 自带）等。
- 仅在本 checkout 内构建；不要指向任何其他会话的可写源码目录。
- **完整 clean 构建尚未验证。** 下面的准备步骤不等于已验证可启动。

## 1. 只读校验

```sh
python3 prepare-source-nova.py --dry-run
python3 prepare-source-nova.py --verify-only
python3 prepare-source-nova.py --verify-only --all
```

- `--dry-run` 不写文件、不联网；只打印将恢复的三个快照、配置与 owner 头计划。
- `--verify-only` 校验 `sources-nova/*` 与 `configs-nova/*` 的清单/关键哈希。
- `--verify-only --all` 额外校验整个 inventory（含文档/工具/来源元数据）。

## 2. 准备 XNU/libdispatch/libpthread

### A. 已有本地上游参考 checkout（推荐，避免联网）

把参考目录中的 `xnu`、`libdispatch`、`libpthread` 固定到
`provenance/upstream-pins-nova.json` 中对应 commit，然后：

```sh
python3 prepare-source-nova.py --reference-root /path/to/references
```

参考目录只读；helper 会在 `unixnu-main-nova/out/feeds/` 下建立新的 base Git
元数据（`--no-hardlinks`），不复用其他仓库对象。

### B. 显式允许联网拉取上游开源依赖

```sh
python3 prepare-source-nova.py --fetch-references
```

只从 `provenance/upstream-pins-nova.json` 记录的公开 upstream URL 拉取固定 commit；
不会下载 Apple 专有 kernelcache/SDK，也不会复制厂商二进制。

### helper 的精确动作

`prepare-source-nova.py` **只**做以下事情：

1. 在写入前逐级检查所有目标与祖先：`unixnu-main-nova/out`、`out/meizu20pro`、
   `tree`、`tree/.cache`、`tree/build`、`tree/build/xnu`、`.config`、owner 头、
   `out/feeds` 与三个 cache 路径。任何目标/祖先是指向外部的 symlink 时立即失败，
   不 mkdir、不 copy、不 move、不 clone，也不在外部写 marker。
2. 在 `unixnu-main-nova/out/meizu20pro/tree/` 下建立 `.cache/` 与 `build/xnu/`。
3. 用 pinned commit 建立 `xnu`/`libdispatch`/`libpthread` 的 base Git 元数据，
   再把 `sources-nova/{xnu,libdispatch,libpthread}` 覆盖到
   `tree/.cache/{xnu,libdispatch,libpthread}-work`；删除快照中已不存在的 tracked
   base 文件（模拟源树的删除），但不会 `git clean`/`git reset`。
4. 将 `configs-nova/meizu20pro-r18.config` 选为 `out/meizu20pro/.config`。
5. 放置 `configs-nova/owner-data-volume.h` 到 `tree/build/xnu/owner-data-volume.h`。

它**不会**：

- 初始化 UEFI `Mu_Basecore` 或 `OpenSSL`；
- 拉取 libplatform/AvailabilityVersions/corecrypto/IOStorageFamily/edk2 等其他 feed；
- 执行完整构建、设备操作或 `git clean`/`git reset`；
- 删除不匹配的 dirty cache。默认拒绝覆盖；只有显式 `--replace-cache` 才把旧 cache
  移动到带时间戳的备份，再恢复快照。

## 3. UEFI 固定依赖（手动，helper 不处理）

UEFI 构建还需要两个未内嵌的固定 checkout。以下路径以导出树根为 `$PWD`：

### 3.1 Mu_Basecore（base + 11 文件本地补丁）

```sh
git clone https://github.com/microsoft/mu_basecore.git /tmp/mu_basecore-nova
git -C /tmp/mu_basecore-nova checkout --detach cfb93724a06ff86ff5ba971413368a7809ee42a5
git -C /tmp/mu_basecore-nova apply --check "$PWD/provenance/patches/mu_basecore-local-changes.patch"
git -C /tmp/mu_basecore-nova apply "$PWD/provenance/patches/mu_basecore-local-changes.patch"
git -C /tmp/mu_basecore-nova status --porcelain   # 应为 11 个 M
```

补丁只对 **clean pinned base** 应用；原始 Nova 工作区里的
`uefi-sheng-nova/Mu_Basecore` 已经是 dirty 状态，不能作为应用目标。导出树中的
`uefi-sheng-nova/Mu_Basecore/README-nova.md` 只是占位说明；按你的 UEFI 构建脚本
要求把上述干净 checkout 放到 `uefi-sheng-nova/Mu_Basecore`（或等价的本地路径）。
补丁的 11 个文件与 `provenance/mu_basecore-local-changes.json` 一致。

### 3.2 OpenSSL（固定 commit，无本地补丁）

```sh
git clone https://github.com/openssl/openssl.git /tmp/openssl-nova
git -C /tmp/openssl-nova checkout --detach 67b5686b4419b4cb8caa502711c41815f5279751
git -C /tmp/openssl-nova status --porcelain      # 应为空
```

把该 checkout 放到
`uefi-sheng-nova/Silicon/Silicium/OpensslPkg/Library/OpensslLib/openssl`
（导出树中该路径只有 `README-nova.md` 占位）。`OpensslGen/` 生成源不在源码导出
范围内，需由 OpenSSL 构建流程重新生成，不能从本仓库推断其字节。

## 4. 构建

```sh
./unixnu-nova build kernel -j8
```

`build-xnu-4k-baseline.sh` 会基于已恢复的 `.cache/xnu-work` 工作树继续应用
`patches/xnu/` 中尚未应用的补丁；**不要**先运行会重置该目录的通用 clean/fetch。
当前 `patches/xnu/` 只保留 `zzzz-park-marker.patch`，其余早期补丁已包含在
`sources-nova/xnu` 快照中。

> 网络边界：`prepare-source-nova.py` 本身默认离线，只有 `--fetch-references`
> 才拉取它负责的三个固定 reference。正常 CLI 构建在缺少其他 feed（如
> libplatform/AvailabilityVersions/corecrypto/IOStorageFamily/edk2）或
> `.cache/sources/*.json` 头文件元数据时，仍可能按既有脚本自行访问上游；
> 本轮未做“完全离线构建”验证。该行为不会覆盖已恢复的 Nova XNU 源码。

## 5. 必须由用户自备的输入（不分发）

- 原始 Apple kernelcache（用于 prelink / 符号）；
- 设备固件、DTB、ADT、trust cache / owner-signed root 策略输入；
- 设备暖 Data 分区内容或 data seed；
- GPU addon、Wi-Fi MAC 等私有输入；
- 厂商 `uefi-sheng-nova/Binaries/meizu20pro`（按原
  `unixnu-meizu20pro-nova/scripts/import-meizu-uefi.py` 或既有提取脚本从用户本地
  固件提取）。

缺少上述输入时，内核也许可以编译，但完整 boot/payload/开机流程不能保证；本仓库
**不声称** fresh clone 一条命令即可复现开机。

## 6. 验证边界

本轮导出只做了源码级校验、策略测试、准备脚本 dry-run 与隔离 fixture；没有执行
完整 XNU/UEFI 构建、没有连接设备、没有刷机。构建脚本中依赖硬件或原始
固件/日志的步骤不在本轮验证范围。`docs-nova/history/` 与 `tools-nova/history/`
中的历史脚本是归档，不是可移植构建入口。

## tree/scripts 选择

导出时对实际组装树 `tree/scripts`（303 个文件）与 `unixnu-main-nova/scripts`（291）
+ `unixnu-meizu20pro-nova/scripts`（26）做了逐内容 SHA-256 比较：**没有**不属于
这两处的 unique 构建脚本。因此不额外导出脚本目录；`tree.assemble` 会从主/板级
`scripts/` 重新复制组装。
