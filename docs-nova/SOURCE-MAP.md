# R18 源码映射与覆盖

R18 冻结清单 `source-freeze-r18-nova.json` 共 6186 项。导出映射如下：

| 冻结范围 | 导出位置 |
|---|---|
| `unixnu-main-nova/...` | `unixnu-main-nova/...`（子模块内容同路径扁平化） |
| `unixnu-meizu20pro-nova/...` | `unixnu-meizu20pro-nova/...` |
| `.../.cache/xnu-work/...` | `sources-nova/xnu/...` |
| `.../out/meizu20pro/.config` | `configs-nova/meizu20pro-r18.config` |
| `.../tree/build/xnu/owner-data-volume.h` | `configs-nova/owner-data-volume.h` |
| `unixnu-nova` | `unixnu-nova` |
| `unixnu-main-nova/.gitmodules` | `provenance/gitmodules/unixnu-main-nova.gitmodules` |

## 关键 R18 文件字节校验

| 源文件 | 导出 | 期望 SHA-256 | 实际 | 结果 |
|---|---|---|---|---|

| `unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp` | `unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp` | `08437deda6354ff660a41af7f2ea56ada41c90183a78f1935f0ae326402cbacb` | `08437deda6354ff660a41af7f2ea56ada41c90183a78f1935f0ae326402cbacb` | MATCH |
| `unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c` | `sources-nova/xnu/libkern/crypto/corecrypto_sha2.c` | `a5996632c820928896bd9b2f8790ce703cbde838925343c0c809b193ecb56172` | `a5996632c820928896bd9b2f8790ce703cbde838925343c0c809b193ecb56172` | MATCH |
| `unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/libkern/crypto/native_sha256.h` | `sources-nova/xnu/libkern/libkern/crypto/native_sha256.h` | `faa8a909cf7b5ec353d87b2e1de1258fa33d212906b2617b2a838009d03dc742` | `faa8a909cf7b5ec353d87b2e1de1258fa33d212906b2617b2a838009d03dc742` | MATCH |

## 覆盖

逐项哈希、缺失与额外文件见 `provenance/coverage-r18-nova.json`。
冻结清单内被策略排除的文件数应为 0；若有缺失必须在该 JSON 的 `missing` 中给出
可解释原因。当前工作树在冻结后新增/修改的源码会作为“extra_current”记录，不影响
冻结项字节一致性。

## 快照不是 cache

`sources-nova/xnu` 是当前工作树（含 100+ 修改与 31 个未跟踪源码文件）的完整源码，
包含 `native_sha256` 头/汇编、`kern_exec`、`mach_msg` 等 Nova 修改。不要用
“通用 clean/fetch”覆盖；`prepare-source-nova.py` 会把它恢复到构建树。
