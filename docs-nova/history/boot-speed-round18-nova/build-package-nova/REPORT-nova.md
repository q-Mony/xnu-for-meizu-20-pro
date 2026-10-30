# R18 暖 Data 流式 SHA-256 本地构建与封包 — Nova

## 状态

**BUILT_AWAIT_NEW_USER_INSTALL_COMMAND**

- `installation_allowed=false`
- `install_authorized=false`
- `do_not_flash=true`
- `hardware_tested=false`
- `device_accessed=false`
- `installer_created=false`
- `installer_executed=false`
- `performance_claim=false`

本轮只在 Nova 本地构建和打包，未发送任何设备/adb/fastboot/挂载/切槽/重启/安装命令，未读取或分析运行日志。手机按任务约束保持 Android B；本任务没有枚举或验证设备。

本任务只构建并封装 Sol 已验收的 warm-sha-stream-nova 三份生产修改，不新增生产源修改。36 秒服务接收前等待和 Hello 后黑屏**未修复**，不能声称已修复；暖 Data SHA 的性能收益必须由手机验证，本文不把任何 host 数字当作手机节省。

## 输入与验收

| 输入 | SHA-256 | 状态 |
|---|---|---|
| `artifacts-nova/boot-speed-round18-nova/warm-sha-stream-nova/SOL-ACCEPTANCE-nova.json` | `eb6e4bb529d01c03489fd70ceac14122684fe9f788f828c5cc677bb129cad38a` | `SOURCE_AND_FINAL_GREEN_ACCEPTED_AWAIT_FULL_BUILD` |
| `warm-sha-stream-nova/REPORT-nova.md`（补充更正后） | `058a10dd20bc06b52ca3bfc3ac9bf32816b69172818412116b1b1dab9688bcd1` | 初次失败说明已追加 |
| `warm-sha-stream-nova/RESULT-nova.json` | `1431592168bad630d98e08356f5c1b2826a76ea9205877735fd985287835ad35` | 未改动，与 acceptance 的 `source_result_sha256` 一致 |
| `warm-sha-stream-nova/sol-first-green-evidence-nova.json` | `1edfeedc1537653febe314763359594716bb42bb2664026a4371148f550cefac` | Sol 从 transcript 抽取的初次失败证据 |

### 证据保留更正

按补充要求，先备份 `REPORT-nova.md`（`a93217b3…`）、`RESULT-nova.json`（`14315921…`）和当时 CHANGELOG（`15cd9f6c…`）到 `snapshot-nova/boot-speed-round18-nova/source-acceptance-correction-nova/`，再追加真实说明：最终 GREEN 前的第一次 GREEN 确实失败（`streaming` exit -6、`warmload` exit -6、`production_NativeRamBlock` exit 1），原因是测试侧 padding 计数和 payload/header 绑定假设错误，以及 `EXPORT_HDRS` 生成头仍是旧 357 字节；修正测试并刷新 903 字节 `faa8a909…` 生成头后通过。执行方曾删除初次失败 `green-run-nova` 并在原名重跑，原始完整失败目录已不存在，不能声称保留或还原；只有 transcript 证据。未改 GREEN 结果、未重跑已通过测试、未改生产代码、未删除目录。

## 源冻结：6186 项，仅三份预期差异

基线为 R17 `artifacts-nova/boot-speed-round17-nova/log-encode-completion-nova/source-freeze-r17-nova.json`，SHA-256 `1ebeb2bb4ece61019c7a4567ffe0455c1dd4174d23207d68957a08408f32b69b`，计数 6186。

R18 逐项核对结果：`verified=6186`、`missing=0`、`unexpected_changed=0`，仅以下三份文件与 R17 不同：

| 文件 | R17（before） | R18（after） |
|---|---|---|
| `unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/libkern/crypto/native_sha256.h` | `b7fa8774e982f365c59273d254cb855515f45293ecf7f7fb5e262f6f5e7c611e` | `faa8a909cf7b5ec353d87b2e1de1258fa33d212906b2617b2a838009d03dc742` |
| `unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c` | `95da14af9619dafcfc4168c92d7c0c858b70fee68c6f47324b07b66fc4a6b421` | `a5996632c820928896bd9b2f8790ce703cbde838925343c0c809b193ecb56172` |
| `unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp` | `228ad81cd6e2c65839d7293f3f7929124db2cfbd190b13e4f6af5e67a338c699` | `08437deda6354ff660a41af7f2ea56ada41c90183a78f1935f0ae326402cbacb` |

构建后再次逐项复核 6186 项：`missing=0`、`mismatched_after_build=[]`。源冻结文件保留原前后 hash：`source-freeze-r18-nova.json` `77f483d6…`，`source-verify-r18-nova.json` `8d1d4684…`。

## 构建

完整命令（Nova 根目录，沿用已审核 R14 PATH bash shim）：

