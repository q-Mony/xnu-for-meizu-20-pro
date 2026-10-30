# 仅本地保留的证据（不分发）

以下内容因体积、隐私、设备/固件属性或运行产物属性被排除；它们仍是历史复核的
本地来源，不应从本仓库推断其内容：

- `logs-nova/` 原始 auto-log、kernel log、文本导出与 capture。
- `artifacts-nova/**/dsh-events*.jsonl`、`dsh-diagnostics*.log`、dsh 运行包装器/
  配置/transcript；以及任何 `run-dsh-*` 脚本。
- 手机镜像、备份、dump、分区镜像、生成 kernel/boot/firmware、`out/` 与 `Build/`。
- `inputs-nova/`（DTB、kernelcache、私有输入）、`snapshot-nova/`。
- `artifacts-nova` 中的第三方 research clone、reference-sources、sources-nova、
  大型测试 fixture、构建产品与设备派生数据。
- 原始 Apple kernelcache、签名 root 策略输入、设备暖 Data 分区、厂商
  `uefi-sheng-nova/Binaries/meizu20pro`。

精选报告中的本地路径与设备序列号已替换为 `<nova-root>/...`、
`[local-home]/...`、`[redacted-device-serial]`；原始报告哈希不再适用于替换后的
副本，引用时以本仓库文件为准。
