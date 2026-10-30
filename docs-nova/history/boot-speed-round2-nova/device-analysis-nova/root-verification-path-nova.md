# Nova 根盘 VerifiedRoot SHA 调用路径核查

2026-10-29；新建报告，无旧文件。仅只读源码、现有编译对象和交付镜像；未修改源码、计划、CHANGELOG，未构建、操作设备或重跑既有测试。本报告固定检查 raw kernel `3f590ddaeaef16c0b7a0fe98895e9d70279a23ba571ed53b95bcc8b1f7edf885` 与本轮最终 payload `55a3b5a2d2a9d6a11b150109cb039ad930749a3ef85662fa2ab2bebdb7fb8a35`，不能用后续活动源码替代此候选证据。

**结论：本轮根盘数据的 VerifiedRoot 校验仍然走软件 SHA-256，没有进入已经用于 cs_native4k 的 native_sha256_digest。** 已链接的硬件 SHA 实现不会自动替换原 SHA256_Init/Update/Final。本轮用户测得 Apple 97 秒、Hello 245 秒；仅凭本静态检查不能归因本轮与此前 237/190 秒的差异。主代理报告累计 root verify 为约 33.47 秒，但覆盖约 344.55 秒采集窗口且包含抢占/中断时间，不能当成 Hello 前可确定节省的秒数。

建议下一单变量候选采用主代理提出的窄改动：保留 vr_xnu_crypto 用于 vr_open、policy/header/hash table 与 RSA；新增 const vr_crypto vr_xnu_data_crypto，SHA callback 调用 native_sha256_digest 并返回原成功契约，RSA callback 仍复用原 verify；仅 NativeUfs 的 vr_verify_block 调用改传新 provider。此处输入和调用时机适合复用已测的公开 C 入口，保留后文约束。

## 真实调用链

源码从 [NativeUfs.cpp:1733](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1733) 传入 vr_xnu_crypto；[vr_verify_block](<nova-root>/unixnu-main-nova/kits/VerifiedRoot/src/verified_root.c:47) 先检查索引与实际长度，再调用 crypto->sha256，对完整 32 字节摘要作原有比较。当前 [XNU adapter](<nova-root>/unixnu-main-nova/kits/VerifiedRoot/adapters/xnu_crypto.c:7) 的 hash callback 是 SHA256_Init → SHA256_Update → SHA256_Final。

实际链接证据如下，地址为候选的未滑动虚拟地址：

| 步骤 | 实际证据 |
|---|---|
| UFS 批读取传入 provider | `0xfffffff008941c64/…c68` 的 ADRP+ADD 将 x4 设为 `0xfffffff007212040`，随后在 `…1c6c` BL 到 `_vr_verify_block` |
| provider | `_vr_xnu_crypto` 位于 `0xfffffff007212040`；第一指针为 `0xfffffff00895391c`，第二 RSA callback 为 `0xfffffff00895398c` |
| verifier callback | `_vr_verify_block` 位于 `0xfffffff008953828`；从 `[x4]` 加载 callback，并在 `0xfffffff0089538ac` 执行 BLR |
| adapter hash | `0xfffffff00895391c` 的 112 字节函数，在 `…3954/…3964/…3970` 分别 BL 到 `_SHA256_Init/Update/Final` |
| 旧 SHA wrapper | Init/Update/Final 分别位于 `0xfffffff0088435fc / …3638 / …3674`；都调用 `_ccsha256_di`，没有调用 native_sha256_digest |
| global descriptor | `_ccsha256_di` 位于 `0xfffffff008389868`，实际只有 ADRP+ADD+RET，返回 `_ccsha256_ltc_di`=`0xfffffff007176908` |
| compressor | 上述 descriptor 的 +48 字节 compress 指针为 `_ccsha256_ltc_compress`=`0xfffffff008389874`；反汇编未出现 SHA256H/H2/SU0/SU1 指令 |

