# R12 安装验收 — Nova

用户于2026-10-30明确命令“安装”，本轮授权仅覆盖已验收R12候选。dsh实际request/header已核对为deepseek-official/deepseek-flash、high，执行退出码0。

Sol复核安装及启动原始JSON：Android B预检通过，A槽从R11预期完整哈希更新为R12 c8addd613a564fc2…；整分区回读匹配。B槽7df205f1…保持；启动前再次核对A/B、GPT及暖输入，Fastboot确认A槽并成功发出reboot。7项执行证据重新hash全部匹配。manifest原件已备份，变化仅为5个授权门字段；安装器/生产源码未改。

状态为INSTALL_AND_START_ACCEPTED_AWAIT_RUNTIME_RESULT，仅证明安装与发出启动命令。尚无Hello/失败画面反馈、无启动耗时或e22修复结论。R12用于采集probe12目录实际mode/uid/gid；保留R11策略及panic文字。当前设备已选择A进行测试，不能沿用此前Android B状态。后续候选仍须用户明确安装命令。

本验收文件新建，无旧版本。
