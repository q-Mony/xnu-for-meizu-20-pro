# 启动优化第二轮基线 — Nova

2026-10-29。新文件，无旧版本。仅冻结本地证据；未改生产源码、计划或 CHANGELOG，未构建、未执行上游脚本。此子代理未操作设备。

本地候选、旧 A 回退备份、raw kernel、板级配置、所选配置和 payload 均已重新核对。主代理另完成当轮 Android B 与 boot_a/boot_b 只读核对。完整机器可读记录见 [baseline-nova.json](<nova-root>/artifacts-nova/boot-speed-round2-nova/baseline-nova/baseline-nova.json)。

| 项目 | 本轮结果 |
| --- | --- |
| 已到 Hello 候选 | 96 MiB，SHA256 `2336c87cc0c71f810d3967c8601124075e381f8efebd67834b3526124a3e1dab`；与既有打包/成功运行记录及本轮设备 A 槽一致 |
| 旧 A 回退备份 | 96 MiB，SHA256 `a85498e89528303d47f55dc1b57dc786a2e9fd719837fdea3f55442e48a25c07`；与原备份前/后及本地三方记录一致 |
| Raw prelink | SHA256 `142e3a63fb4314ac53826333ed0abe22c95f8c1fed83eb11989ba932a5ecb10a`；独立 APFS 副本已保存 |
| Payload kernel | SHA256 `6e2f63e577e2fa32f5beaf3c380b03b65ad0db15fb36bebd8d91c07760e0fe30`；入口补丁后的版本，与打包记录一致 |
| board / selected .config | 当前文件与优化前备份哈希相同；已保存独立副本 |
| 其他 payload 文件 | ADT、trustcache、boot config、Data seed 哈希均与固定打包记录一致；Data seed 逻辑大小 512 MiB |
| 本地 root 镜像 | 8,380,219,392 字节，整镜像及两个窗口均重新流式哈希；两个窗口均与当前 VerifiedRoot policy 一致 |
| Policy 与运行镜像绑定 | OS/SystemOS 两个完整 manifest 字节在已验收 payload kernel 中各出现一次 |
| 既有优化源码 | 6 个源文件与首轮构建前固定哈希全部相同 |

## 必须保留的打包边界

已成功的 payload bootargs 与当前 board.toml **并非相同**。board 多出 `sheng-smp=1`、`armcpu-spi-ipi=1`、`armcpu-diagnostics=1`；成功 payload 则额外包含 `sheng-log=1`、`meizu-aks-notify=1`。下一轮应继续固定 envelope 只替换内核，除非明确将启动参数变化作为独立实验。当前 board/.config 未被他人修改，不代表重新从 board 生成的整包等同于已验收包。

固定打包入口（本轮仅阅读、未执行）：[package-candidate-nova.py](<nova-root>/artifacts-nova/boot-optimization-nova/packaging-nova/package-candidate-nova.py)。它使用 Nova 的冻结输入，保留 firmware/DTB、ADT、bootargs、trustcache、Data seed 和继承 AVB 尾部；当前验证记录不声称新 AVB 签名。

## 发现的旧元数据

`userdata-root.json` 保留了修改前的整镜像和 SystemOS 哈希。本轮实际重算：

- 整镜像：`c0f1c722e9134c397811b1b6e643e1001a546955ca539a3a0ff7ff4e87b771f4`。
- OS：`6e87bfcaa8eca952e00b719f333d1f1060715a63566112267ad8730e1ed98906`，与旧 JSON/现行 policy 一致。
- SystemOS：`e27031112ade44d12d1ddbc22a9468e023c736e46c166917a4dd93aead90aa1a`，与现行 policy 以及已验收 payload kernel 中嵌入的 policy 一致；旧 JSON 的 `9cd738…` 属于旧输入。

因此这是旧 JSON 过期，不是当前本地 root 与已验收策略失配。旧 JSON 还含写向 userdata 的过时命令；本轮未执行、未修改该 JSON。设备根盘位于专用 unixnu 分区，后续不能把旧命令当作刷写配方。

## 系统与设备证据

配置目标为 iPhone10,3 / iOS16.7.16 / 20H392；实际运行日志第3行为 Darwin23.6.0。原 Apple `kernelcache.decompressed` 本轮 SHA256 为 `2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221`，路径与首轮 build log 的 prelink 输入一致。路径匹配不补造构建当时未记录的哈希证明。

主代理当轮证据：[Android B 状态](<nova-root>/logs-nova/boot-speed-round2-nova/initial-device-state-unrestricted-nova.json)、[boot 分区哈希](<nova-root>/logs-nova/boot-speed-round2-nova/initial-boot-partition-hashes-nova.json)。状态核对时间为 2026-10-29 15:54:17、boot 分区哈希时间为 15:55:13（Asia/Shanghai）。A 为已验收候选；B 哈希保持 `7df205f1…1745a2`。本轮尚未完整读取设备 unixnu 根分区。

既有用户 Boot→Hello 记录为 237 秒和 190 秒，同镜像同起点，差值来源仍未知。该历史测量不作为本轮候选成绩。

## 冻结副本与下一步

`frozen-inputs-nova/` 保存 board、selected config 和 raw kernel 的独立 APFS 副本；三者均经复制后哈希验证、inode 不同。候选、旧 A 备份及固定 payload 继续使用既有不可混淆的 Nova 历史路径；本地 root 本轮已哈希但没有额外复制 8.38 GB 输入。

后续只需保证本轮新改动与上述基线分别留存，继续按单变量生成候选。若 root 或 firmware 输入改变，应另立基线，不能复用本报告作为未变化证明。
