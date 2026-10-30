# R11 构建、封包和实际机器码交付

状态：全部约定构建/封包/静态审计通过，交主代理执行最终 READY 闸门。本代理未操作设备，未声称启动成功或提速。

- 以明确批准的 5 个 SHA 冻结源文件。6186 项源输入构建前后逐项一致，交付收尾时仍与冻结一致；仅 Policy.c、实际 kern_exec.c、对应 board patch 和两个 host 测试相对 R10 改变。
- 原 Nova 完整内核构建通过，耗时 49.617 秒。原 builder 的 checked patch reverse-check 保持通过，没有跳过；保存 17 项构建对象/编译记录。
- 7 个 policy 函数与新 caller/mode/force 函数完成正式对象→raw 比较；原 RamBlock/UFS 对象与 R10 逐字节相同，保留 warm 路径绑定。所有选定 raw→payload 绑定通过。load_init 的继承 checkpoint 只覆盖入口 4 字节，模板、回放指令、日志目标和重新进入 caller+4 分别验证。
- 额外 53 项实际指令检查与 4 条文本地址绑定通过：三处 open flags 为 0x20100000、0x20000a01、0x20000004，均保留 O_NOFOLLOW_ANY 且无非法组合。normal/periodic mode=0，失败 force-once mode=1；日志 getter→printf→stats→save→新 panic 顺序确认；force 64 KiB 快照、busy guard、仅 force 且写入成功才 sync 已核对。64 KiB 是复制上限，不是 UFS 等待时限。
- 仅替换 R10 内核；TOML、Data512 seed、trust cache、ADT、固件、DTB 与继承封套保持原字节。入口补丁只应用一次，PAN 和最终 FAT 提取校验通过。
- 独立终审输入已绑定，33 个实际 logger/caller 测试与 119 个 policy delta 检查通过；742 项 policy 实现验证由实现代理产物记录。

| 产物 | SHA256 |
|---|---|
| raw kernel | `2bc94fee376610b00757b389d92b5f05d6c740dc5d296131e6d1a2f101165785` |
| payload kernel | `412b57d7589be1ca99b017e2048ad444cad50650bd377fb3f0e024fb768c47e8` |
| 96 MiB boot candidate | `67ce00ec7ef090c3ef9d544f570e25cf3a06172e729bfb6db76e3772c4417707` |

正式候选：`<nova-root>/artifacts-nova/boot-speed-round11-nova/packaging-nova/watchdog-r11-candidate-final-nova/boot-warm-watchdog-r11-candidate-nova.img`

完整证据与逐文件哈希见 `final-result-nova.json`；保留原继承 AVB metadata，未生成新的认证 AVB 签名。源代码语义终审、静态绑定和封包通过不替代实机验证。
