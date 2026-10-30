# docs-nova 索引

- [BUILD.md](BUILD.md) — 可移植准备与构建步骤、UEFI 手动固定依赖、用户自备输入、限制。
- [PROVENANCE.md](PROVENANCE.md) — 上游 URL、固定 commit、子模块 URL/HEAD、许可、历史文档来源。
- [EXPORT-POLICY.md](EXPORT-POLICY.md) — 包含/排除规则与安全边界。
- [SOURCE-MAP.md](SOURCE-MAP.md) — R18 冻结到导出的映射、关键文件哈希、覆盖（6185+1）。
- [R18-STATUS.md](R18-STATUS.md) — R18 实测状态与未解决问题。
- [LICENSES.md](LICENSES.md) — 许可/归属文件清单。
- [UEFI-SELECTION.md](UEFI-SELECTION.md) — UEFI 源码包含/排除与固定依赖。
- [BINARIES.md](BINARIES.md) — 厂商 Binaries 完全排除与本地提取。
- [PORTABILITY-NOTES.md](PORTABILITY-NOTES.md) — 继承的本地路径与可移植性。
- [TEST-EVIDENCE.md](TEST-EVIDENCE.md) — 本轮策略/准备/字节校验证据摘要。
- [LOCAL-ONLY-EVIDENCE.md](LOCAL-ONLY-EVIDENCE.md) — 未分发的本地证据。
- [AGENTS-source-nova.md](AGENTS-source-nova.md) — 项目根 `AGENTS.md` 的脱敏归档
  （原始文件只读；保留 Nova 工作区规则与模型/设备约束）。
- `history/` — 作者 Markdown 报告/计划/README 归档，按 `artifacts-nova/` 相对结构
  组织；已做隐私替换，保留原始假设，不是可移植构建入口。

`history/` 不包含导出子树本身、`TASK-nova.md` 智能体提示、第三方
`reference-sources`/`pinned-sources`/`sources-nova` 克隆、snapshot/failed-run 目录、
dsh 事件/transcript/wrapper/config，也不包含原始日志或手机数据。未分发的原始证据
见 `LOCAL-ONLY-EVIDENCE.md`；不要声称本仓库包含全部原始证据。
