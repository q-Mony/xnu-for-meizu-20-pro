# R15 本地验收

状态：诊断候选已通过本地验收，等待用户新的安装命令。未访问或操作设备，未刷入、切槽或重启。

R14 已能进入 Hello。日志里的 launchd→SpringBoard 为 21.094823698 秒，SpringBoard→首次 Setup armed 为 46.176911094 秒，合计 67.271734792 秒。它们不包含 Boot 菜单到 launchd 的全程，也不能把 Setup armed 当成 Hello 首帧。

早期约 19.796 秒窗口中，23 段签名准备占 19.326 秒，其中读取约 10.900 秒、哈希约 8.403 秒。进入 SpringBoard 后，seq200/send_id6001009 的 WAIT 阶段采样年龄达到 27.246 秒，完成记录被共享日志环丢失；这不是测得的最终 RPC 总时长。

静态缓存证据将 ID6001009 对应到 BackBoardServices 的 `_BKSDisplayGetMainScreenInfo` 请求模板。backboardd 注册 `com.apple.backboard.display.services`。这不能单独证明实际 seq200 的调用栈或等待原因；三个 FOREVER 截止时间的 group wait 也仍是待验证候选，不能凭静态扫描排除有限时间的依赖等待。

R15 在独立保留槽中保存选定请求的 begin/wait/finish、返回码、回复 ID 和有限调用栈，附带时钟有效性、稳定记录身份与有上限的汇总。审查发现的接收 ID、记录关联、短请求输出、汇总窗口、时钟处理、热路径转换和共享初始化并发问题已修正。生产改动仅限 mach_msg.c。

验收核对了实际生产核心的 ASan/UBSan 主机测试记录、ARM64 单文件编译与完整增量构建；独立核验了 6,186 项源码清单、受保护对象、源码/对象/内核/镜像哈希、R14 镜像封装、内核外 EFI 内容、Android 校验和及 PAN 结果。实际 dsh 请求配置均为 deepseek-official/deepseek-flash/high。测试是本地主机与静态证据，尚无 R15 硬件运行结果。

唯一接受的候选：`arm-lock-review-nova/package/sblong-r15-arm-lock-final-nova/boot-warm-sblong-r15-candidate-nova.img`，SHA-256 `53470de0d676da3e41cb9c6a894b62b10686e0ef0480799535db0d1e331fe0ed`。前两版 R15 已被替代。详细绑定见同目录 `SOL-LOCAL-ACCEPTANCE-nova.json`；后续安装应以该记录和重新核对的设备状态为准。

该版用于补齐运行证据，没有直接提速承诺。下次经用户命令安装后，正常启动至 Hello 即可，不需要逐段手工计时。
