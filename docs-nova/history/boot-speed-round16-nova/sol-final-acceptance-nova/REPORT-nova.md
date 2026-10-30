# R16 本地验收记录（新文件，无旧版）

R16 诊断候选已通过本地验收，尚未安装。原开机耗时与 SpringBoard 崩溃的根因仍待运行数据验证，不能称为已修复或已提速。

R15 日志已确认主屏幕信息查询耗时 36.3342803646 秒；SpringBoard 在 libate 的 ASTC DecodeRow 中执行 ldr q0,[x28] 时发生无效读取并退出，随后被重新拉起。R8 也记录到相同未滑动故障 PC，表明该故障早于 R15。

R16 增加同一显示请求的接收、copyout、回复及返回时间，并为 SpringBoard 故障保存全部通用寄存器和有界调用栈。Sol 审查发现的诊断事件串线、限额前读取、并发计数和寄存器多次打印问题均由 dsh 修正。采集受限额约束，缺失事件仍标为未知。

独立核对 6186 个基线源码条目，无缺失，只有 sleh.c 和 mach_msg.c 变化。生产 helper 的 ASan/UBSan 测试、解析器检查、两项真实 ARM64 编译和完整内核构建通过；两个独立 TU 对象与完整构建字节一致。独立验证最终镜像 SHA、Android v0 SHA1、布局及与 R14/R15 的外壳和非内核 EFI 字节关系。包内 entry patch、PAN 检查通过。

镜像 SHA256：51a0248347f0e70644a28d30f932ce59245ada3920ade777ac5835578e30bf51。

完整证据索引见 SOL-LOCAL-ACCEPTANCE-nova.json。5 个 dsh 会话的实际 request/header 均核对为 deepseek-official/deepseek-flash/high。文档中错误的 FAR 去滑动及未经验证的 BL 被调用函数名已勘误。

下一步需用户新的安装命令后运行 R16、读取自动保存的日志。无需逐段人工计时。先根据服务接收/回复耗时和 ASTC 缓冲区现场选择实际修复，再验证等待缩短且 SpringBoard 不再发生同类崩溃。

本轮未访问设备。installation_allowed=false、install_authorized=false、do_not_flash=true、hardware_tested=false、diagnostic_only=true。历史中间版本缺少精确修改前快照的限制已披露，未声称可完整恢复该中间版本。
