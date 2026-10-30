# R9 独立集成审阅 — Nova

2026-10-30。此目录及文件均为本轮新增，无旧版。仅写入本 review 目录；未修改生产代码、中央 CHANGELOG，未操作手机或启动完整构建。

**结论：冻结版本的独立 host 审阅通过，未发现阻止受控 R9 实机试验的剩余问题。** 该结论不证明暖 Data 能加速，也不证明 APFS 一定接受这个 block cut。最终代码、构建/镜像绑定和实机回读仍由主代理与构建代理负责。

## 冻结来源

逐字匹配 `warm-load-implementation-nova/review-candidate-sources-nova.json`，并在全部测试前后复核未变化：

| 文件 | SHA256 |
| --- | --- |
| NativeRamBlock.cpp | `228ad81cd6e2c65839d7293f3f7929124db2cfbd190b13e4f6af5e67a338c699` |
| NativeUfs.cpp | `d4ebd181d1bc96a84b83f3af66b23e27f2a1b6144ab75bd22ac9efce02a56ad6` |
| WarmLoad.h | `f1f7d33b96cae0ee1dfdf39f0177adf002dd27fc6731e26b13baedfac7f9324e` |
| WarmLoadContract.h | `ca199fd895803ed6dab9ac1abab682cfb9842db605daac0ba3543cfbc7c2ef54` |

## 实际代码测试

运行 `python3 artifacts-nova/boot-speed-round9-nova/integration-review-nova/run-integration-review-nova.py` 可复现。脚本只在本目录生成提取文本、host 二进制及证据；没有设备调用。全部使用 AddressSanitizer 与 UndefinedBehaviorSanitizer，结果见 `integration-result-nova.json`。

| 组 | 数量 | 实际执行的生产边界 |
| --- | ---: | --- |
| source-contract | 20,030 | 直接 include `sourceWindow` / `validRead`，覆盖空间守卫、READ10 上限、加法溢出、句柄篡改和读取边界 |
| source-wrappers | 62 | 提取原样 `warm*Configuration`、source open/read、export write、完整 `startRoot` / `publishThread` / discard；覆盖所有 prepare 阶段故障、线程分配失败、重复 root 尝试、源发布顺序和 OS gate |
| ram-lifecycle | 26 | 提取原样 `warmLoad` / worker / publish / stop，及原 `start` 完成冷种子导入后的尾部；覆盖持有引用、失败回退、释放候选、停止交错、同步 I/O / stop 重入 |
| ram-gate | 38 | 提取原样 `native_ram_block_warm_media_ready` 与 `warmMediaAvailable`；覆盖 UUID、目标 registry ID、BSD 身份、深度边界、重复匹配、失效遍历、分配失败及停止状态 |
| 合计 | **20,156** | 全部通过，sanitizer 无报告 |

测试区分了验证边界：IOKit 对象、调度器、UFS 命令与时钟均为可控 fixture。lifecycle 组在 importer 接口注入结果，以观察真实 wrapper 是否交换/释放错误对象；没有用该组声称完整导入器已被重新验证。真实 `WarmLoad.h` 导入器及 R8 完整 export 的 386 组合成故障/fixture 测试由实现代理负责，见其 `REPORT-nova.md`。注册表 fixture 统计引用持有/释放；原 API 生命期另外对照了当前 XNU `IORegistryEntry.cpp` 中 iterator 的保留语义。

## 审阅结论及已解决问题

