# R12 Nova 本地构建与静态审计

状态：`BUILT_AWAIT_USER_INSTALL_COMMAND`。未操作设备，未安装。

生产基线固定为 R11 静态 final-result（SHA `790ad42ccb8250af85f2369924e1220c28fe3f908318ce24b79ebbfcc85ace37`）及 R11 6,186 项源码清单，不以 R11 硬件成功或 READY 为条件。R11 实机目录校验失败仍是既有事实；R12 增加观测，未主张已修复根因或加速。

冻结后仅 Policy.c / 现有 WatchdogHostTests.c 与 R11 不同，构建前后和交付时源码清单完全一致。kern_exec.o、BootLog.o、NativeRamBlock.o、NativeUfs.o、ufs-original-bindings.o 与 R11 完全一致；三个静态库成员清单及所有成员字节一致，归档封套的时间戳允许重建。首次过严归档整文件校验失败记录与脚本修正前备份均保留；修正后只重建一次。

最终链接审计核对7个Policy函数、3个调用端函数、33个warm/UFS函数的对象到raw绑定；仅排除逐项列出的重定位字，未豁免整个Policy函数。43个函数raw到payload一致。load_init_program继承入口钩子逐项校验R11模板、重新定位BL、原指令重放、回到caller+4以及真实policy调用。

105项最终指令检查及6项字面量绑定通过，其中复用并重新绑定36项R11调用端指令证据。probe12检查包括mkdir错误保留、created来源、目录mode/uid/gid/nlink/dev/ino字段读取和printf栈参数顺序，两个文件kind选择，文件size/nlink参数，printf目标与格式地址，原目录谓词、errno和三组open flags。

747例实际函数ASan/UBSan与零警告ARM64目标编译使用已冻结实现方证据；主代理独立剥离五处新增观测块精确恢复R11 Policy。未重复扩大测试。

仅替换R11 boot封套中的内核。TOML、全部非内核EFI、固件、头部、DTB和AVB封套字节保持；PAN和符号约束通过。沿用既有AVB封套，未生成新的AVB签名。最终结果JSON包含全部路径、SHA和证据绑定。
