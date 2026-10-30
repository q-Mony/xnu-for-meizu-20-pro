# Nova 冻结源码对比

新建报告，无旧版本。对 `snapshot/setup-selection-provenance/frozen-source-manifest.json` 与当前 Nova XNU 源码做只读比较，并重复读取确认扫描期间内容稳定。

冻结清单覆盖 `xnu-work` 内递归扫描得到的 5,211 个文件（排除 `.git`），包括测试和文档；它**不是实际编译依赖清单**，也不涵盖外部驱动、SDK、工具链、板级配置或构建脚本。

| 分类 | 文件数 |
|---|---:|
| 与冻结清单相同 | 5,206 |
| 本次优化修改 | 4 |
| 本次优化新增 | 2 |
| Nova 隔离迁移后缺失 | 1 |
| 冻结清单内路径重定位改写 | 0 |
| 其他继承或未解释的内容漂移 | 0 |

六个优化文件均匹配 `source-before-build-nova.json`。四个修改文件的改前备份 SHA-256 均与冻结清单一致，支持将这些差异归于本次优化；两个新增文件为 SHA-256 汇编实现与声明头。

唯一额外差异是 `tools/tests/perf_index/PerfIndex_COPS_Module/Prefix.pch`。它是 182 字节的纯文本 Objective-C 前缀头，因 `.pch` 后缀被早前隔离过程当成缓存移走；保留副本与冻结 SHA-256 完全一致。当前 PerfIndex 测试工程仍引用它。本次只读任务没有恢复或修改它。

XNU 根目录下唯一文本路径迁移记录是 `.git/config`，不属于冻结清单。历史 selection-provenance 的四文件变更已包含在冻结基线中，不能归因于 Nova。

因此可以报告「六个本次优化文件，加一处已解释的隔离迁移缺失」；不能报告整棵源码严格只改六文件，也不能据此主张完整构建输入或最终内核只存在这些差异。详细哈希、备份证明与分类见 `frozen-source-drift-nova.json`。
