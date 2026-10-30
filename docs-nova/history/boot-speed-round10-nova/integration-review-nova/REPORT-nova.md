R10 BootWatchdogPolicy 独立审阅：通过，无阻断项。

审阅的生产文件 SHA-256：`8cab1ad1971a3d03b1c581102cb9fd61b7e381a73904d36aad6f77eb1a696bce`。测试前后的生产源码与独立测试快照逐字节一致，且匹配实现代理冻结清单。

本次只在本目录创建审阅文件；未改生产代码、未操作设备或挂载镜像、未跑完整内核构建。中央 CHANGELOG 由父代理记录。

验证结果：

- 独立编写的 syscall shim 运行实际生产函数，3,933 项测试通过，ASan/UBSan 无报告，`-Wall -Wextra -Werror` 编译通过。
- 正例直接从父代理提取的真实 356 字节二进制读取，SHA `f3f3b53d466d3e78484d547d993561a5b660fbe9544795f7fd723cdaf0cfa233`。空 marker 的长度、SHA 和所有权来自实际快照；目录 501:20 / 0755、policy 0:0 / 0644、marker 0:20 / 0644 通过。四条 policy 决策及已有 inode 的内容、属主和权限全部保持。
- 第二个正例由冻结旧源码独立提取 222 字节模板，不用当前实现的字符串常量生成预期值。实现代理的旧函数回归记录同一真实 R8 fixture 得 errno 17；独立新函数实测返回 0。
- 仅缺失文件被创建；只对新 inode 写入、fchown、fchmod、fsync。已有 0:20 目录被拒绝；本次 mkdir 成功的新建 0:20 目录接受，并实际检查打开后的类型、权限、uid/gid。
- 非法对象类型、属主/属组、权限位、硬链接数、跨设备、错误长度、逐字节损坏、额外 EOF 字节、短读和短写均拒绝。三个身份复核时点上的 dev/ino/mode/uid/gid/nlink/size 变化也被拒绝。
- 对七条成功路径的每个 syscall/copy 边界注入六种 errno，验证错误原样返回；清理 close 再出错不覆盖前一个错误。每个打开的描述符恰好执行一次 close；现有对象无修改。0x400..0x7ff 环境区保持原值。
- 新行为只在 warm=1 且原 Data preflight 成功后进入。warm 缺失/0/2/3 时已有文件继续返回 EEXIST。原 opt-in/preflight 和 mkdir 到 marker 创建整段源码逐字节保持，冷路径成功及全部 15 个边界失败通过。
- 真实 XNU `sysproto.h` 与实现匹配；`fstatat64` 明确允许 `AT_SYMLINK_NOFOLLOW_ANY`。目标编译日志为空且对象 SHA 匹配实现代理记录；该 ARM64 编译是实现代理执行的证据，审阅者未额外构建。

这说明 R10 的兼容修复可继续进入设备测试，尚不说明 Boot→Hello 已提速。身份复核也不是文件系统事务；本次结论限于固定的 pre-launchd 暖快照。

机器可读结果：[integration-result-nova.json](<nova-root>/artifacts-nova/boot-speed-round10-nova/integration-review-nova/integration-result-nova.json)

独立测试结果：[host-result-nova.json](<nova-root>/artifacts-nova/boot-speed-round10-nova/integration-review-nova/host-result-nova.json)；复现命令：[commands-nova.json](<nova-root>/artifacts-nova/boot-speed-round10-nova/integration-review-nova/commands-nova.json)；ABI 证据：[abi-evidence-nova.json](<nova-root>/artifacts-nova/boot-speed-round10-nova/integration-review-nova/abi-evidence-nova.json)。
