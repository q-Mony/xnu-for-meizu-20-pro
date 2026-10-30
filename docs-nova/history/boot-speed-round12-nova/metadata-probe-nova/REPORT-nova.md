# R12 metadata-only watchdog probe — Nova

本轮只新增 `nova: policy-metadata probe=12` 属性诊断；未更改任何策略判断、allowlist、tuple、errno、cleanup、cold语义或已有inode。R11 `policy-diag version=11`、core日志、kern_exec/bridge与panic文本保留。没有执行全内核构建、安装或设备操作；后续安装等待用户明确命令。

## 最小改动

- warm目录 fstat/copyin 成功后，在原CHECK_DIRECTORY predicate前输出一次：mkdir原始返回errno、created_directory、完整mode八进制、uid/gid/nlink/dev/ino。mkdir的返回值只复制到局部变量，不改变原error分支。
- 文件元数据拒绝分支输出kind（disabled.plist或disabled.migrated）、完整mode八进制、uid/gid/size/nlink；cold现有元数据拒绝也只增加相同行。谓词完全原样。
- 测试文件内捕获实际printf输出，新增5个完整实际函数用例：已存目录、当次新建0:20目录、仍拒绝已存0:20目录、plist拒绝字段、marker拒绝字段；另给原READ_CONTENT EIO随后close EACCES用例增加最终errno/op日志断言。未改Shim。

## 验证

- ASan/UBSan：747个实际函数案例通过（原742+5）；零编译警告。所有FD关闭，existing_mutations=0，R8完整策略字节保持。
- 原语调用次数不变：cold=15、warm_existing=34、warm_fresh=44。mkdir EEXIST=17与created=0、mkdir成功0与created=1均由输出断言覆盖。
- 首个read EIO=5保留为最终errno=5/op=15/stage=warm disabled.plist，即使后续close EACCES；已有0:20目录仍errno=22/op=9。
- 目标ARM64 iOS17：复用R11记录的真实编译cwd、头文件及全部参数，仅替换 `-o` 为本交付目录。编译rc=0，stdout/stderr均为空。
- 精确源码校验：只移除新增属性printf helper、两个调用、目录printf和mkdir返回值局部副本，即可逐字节重建R11 Policy.c；`validation-nova.json`记录PASS。
- 初次新增输出断言从匹配行第2字符开始查重复，误匹配自身；原742案例已通过，第743新增断言暴露该测试错误。已改为从完整匹配行结尾查找，最终747通过；初次日志保留为 `*-initial-assert-failure-nova.*`。

## 冻结SHA-256

| 文件 | SHA-256 |
| --- | --- |
| BootWatchdogPolicy.c | `e83f95c4e0598938992660b38216f4390a83ab0f97c9876686e575d6d7abf0e3` |
| WatchdogHostTests.c | `7761e280731e26798bac8498c80a4866cb826ad4e8f18a6649a2f453badd36e2` |
| WatchdogHostShim.h（未变） | `bb3691ed9d2d6bd0112b54825ef03a35c6fc503f855f26252803b8f005fdc9ab` |
| BootWatchdogPolicy-target-nova.o | `6f28985067376571101c8440de2aef27b845ae07e0e68a04f1cbefb25f6e4b91` |

R11原Policy.c SHA为 `428e818dd4a6479a67f21d93aac1a36b325338e211860f60e888eae11149d4de`。

## 文件与范围

本目录及所有交付文件均为新建，无旧文件。`implementation-diff-nova.patch`仅包含上述两个源码文件相对R11备份的最小diff；`host-command-nova.json`、`host-result-nova.json`、`host-compile-nova.log`、`host-tests-nova.log`保存host验证；`target-command-nova.json`、`target-result-nova.json`、`target-compile-nova.log`与目标对象保存目标编译证据；`frozen-sources-nova.json`、`validation-nova.json`保存冻结及验证摘要。

改前备份位于 `snapshot-nova/boot-speed-round12-nova/metadata-probe-nova/`，保留原相对路径；两次测试断言调整也有单独改前备份。CHANGELOG和构建GO由主代理维护。

R11日志及R12 stat ABI审计只作已知输入，未重复调查。当前没有实机mode/uid/gid值，不能声称定位或修复op9，也不能声称启动提速。本探针只为下一次明确授权的运行提供判定所需属性。
