# Round 3 数据 SHA 调用链独立审计

结论：`PASS_KEY_CALL_CHAIN`。本审计范围内没有阻断实机测试的问题。所有本目录文件均为新文件，无旧版本；未修改生产代码、其他会话目录或设备。

固定输入：

| 输入 | SHA-256 |
|---|---|
| raw kernel | `240fed963ddd0271afc4e86c3410b2d049214f41099b35400531a84a75b2e378` |
| packaged payload | `839f460f9e38fbe217c3eea88adbdeca1a13b70cea38349ec02a83ae001d7ff1` |

实际链接结果：

| 路径 | 二进制证据 |
|---|---|
| `configureVerification → vr_open` | `0xfffffff008940890` 的 BL 前，x4 解析为原 `_vr_xnu_crypto = 0xfffffff007212040`；ADRP/ADD 后没有覆盖 x4。 |
| `ufsReadWindow → vr_verify_block` | `0xfffffff008941c6c` 的 BL 前，x4 解析为 `_vr_xnu_data_crypto = 0xfffffff007212050`。 |
| 原 provider | SHA 指针 `0xfffffff00895391c`；RSA 指针 `0xfffffff00895398c`。SHA 仍直接调用 `SHA256_Init/Update/Final`，RSA 仍调用 `rsa_make_pub/rsa_verify_pkcs1v15`。 |
| 数据 provider | SHA 指针 `0xfffffff008953a28`；RSA 指针与原 provider 相同。`hash_data` 在 `0xfffffff008953a30` 直接 BL 到 `native_sha256_digest = 0xfffffff0088437f4`，随后返回 1。 |

完整性路径仍在：`vr_verify_block` 从 provider 的首指针取 SHA 函数，在调用前要求实际长度严格等于签名的 16 KiB 单元或末尾剩余长度，之后比较完整的 32 字节摘要。批读循环仍按 16 KiB 校验；校验失败在复制前退出。`RootWindowRead.h` 的固定哈希未变，批读上限仍为 128 KiB。

归档和保留性检查均通过：

- `xnu_crypto.o`、`verified_root.o`、`NativeUfs.o` 与各自 archive 中的实际成员逐字节相等。
- 8 个关键函数的对象机器码与最终 raw kernel 一致，排除的重定位指令逐项列在 JSON；上述 provider 地址和直接调用另行解码验证。
- 原 SHA/RSA、`vr_open`、`vr_verify_block`、UFS 批读/config/stats 共 7 个函数的对象指令字节与上一轮一致。重定位目标不在这项字节比较内，新 provider 的重定位与最终地址已经独立核对。
- payload 中的两个 provider 结构及 10 个关键函数与 raw 完全同字节，包含 native digest、数据/原 SHA、RSA、VerifiedRoot 核心、UFS 批读/config/stats 和 heartbeat。
- 最终 kernel 保留 `meizu_autolog_dump → native_ufs_log_read_stats → printf` 直接调用，可继续取得读、校验、复制分项耗时。
- 两个改动源码匹配父代理 manifest；`RootWindowRead.h`、`kern_exec.c`、`corecrypto_sha2.c` 匹配固定的未变哈希。审计完成时再次检查 raw、payload、当前对象和源码均未变化。

注意：最终 kernel 存在重复的局部 `_hash`、`_verify` 符号。脚本保留全部符号，并从 provider 的真实指针选取函数，避免按名字误选另一套函数。

这是静态输入/调用链审计，不证明实机速度或运行时是否命中硬件分支。native SHA 实现/汇编与 PAN/打包审计由父代理负责；真实 adapter 的功能与 sanitizer 测试由另一子代理负责。这里未重复那些测试，也未进行刷机。

机器证据：`audit-nova.json`。可复现脚本：`audit-linked-data-sha-nova.py`。四份 `*-nova.disassembly.txt` 保留了实际函数反汇编，`ufs-provider-relocations-nova.txt` 保留 UFS provider 重定位记录。
