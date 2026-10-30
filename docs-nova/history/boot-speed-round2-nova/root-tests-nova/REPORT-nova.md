# RootWindowRead 批量回归验证 — Nova

新文件，无旧版本。仅测试代码由本代理修改；生产 RootWindowRead.h/NativeUfs.cpp 由主代理负责。未改 CHANGELOG、未操作设备。

## 结果

- 旧生产实现：128 KiB 请求实际调用 8 次读取、8 次认证、8 次复制；新批量断言返回 RED（exit 1）。
- 旧生产实现：第 2 个认证单元失败时，已认证的首块产生 16 KiB 部分进度；新“整批认证后一次复制”契约返回 RED。该部分进度不包含未认证数据，不视为旧版安全漏洞。
- 新实现：ASan/UBSan 的 RootLayout、RootWindowRead、RootDiagnostics、RootPublication 全部通过。
- QEMU：真实生产 RootLayout/RootWindowRead/RootCommands，加 portable VerifiedRoot/RSA3072/OpenSSL，两个 DMA 地址均通过。128 KiB 为 1 个 READ(10)、8 个认证单元、1 次复制；该计数取自生产 hostSubmit 的提交计数增量。
- 真签名验证覆盖完整请求、单位内偏移、512/4608 尾部、小请求、认证单位跨界、末块损坏、第一批后段换序、坏签名/表/公钥/容量/空策略拒绝。损坏的第二批保留先前 128 KiB，失败批次不复制；第一批第 7 个单元换序失败时 done/copy 均为零。
- mock/边界检查另覆盖全部 32 种 512 字节偏移、3 种物理窗口基址、末尾 READ(10) LBA、无效/溢出参数、首批/次批各认证单元失败、传输失败、短复制原语义、共享 bounce 锁与完成回调前解锁。

## 运行入口与证据

主机：`python3 -B unixnu-main-nova/drivers/NativeUfs/tests/check_root_path_host.py --output artifacts-nova/boot-speed-round2-nova/root-tests-nova/host-green2-nova --openssl-prefix /opt/homebrew/opt/openssl@3`

QEMU：`python3 -B unixnu-main-nova/drivers/NativeUfs/tests/check_qemu_root_commands.py --output artifacts-nova/boot-speed-round2-nova/root-tests-nova/qemu-green2-nova --qemu /opt/homebrew/bin/qemu-system-aarch64 --openssl-prefix /opt/homebrew/opt/openssl@3`

- RED：`red-results-nova.json` 与两个 RED 日志、保留旧生产函数的可执行文件。
- GREEN：`host-green2-nova/*.log` 与 `qemu-green2-nova/report.json`。
- 修改/备份哈希与运行状态：`completed-manifest-nova.json`。
- QEMU 初次因沙箱拒绝 Unix 套接字退出，自动审批允许后通过。主机入口初次遇到既有未使用 setter 的编译警告，在测试中显式选择合成 userdata 分区后通过，没有降低编译告警要求。

验证范围是生产读取/命令/认证路径及合成设备。手机缓存一致性、SM8550/SMMU、真实 UFS 延迟和 Boot→Hello 收益仍需主代理真机测量。
