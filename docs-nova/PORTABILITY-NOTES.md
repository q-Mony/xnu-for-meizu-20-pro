# 可移植性与继承的本地路径

本仓库没有绝对文件系统符号链接、嵌套 Git 仓库或 gitlink；源码快照与主/板级
字节和 R18 冻结一致。少数从 Nova 工作树继承的**源码文本**仍包含绝对路径默认值，
它们不是符号链接，也不会被导出脚本自动改写（改写会破坏 R18 字节一致性）。

## 需要覆盖的继承默认值

| 文件 | 继承默认 | 处理 |
|---|---|---|
| `unixnu-meizu20pro-nova/scripts/import-meizu-uefi.py` | `UEFI_TREE` 默认指向原 Nova 工作区 | 导出后设置 `UEFI_TREE=$PWD/uefi-sheng-nova` |
| `unixnu-meizu20pro-nova/scripts/make-meizu-boot-avb.py` | 默认 boot 备份路径在原工作区 | 显式传 `--boot`/对应参数 |
| `tools/relocate-workspace-nova.py`、`tools/create-workspace-nova.py` | 原工作区布局 | 只在原 Nova 环境使用，不用于本仓库准备 |
| `unixnu-nova` | `UNIXNU_DTB=$ROOT/inputs-nova/meizu20pro.dtb` | 用户自备 DTB 后设置 `UNIXNU_DTB`，或保留该路径 |
| `unixnu-meizu20pro-nova/board.toml` | 可能有本地 `inputs-nova`/固件路径 | 按 `docs-nova/BUILD.md` 以用户自备输入覆盖 |

`docs-nova/history/` 归档中的 `<nova-root>/...`、`[local-home]/...` 是导出时隐私
替换，不是可执行路径。

## 构建树位置

`prepare-source-nova.py` 只写入本 checkout 的 `unixnu-main-nova/out/`：

```
unixnu-main-nova/out/
├── feeds/                      # 固定上游 reference checkout
└── meizu20pro/
    ├── .config
    └── tree/
        ├── .cache/xnu-work/        # sources-nova/xnu 恢复 + base Git 元数据
        ├── .cache/libdispatch-work/
        ├── .cache/libpthread-work/
        └── build/xnu/owner-data-volume.h
```

它不会写入其他 checkout，不会修改原始 Nova 工作区，也不会执行 `git clean`。
写入前会逐级拒绝 symlink 目标/祖先；已有不匹配的 dirty cache 默认拒绝覆盖，只有
显式 `--replace-cache` 才会移动到带时间戳的备份，而不是删除。

`docs-nova/history/` 与 `tools-nova/history/` 中的归档脚本保留原始本地路径/日志
假设，只用于阅读或在隔离 fixture 中复用纯逻辑；不要直接执行。

## 缺少的私有输入

原始 kernelcache、固件/DTB、ADT、trust cache、owner-signed root 策略、暖 Data
分区、GPU addon、Wi-Fi MAC、厂商 `Binaries/meizu20pro` 都需要用户自备。
因此本仓库**不能**保证 fresh clone 一条命令复现开机；它保证源码、固定来源、
配置和可移植准备逻辑完整。
