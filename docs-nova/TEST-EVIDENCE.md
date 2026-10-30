# 本轮验证证据摘要

所有测试均为本地、无设备、无完整构建、无网络拉取；精确计数以
`provenance/inventory-nova.json`、`provenance/coverage-r18-nova.json` 与
`provenance/validation-nova.json` 为准。

## 导出策略

- 代表性 fixture：允许 `code/main.c`、`docs/README.md`、`docs/diagram.svg`；
  拒绝 `phone.img`、`boot.macho`、`backup.bak-*`、`id_ed25519`、含 GitHub token
  的 `token.txt`、扩展名缺失的二进制 `blob`、`.env`。
- 结果：10/10 符合预期；见 `tools-nova/export-policy-nova.py --test`。

## 可移植准备脚本

- `python3 prepare-source-nova.py --dry-run`：在导出 checkout 上通过，未写文件、
  未联网；计划恢复 xnu、libdispatch、libpthread 三个快照，并选择 R18 `.config`
  与放置 `owner-data-volume.h`。
- `python3 prepare-source-nova.py --verify-only --all`：inventory 全量只读校验通过。
- 隔离 fixture（`review-fixes-nova/tests/test-prepare-guards-nova.py`）：
  - 正常恢复：三个 cache 首次 `restored`，第二次 `already-current`；修改源码、
    未跟踪头文件、删除 base 文件、配置与 owner 头均符合预期，`network=false`。
  - dirty cache：默认非零拒绝且 dirty 字节保留；`--replace-cache` 成功后旧 cache
    在带时间戳备份中完整保留，cache 恢复为快照内容。
  - symlink 逃逸：(a) `unixnu-main-nova/out` 指向外部目录、(b)
    `tree/build` 指向外部目录、(c) `.config` 指向外部文件，三者均在第一次
    mkdir/copy/move/clone 前以退出码 2 拒绝；外部递归 inventory+哈希前后一致，
    checkout 内部也没有新增写入，链接未被跟随/删除。

## 字节与覆盖

- R18 冻结 6186 项：6185 项导出文件哈希逐项匹配；1 项为
  `unixnu-main-nova/feeds.toml.bak-21H461`，按备份后缀策略排除并记录原因；
  未解释缺失 0，覆盖率（含可解释排除）1.0。
- 关键 R18 文件：
  - `sources-nova/xnu/libkern/libkern/crypto/native_sha256.h`
  - `sources-nova/xnu/libkern/crypto/corecrypto_sha2.c`
  - `unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp`
  三者字节与 R18 冻结一致。
- 源文件→导出文件逐字节复核：`validation-nova.json` 中 checked=matched，
  0 mismatch（脱敏历史文档单独记录原始/导出哈希，不参与源字节等式）。

## Mu_Basecore 与 Sol 基线

- `provenance/patches/mu_basecore-local-changes.patch` 与当前 live
  `uefi-sheng-nova/Mu_Basecore` 的 `git diff --binary` 逐字节一致，11 个文件；
  live base 为 `cfb93724a06ff86ff5ba971413368a7809ee42a5`。
- `provenance/mu-baseline-verification-nova.json`：
  Sol dirty-source 基线 204 项 = 193 项导出逐字节一致 + 11 项 Mu 补丁完整覆盖，
  未解释 0。补丁对 live dirty tree 的 reverse-check 通过。

## 历史文档与工具

- `docs-nova/history/` 收录 200 个作者 Markdown 文件（含首轮保留的 22 个），
  按原 `artifacts-nova/` 相对结构组织；隐私替换与原始/导出哈希见
  `provenance/history-sources-nova.json`。
- `tools-nova/history/` 收录 7 个作者源码文件：canonical R18 stream SHA 测试
  五件套、`preserve-vectors-nova.s` 与 R18 runtime analysis 源码。工具源码保留
  原始字节，未做路径替换。
- 历史脚本只做语法/静态检查，未执行任何硬件、设备、刷机、恢复或日志采集步骤。

## 仓库卫生

- 无嵌套 `.git`、无 symlink、无 gitlink、无活动 `.gitmodules`。
- 无 >=50 MiB 文件；最大文件 3,630,250 B（Mu 的文档用 PNG）。
- 高置信 secret 扫描 0 命中；设备序列号 0 命中；厂商 `Binaries`/`Build`、
  原 `out/`、`inputs-nova/`、`snapshot-nova/`、`logs-nova/` 均不存在。
- 8 个继承的绝对路径文本（源码默认值/原始冻结证据），不是 symlink，已在
  `docs-nova/PORTABILITY-NOTES.md` 说明。

## 未验证

- 未执行完整 XNU/UEFI clean 构建、未运行设备、未刷机、未推送。
- 原始 kernelcache、固件/DTB、签名 root 输入、暖 Data、厂商 Binaries 的缺失
  使 fresh clone 开机复现不在本轮保证范围。
