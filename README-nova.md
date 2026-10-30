# Nova 独立工作区

本会话使用 **Nova / `-nova`** 标识。工作根目录：

`<nova-root>`

2026-10-29 从当时的真实源码复制，保存未提交改动。复制期间源文件清单未变化，副本清单一致，普通文件 inode 不共享。使用 APFS 写时复制；对 Nova 的修改不会修改源文件。复制不是对历史改动作者或镜像可用性的认定。

| 用途 | 本目录内路径 |
|---|---|
| 构建入口、公共驱动、feeds、输出 | `unixnu-main-nova/` |
| 板级配置及设备驱动 | `unixnu-meizu20pro-nova/` |
| UEFI | `uefi-sheng-nova/` |
| 实际 XNU | `unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/` |
| 日志 | `logs-nova/` |
| 备份、继承缓存 | `snapshot-nova/` |
| 新交付镜像及固件 | `artifacts-nova/` |
| 独立 DTB 与 UEFIExtract 输入 | `inputs-nova/` |
| 临时目录和编译缓存 | `.cache-nova/` |

主仓库、UEFI、XNU 已各自切换到本地 `nova-workspace` 分支。设备目录仍为原有非 Git 结构。Sol 的 `out-sol` 未复制，Astra 的独立修改未合入。

## 使用入口

本会话后续工具调用显式使用 Nova 或其子目录作为工作目录。

```sh
cd <nova-root>
./unixnu-nova status
python3 tools/audit-isolation-nova.py
```

`unixnu-nova` 固定本地 device/out/UEFI/EDK2，清理其他环境遗留的构建、Git 和 shell 注入变量，将临时缓存放在本目录，并校验显式路径参数不能越出本工作区。系统 HOME 不变，Homebrew / Xcode 等已安装工具可作为外部只读依赖。普通 CLI 使用方式保留，例如未来构建可用 `./unixnu-nova build kernel -j 8`；本次没有执行它。

打包参数必须从选定基线重新核对 trust cache、Data seed、日志和平台身份；不能因已有副本而认定其可刷。新交付文件使用 `-nova` 后缀，例如 `boot_a_setup-nova.img`、`firmware-nova.fd`。

## 已处理的隔离事项

- 重定位 270 个链接与 8,308 次文本文件路径改写，包括会被重新执行的编译 JSON、Makefile、UEFI 配置和 state。
- 迁移前内容保存在 `snapshot-nova/before-path-migration-nova/`；这些备份保留旧路径，仅作证据，不能直接执行。
- 33 处继承的 Python / 编译 / EDK2 缓存移入 `snapshot-nova/inherited-caches-nova/`，由后续构建在新路径重新生成。
- 活动树只保留 4 个 Homebrew LLVM/LLD 外链；UEFIExtract 已复制为本地文件。
- 校验 CLI 状态、旧环境变量覆盖与越界路径拒绝；这些检查不操作设备。

## 已知边界

235 个 corecrypto 生成头文件链接在原树中已经失效；本次复制没有新增失效链接。它们需要在之后的构建任务中处理，不能把隔离校验通过等同于完整构建通过。

复制过来的镜像、对象文件、旧日志及 provenance 是历史基线。部分历史报告和二进制调试信息仍可能提到原路径；不得把它们作为未经检查的可执行构建配方。原工作区、Sol、Astra 均只读。

本次未编译固件、未刷机、未重启或切换设备槽位。所有后续修改先备份并记入 `CHANGELOG-nova.md`。

机器可读记录：`copy-manifest-nova.json`、`path-migration-nova.json`、`isolation-audit-nova.json`。
