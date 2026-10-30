# R13 owner 兼容实现与验证

仅改 `BootWatchdogPolicy.c` 与 `WatchdogHostTests.c`；R12 两文档已纠错。未碰设备、未刷写/切槽/重启或全核构建。

## 实现
CHECK_DIRECTORY 在 S_ISDIR/0755 及 501:20、0:0、新建0:20 外，仅增 `created_directory==0 && uid==99 && gid==99`。通过后打印一次 R13 compat 标记，不早于类型/权限检查，不改 op，不 normalize 已有 inode。

## 验证
host 命令取自 R12 仅换输出路径；target 同 argv/cwd 单文件编译仅换 `-o`。host ASan/UBSan PASS 798 cases，rc=0；target rc=0。覆盖 99:99 两文件校验成功、坏内容失败且零 existing_mutations、99:0/0:99/98:99/99:98、新建99:99、0777/0700/setuid 等、冷分支与 501:20 无标记、清理与 FD 关闭。

## 边界
只修正实测目录属主拒绝，不证明后续文件/Hello/提速。99 的 APFS 源 inode 未证明，hdiutil owner-uid=501 可能仅附加设备所有者。安装等新命令。