UFS 的实参装载与 BL 在已有 [批读取反汇编](<nova-root>/artifacts-nova/boot-speed-round2-nova/linked-rootio-audit-nova/fixed-3f590d-verified-nova/batch-nova.disassembly.txt:120) 中可复核。provider 的 local `_hash` 名字与别的编译单元重名，所以本次按 provider 表中的实际指针取函数，而非用名字猜测对应 hash。

`xnu_crypto.o` 的 undefined 列表包含 SHA256_Init/Update/Final，没有 native_sha256_digest；其 `_hash` 三个 BRANCH26 relocation 位于对象函数偏移 0x38、0x48、0x54。该对象与 libVerifiedRootProbe.a 的 xnu_crypto.o 成员完全一致；排除这三个重定位字后，其 112 字节 hash 与上述 kernel 指针所指函数一致，三个重定位目标也分别解码确认为旧 SHA 入口。vr_verify_block 的 244 字节对象函数与 kernel 的非重定位字一致。

同一虚拟地址处的 provider 表、112 字节 hash、旧 SHA 三入口、ccsha256_di、LTC descriptor 和软件 compressor，均在最终 payload 与 raw kernel 中逐字节一致。vr_verify_block 所在比较范围也一致。raw/payload 在检查结束时重新读取，内容没有变化。这证明该链存在于已交付候选，而非仅存在于源码或遗留对象。

## 构建配置支持

[build-meizu-verified-root.py:158](<nova-root>/unixnu-meizu20pro-nova/scripts/build-meizu-verified-root.py:158) 从 bsd_init.o.json 继承实际 C 编译参数，编译 verified_root.c、xnu_crypto.c 和 probe，随后归档到 libVerifiedRootProbe.a。本次未重新执行构建脚本；使用现有对象和 archive 成员核对。

对当前 [ccsha256_di.o.json](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/ccsha256_di.o.json:1) 的原参数只执行 `-E -dM`（删除 `-c/-o`），返回码 0，最终宏为：

```text
CC_KERNEL=1
CC_USE_ASM=0
CCSHA2_VNG_ARM=0
CCSHA256_ARMV6M_ASM=0
CRYPTO_SHA2=1
XNU_SOURCE_CORECRYPTO14=1
```

这与 [ccsha256_di.c:42](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/corecrypto/ccsha256_di.c:42) 选择 LTC 分支、以及真实 descriptor 指针一致。[corecrypto_sha2.c:57](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c:57) 中原三入口未全局替换。

## 与 cs_native4k 的区别及复用约束

cs_native4k 在 [ubc_subr.c:5810](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/ubc_subr.c:5810) 对 SHA256/截断 SHA256 选择 native_sha256_digest。当前 [链接审计](<nova-root>/artifacts-nova/boot-speed-round2-nova/linked-sha256-rootio-fixed-nova/audit-nova.json:3) 的 kernel SHA 与本次完全相同，记录了 cs_native4k_digest → native_sha256_digest → 局部 compress callback → native_sha256_compress_arm64 的真实机器码。它是独立 opt-in 路径；将整个内核的 CC_USE_ASM 打开或更换全局 descriptor 会扩大范围，不是本轮需要的接入点。

正确入口是 [native_sha256_digest](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c:208)，由 [native_sha256.h](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/libkern/crypto/native_sha256.h:11) 声明，返回 void；adapter 保持原 SHA callback 的 int 成功返回契约。不要直接调用 native_sha256_compress_arm64：该符号仅处理完整 64 字节压缩块，不负责完整消息的 init/padding/final，且能力检测与抢占保护在 C wrapper。

