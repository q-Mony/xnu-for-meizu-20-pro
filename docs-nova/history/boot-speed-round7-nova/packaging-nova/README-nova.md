# 第七轮计时构建与封装

本目录及上层 `run-build-nova.py` 均为新文件，无旧版本。第六轮脚本、镜像和报告只读。

本子任务只修改生产文件 `bsd/kern/bsd_init.c`：给 `setconf()` 添加 `root_device_resolve` 起止记录；给每一次 `vfs_mountroot()` 添加 `root_mount` 起止记录并保留返回错误码。每次调用的标识由 console 提供的 `nova_boot_phase_begin/end` 生成、配对。局部标识每次使用前重新赋值，不改变失败重试分支。原件位于 `snapshot-nova/boot-speed-round7-nova/root-before-nova/`；补丁见 `root-mount-timing-nova.patch`。

构建器沿用第六轮的固定构建命令，增加此次 console 两个头文件、serial、bsd_init、kern_cs、kern_exit、sleh 的源码哈希；继续覆盖 BootLog、kern_exec 及第六轮原有输入，共 19 项。运行前后分别取哈希，任何漂移导致非零退出。输入预检不会冻结其他工作者仍在编辑的源码。

所有命令均从 `<nova-root>` 执行。

只读预检：

```sh
python3 artifacts-nova/boot-speed-round7-nova/run-build-nova.py --check-inputs-only
python3 artifacts-nova/boot-speed-round7-nova/packaging-nova/package-timestamp-candidate-nova.py --check-inputs-only
```

主代理完成 source freeze 后才启动完整构建：

```sh
python3 artifacts-nova/boot-speed-round7-nova/run-build-nova.py
```

完整构建及链接审计通过后封装；输出目录必须不存在：

```sh
python3 artifacts-nova/boot-speed-round7-nova/packaging-nova/package-timestamp-candidate-nova.py \
  --kernel unixnu-main-nova/out/meizu20pro/kernel/kernel.macho \
  --output artifacts-nova/boot-speed-round7-nova/packaging-nova/timestamp-candidate-nova
```

封装器按固定 SHA256 加载第六轮已验证封装器，保留其 main 的全部行为，只将可写输出根设为本轮目录。第六轮输入、固定入口补丁、PAN 检查、Android 镜像校验、提取文件集合验证均复用。第七轮额外要求两个 phase 计时导出，随后与第六轮正式产物逐字节比较全部非 kernel EFI 文件及固件/DTB/header/AVB 区间；启动配置与第六轮完全相同，保留原 `nova-cpufreq-probe=1`，不新增启动参数。

最终文件为 `boot-timestamp-candidate-nova.img`，正式结果在 `evidence-nova/verification-nova.json`。被复用流程的原报告单独保存在 `reused-r6-verification-nova.json`；只有外层第七轮报告成功才表示全部封装检查完成。任意后续检查失败会留下 `DO_NOT_FLASH-round7-packaging-incomplete-nova.txt`。

本次已通过真实 XNU 编译参数的 `bsd_init.c -fsyntax-only`（`XNU_BOOT_PROGRESS=0`）、两个 Python 文件 AST 检查，以及 8 项 CLI/输入验证，见 `validation-nova/`。拒绝第六轮旧内核、越界输出、已存在输出和混合只读模式参数时均未创建候选输出。本次未启动完整内核构建、未执行镜像封装、未操作设备；函数导出检查不能替代后续链接调用点审计或真机验证。

建议由主代理统一追加中央变更记录：

> 2026-10-30：第七轮新增根设备发现与每次根盘挂载起止计时，生产仅修改 bsd_init.c，原件已备份 root-before-nova。新增 19 项源码哈希构建器及固定复用第六轮流程的严格封装器（新文件无旧版）；封装增加第六轮全部非内核文件与启动配置逐字节一致检查。真实编译参数语法检查及 8 项 CLI/输入检查通过，未开启 XNU_BOOT_PROGRESS，尚未完整构建、封装或操作设备。
