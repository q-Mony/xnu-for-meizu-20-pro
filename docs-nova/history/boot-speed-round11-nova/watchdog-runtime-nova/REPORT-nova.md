# R11 watchdog 语义修复与运行时诊断 — Nova

本轮按父代理授权修改三个生产/测试文件，未操作设备、未改kern_exec或R9/R10冻结产物。原始分析见 `READONLY-FINDINGS-nova.md`；它记录实施前的只读状态。

修复确定存在的XNU flags错误，并给下一次实机失败留下可区分的版本、冷热分支、原语和阶段。**尚未解释R10实机errno17，也未证明能到Hello或缩短启动时间。**

## 改动

- warm目录、create、read三个open删除互斥的O_NOFOLLOW，保留O_NOFOLLOW_ANY；cold路径标志不变。真实XNU会拒绝两者同时存在，原模型遗漏此限制。
- metadata、允许的222/356字节完整策略、空marker、EOF、前后FD/路径一致性、冷Data和volatile约束全部保留。没有扩大白名单、吞掉错误或覆盖现有inode。
- 新静态诊断状态导出给kern_exec；入口重置并增加call计数，enable/warm bootarg解析后和成功/失败返回处打印version=11记录。不打印完整bootargs。
- 所有policy原语标记稳定op；cleanup遇到原先错误时保留其op，只有此前全部成功而close失败时才记close。
- host open/openat现在执行真实XNU互斥flag guard；增加诊断ABI、分支及二次cleanup错误覆盖。

## 诊断ABI

```c
const char *meizu_watchdog_policy_diagnostic(int *warm_found, int *warm_value, int *branch, int *op);
```

返回静态阶段字符串；所有输出指针允许NULL。warm_found=-1表示尚未解析，0为未找到，1为找到；warm_value保存解析得到的int。branch=-1未选择、0disabled、1cold、2warm。

| op | 原语 |
|---|---|
| 0 | WATCHDOG_OP_NONE |
| 1 | WATCHDOG_OP_ENABLE_ARG |
| 2 | WATCHDOG_OP_PREFLIGHT |
| 3 | WATCHDOG_OP_WARM_ARG |
| 4 | WATCHDOG_OP_COPYOUT_PATH |
| 5 | WATCHDOG_OP_MKDIR |
| 6 | WATCHDOG_OP_OPEN_DIRECTORY |
| 7 | WATCHDOG_OP_FSTAT |
| 8 | WATCHDOG_OP_COPYIN_STAT |
| 9 | WATCHDOG_OP_CHECK_DIRECTORY |
| 10 | WATCHDOG_OP_OPEN_CREATE |
| 11 | WATCHDOG_OP_FSTATAT |
| 12 | WATCHDOG_OP_CHECK_METADATA |
| 13 | WATCHDOG_OP_OPEN_READ |
| 14 | WATCHDOG_OP_CHECK_IDENTITY |
| 15 | WATCHDOG_OP_READ_CONTENT |
| 16 | WATCHDOG_OP_COPYIN_CONTENT |
| 17 | WATCHDOG_OP_CHECK_CONTENT |
| 18 | WATCHDOG_OP_READ_EOF |
| 19 | WATCHDOG_OP_CHECK_EOF |
| 20 | WATCHDOG_OP_COPYOUT_CONTENT |
| 21 | WATCHDOG_OP_WRITE_CONTENT |
| 22 | WATCHDOG_OP_FCHOWN |
| 23 | WATCHDOG_OP_FCHMOD |
| 24 | WATCHDOG_OP_FSYNC |
| 25 | WATCHDOG_OP_CLOSE_FILE |
| 26 | WATCHDOG_OP_CLOSE_DIRECTORY |

成功保留最后执行的原语（warm正常为26）；失败保留首个未处理错误的原语。EEXIST引发正常复用不算失败。stage区分warm directory / warm disabled.plist / warm disabled.migrated，结合op识别失败边界。

## 验证

- 742个实际policy函数host案例通过ASan/UBSan，包含真实R8 fixture完整字节、错误元数据/内容拒绝、缺失文件受控创建和全部FD回收。
- 相同修正后的host模型回放：冻结R10源返回22并exit1（RED），R11返回0并exit0（GREEN），两者existing_mutations=0。这只锁定flags缺陷，不等同于复现设备errno17。
- 双故障用例先让read返回EIO，再让cleanup close返回EACCES：最终errno仍EIO、op仍15、stage仍warm disabled.plist。
- ARM64 iOS17真实头文件和原编译参数编译通过，零warning。首次测试命令误用父目录cwd导致meta_features.h找不到，已保留失败日志；修正为原recipe cwd后通过。

## 冻结与产物

源码备份：`snapshot-nova/boot-speed-round11-nova/watchdog-runtime-nova/`。
`frozen-sources-nova.json`记录以下最终SHA：

- `BootWatchdogPolicy.c`：`428e818dd4a6479a67f21d93aac1a36b325338e211860f60e888eae11149d4de`
- `tests/WatchdogHostShim.h`：`bb3691ed9d2d6bd0112b54825ef03a35c6fc503f855f26252803b8f005fdc9ab`
- `tests/WatchdogHostTests.c`：`4604df52b9105c709cd0e0f333c8faf7d6ada3add3e8bed88063f7c5fa846bb2`

实现diff：`implementation-diff-nova.patch`；机器可读诊断映射：`diagnostic-contract-nova.json`；验证摘要：`validation-nova.json`；RED/GREEN：`red-green-regression-nova.json`；host输出：`host-tests-nova.log`；目标编译：`target-command-nova.json`与`target-compile-nova.log`。

后续由父代理实现kern_exec失败标记及一次有界保存，然后完整构建、审计、设备试验。未获运行时证据前不宣称panic17已解决。
