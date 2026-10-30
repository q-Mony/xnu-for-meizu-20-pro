# docs-nova/history — 作者文档归档

本目录把 `artifacts-nova/` 下作者 Markdown 报告、计划与 README 按原
`artifacts-nova/` 相对结构归档，例如：

```
artifacts-nova/boot-speed-round18-nova/warm-sha-stream-nova/REPORT-nova.md
  -> docs-nova/history/boot-speed-round18-nova/warm-sha-stream-nova/REPORT-nova.md
```

## 规则

- 已存在的首轮精选文档保持不变；新增文档记录原始与导出 SHA-256 于
  `provenance/history-sources-nova.json`。
- 文档中的本地 home 路径与设备序列号替换为 `<nova-root>/...`、
  `[local-home]/...`、`[redacted-device-serial]`；其他内容尽量保留原文。
- **不包含**：本导出子树、`TASK-nova.md` 智能体提示、第三方
  `reference-sources`/`pinned-sources`/`upstream-*`/`sources-nova` 克隆、
  snapshot/failed-run 目录、dsh 事件/transcript/wrapper/config、原始日志/手机数据。
- 归档保留作者当时的路径、设备、日志和构建假设；**不是可移植构建入口**，也不
  代表当前源码仍能复现当时结果。不要执行其中的硬件、刷机、恢复或 per-run 步骤。
- 本目录不是完整证据包；原始日志与设备证据仍只在本地 Nova 工作区。详见
  `docs-nova/LOCAL-ONLY-EVIDENCE.md`。