- **时机与锁**：当前 doAsyncReadWrite 已检查 online；ufsReadWindow 持有每个窗口的 IOLock，hostRead4k 完成后释放 UFS host 锁，才进入 verify。这个现有可睡眠 I/O 路径属于正常线程上下文；新 provider 不应被挂到 IRQ progress 或早期 configureVerification。窄 seam 保留 policy 初始化原路径，避免扩大 CPU/NEON 初始化时机。
- **缓冲生命周期**：[NativeUfs.cpp:1322](<nova-root>/unixnu-main-nova/drivers/NativeUfs/NativeUfs.cpp:1322) 用 IOMalloc(kDataMax) 分配每窗口 bounce；verify 输入是 bounce+offset，在窗口锁内稳定，摘要写入 verifier 独立栈缓冲。native digest 的 descriptor/context 均为栈局部对象，无共享可变摘要状态、无动态分配，可在独立输入/输出缓冲上并发。
- **长度与信任边界**：[RootWindowRead.h:51](<nova-root>/unixnu-main-nova/drivers/NativeUfs/RootWindowRead.h:51) 逐个原始 16 KiB 认证单元验证，尾块传真实长度。保持 16 KiB policy 单位、32 字节摘要、原有同一 signed hashes 索引、失败状态和整批认证后才复制；4 KiB 传输填充不能进入摘要。即使传输批次为 128 KiB，也不能把它合成一个新认证摘要。
- **抢占与异构 CPU**：[compress wrapper](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c:179) 每批最多 256 个 SHA block（256×64 B=16 KiB），先禁抢占，再读取当前 CPU 的 ID_AA64ISAR0_EL1[15:12]；只有值 1/2 使用硬件 SHA，其余值恢复抢占后回退原软件 compressor。每批重新检查；新 adapter 不需要自行关中断、缓存能力或延长禁抢占。
- **中断与 SIMD**：独立汇编保存和恢复实际使用的 q0–q7、q16–q24（17 个寄存器、272 字节），异常 [保存路径](<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/arm64/exception_asm.h:165) 保存 q0–q31、FPSR、FPCR并有相应恢复。禁抢占不等于禁 IRQ。完整 C digest 仍遵循 C ABI，不能宣称它保持全部 caller-saved SIMD 寄存器不变；它也不会自行建立 CPACR/FPEN 或修复非法/可缺页输入。因此不把这一结论推广到任意 IRQ 或早期启动上下文。
- **已有证据边界**：[5480 项摘要和 SIMD 测试](<nova-root>/artifacts-nova/boot-optimization-nova/sha256-nova/run3-nova/results-nova.json:1) 属于此前 Mac 执行与抢占模型证据；本次不重跑。下一候选需真实 adapter 路由测试、原 signed block/尾块/篡改拒绝契约测试，以及链接后证明只有 NativeUfs 数据认证使用新 provider。最终收益仍需同条件真机测量。

## 固定证据哈希

| 输入 | SHA-256 |
|---|---|
| xnu_crypto.c（接入前） | af24f73b42dbbd3d99d346fda62596bd2f6e525d7185d435a712b2b8a66d21b7 |
| verified_root.c | d3a07258f96664c24764e429a860a47998b28475b945c1ba2ec78f832606c149 |
| NativeUfs.cpp（接入前） | f719119ec580a68d1d4817bd01290e7cd0e0e171b782220a9b696647a4d1ee83 |
| corecrypto_sha2.c | 95da14af9619dafcfc4168c92d7c0c858b70fee68c6f47324b07b66fc4a6b421 |
| xnu_crypto.o | 23faa949b6241f095dc609823738567c569a04b0434fdf7cee1470f301636908 |
| libVerifiedRootProbe.a | ac7f8e6852b3a5435b73ae73695b8150008227763261049f0d483156c74c8a57 |
| build-meizu-verified-root.py | d20a87fbd3984a82250f9122c679c23090613f07affc46b4cff5c86973b2b3ef |

外部 SHA API 调用、provider 表和软件 compressor 均已检查实际链接结果；没有把源码声明、编译通过或硬件指令符号存在单独作为根盘已加速的证据。
