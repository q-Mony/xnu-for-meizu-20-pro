# 第六轮 CPU 探针独立构建与严格封装

新增文件，无旧版本。由 `cpu_probe_build_package` 子代理维护；中央变更日志由主代理汇总。
本目录所有自检均未接触设备。`DO_NOT_FLASH-*` 目录包含第五轮旧核，只用于验证封装流程，禁止作为第六轮候选刷入。

## 已完成验证

- 真正的 ARM64 iOS 17.0 对象构建通过（来自项目现有 XNU 编译记录）。对象无 `__init_entry_set` / `__mod_init_func` 自动入口，仅导出 `nova_cpufreq_probe_dump_once` / `nova_cpufreq_get_snapshot`。
- 外部依赖严格等于 `_PAGE_SHIFT_CONST`、`_PE_boot_args`、`_io_map`、`_ml_get_interrupts_enabled`、`_ml_io_unmap`、`_printf` 六项；没有 `memcpy` / `bzero` 依赖。
- 真实 TOML 转换函数的 13 个测试方法通过，含多个子案例：重复或裸参数、不同参数值、入口身份、额外入口/字段、畸形 TOML、缺失/重复 args、转义/多行/非 ASCII、1023 字节边界、重复执行、相似参数和 CRLF 保留。
- 5 个 CLI 拒绝案例通过，且拒绝后没有创建输出：正式模式旧内核、越界输出、重复输出、未标记的自检、已打入口补丁的内核。
- 使用第五轮原始内核完成一次全镜像封装自检，PAN、Android v0 SHA1、固定 96 MiB 布局、独立提取的完整 EFI 集合均通过。固件、DTB、ADT、trustcache、Data512、继承的 AVB 尾保持字节一致；TOML 只追加 ` nova-cpufreq-probe=1`。

## 文件与报告

- `../../../unixnu-meizu20pro-nova/scripts/build-native-meizu-cpufreq-probe.py` 是驱动构建器（相对本文实际路径请以工作区根为基准，命令如下）。
- `package-cpufreq-candidate-nova.py` 是严格封装变体；通过固定哈希加载旧封装器的验证函数，不修改或调用旧 `main()`，输出限定在本目录的全新子目录。
- `test-cpufreq-packaging-nova.py` 直接测试真实转换函数；`config-tests-nova.json` 保存结果。
- `cli-rejection-tests-nova.json` 保存 CLI 拒绝验证。
- `isolated-probe-build-exact-deps-nova/build-report.json` 是最终对象构建报告。
- `DO_NOT_FLASH-packaging-selftest-nova/evidence-nova/verification-nova.json` 是第五轮内核封装自检，`do_not_flash=true`。

## 主代理使用

在 Nova 工作区根执行对象隔离验证（输出目录选一个新名称）：

```sh
python3 unixnu-meizu20pro-nova/scripts/build-native-meizu-cpufreq-probe.py \
  --tree unixnu-main-nova/out/meizu20pro/tree \
  --source unixnu-meizu20pro-nova/drivers/NativeMeizuCpuFreqProbe/CpuFreqReadiness.c \
  --out artifacts-nova/boot-speed-round6-nova/packaging-nova/isolated-probe-build-new-nova
```

第六轮完整内核准备并审计后，正式封装：

```sh
python3 artifacts-nova/boot-speed-round6-nova/packaging-nova/package-cpufreq-candidate-nova.py \
  --kernel unixnu-main-nova/out/meizu20pro/kernel/kernel.macho \
  --output artifacts-nova/boot-speed-round6-nova/packaging-nova/cpufreq-candidate-nova
```

输出包含 `boot-cpufreq-candidate-nova.img`、`kernel-payload-nova.macho`、`final-extracted-nova/` 和 `evidence-nova/verification-nova.json`。报告记录原始内核、payload 内核、最终 boot 的 SHA256 及提取树路径。
正式模式要求内核包含两个探针导出。该检查不代替主代理的全内核来源、链接调用点和设备测量审计。
继承 AVB 尾不等于重新生成签名。封装器不调用普通 payload builder，不引入板级其它参数。

备份：`snapshot-nova/boot-speed-round6-nova/cpu-build-package-before-hardening-nova/` 与 `cpu-build-package-before-exact-deps-nova/` 保存本轮新增文件后续加固前的版本。
