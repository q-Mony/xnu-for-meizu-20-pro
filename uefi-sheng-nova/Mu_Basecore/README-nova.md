# Mu_Basecore（固定依赖，未内嵌完整源码）

本目录不包含 Mu_Basecore 的完整 900MB checkout，以避免复制上游测试 fixture 与
二进制工具数据。`prepare-source-nova.py` **不会**初始化本依赖；请按
`docs-nova/BUILD.md` 手动准备：

- URL: `https://github.com/microsoft/mu_basecore.git`
- commit: `cfb93724a06ff86ff5ba971413368a7809ee42a5`（`release/202608`）
- 本地修改：在 clean pinned base 上应用
  `provenance/patches/mu_basecore-local-changes.patch`（11 个文件）。

原始 Nova 工作区里的同名 checkout 已经是 dirty 状态；不要把补丁应用到 dirty
树，也不要把本占位 README 当作完整源码。