- **默认关闭且实验互斥。** 仅 warm=1 开启；出现 capture/persist/reset 参数即拒绝暖导入。warm 请求时 capture 与 export 写入被阻止，attachStore 保持 mode0。配置拒绝会保留冷种子，实际试验必须核对 warm status，不能把冷回退当暖启动数据。
- **源发布顺序正确。** `startRoot` 的决定对象只在 manifest、全部 owner window 准备/签名 UUID 校验、publisher 线程派发完成后发布 Ready。每个早退均 Rejected。单次 CAS 拒绝重复 root 尝试，保留首个已发布源的句柄。源读取不依赖 log/store 打开，也不调用写入或 sync。
- **恢复发生在发布前。** 原冷种子先被完整导入/哈希；独立持有 service 的 worker 构建私有 candidate。仅成功且未取消、未 online/发布时交换。失败仅释放 candidate，保留冷对象与冷 hash；成功释放旧冷对象，seed hash 更新为暖内容。R8 pinned header/payload/logical hash 在导入器内验证。
- **异步生命周期满足本轮边界。** 线程派发持有引用，完成后释放，派发失败归还引用并发布冷盘。publicationGate 与 block gate 分开；registerService 不持有 block gate，且同步 stop 可递归进入 publicationGate。stop 先设置取消，再串行化 publication 并置 offline。确定性交错测试覆盖 worker 前、源等待中、导入完成到交换之间和 registerService 重入。
- **OS gate 满足本轮身份要求。** 只接受本次选定 RamBlock registry ID 下、实际 UUID 为 `691978E3-CC8E-4F14-9BF9-D55DAB2D1A4C`、拥有完整且有界 BSD 身份的唯一媒体。外来祖先、重复 UUID、失效迭代器、深度超限、已停止盘都不能放行。SystemOS 先发布；Data gate 拒绝/超时后不启动 OS，释放未启动对象，保留已经启动的服务。
- **初始 UUID 错配已修正。** 第一版合同沿用了 `18AB5557-...`，与 R8 `handle_mount`、离线 `fsck_apfs`、当前 `owner-data-volume.h` 不符。审阅指出后，最终生产常量与合同均改为上述实际 R8 UUID。此问题已解除。
- **诊断可区分暖成功与冷回退。** 最终 durable summary 增加实际 payload/logical SHA、status、capture ID、读取字节数与 source/import/publish 时间。成功状态发布在 digest 复制之后。不能仅凭 Hello 画面认定暖源生效。

## 设备脚本的附加静态审阅

主代理要求审阅 `device-run-nova.py` 新的 Android B 直写 inactive A 流程。此次只读检查未发现阻断：固定唯一原始写目标为 `boot_a`；push 后核对 staged SHA/大小，写前重新检查 B 活动槽与 A/B 全分区 SHA；`dd conv=fsync` 后在原 Android B 复核 A=候选、B=原始。安装失败路径不重启或切槽；独立 start 阶段必须有成功安装记录，并重新核对 A/B 才切 A。设备原始序列号在输出/异常中脱敏，记录只保留既有哈希 tag。

主代理另已对实际 installer 完成 6 项 mock 测试，见 `device-install-host-tests-nova.json`；本代理未重复同类测试。审阅时 CANDIDATE 为 UNBUILT_NOT_READY，主代理需要在最终候选 hash 替换后继续绑定/复核。该脚本不是本目录生产冻结清单的组成部分。

## 尚需实机验证的边界

- 10 秒 source、30 秒 load、60 秒 Data gate 都是软件检查预算。当前执行中的硬件命令、锁、分配/释放、哈希或 registerService 可能越过下一个检查点；没有硬实时保证。
- 传输失败可能 poison UFS，保留冷 Data 不等于根盘 I/O 必定恢复。本轮未增加重试/reset，也未改变旧硬件超时。
- Host fixture 覆盖确定性停止交错和同步重入，不能穷尽真实内核并发时序；未进行真实 IOKit 热插拔或线程竞态测试。
- R8 是 block cut，离线文件系统检查通过不代表所有跨服务事务一致，也不保证 LaunchServices/MCM 接受并复用缓存。
- 必须从 R9 日志确认 success、两个 exact digest、Data mount/owner 准备成功，再比较注册次数与 SB→首个 Setup 区间，最后由用户确认 Hello。Setup watchdog 与后续 DecodeRow 问题继续保留，不与本轮实验混改。
