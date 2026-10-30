# 根盘数据块认证提速 — Nova 第三轮

2026-10-29，新文件，无旧版本。目标是缩短实际启动时间；本轮已完成一次真机测量与保留日志回收，重复稳定性尚待验证。

## 依据

上一轮128 KiB底层合并候选 `3d441023…06fc47d` 到Hello用245秒（Apple97秒）。同样23次大文件转换44.002秒，比此前49.485秒减少5.483秒，仍不足以解决整体启动慢。新rootio显示，覆盖大文件初始化的首段49.514秒观测窗口中，读取路径累计3.865秒、认证27.846秒、最终复制0.563秒，平均命令116.3KiB。此窗口包含其他读取、并非Boot起点，累计认证还可能含抢占，不能把27.846秒全部当作必然收益。

实际链接证据确认根盘数据认证走 `vr_xnu_crypto → SHA256_Init/Update/Final → ccsha256_ltc_compress` 软件路径；之前硬件加速只接入cs_native4k签名转换，两条路径不同。SpringBoard服务等待下界仍约132秒，后续必须独立处理。

## 本轮改动

只改两份生产文件：

- `kits/VerifiedRoot/adapters/xnu_crypto.c` 新增独立 `vr_xnu_data_crypto`，数据摘要调用既有 `native_sha256_digest`；共享的RSA回调未变。
- `drivers/NativeUfs/NativeUfs.cpp` 仅将 `vr_verify_block` 的provider切换到新接口。

`vr_open` 和策略公钥/header/hash table仍使用原provider；每个16KiB数据块及真实尾长的校验、摘要比较、损坏拒绝和整批通过后复制保持原逻辑。硬件摘要通过已有本核能力检查、软件回退、每段最多16KiB禁抢占及向量寄存器保存恢复实现；不直接调用compress汇编。

## 候选与验证进度

构建、prelink、PAN与既有SHA汇编一致性审计通过；相同OS/SystemOS策略完整字节在payload中各出现一次。固定成功封装只替换内核，bootargs/UEFI/ADT/trust cache/Data seed不变。

- boot SHA256：`8ba3c90fe9078d4b5da04359a266596bdfb5a08026c19871065aee7e31533894`
- raw kernel SHA256：`240fed963ddd0271afc4e86c3410b2d049214f41099b35400531a84a75b2e378`
- payload SHA256：`839f460f9e38fbe217c3eea88adbdeca1a13b70cea38349ec02a83ae001d7ff1`

候选路径：`artifacts-nova/boot-optimization-nova/packaging-nova/root-data-sha-candidate-nova/boot-optimized-candidate-nova.img`。

旧适配器缺数据provider已捕获RED。实际 `vr_verify_block → xnu_crypto adapter → native wrapper/生产ASM` 集成测试GREEN：1440项ASan/UBSan用例通过，其中648次硬件摘要、684次动态软件回退、108次参数拒绝未触发摘要；3份真实签名policy使用OpenSSL支持的RSA ABI兼容层验签，策略继续调用原SHA三次和原RSA一次。特权寄存器和抢占仅为host模型，不代表手机调度验证。

独立二进制审阅确认原provider、数据provider及其调用位置正确：数据hash真实BL到native，原hash仍到SHA256_Init/Update/Final、原RSA仍到rsa_make_pub/rsa_verify_pkcs1v15；最终payload的关键函数和descriptor与raw同字节。已完成一次设备验证，结果见下。

## 测量与回退

与245秒的上一轮使用相同Boot菜单起点，分别记录Apple、Hello，再读取rootio和cs4k统计。核对verify时间下降及完整认证失败数，再比较服务等待。不能只报告最好一次，也不能把主机速度替代手机结果。

上一轮成功候选仍是本轮前boot_a的准确回退副本，SHA256 `3d441023efd1cba90dccb6cadcb765a3fedc174968d6cad3b05329f2406fc47d`。此前原A备份和第一轮成功候选也继续保留。Android B用于日志回收，不改变其内容。

源码、build/kernel/log备份位于 `snapshot-nova/boot-speed-round3-nova/`；补丁、源码哈希、构建/交付证据在本报告同目录。

## 真机结果（用户已澄清计时口径）

17:07:31–33只刷boot_a，17:08:46完成A/B整分区SHA256回读；A与候选一致、B未变。17:09:52.994切A发出重启。用户实际从Boot菜单17:12:50开始计时，苹果73秒、苹果之后另139秒，**Boot→Hello总212秒（3分32秒）**，推算Hello约17:16:22。此前把139秒误作总时间的解释已撤回，原记录保存在snapshot，本轮最终观察记录包含用户澄清原文。

| 指标 | 第二轮批读候选 | 第三轮数据SHA候选 | 差值 |
| --- | ---: | ---: | ---: |
| Boot→Apple | 97秒 | 73秒 | -24秒 |
| Apple→Hello | 148秒 | 139秒 | -9秒 |
| Boot→Hello | 245秒 | 212秒 | -33秒（13.47%） |
| 同样23次cs4k总计 | 44.002秒 | 20.209秒 | -23.793秒 |
| cs4k read（含下层认证） | 35.538秒 | 11.761秒 | -23.777秒 |
| cs4k自身hash | 8.435秒 | 8.418秒 | -0.017秒 |

23次cs4k的顺序、2,724,626,432 bytes及10,403 reads一致。覆盖这批转换的早期rootio窗口物理读取量相差约0.078%；根盘verify累计27.845667→3.632708秒（约-86.95%），read3.864599→3.951586秒、copy0.563139→0.560132秒。窗口含其他读操作与可能抢占，不能逐秒相加为Boot预算；但与cs4k同工作量的下降共同支持根盘SHA加速生效。24条rootio严格解析clean，未见bad_hash。

SB对launchd和lsd的串行等待下界仍35.644+101.036=136.680秒；S03继续定位服务启动，整体启动慢尚未解决。本轮仅一次，且此前旧镜像曾有237/190秒两次波动；不能据本轮声称稳定快33秒或成为历次最快。未匹配到Setup scene-create watchdog，不代表其他所有服务无异常。

已返回Android B回收日志，A仍为本候选，B整分区哈希保持不变。新日志SHA256 `57cbb9046f33237974d31bdef848ed537f446520d99ee869dc8c2c2277bc9633`，dropped=0、NUL=0；不能仅凭这两个字段声称所有事件完整无损。capture.bytes比total多173字节来自额外boot-video前缀。分析见同目录 `device-analysis-nova/`。
