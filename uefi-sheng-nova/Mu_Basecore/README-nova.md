# Mu_Basecore（固定依赖，未内嵌完整源码）

本目录不包含 Mu_Basecore 的完整 900MB checkout，以避免复制上游测试 fixture 与
二进制工具数据。构建时请按 `provenance/upstream-pins-nova.json` 拉取：

- URL: `https://github.com/microsoft/mu_basecore.git`
- commit: `cfb93724a06ff86ff5ba971413368a7809ee42a5`（`release/202608`）
- 本地修改：应用 `provenance/patches/mu_basecore-local-changes.patch`
  （11 个文件，导出时已核对）。

`prepare-source-nova.py` 在准备 UEFI 依赖时会提示/初始化该 checkout；不会把
完整依赖复制进本仓库。
