# R12 本地诊断候选 — Nova

新增目录实际mode、UID/GID、mkdir原始结果及文件元数据拒绝字段，使用policy-metadata probe12识别。保留R11策略条件和core；本轮不声称已解决op9或启动过慢。

747实际函数ASan/UBSan、ARM64目标编译、6项安装控制流host测试通过。6186源稳定，仅Policy和HostTests两项变化；kern_exec、BootLog、RamBlock、Ufs对象与R11相同，重建archive的成员逐字节相同。43个raw/payload函数、105项指令检查及6个文字地址绑定通过；非kernel EFI、固件、启动参数和固定暖快照输入保持。首次.a封套严格比较误拒已留证，改为实际成员比较后重新构建及审计通过。

正式输入、镜像与审计hash见delivery-manifest-nova.json；实际安装门验证见install-gate-verification-nova.json。状态为BUILT_AWAIT_USER_INSTALL_COMMAND。用户明确命令前不安装、不切槽、不重启，本轮无设备操作。所有子智能体显式使用GPT-6.1-Sol/high。
