# 第五轮状态刷新限频：真实生产函数 host 验证

2026-10-30（上海时间）完成。**19 个场景全部通过；4 个负对照全部按预期拒绝。** 本报告与测试程序均为新文件，无旧版。此目录的 README-nova.md 保留最初准备阶段记录，完成状态以本报告和 results-nova.json 为准。未修改生产源码、镜像、设备或其他会话文件。

受测 BootLog.c SHA256：`e5d936a54810e1d0ae4aa605ad48a7416a789d7f6f65a919607eea7a5061973f`。

受查 kern_exec.c SHA256：`27f8cdfea3e2d6a42ab5f2bcfb04d17baa48da7f0b39ec0ff0d6207595a8afdf`。

结果目录：[run-20261029T163645236617Z-nova](<nova-root>/artifacts-nova/boot-speed-round5-nova/status-refresh-tests-nova/run-20261029T163645236617Z-nova)。机器可读汇总：[results-nova.json](<nova-root>/artifacts-nova/boot-speed-round5-nova/status-refresh-tests-nova/run-20261029T163645236617Z-nova/results-nova.json)。运行前后校验生产源字节不变；完整函数提取、替换清单、编译日志、逐场景日志及负对照失败日志均保留在该目录。

## 测试方式

运行脚本从生产源码直接抽取状态声明及 8 个函数：共享 gate 的获取/释放、periodic 状态更新、timing dump、IRQ、FIQ、IRQ 计数读取、换行滚屏。IRQ 内 3 个寄存器读取改为受控现场值，其他抽取函数体不改写。硬件时钟、身份校验、绘制、cache clean 和 printf 通过明确替身控制；测试里没有另写一套节流器。

使用系统 clang、C11、`-Wall -Wextra -Werror`（仅关闭抽取的未用全局变量告警）、ASan 与 UBSan。每个场景启动全新进程，报告函数局部静态状态自然重置；每次进程运行设置 30 秒超时。LeakSanitizer 关闭，未运行 ThreadSanitizer。

## 已通过的验收

| 验收 | 结果 |
|---|---|
| 第一次立即绘制，后续从前次完成时间起满 1 秒才绘制 | PASS；7 tick 首次绘制结束后，在 1006 跳过、1007 允许 |
| 只测绘制区间，不把准入及前置逻辑算入绘制耗时 | PASS；准入 100、绘制前 123、结束 134，只累计 11 tick |
| 累计绘制次数、总耗时与最大耗时 | PASS；7 与 11 tick 两次绘制得到次数 2、总耗时 18、最大 11 |
| uint64 真实回绕、零频率、超 u32 频率、频率变化、时间逆向 | PASS；有效回绕保留，异常按最终契约跳过/重定位 |
| 绘制过程中计时逆向 | PASS；记录错误，不累计超大耗时，后续可恢复 |
| inactive、busy、身份失败不消耗绘制预算 | PASS；解除条件后能按原预算首次/到期绘制 |
| IRQ/FIQ 共享预算及原有采样 | PASS；IRQ 第 512 次绘制，紧随 FIQ 第 64 次被同一预算限制，后续满 1 秒可绘制；EL0/EL1、init/non-init、CPU 范围、busy/inactive 计数均验证 |
| 原 IRQ/FIQ 采样和 512/64 入口过滤 | PASS；与修改前基线前缀逐字节相同 |
| 递归及双线程 gate 竞争 | PASS；竞争者立即返回，不重复绘制、不清除持有者 gate；dump 竞争后重试未消耗报告序号 |
| 报告字段、10 秒边界、gate 释放后输出 | PASS；捕获 printf 验证未在中断上下文、打印时 gate 已释放 |
| 不规则 final：0、295、300、305 秒 | PASS；300 秒不输出，305 秒 final，后续不再输出 |
| 相对上次报告逆向：0、10、9、20 秒 | PASS；9 秒终止报告，20 秒仍不输出，预算不重置 |
| 0–300 秒每 10 秒报告、上限 31 条 | PASS；第 31 条 final，此后保持结束 |
| 报告时钟回绕、频率变化、相对起点倒退、非法频率 | PASS |
| 真实换行函数滚屏统计和 framebuffer 内容 | PASS；普通换行不累计，跨尾行实际移动并清尾时累计一次，页外前缀保留 |
| 单弱 CAS、无软件自旋、周期/IRQ/FIQ 函数无 printf | PASS；counter 汇编含 memory clobber，kern_exec 中只有一个 dump 调用 |

## 负对照

仅修改本次运行目录内的提取副本，生产源码保持不变：

1. 删除真实 blog_periodic_status：编译失败，证明没有测试内替代状态机兜底。
2. 将 last_end 改成绘制起点：边界场景断言失败。
3. 恢复 final 绕过 10 秒门限：295→300 秒输出被断言检出。
4. 删除相对 reported_at 的逆向检查：10→9 秒输出被断言检出。

## 结论边界与复现

上述结果支持本次限频/统计实现符合约定，未发现剩余生产逻辑阻断。它们不证明真机 Boot→Hello 已加速，也不测量真实 framebuffer、cache clean 或中断运行成本；真机时长及 status timing dump 仍需后续刷入读取。已有 console volatile busy 的跨核同步问题不在本次修复/验证结论内，报告假定生产约定的单一 autolog 调用者。

在 Nova 根目录运行：

```sh
python3 -B artifacts-nova/boot-speed-round5-nova/status-refresh-tests-nova/run-nova.py
```

每次生成独立 run 目录。父任务负责将这些新交付物登记到中央 CHANGELOG-nova.md。
