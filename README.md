# xnu-for-meizu-20-pro — Nova 源码导出

本仓库是 **Meizu 20 Pro（SM8550）** 的 Nova 工作源码导出，从 Nova 本地工作树按
“当前工作内容（含未提交与未跟踪源码）”导出，**不是**上游仓库的浅克隆，也**不包含**
设备镜像、内核、固件、备份、SDK、工具链或 `.git` 元数据。仓库当前处于初始源码
准备阶段，构建与设备验证状态以本文和 `docs-nova/` 为准。

## R18 实测状态（必须如实理解）

- 暖 Data 哈希：**8.666060 s → 1.123898 s**，减少 7.542161 s（约 87.03%）。
- 目标 IPC 请求到 backboardd 接收：**36.484388 s**（R17 为 36.320938 s），
  **长等待仍未解决**；不能把暖 Data 的收益当作整机 Boot→Hello 的总节省。
- Hello：用户确认已进入 Hello，但随后**黑屏复现，尚未修复**。
- 上述为各一次同输入实机对比，缺多次运行方差；Boot 菜单选中到 Hello 首帧的精确端点
  仍缺失。详细证据见 `docs-nova/R18-STATUS.md`。

## 构建验证状态

**完整的 clean 构建尚未验证。** 本轮只做了源码级校验、准备脚本的隔离 fixture、
`--dry-run` 与只读校验；没有执行完整 XNU/UEFI 构建、没有连接设备、没有刷机。
即使 dry-run 和源码校验通过，也不能据此推断可启动。

原始 Apple kernelcache、固件/DTB、ADT、trust cache / owner-signed root 策略输入、
暖 Data 分区、GPU addon、Wi-Fi MAC 与厂商 `Binaries/meizu20pro` 等均需用户自备，
不随本仓库分发；缺失时构建会失败或无法完成 boot/payload。

## 目录

| 路径 | 内容 |
|---|---|
| `unixnu-main-nova/` | 主构建器源码；`drivers/` 与 `kits/` 已扁平化为普通源码目录 |
| `unixnu-meizu20pro-nova/` | Meizu 20 Pro 板级源码、配置与板级脚本 |
| `uefi-sheng-nova/` | UEFI 源码（Meizu 平台、Mu/Mu_OEM_Sample、Silicium、Silicium-ACPI、BootShim） |
| `sources-nova/xnu/` | **完整当前 XNU 源码快照**（含 Nova 修改；不是可丢弃的 cache） |
| `sources-nova/libdispatch/` | 当前 libdispatch 源码快照（含本地修改） |
| `sources-nova/libpthread/` | 当前 libpthread 源码快照（含本地修改） |
| `configs-nova/` | R18 内核 `.config` 与生成的 `owner-data-volume.h` |
| `tools/` | 原有 Nova 工具脚本；`prepare-source-nova.py` 为可移植准备入口 |
| `docs-nova/` | 构建、来源、排除策略、R18 状态、许可与作者历史文档 |
| `tools-nova/history/` | 归档的 build/test/analysis 源码（含 R18 stream SHA 测试） |
| `provenance/` | 上游固定版本、子模块 URL/HEAD、清单、R18 源冻结与覆盖报告 |

历史文档保留在 `docs-nova/history/`，按原 `artifacts-nova/` 相对结构组织；它们是
作者报告/计划归档，不是可移植构建入口，也没有包含全部原始日志或设备证据。

## 构建

先阅读 [`docs-nova/BUILD.md`](docs-nova/BUILD.md)。最短路径：

```sh
python3 prepare-source-nova.py --dry-run
python3 prepare-source-nova.py --fetch-references   # 显式允许联网拉取上游开源依赖
./unixnu-nova build kernel -j8
```

`prepare-source-nova.py` 只恢复 `sources-nova/{xnu,libdispatch,libpthread}` 到
`tree/.cache/*-work`、建立 base Git 元数据、放置 R18 `.config` 与
`owner-data-volume.h`；它**不**初始化 UEFI `Mu_Basecore`/`OpenSSL`。UEFI 依赖需按
[`docs-nova/BUILD.md`](docs-nova/BUILD.md) 的手动固定版本步骤补齐。

**不能声称“全新 clone 一条命令即可复现开机”**。原始 kernelcache、固件/DTB、
签名 root 策略输入、设备暖 Data 分区、厂商 `Binaries/meizu20pro` 等均为用户自备，
不随本仓库分发；部分板级脚本依赖这些本地输入。

## 来源与许可

- XNU 上游：`https://github.com/apple-oss-distributions/xnu.git`
  pinned base `d8b80295118ef25ac3a784134bcf95cd8e88109f`（tag `xnu-10063.141.1`）。
- 其他固定版本、子模块 URL/HEAD、逐文件哈希见 `provenance/`。
- 各子目录保留原始 `LICENSE`/版权文件；XNU 的 `APPLE_LICENSE` 位于
  `sources-nova/xnu/APPLE_LICENSE`。本导出不改变上游许可，详见
  `docs-nova/LICENSES.md` 与 `docs-nova/PROVENANCE.md`。

## 明确不包含

手机镜像/备份/dump、生成的内核与 boot/firmware、`.img/.macho/.o/.a/.efi/.dtb/.fd/.fv`
等二进制、SDK/工具链/cache、`inputs-nova/`、`snapshot-nova/`、原始日志、`.git`、
`node_modules`、私钥/证书/凭据、`local.toml`。完整规则与导出计数见
[`docs-nova/EXPORT-POLICY.md`](docs-nova/EXPORT-POLICY.md)；UEFI 选择、可移植性、
测试证据与本地路径限制见 [`docs-nova/`](docs-nova/README.md)。

## 智能体规则

根 [`AGENTS.md`](AGENTS.md) 规定：Codex 原生子智能体固定 `GPT-6.1-Sol` / high；
Sol 规划验收，dsh 固定 `deepseek-official/deepseek-flash` / high；任务级配置与
实际请求/响应头必须核对；无明确新安装命令不得操作设备。项目工作区原始 AGENTS
规则以脱敏形式归档在 [`docs-nova/AGENTS-source-nova.md`](docs-nova/AGENTS-source-nova.md)。
