# Nova 固定基线打包器

本目录新建，无旧版本。用于把**未注入 entry probe 的新 prelinked kernel**放入已固定的 032506 启动配置，所有写入限制在 Nova。没有设备访问、刷机或固件编译。

## 调用

工作目录固定为 `<nova-root>`。

```sh
python3 -B artifacts-nova/boot-optimization-nova/packaging-nova/package-candidate-nova.py --prepare-only

python3 -B artifacts-nova/boot-optimization-nova/packaging-nova/package-candidate-nova.py \
  --kernel unixnu-main-nova/out/meizu20pro/kernel/kernel.macho \
  --output artifacts-nova/boot-optimization-nova/packaging-nova/candidate-nova
```

`--kernel` 必须指向 Nova 内的新 raw prelink；实际构建输出路径须由调用者核对。`--output` 必须是本打包目录内尚不存在的目录。脚本对内核副本应用一次历史 entry probe 配置，原输入不会修改。重复注入或已有输出目录会失败。

主产物为 `<output>/boot-optimized-candidate-nova.img`；证据在 `<output>/evidence-nova/verification-nova.json`。名称不表示已经通过设备验证，源码与构建来源另需调用者的构建记录。

## 固定配置与校验

输入副本固定在 `inputs-nova/boot-optimization-nova/`，清单为 `frozen-inputs-nova.json`。

| 输入 | SHA-256 |
|---|---|
| 历史 boot | `4fcf20feb1639c19fac0331c3ea4180ba0d0f302078ed2bde88f04c5434a1ac1` |
| 历史 raw prelink | `651c44833298350a9c8e79ed74ca77b561547ffc4b280bd7f994e74c651b852b` |
| 历史 FAT payload | `1cdd1b73a8ca7466eff08d255e7ed7731dd80dbd74c6e60cc34cc20b6ee6a939` |

只替换 FAT 中的 `EFI/UniXNU/Payloads/kernel.macho`，重算 Android v0 SHA-1 image ID。最终镜像重新提取后核对全部 EFI 文件，保留固件、DTB、启动参数、ADT、trustcache、Setup 配置及 Data512 种子。镜像大小保持 100,663,296 字节，Data 逻辑大小为 536,870,912 字节。PAN 校验必须通过。

继承的 AVB 尾部保持原始字节，**没有生成新的 AVB 签名，也不主张该元数据重新通过了密码学认证**。

## 已完成的离线验证

`baseline-selfcheck-nova/` 与 `baseline-repeat-nova/` 使用历史 raw prelink 做两次独立自检，均通过。两者完整镜像 SHA-256 都是：

`2be63de534aa3d82eac710e8aabd20ddb7dea078665f19e3204e1c0444fcc4f8`

两者注入后的内核均精确复现历史 payload 内核：

`9a4212834eb4ac1575d1d47c91802471f833eb7e268d8b351a5913cbe90636ae`

完整 boot 与历史 boot 的字节不完全相同：FAT 文件替换会改变目录时间与分配信息。上述自检中提取出的五个 EFI 文件内容均与历史基线一致，且两次新镜像完全一致。机器可读比较见 `reproducibility-nova.json`。

`guard-tests-nova.json` 记录了对已注入内核、已存在输出目录的拒绝；拒绝时没有创建候选目录。

这两份自检镜像**不是启动优化候选，也没有测量启动加速或做设备测试**。实际候选需使用优化后的新 raw prelink 再执行打包。
