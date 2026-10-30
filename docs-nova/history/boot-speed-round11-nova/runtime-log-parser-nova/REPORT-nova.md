# R11 离线诊断解析器 — Nova

全部为本目录新产物，无旧版本。未修改生产、计划、设备或其他代理文件。入口 `parse-r11-nova.py` 仅用Python标准库读取一个日志并输出JSON；支持导出的文本和以MEIZULOG开始的原始窗口。

解析内容：R11 pre-data版本/暖selector、policy入口/两个参数解析/结果、失败branch/op/stage/errno、panic返回的保存状态、NativeUfs实际rootio/warmsource输出，以及RamBlock实际warmload计数和digest摘要。固定capture_id、物理读取字节231153664、payload/logical SHA来自本轮WarmLoadContract和Reader合同。

可直接运行（替换LOG为本轮实际日志路径）：

```sh
python3 <nova-root>/artifacts-nova/boot-speed-round11-nova/runtime-log-parser-nova/parse-r11-nova.py LOG --output RESULT.json --require-complete
```

默认退出0只表示解析完成；加`--require-complete`后，只有完整暖policy证据才退出0，证据不足或policy失败退出3，输入格式/读写错误退出2。JSON状态分别为`warm_policy_pass_observed`、`policy_failed_observed`、`insufficient_evidence`；不能把脚本执行成功当成手机成功启动。

解析器只使用最后一个boot/pre-data段以及最后一次policy调用和最后一个暖摘要样本，不用更早段或更早完整样本补全缺项。原始MEIZULOG严格限制在4096+hdr.bytes范围内，后面的旧尾部即使包含完整成功记录也忽略；header声明长度超过实际文件就报错。摘要不能跨rootio样本边界拼接，也不能用pre-data之前的摘要补全当前段。

`save=attempting`仅表示调用前标记。只有实际出现格式正确且不与失败记录矛盾的`nova-r11 policy fail ... save=1`时，才输出`sync_success_reported=true`，并明确这是post-call panic的运行时报告。日志里只有attempting时，返回码为null、sync报告为false；busy/写入失败/sync失败等保留其真实编号和含义。

14个小型fixture检查通过；CLI合成正例退出0，真实R10负例退出3。覆盖完整暖policy、阶段含空格、attempting误报防护、明确sync成功/失败、旧段隔离、原始窗口旧尾部、截断header、digest错配、摘要拆分、缺结果/阶段、矛盾panic及真实R10日志。正例均为合成数据，不是设备成功记录。实际R10输入SHA为`4da5795493ab13556ed2d569295ac033f12702ad111559d6c63f212bf0ee5fc7`，结果明确证据不足。

关键产物：`test-result-nova.json`、`fixture-tests-nova.log`、`r10-negative-result-nova.json`、`synthetic-positive-result-nova.json`、`fixture-manifest-nova.json`、`source-provenance-nova.json`。

限制：标记是运行时声明，不能独立证明固件/内核的密码学身份或存储耐久性。文本输入本身需要回收工具保证边界和来源；解析器不能从任意拼接文本推断真实物理启动会话。暖policy和digest吻合也不代表Hello、用户缓存有效或启动提速，`hello_or_boot_speed_proven`始终为false。没有加载旧日志、没有补证、没有联网或设备调用。
