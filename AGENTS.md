# AGENTS.md — Nova / xnu-for-meizu-20-pro

本仓库是 Nova 的 **source-only** 导出。适用以下硬规则。

## 范围与隔离

- 本仓库只保存构建源码、文档、固定来源信息与可移植准备脚本。
- 不提交手机镜像、备份、dump、生成内核/固件、SDK、工具链、cache、原始日志、
  `.git` 元数据、`node_modules`、私钥/证书/凭据或 `local.toml`。
- 原始 Nova 工作区、其他会话目录及其 `.git` 均为只读；不要从别处复制 `.git`
  历史/对象/alternates，也不要修改其 remote。
- 不要把本仓库当作可直接刷机的交付物；任何设备写入都必须由用户明确授权并单独执行。

## 构建规则

- 只用本 checkout 内的路径构建；先运行 `python3 prepare-source-nova.py --dry-run` 核对。
- `sources-nova/xnu` 是完整修改源码，不是 cache。通用 clean/fetch **不得**静默覆盖它。
- `prepare-source-nova.py` 才负责把快照恢复到 `tree/.cache/xnu-work` 等位置，并建立
  构建脚本要求的 base Git 元数据；它只在本 checkout 内写入。
- 上游开源依赖只在显式 `--fetch-references` 时联网拉取；不复制专有 SDK/二进制。
- 原始 kernelcache、固件/DTB、签名 root 策略输入、暖 Data 分区、厂商
  `Binaries/meizu20pro` 等由用户自备，缺失时构建会失败，不能伪称一键复现。

## 证据与变更

- 修改现有文件前先做唯一命名备份；保留失败证据，不删除失败目录。
- 不输出 secrets、设备私有身份数据或未过滤的事件/transcript。
- 历史报告中的 `<nova-root>/...`、`[local-home]/...` 与
  `[redacted-device-serial]` 是导出时的隐私替换；原始证据只存在于本地 Nova 工作区。

## R18 事实

暖 Data 哈希 8.666060 s → 1.123898 s；目标 IPC 请求到 backboardd 接收
36.484388 s，长等待未解决；Hello 后黑屏仍复现、未修复。不得夸大或把阶段收益
当作整机 Boot→Hello 总收益。
