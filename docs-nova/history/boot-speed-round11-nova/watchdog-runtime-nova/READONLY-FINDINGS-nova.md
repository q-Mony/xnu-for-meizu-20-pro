# R11 watchdog 实机语义只读审查 — Nova

新分析产物，无旧版本。未修改生产代码、R9/R10冻结产物或设备。R10 BootWatchdogPolicy.c SHA256仍为 `8cab1ad1971a3d03b1c581102cb9fd61b7e381a73904d36aad6f77eb1a696bce`。

结论：发现一个此前737/3933个模型案例均未覆盖的真实XNU限制；它会让R10暖分支在目录open处返回22。实机却报告17，所以这个静态缺陷不能直接当成当前panic17的完整根因。下一轮必须首先确证实际运行版本、暖分支选择、返回阶段和调用次数。当前没有可重放真实手机panic17的本地测试，不应把以下小型语义回放当作已经复现/修复该实机问题。

## 1. 确定遗漏：两个open标志互斥

`bsd/vfs/vfs_vnops.c:411–414` 的真实 `vn_open_auth`：

```c
if ((fmode & O_NOFOLLOW_ANY) && (fmode & (O_SYMLINK | O_NOFOLLOW))) {
    error = EINVAL;
    goto out;
}
```

R10 `BootWatchdogPolicy.c` 的warm目录open、warm文件create、warm文件read都同时指定 `O_NOFOLLOW_ANY|O_NOFOLLOW`。目录open是最早命中点。正常成功的copyout/mkdir之后，R10暖分支应立即返回 `EINVAL=22`，还没有开始校验policy内容。

旧host model不但遗漏此限制，`WatchdogHostTests.c:78`还断言目录必须同时带这两个标志；`open_file:61`统一要求O_NOFOLLOW。这使得错误组合通过模型测试。

构建审计子代理独立核对最终payload机器码：`_watchdog_warm_policy`在 `0xfffffff00895aa18`/`...aa1c`构造flags=`0x20100100`，`...aa34`调用`_open`；payload的`_vn_open_auth`也保留bit29与0x200100互斥检查以及EINVAL跳转。证据在同轮 `runtime-binding-nova/`。因此不是只根据当前源文件推测最终payload。

修复方向在被授权时应保留 `O_NOFOLLOW_ANY`，去掉互斥的 `O_NOFOLLOW`，并保留所有metadata、内容、EOF、路径/FD一致性检查。这不需要吞掉任何错误。当前未做修复。

## 2. syscall返回值和宏没有发现17/22混淆

真实 `open/openat` 在 `bsd/vfs/vfs_syscalls.c:5125–5175` 经nocancel包装返回`openat_internal`错误码；文件描述符写到第三参数。调用方正确读取函数返回的errno，没有把fd17当错误17。

`open1:4607`用FFLAGS转换访问位，不能去掉两个NOFOLLOW位；`vn_open_auth`返回错误后`open1:4643–4657`原样传播，只有ERESTART转EINTR。`__pthread_testcancel`在`kern_sig.c:908–925`可能非局部返回EINTR，不会把22变17。真实errno.h中EEXIST=17、EINVAL=22。

EEXIST在目录已存在和O_CREAT|O_EXCL遇既有节点时合理；R10warm会消化create EEXIST并进入只读验证，cold分支则故意直接拒绝。对冻结R10warm而言，互斥flag更早挡住，无法在正常路径走到warm文件create/validate。因此“warm只读open又返回EEXIST”目前没有证据，不能再添加忽略17的例外。

## 3. 晚期bootarg没有已知生命周期问题

ARM64 `arm_init.c:361–363`将输入boot_args完整复制到`const_boot_args`，随后`PE_init_platform:425`将PE_state.bootArgs指向该稳定副本。`PE_boot_args:11`每次返回该副本CommandLine，`PE_parse_boot_argn:191–196`每次解析它；没有消费token或一次性缓存。可见源码搜索没有找到晚期CommandLine清空/释放路径。

