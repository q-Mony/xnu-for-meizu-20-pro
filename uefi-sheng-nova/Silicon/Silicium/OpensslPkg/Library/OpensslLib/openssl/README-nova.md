# OpenSSL（固定依赖，未内嵌）

本目录只有占位说明，`prepare-source-nova.py` **不会**初始化 OpenSSL。请按
`docs-nova/BUILD.md` 手动准备：

- URL: `https://github.com/openssl/openssl.git`
- commit: `67b5686b4419b4cb8caa502711c41815f5279751`（`openssl-3.5.5`）
- 本地修改：无（导出时 `git status` 干净）。

完整源码与 `OpensslGen/` 生成源在构建时按固定 commit/构建流程生成；本仓库不复制
其测试数据/二进制。
