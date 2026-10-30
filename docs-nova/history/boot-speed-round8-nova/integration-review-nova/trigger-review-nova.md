# R8 capture trigger 独立复核

新文件，无旧版本；仅本目录内host测试与报告，无生产编辑、内核构建或设备动作。测试脚本初稿修正备份保存在本目录的 `snapshot-nova/`，符合父任务限定的目录写权限。

结果：**14/14 PASS，未发现阻断项。** 运行命令：`python3 artifacts-nova/boot-speed-round8-nova/integration-review-nova/test-capture-trigger-nova.py`。

测试读取真实 `kern_exec.c` 并原文提取 `meizu_setup_capture`、`nova_data_capture_poll`、完整 `meizu_autolog_thread` 三个函数；未重写被测函数。函数哈希、完整源码哈希、每例stdout/exit/status见 `trigger-test-results-nova.json`，生成的翻译单元为 `capture-trigger-extracted-nova.c`。clang使用 `-O2 -Wall -Wextra -Werror -fsanitize=address,undefined`，仅对原有未使用函数/变量警告作host适配。

覆盖默认关闭、参数0/2/负数拒绝、无Setup、90秒前最后一个tick/正好90秒、时钟回退后恢复、try-lock忙后重试、成功仅一次、失败也仅一次、后续Setup不推迟首次anchor、p_comm/真实路径/path error/generation身份过滤、释放20秒观测引用后仍能触发，以及实际autolog第一圈在dump结束后无锁调用。时间换算stub使用1 tick=5ns，避免把“秒数”直接当ticks的错误被恒等转换掩盖。

锁断言覆盖调用线程的Setup mutex、模拟日志锁与dump busy：获取/释放平衡，printf与实际capture stub均不能持Setup/log锁。真实autolog函数通过tsleep stub退出一圈，没有截短或另写循环。host没有模拟并发XNU调度或设备IO，因此不将此结果扩张为实时系统无竞争证明。

实现审阅：首次anchor写于确切Setup可执行文件成功捕获、持Setup锁区；之后只在同锁下复制，捕获前释放锁。静态attempted由唯一autolog线程使用，在调用provider前置1；锁忙、无anchor、90秒未到、时钟回退均不消耗attempt。首次anchor保留在20秒Setup observer生命周期之外；重新exec不推迟该时间。日志准确称post-Setup，不宣称Hello已绘制。

**weak_import语义边界：** 源声明保留 `__attribute__((weak_import))`，provider为NULL时运行路径返回-1并只尝试一次。Darwin默认executable链接仍拒绝完全缺失的provider；缺失运行测试明确只对 `_native_ram_block_capture_warm` 使用 `-Wl,-U` 允许未解析，再验证NULL路径。没有用宏或替换声明伪造NULL。weak_import本身不是无provider时保证内核仍可链接的承诺。R8最终封装要求capture和poll符号有定义的门禁应继续保留。

测试执行时完整 `kern_exec.c` SHA256为 `9723d2e7d8104ac9c29702db785c2787ceedf49a552743f466892828c9537bc8`。后续若仅其他函数改变，用记录的三个函数哈希核对；若触发函数变化，应再运行本测试。
