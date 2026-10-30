# 来源、固定版本与许可证

本文件由导出脚本生成；逐文件哈希见 `provenance/inventory-nova.json`。

## 上游固定版本

| 名称 | 上游 | commit / tag | 导出方式 |
|---|---|---|---|
| xnu | https://github.com/apple-oss-distributions/xnu.git | `d8b80295118ef25ac3a784134bcf95cd8e88109f` / `xnu-10063.141.1` | 完整源码快照 `sources-nova/xnu` |
| libdispatch | https://github.com/apple-oss-distributions/libdispatch.git | `30fe6a7d945340ce69cef121132f049e810089a5` / `libdispatch-1477.100.9` | 快照 `sources-nova/libdispatch` |
| libpthread | https://github.com/apple-oss-distributions/libpthread.git | `c0e60fb5ecfc54cf379c261d3f9572cc22ded891` / `libpthread-519.120.4` | 快照 `sources-nova/libpthread` |
| libplatform | https://github.com/apple-oss-distributions/libplatform.git | `a00a4cc36da2110578bcf3b8eeeeb93dcc7f4e11` | 固定 manifest，构建时拉取 |
| AvailabilityVersions | https://github.com/apple-oss-distributions/AvailabilityVersions.git | `933f9360f1b70dfbda5548386a3d65e0358b4f7d` | 固定 manifest |
| corecrypto-2023 | https://github.com/ONLYA/corecrypto-2023.git | `11fba65b12d8830630e248dfa8344caf8b065f26` | 固定 manifest |
| IOStorageFamily | https://github.com/apple-oss-distributions/IOStorageFamily.git | `e45fbe30a7879e0b16e26dddb78538f1b00e0bff` | 固定 manifest |
| Mu_Basecore | https://github.com/microsoft/mu_basecore.git | `cfb93724a06ff86ff5ba971413368a7809ee42a5` | 固定 manifest + 本地补丁 |
| OpenSSL | https://github.com/openssl/openssl.git | `67b5686b4419b4cb8caa502711c41815f5279751` | 固定 manifest |


## 子模块 URL 与 HEAD（导出时）


### main

| path | url | HEAD | dirty | 导出方式 |
|---|---|---|---:|---|
| `unixnu-main-nova/drivers/ArmCpu` | https://github.com/UniXNU/ArmCpu.git | `1951cc8444b340d7d8879e9418220cf29eb711d7` | 0 | flattened-source |
| `unixnu-main-nova/drivers/ArmDTPlatform` | https://github.com/UniXNU/ArmDTPlatform.git | `0d544705c2e89725caabc0542bb96bc8febd8c70` | 0 | flattened-source |
| `unixnu-main-nova/drivers/ArmVirtPlatform` | https://github.com/UniXNU/ArmVirtPlatform.git | `532de8c017e394e69a0b48dc0ffc4ac0d0701177` | 0 | flattened-source |
| `unixnu-main-nova/drivers/DarwinBoot` | https://github.com/UniXNU/DarwinBoot.git | `d099354d0eab34df5088ca1ba89c9c88bc3260bb` | 3 | flattened-source |
| `unixnu-main-nova/drivers/NativeAES` | https://github.com/UniXNU/NativeAES.git | `a34964b8c2fec2e83c8e4f68fb7d6fb7e4b2227b` | 0 | flattened-source |
| `unixnu-main-nova/drivers/NativeAdreno` | https://github.com/UniXNU/NativeAdreno.git | `cedcaa2949c795e71a47e78ae413c8a41b17d7d0` | 0 | flattened-source |
| `unixnu-main-nova/drivers/NativeGeniSpi` | https://github.com/UniXNU/NativeGeniSpi.git | `b5399b5646c4d7415deec6d887fd51715a97a024` | 0 | flattened-source |
| `unixnu-main-nova/drivers/NativeHidInput` | https://github.com/UniXNU/NativeHidInput.git | `ba03e5d147452b2166aab7435d219fa10877672b` | 0 | flattened-source |
| `unixnu-main-nova/drivers/NativeKeyStoreEvents` | https://github.com/UniXNU/NativeKeyStoreEvents.git | `e59bac4ad1ac8825919ae94da8440a64d8ab90b3` | 0 | flattened-source |
| `unixnu-main-nova/drivers/NativeMetal` | https://github.com/UniXNU/NativeMetal.git | `a96feceda55e8fed1e20c0a2998736cef7aab006` | 0 | flattened-source |
| `unixnu-main-nova/drivers/NativeMobileDisplay` | https://github.com/UniXNU/NativeMobileDisplay.git | `c031d882b9721d943383d11f66dc4748252ce929` | 0 | flattened-source |
| `unixnu-main-nova/drivers/NativeRamBlock` | https://github.com/UniXNU/NativeRamBlock.git | `d4d22d9819b0162eda49f6402235dddc2279410d` | 4 | flattened-source |
| `unixnu-main-nova/drivers/NativeUfs` | https://github.com/UniXNU/NativeUfs.git | `af785c1777161a62931425c2faaa536ce0bebd5b` | 7 | flattened-source |
| `unixnu-main-nova/drivers/NativeVirtioBlock` | https://github.com/UniXNU/NativeVirtioBlock.git | `98909eb9198a4360d7a43be831887cb8b74d814d` | 0 | flattened-source |
| `unixnu-main-nova/drivers/NativeVirtioConsole` | https://github.com/UniXNU/NativeVirtioConsole.git | `066f9bded311233ed56824e13a5628077619efda` | 0 | flattened-source |
| `unixnu-main-nova/kits/AdrenoFirmware` | https://github.com/UniXNU/AdrenoFirmware.git | `40fdb8ea87606612ac6f1e80bbb49955d0ce4051` | 0 | flattened-source |
| `unixnu-main-nova/kits/AdrenoPM4` | https://github.com/UniXNU/AdrenoPM4.git | `445f8fe6d115738352b9d78dc70af6d61aa7f62f` | 0 | flattened-source |
| `unixnu-main-nova/kits/AirNir` | https://github.com/UniXNU/AirNir.git | `032a867b5dc3121164bbd706e4e81e3b9fcd2b8f` | 0 | flattened-source |
| `unixnu-main-nova/kits/ArmSmmu4K` | https://github.com/UniXNU/ArmSmmu4K.git | `4d0d6598b1a36aeec3de01091893fa6660e49388` | 0 | flattened-source |
| `unixnu-main-nova/kits/NativePrelink` | https://github.com/UniXNU/NativePrelink.git | `2b6ffb50117eee820b98aca9bd51b5c7dfb2970e` | 1 | flattened-source |
| `unixnu-main-nova/kits/VerifiedRoot` | https://github.com/UniXNU/VerifiedRoot.git | `2854c61f6c4dfda870ac3d93e1f03d9181edb041` | 3 | flattened-source |

