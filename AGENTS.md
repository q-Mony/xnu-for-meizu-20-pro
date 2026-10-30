# AGENTS.md — Nova / xnu-for-meizu-20-pro

本仓库是 Nova 的 **source-only** 导出。以下规则对在本仓库工作的所有智能体生效。

## 模型与执行器（强制，不得继承或回退）

- **Codex 原生子智能体**：每个子智能体必须显式配置 `GPT-6.1-Sol`、
  `reasoning_effort=high`。不得继承父会话或已保存会话的不符合要求的模型，不得
  自动降级、换模型或省略强度；任务级配置必须与实际请求/响应头核对。
- **Sol**：负责分析、规划、拆分任务、制定验收标准与最终验收。
- **dsh 执行器**：实现、测试与构建固定使用 provider=`deepseek-official`、
  model=`deepseek-flash`、reasoningEffort=`high`。这是用户明确指定的执行器例外，
  不得替换为其他 provider/model/强度，也不得回退到默认模型。
- 每次派发都要核对**最终生效配置**与**实际请求/响应头**；Web、headless、已保存
  会话不假设自动共用模型选择。未确认同一配置时不得委派；dsh 再委派也必须保证
  同一配置，否则不得子委派。
- 不使用自动模型路由、fallback、隐藏重试或“先跑再换模型”。配置不符时停止并
  报告，而不是降低要求继续。

## 范围与隔离

- 本仓库只保存构建源码、文档、固定来源信息与可移植准备脚本。
- 不提交手机镜像、备份、dump、生成内核/固件、SDK、工具链、cache、原始日志、
  `.git` 元数据、`node_modules`、私钥/证书/凭据或 `local.toml`。
- 原始 Nova 工作区、其他会话目录及其 `.git` 均为只读；不要从别处复制 `.git`
  历史/对象/alternates，也不要修改其 remote。
- 修改现有文件前必须在 `snapshot-nova/` 下做唯一命名备份；保留失败证据，不删除
  失败目录。任何验证都要保存实际命令、stdout/stderr 与退出码。
- 不输出 secrets、设备私有身份数据或未过滤的事件/transcript。

## 设备与安装（等待新的明确命令）

- 本仓库不是可直接刷机的交付物。未收到用户**新的、明确的安装命令**前，任何
  智能体都不得刷入、切槽、重启、格式化或向设备写入；旧的持续授权不覆盖此规则。
- 未来任何设备使用都必须同时满足：用户提供当时**唯一允许的精确设备序列号**，并
  对该次操作给出**独立、明确的授权**。不得把单次许可扩大为“所有设备”或长期授权。
- 公开导出中不写原始设备序列号；只使用类似 `[redacted-device-serial]` 的占位符。
  设备身份、分区状态和历史镜像哈希必须在操作前重新核对，不能依赖历史记录。

## 构建规则

- 只用本 checkout 内的路径构建；先运行 `python3 prepare-source-nova.py --dry-run` 核对。
- `sources-nova/xnu` 是完整修改源码，不是 cache。通用 clean/fetch **不得**静默覆盖它。
- `prepare-source-nova.py` 只负责把 `sources-nova/{xnu,libdispatch,libpthread}`
  恢复到 `tree/.cache/*-work`，建立构建脚本要求的 base Git 元数据，并放置 R18
  `.config` 与 `owner-data-volume.h`；它**不**初始化 UEFI Mu_Basecore/OpenSSL。
- 上游开源依赖只在显式 `--fetch-references` 时联网拉取；不复制专有 SDK/二进制。
- 原始 Apple kernelcache、固件/DTB、签名 root 策略输入、暖 Data 分区、厂商
  `Binaries/meizu20pro` 等由用户自备，缺失时构建会失败，不能伪称一键复现。
- 完整 clean 构建尚未验证；不要根据源码齐全或 dry-run 通过推断可启动。

## 证据、历史与文档

- 历史报告中的 `<nova-root>/...`、`[local-home]/...` 与
  `[redacted-device-serial]` 是导出时的隐私替换；原始证据只存在于本地 Nova 工作区。
- `docs-nova/history/` 与 `tools-nova/history/` 是**归档**：保留原始假设与当时的
  设备/路径条件，不是可移植构建入口。不得执行其中的硬件、刷机、恢复或 per-run
  脚本；只可阅读或在隔离 fixture 中复用纯逻辑。
- 不要声称本仓库包含全部原始证据、完整日志或全部第三方源码。

## R18 事实

暖 Data 哈希 8.666060 s → 1.123898 s；目标 IPC 请求到 backboardd 接收
36.484388 s，长等待未解决；Hello 后黑屏仍复现、未修复。不得夸大或把阶段收益
当作整机 Boot→Hello 总收益。