```sh
./unixnu-nova build kernel -j8
```

| 项 | 值 |
|---|---|
| PATH shim | `artifacts-nova/boot-speed-round14-nova/build-audit-nova/build-helpers-nova/bash` |
| shim SHA-256 | `7bf8733a9ae09c886d180b17d49919138c1bcff191063b2b92549c8f7fc3d169` |
| 退出码 | `0` |
| stdout | `build/build-output-nova.log` `181ae5aa…` |
| stderr | 空 `e3b0c442…` |
| raw kernel | `unixnu-main-nova/out/meizu20pro/kernel/kernel.macho` `6baa8e83f6cc408f843c9bd5cb5c5e1c3fa7f48e4d6e235338f936e461a99055`，73,949,184 B |
| R17 raw kernel | `658c301b39597f2879ab1a0ac0730a93f5d4677592d490deac581dfa25273946` |

构建前备份当前 raw kernel、`corecrypto_sha2.o`、`NativeRamBlock.o` 及全部受保护 object 到 `snapshot-nova/boot-speed-round18-nova/build-package-nova/`，并记录 `prebuild-hashes-nova.json`、`SHA256SUMS.txt`。R17 原产物和旧报告镜像均未覆盖。

### 两个实际改动对象

实际完整构建对象与已验证 GREEN production TU 对象逐字节一致；TU 重放只把 `-o` 改到新目录，保留 `-g` 和全部其他参数。

| TU | 完整构建对象 SHA-256 | GREEN TU | TU 重放 | 字节一致 |
|---|---|---|---|---|
| `corecrypto_sha2` | `0e5e79e708f7238fc35210077b9ba004ddac77b392aa885e9205c5220b8b05a3` (19,200 B) | `0e5e79e7…` | exit 0，`tu-r18/corecrypto_sha2-nova.o` | 是 |
| `NativeRamBlock` | `05304db41b0f6c76e1f9dc6726dde98679418ee3c853f6b0fc28ef4773c19cee` (381,656 B) | `05304db4…` | exit 0，`tu-r18/NativeRamBlock-nova.o` | 是 |

TU 记录：`tu-result-r18-nova.json` `5818d6ad…`。生成构建头 `EXPORT_HDRS/libkern/libkern/crypto/native_sha256.h` 构建后为 903 B `faa8a909…`，与允许源逐字节相同。

### 受保护 object（构建前后逐字节相同）

| Object | SHA-256 | 结果 |
|---|---|---|
| `BootLog` | `f60e1644f7c3ece260f88ae820f06279a015f19e6f5c4ee8b3c2230246ad1228` | unchanged |
| `BootWatchdogPolicy` | `5bfde100b6e66ef0a2da8fc2f10b1bf1dd846fb211775b274096164ed318fa72` | unchanged |
| `NativeUfs` | `49154bbeb3c74300353d57686905b7f008eff06dd1d1293fe4e51d9b54e412d7` | unchanged |
| `kern_exec` | `70e14124c8dc5c2a16fca6a4fc163dc42599d02c4db15723ba6929055a073ad4` | unchanged |
| `mach_msg` | `04b5e54efddf4f62fcd6f8e2ccb2843abc703d693d0fd724aefe07292760537d` | unchanged |
| `sleh` | `9fdf2e13943402dc866b828c227d325705bd6ad68bb9818f84c7bf64572b90d1` | unchanged |
| `NegroniFramebuffer` | `c0a4bf1f2f5c4fa1d772590eb4b333fed7be70928b7da66198ae5e5ec431758a` | unchanged |
| `MeizuHidProbe` | `7702d32cf344b23959e8f6ba722b0a25380a9069d344fef3a940df472b3c1d9f` | unchanged |
| `native_sha256_arm64` | `9d0688778da1e22348c96446d88ab0f7b3024b081187c075523a5bb9f9f51620` | unchanged |

头文件变化导致部分受保护 object 被合理重编译（BootLog/BootWatchdogPolicy/NativeUfs/kern_exec/NegroniFramebuffer/MeizuHidProbe），但字节完全相同；`mach_msg`/`sleh`/`native_sha256_arm64` 未重编译。没有忽略任何意外差异。

## 封包

沿用已验收 R14 96 MiB / FAT 90 MiB 封套，并以 R17 镜像核对候选基线：

| 输入 | SHA-256 | 核对 |
|---|---|---|
| R14 envelope image | `6ea4078227980d8c1f0a98982fa6dd37e5a91b251384674dca53699b0f264641` | 固定 |
| R15 image | `53470de0d676da3e41cb9c6a894b62b10686e0ef0480799535db0d1e331fe0ed` | 外壳逐字节一致 |
| R17 candidate image | `8796d6c5824ce563fda8b4d0e1a429f235749254648cc160534e5807283338d6` | 候选基线，非 FAT 逐字节一致 |