### uefi

| path | url | HEAD | dirty | 导出方式 |
|---|---|---|---:|---|
| `uefi-sheng-nova/Binaries` | https://github.com/Project-Silicium/Device-Binaries.git | `a1501e0c2e642d6ac0d8f52c85af2a67b75a7799` | 1 | excluded-vendor-binaries |
| `uefi-sheng-nova/Common/Mu` | https://github.com/microsoft/mu_plus.git | `b8d46c71610318e36a661819e1e3c0b8bdda413a` | 0 | flattened-source |
| `uefi-sheng-nova/Common/Mu_OEM_Sample` | https://github.com/microsoft/mu_oem_sample.git | `b70b4b7bb02c43bf9db2172dfb3ed0457e2361d6` | 0 | flattened-source |
| `uefi-sheng-nova/Mu_Basecore` | https://github.com/microsoft/mu_basecore.git | `cfb93724a06ff86ff5ba971413368a7809ee42a5` | 11 | pinned-source-plus-local-patch |
| `uefi-sheng-nova/Silicium-ACPI` | https://github.com/Project-Silicium/Silicium-ACPI.git | `2d465ad77c25de908f0ec7bdfcb57f865b61233c` | 1 | flattened-source |
| `uefi-sheng-nova/Silicon/Silicium/OpensslPkg/Library/OpensslLib/openssl` | https://github.com/openssl/openssl.git | `67b5686b4419b4cb8caa502711c41815f5279751` | 0 | pinned-source-manifest |

## 许可证与归属

- `unixnu-main-nova/LICENSE`、`unixnu-meizu20pro-nova/LICENSE`、
  `uefi-sheng-nova/LICENSE` 及其子模块内的 `LICENSE`/`COPYING`/`NOTICE` 原样保留。
- 上游 XNU 使用 Apple 公共源码许可；Mu/UEFI 使用 BSD-2-Clause-Patent 等各自许可；
  OpenSSL 使用 Apache-2.0。以各目录原始许可文件为准。
- 本导出只做源码搬运与文档整理，不对第三方代码重新许可。

## 历史报告中的本地路径

精选报告中的 `<nova-root>/...`、`[local-home]/...` 与
`[redacted-device-serial]` 是导出时替换；原始证据仅存在于本地 Nova 工作区，
不再分发。详见 `docs-nova/LOCAL-ONLY-EVIDENCE.md`。
