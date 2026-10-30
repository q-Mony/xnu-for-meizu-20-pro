# 本轮验证证据摘要

所有测试均为本地、无设备、无完整构建；精确计数以
`provenance/inventory-nova.json` 与 `provenance/validation-nova.json` 为准。

## 导出策略

- 代表性 fixture：允许 `code/main.c`、`docs/README.md`、`docs/diagram.svg`；
  拒绝 `phone.img`、`boot.macho`、`backup.bak-*`、`id_ed25519`、含 GitHub token
  的 `token.txt`、扩展名缺失的二进制 `blob`、`.env`。
- 结果：10/10 符合预期；见 `tools-nova/export-policy-nova.py --test`。

## 可移植准备脚本

- `python3 prepare-source-nova.py --dry-run`：在导出 checkout 上通过，未写文件、
  未联网；计划恢复 xnu（5212 文件）、libdispatch（221）、libpthread（209），
  选择 R18 `.config` 并放置 `owner-data-volume.h`。
- 离线隔离 fixture：本地 pinned reference + 快照，首次运行为三个
  `restored`；第二次为三个 `already-current`；修改源码/未跟踪头文件/删除 base
  文件/配置/owner 头均符合预期，`network=false`。
- `python3 prepare-source-nova.py --verify-only`：快照与配置哈希全部通过。

## 字节与覆盖

- R18 冻结 6186 项：6185 项导出文件哈希逐项匹配；1 项为
  `unixnu-main-nova/feeds.toml.bak-21H461`，按备份后缀策略排除并记录原因；
  未解释缺失 0，覆盖率（含可解释排除）1.0。
- 关键 R18 文件：
  - `sources-nova/xnu/libkern/libkern/crypto/native_sha256.h`
  - `sources-nova/xnu/libkern/crypto/corecrypto_sha2.c`
  - `unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp`
  三者字节与 R18 冻结一致。
- 源文件→导出文件逐字节复核：全部匹配，0 mismatch（生成/脱敏文档单独标记）。

## 仓库卫生

- 无嵌套 `.git`、无 symlink、无 gitlink、无 `.gitmodules` 活动文件。
- 无 >=50 MiB 文件；最大文件约 3.6 MiB（Mu 的文档用 PNG）。
- 高置信 secret 扫描 0 命中；设备序列号 0 命中；厂商 `Binaries`/`Build`、
  原 `out/`、`inputs-nova/`、`snapshot-nova/`、`logs-nova/` 均不存在。
- 8 个继承的绝对路径文本（源码默认值/原始冻结证据），不是 symlink，已在
  `docs-nova/PORTABILITY-NOTES.md` 说明。

## 未验证

- 未执行完整 XNU/UEFI 构建、未运行设备、未刷机、未推送。
- 原始 kernelcache、固件/DTB、签名 root 输入、暖 Data、厂商 Binaries 的缺失
  使 fresh clone 开机复现不在本轮保证范围。
