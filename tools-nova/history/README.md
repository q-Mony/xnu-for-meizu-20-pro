# tools-nova/history — 归档分析/测试源码

本目录保存少量有独立阅读价值的作者 build/test/analysis 源码，按原
`artifacts-nova/` 相对结构组织。重点包括：

- `boot-speed-round18-nova/warm-sha-stream-nova/`：canonical R18 warm-Data
  stream SHA-256 测试、host adapter、header probe、fixture 与所需
  `preserve-vectors-nova.s`。这些文件保留原始源码字节。
- `boot-speed-round18-nova/runtime-analysis-sol-nova/analyze-r18-nova.py`：
  R18 日志分析脚本，保留原始本地日志路径假设。

## 边界

- 这些脚本是**归档**，不是可移植构建入口；保留作者当时的本地日志、设备、缓存与
  路径假设，未针对本导出 checkout 重写。
- 本轮没有执行任何硬件、设备、刷机、恢复或日志采集步骤。只对纯源码做过语法检查
  与静态扫描。
- 不要直接运行依赖 `logs-nova/`、`out/`、设备或原始固件的脚本；如需复用，先在
  隔离 fixture 中抽取纯逻辑并补齐输入。
- 排除 `run-dsh-*` wrapper/config、dsh transcript/事件、设备安装/刷机/恢复编排、
  per-run 生成 fixture 与二进制。