| 输出 | 值 |
|---|---|
| candidate | `package/boot-warm-r18-streamsha-candidate-nova/boot-warm-r18-streamsha-candidate-nova.img` |
| candidate SHA-256 | `be40edfedadbc161e3e86864c9d8803243c34426f78007f983213c07c879c21b` |
| candidate bytes | `100663296`（96 MiB） |
| FAT bytes | `94371840`（90 MiB） |
| payload kernel SHA-256 | `46a1dbd844d42350b306d04c8603526f7ab1b6b760b945b084adb97ae4f6b8d2`（73,949,184 B） |
| raw before patch SHA-256 | `6baa8e83f6cc408f843c9bd5cb5c5e1c3fa7f48e4d6e235338f936e461a99055` |
| manifest | `package/boot-warm-r18-streamsha-candidate-nova/evidence-nova/verification-nova.json` `4a63a767…` |

### 只改 kernel payload

- 原始输入 raw kernel 未打 entry patch：entry `0xfffffff0083b9488`，`branch_target == start_first_cpu == 0xfffffff0083bb05c`。
- payload 只 patch 一次：entry 不变，`branch_target == entry + 0x78 == 0xfffffff0083b9500`。
- PAN 检查通过：`evidence-nova/payload-pan-nova.json`，工具哈希与 R14/R17 固定值一致。
- 唯一 EFI 变化为 `UniXNU/Payloads/kernel.macho`；其余 4 个文件保持 R17（也等于 R14）逐字节一致：
  - `UniXNU/Payloads/kernel.macho.adt` `e619e34a…`
  - `UniXNU/Payloads/kernel.macho.trustcache` `eb1475f6…`
  - `UniXNU/Payloads/scratch-data.img` `0f1a60ef…`
  - `UniXNU/unixnu.toml` `21d1886a…`
- 除按规范重算的 Android v0 SHA1 槽（576–596）外，非 FAT 封套（0–576 header、DTB/AVB 区域、FAT 后尾部）与 R14/R17 逐字节一致；TOML、scratch-data、trustcache 均逐字节一致。
- Android v0 SHA1 已重算；最终 FAT 已重新提取并逐文件校验。
- payload 保留 R17 logger 修复标记：`nova: life v=16`、`meizu: sbfault-regs`、`nova: sblong v=15`、`meizu: selection attempt=`。
- payload 含 R18 streaming 符号：`_native_sha256_stream_init/update/final`、`_native_sha256_digest`。

独立验证 `package-validation-r18-nova.json` `ba221226…`：41/41 检查全部通过，`pass=true`。

## 证据与产物

| 文件 | SHA-256 |
|---|---|
| `build-result-r18-nova.json` | `b06d9e4cc1f07f468894b872350a2c19a9c60fee890fe089fdc99d2f1df90074` |
| `source-freeze-r18-nova.json` | `77f483d631ad403930c2631df0a324754d88893046caffbaa8d4db15cd9355fc` |
| `source-verify-r18-nova.json` | `8d1d4684bd3ab017b9d2dc5773500c7c2f4ddafe319bbe738fe59a1c652becd5` |
| `source-freeze-postbuild-check-nova.json` | `638f99aa9402e0bcff5baf470982ba87b3cc7978ac2919d430d69d3211e4c3d8` |
| `tu-result-r18-nova.json` | `5818d6adc0106883490db57b85668ac6cadd457cdcb8e36d63f9c4d8a5297618` |
| `package-validation-r18-nova.json` | `ba221226f48a4c1672ed4a29a19962585cae2d1ca8caa2b5f9f5451ac8556e71` |
| package `verification-nova.json` | `4a63a7675930f6de13213efd83f9b5d74ae9dbd8cd14510824284451b59e9110` |
| `artifact-hashes-r18-nova.json` | `2477b88e6c526134b669246ca81ec5a46e6a57df5f5c529fb8559c40255f6a32` |
| build stdout / stderr | `181ae5aa…` / `e3b0c442…`（空） |

所有脚本为 R18 新文件（无旧版）；`pack-r18-nova.py`/`validate-package-r18-nova.py` 从 R17 只读复制后按本轮三源、两个对象及 R18 输出重写，不是全局字符串替换。历史 helper 的 `__pycache__` 在运行前后逐文件 hash/mtime 未变，R18 目录没有生成 `__pycache__`。

## 限制

- 本任务未安装、未刷入、未切槽、未重启、未挂载，未访问设备；候选必须等待新的用户安装命令。
- 未做手机启动或性能测量；不声称 8.666060 秒已节省，不声称启动提速。
- 36 秒服务接收前等待和 Hello 后黑屏仍未修复。
- 只声明 Sol 已验收的本地 warm Data 流式 SHA 接入及本轮的完整构建、对象一致性和封包验证。
