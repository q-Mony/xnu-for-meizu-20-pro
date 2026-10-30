# Nova 第三轮根盘 SHA adapter 集成测试

2026-10-29。**PASS，无测试阻断。** 本目录所有文件为新建，无旧版。仅新增此测试目录，未修改生产、旧测试、计划或 CHANGELOG，未操作手机，未运行原先不相关的完整测试组。

先记录旧代码 RED：[red-results-nova.json](<nova-root>/artifacts-nova/boot-speed-round3-nova/root-sha-tests-nova/red-results-nova.json:1) 和 [provider-red-nova.log](<nova-root>/artifacts-nova/boot-speed-round3-nova/root-sha-tests-nova/provider-red-nova.log:1)。旧真实 xnu_crypto.o 不导出 vr_xnu_data_crypto，符号契约检查按预期退出 1，源代码 SHA 为 af24f73b…d21b7。该 RED 只证明新增 provider 接口缺失，不被描述为旧代码密码学错误。

修改后 [run1 结果](<nova-root>/artifacts-nova/boot-speed-round3-nova/root-sha-tests-nova/run1-nova/results-nova.json:1) 与 [运行日志](<nova-root>/artifacts-nova/boot-speed-round3-nova/root-sha-tests-nova/run1-nova/integration-test-nova.log:1) 通过。真实 adapter 源 SHA 为 `736557030926a03b0085aabed04ffb0d818335a488085a4c0decca40ed365dd8`；测试结束后重新读取确认未变化。当前生产 xnu_crypto.o 也通过同一个 provider 符号契约检查。

| 覆盖 | 结果 |
|---|---|
| 真实 vr_verify_block 数据路径 | 1440 个用例全部通过 |
| 实际生产 ARM64 SHA 汇编路径 | 648 次摘要调用，feature=1/2；真实执行 SHA 指令 |
| 动态软件回退 | 684 次摘要调用，feature=0/15 及其余保留值；真实 scalar corecrypto |
| 参数拒绝 | 108 次在摘要前拒绝，native/旧 SHA/RSA 计数均未增加 |
| 消息长度 | 16 KiB 单元、512 字节与4608 字节真实尾块，三种窗口均先验真实签名 |
| 输入偏移 | 每单元地址偏移0–31，前后 guard 与输入内容保持不变 |
| 完整性负例 | 每单元首/中/末位翻转，同长度单元换序均拒绝 |
| 旧路径对照 | 每一单元额外使用原 provider 校验；旧 SHA API 各1次，native/feature/硬件回退计数为0 |
| policy/RSA | 每个成功 vr_open 旧 SHA init/update/final 各3次、RSA make/verify 各1次，native计数0；签名/哈希表/anchor/代数/空manifest负例保持原状态 |
| RSA callback | 新旧 provider 的 RSA callback 指针相同；真实 adapter verify 函数未经替换 |
| 抢占模型 | 每批≤256 SHA block，disable/enable/feature-read 配对，嵌套深度0/1/2恢复；硬件检查期间禁抢占，软件回退时恢复 |
| sanitizer | AddressSanitizer + UndefinedBehaviorSanitizer 通过，编译日志无诊断 |

使用完整消息 SHA，所以16 KiB输入含padding后共257个64字节压缩块；测试按实际 padding 公式校验，没有把认证单元、传输批次和 SHA compressor 的计数混为一谈。

测试调用链：`真实 vr_verify_block → 单独编译的真实 xnu_crypto.c / vr_xnu_data_crypto → 计数转发函数 → 当前 native_sha256_digest C wrapper → 实际生产 ARM64 ASM 或真实 scalar corecrypto`。旧 policy 仍调用同一真实 xnu_crypto.c 的 vr_xnu_crypto。没有使用 tests/openssl_provider.c 替代真实 adapter。

[test-root-adapter-nova.py](<nova-root>/artifacts-nova/boot-speed-round3-nova/root-sha-tests-nova/test-root-adapter-nova.py:1) 复用此前测试的真实 scalar 七个源文件、原生汇编和抢占模型；从当前 corecrypto_sha2.c 重新提取 wrapper，仅把特权 ID寄存器读取与抢占操作换成 host 模型，并通过转发包装记录硬件/软件调用。对真实 adapter 单独编译时将 SHA API 符号指向计数转发，真实 SHA运算没有被假返回值替换。测试汇编反汇编与此前生产编译对象完全一致。

真实 adapter 的 RSA调用由兼容 ABI shim 转给 OpenSSL RSA3072/PKCS#1v1.5/SHA256 验签，合成测试 policy 使用新生成的 fixture key。该部分证明原 callback 路由和合法/损坏签名契约，**不声称重测内核 RSA内部实现**。

详细测试断言在 [root-adapter-cases-nova.c](<nova-root>/artifacts-nova/boot-speed-round3-nova/root-sha-tests-nova/root-adapter-cases-nova.c:1)，OpenSSL-backed RSA shim 在 [rsa-host-nova.c](<nova-root>/artifacts-nova/boot-speed-round3-nova/root-sha-tests-nova/rsa-host-nova.c:1)，完整命令与退出码在 [commands-nova.json](<nova-root>/artifacts-nova/boot-speed-round3-nova/root-sha-tests-nova/run1-nova/commands-nova.json:1)。所有输出写在新测试目录；编译使用 Nova和系统工具作为输入。

可重现命令（输出目录须尚不存在）：

```sh
python3 -B artifacts-nova/boot-speed-round3-nova/root-sha-tests-nova/test-root-adapter-nova.py --output artifacts-nova/boot-speed-round3-nova/root-sha-tests-nova/run-next-nova
```

本结果是 arm64 Mac上真实算法/汇编执行与host模型测试，不能证明XNU调度、实际IRQ时延或手机Boot→Hello收益。没有执行host benchmark。NativeUfs实际传入哪个provider及最终payload字节由主代理/链接审查另行核验。