实际R10最终TOML命令行为260字节，`nova-data-warm=1`在offset244..259，只出现一次。UniXNU配置字符串为按实际长度分配；DarwinBoot CommandLine容量1024；XNU `pexpert/machine/boot.h`在__arm64__选择`arm64/boot.h`的1024字节定义，不能把旧arm32的256字节定义套用。

本轮复用真实PE parser主体、实际最终TOML字符串做ASan/UBSan小型回放：首次和重复调用均found=1/value=1。人为在255字节截断后found=0；这只是下一轮可判别情形，不是现有通路发生截断的证据。尚未验证真实手机晚期CommandLine内容与这份TOML一致。

## 4. 重入可能性需要计数证据

当前源码只有`load_init_program`一处调用meizu策略。`bsd_ast`在调用`bsdinit_task`前先置静态done=1（kern_sig.c:3473–3475），普通顺序重入会被挡住；当前日志只看到一个init done=0。没有证据支持策略被重复执行。

但原cold策略成功创建文件后被第二次调用，的确会返回17。因此下一轮可直接记录现有`meizu_load_init_count`和`meizu_bsdinit_task_count`，再加策略调用计数，而无需凭假设放宽校验。暖selector稳定为1时，重复warm调用仍首先遇flags22，并不能单独解释17。

## 5. 本轮最小语义回放

运行命令：`artifacts-nova/boot-speed-round11-nova/watchdog-runtime-nova/semantic-replay-nova`，用冻结R10源的字节相同副本和原shim，只增加上述真实XNU互斥guard，执行五个场景；ASan/UBSan通过。

| 条件 | 结果 |
|---|---|
| 原模型，warm=1，R8 fixture | errno0 |
| 增加真实flag guard，warm=1 | warm directory / errno22 |
| 真实flag guard，warm=0，既有策略 | open disabled.plist / errno17 |
| 真实flag guard，warm缺失，既有策略 | open disabled.plist / errno17 |
| cold首次成功后再次调用 | open disabled.plist / errno17 |

另运行 `bootarg-replay-nova` 确认实际parser对260字节字符串的正常和重复解析。源码、编译命令、输出和边界声明分别保存在本目录两个`*-result-nova.json`。这些是区分假设的受限实验，不执行APFS、PID1或固件，也没有为panic17建立真实设备闭环。

## 6. 下一轮可验证假设与最小观测

1. **实际执行的仍是旧策略/其他payload，或selector使R10走cold。** 预测：运行时build标记不是R11/R10期望，或warm_found/value不是1，stage是cold open disabled.plist，errno17。记录build标记、selector结果和明确branch才能区分。
2. **正确执行R10warm。** 预测：在任何内容检查前stage=warm directory/open、flags=0x20100100、errno22。此项有源码和最终payload证据；若实机仍17，需要先解释运行路径矛盾。
3. **策略重入且走cold。** 预测：policy调用次数>1，之前有成功记录或已有文件，load_init/bsdinit计数相应增加。仅一次调用可排除此方向。
4. **实际syscall/metadata状态与以上不同。** 只有build/branch确认后才继续细分；每个syscall边界记录operation/result/errno，不能仅靠coarse warm filename。没有这样的证据前不能扩大权限或白名单。

建议失败前保存一条紧凑记录：runtime build、policy call count、load_init count、warm_found/value、bounded commandline length或hash、native_warm_load_status、branch、micro-stage、errno和openflags。panic文字也带短build/branch/stage，使UFS日志失效时屏幕仍可判断。对持久日志应由父代理/日志审阅者设计一次有界强制flush；不能只调用会受1秒限频挡住的现有flush。

R10回收日志SHA256为 `4da5795493ab13556ed2d569295ac033f12702ad111559d6c63f212bf0ee5fc7`，只到systemos_prepare开头和dump=1，尚未含Data mount完成或watchdog stage。log缺少panic不能用于断定策略未执行。R9/R10 disk2显示checkpoint112/superblock17，和R8 disk2 checkpoint28不同，支持Data状态变化，但没有UUID/加载摘要，不能单凭磁盘号认定warm导入成功。
