# R10策略真实快照输入

新文件，无旧版。来源为R8已捕获512MiB Data镜像，SHA256 `372549a9fcb74ba062fbd871a370199c7d32657cfa4e436040c120b6dbfcbd43`。只读附加、按UUID核对、只读挂载并显式owners复核后提取；已按实际backing path卸载并验证镜像哈希不变。没有访问手机。

`disabled.plist` 实际356字节、uid/gid0/0、0644、单硬链接。四键为watchdogd=true、dhcp6d=true、bootpd=true、ftp-proxy-embedded=false，原字节保存在disabled.plist-nova.bin。不得用222字节原模板覆盖，不能仅因与模板不同认定损坏。

`disabled.migrated` 为空普通文件，uid0/gid20、0644、单硬链接。父目录为uid501/gid20、0755。显式owners只读重挂后结果相同，证据见owners-stat-nova.json。

这是本轮固定已验证快照的兼容合同，不是任意Data策略均可信或可复用的证明。仅暖加载实验启用已存在文件分支，必须继续保留原volatile Data检查，拒绝非法内容/类型/链接/权限及实际I/O错误。
