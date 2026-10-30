# lsd 栈观察耗时计量 — Nova 第四轮

2026-10-29。新测试与报告，无旧版本；生产源修改前备份由主任务保存在本轮 source-before-nova，测试脚本兼容修订前副本在 before-portability-fix-nova。

只修改 `bsd/kern/sys_ulock.c`，在既有获准 trace 中计量 backtrace_user、cookie 与 printf 的整个经过时间；不改变 stack walk、trace 格式或原 16 槽/32 trace/16 frame/每 opcode 两次预算。每槽 busy 保证单写者，SC 原子 version/count/total/max 发布；dump 在既有 gate 内有界读取 16 槽，遇见重叠发布即跳过该次、不等待。gate 在 printf 前释放，不跨用户栈读取、计时发布或输出。

`nova_lsd_bt_timing_dump()` 只供单 autolog 调用，按当前捕获 generation 聚合，分别给出 admitted/completed/pending/busy_slots。旧 generation 完成不会混入新代次；单槽快照一致，多槽快照不同时刻，输出标记 snapshot_nonatomic=1 和 elapsed_sum_not_wall=1。sum 是包含调度等影响的经过时间，不是独占 CPU 时间，也不能相加当总启动时间。

首次 lsd capture 固定一个不可重置的 300 秒报告窗口，最多 31 条变化后的汇总、间隔至少 10 秒；任务重启不重置窗口/额度。slot 永不复用。进行中 trace 不计入 completed，pending 为 admitted−completed；busy 是独立瞬时观察，完成发布到 busy 清除之间可能短暂为 1。无有效变化则不打印。每条同时保留原始 ticks 和毫秒整数，避免小样本因截断完全失去精度。

真实生产函数按原字节提取到 host harness，旧源因缺计量接口/字段呈 RED；新源在 ASan/UBSan 下 12 个用例全部 PASS：完整计时21ms、原 admission/递归/线程与全局预算、进行中真实 pthread 快照、代次切换及旧代次迟到完成、重叠 publication 拒绝、无变化抑制、10秒频率、300秒与31条上限。时间/用户栈/printf 为受控 host 替身；不代表设备调度或真实栈成本。publish_overlap 为人为构造的版本边界，其他并发用例运行真实 observer/dump。

首个 run1 仅因该 macOS ASan 不支持 detect_leaks=1 停止，未出现生产断言失败；run2 使用 detect_leaks=0，地址与未定义行为检测保留并通过。证据为 `run2-nova/results-nova.json`、各用例日志、真实提取 include 及生产补丁。未执行内核构建、未操作设备，CHANGELOG 交主任务统一更新。
