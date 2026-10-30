# R13 本地兼容修改验收 — Nova

状态：源码与本地验证通过，尚未构建完整内核、打包或安装。新文件，无旧版。

R12原始日志242行确认probe12、目录040755、99:99、mkdir EEXIST/created0，随后243行op9/errno22。暖源digest与Data准备通过。type/mode匹配，唯一当前拒绝项为目录属主元组。原始32MiB窗口与文本hash重新核对。返回脚本确实做过A→B选择与重启；执行报告末句“未切槽”不准确，以return/post-failure原始记录为准。设备Android B、A仍是R12。APFS源inode及99映射具体来源仍未证明，不能把hdiutil owner-uid501当目录inode证据。

Sol逐段审查实际生产diff：只在既有暖RAM Data预检之后的目录条件添加!created && uid99 && gid99，并在type/mode检查通过后输出version13标记。原501:20、0:0、本次新建0:20逻辑保留；文件uid/gid、类型、模式、单链接、同设备、内容精确比较、EOF、前后身份、NOFOLLOW/O_EXCL以及不修改已有inode的规则均未变。不修改seed/import或挂载行为。99仅为此受限实验路径的实测兼容，不是为任意属主放行或证明挂载正确。

798个实际函数ASan/UBSan案例通过；新51案例覆盖匿名既存目录、坏内容、混合属主、新建99目录、模式拒绝、标记范围与逐原语故障/FD关闭。编译与测试rc0，目标ARM64 iOS17单对象rc0且零输出。host/target命令与R12逐参数比较，仅-o变化。15项交付物hash/size与4项修改前备份、修改后源hash核对；对R12清单29项来源比较只Policy和HostTests变化。两次实际dsh request/header均DeepSeek Flash/high。无依据重复运行全部测试。

边界：本地用例后续文件是构造的严格合法属性；不能据此断言实机后续文件通过或Hello，更不能声称Boot→Hello提速。测试中旧“real R8 uid501”注释按历史host-view fixture理解，不是target实测。后续必须先完成完整构建/封包/链接检查，再等新的用户明确安装命令；R12安装授权不自动覆盖R13。
