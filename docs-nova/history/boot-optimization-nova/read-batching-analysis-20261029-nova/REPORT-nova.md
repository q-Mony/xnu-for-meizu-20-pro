# Nova 读取批量与剩余启动耗时分析

结论：16KiB→256KiB减少的是上层文件读取调用。可以继续增加，但当前根盘驱动仍把请求按16KiB认证块发给UFS，因此简单提高上层数值或整文件读入，不会自动合并底层命令。下一轮更值得分别验证底层128KiB合并读取和VerifiedRoot认证哈希耗时。

## 已有测量能证明什么

第二轮23个大文件共2,724,626,432字节，实际记录10,403次vn_rdwr调用；读取路径41.027秒，签名转换自身哈希8.431秒。对同样的文件大小计算，16KiB模式需166,298次调用，1MiB约2,610次，4MiB约661次。后两者为次数推算，不是已运行测试。既有真实prepare函数host测试已记录16MiB+1输入的调用数1025→65。

这些证据证明调用数量下降；没有仅切换16KiB/256KiB、其余代码完全相同的真机计时对比。此前同时改变了读取批量、缓存复用和签名转换SHA实现，不能把整机收益单独归给256KiB。

本轮用户确认Boot→Hello190秒，首轮237秒，两轮同为Boot菜单起点且同一镜像。SpringBoard等待launchd≥35.481秒、随后lsd≥100.796秒，串行下界136.277秒；这些是相应线程等待区间，不是独立可加到启动总时长上的额外预算，也尚未定位所有依赖。

## 真实读取链路及限制

- cs_native4k_prepare用最多256KiB缓冲，vn_rdwr的ioflg=0。read_ms包住整个vn_rdwr调用，包含APFS、缓存、驱动等待、认证和拷贝；不是纯介质传输时间。
- 运行日志确认APFS root→IOBlockStorageDriver→NativeUfs。APFS与实际IOStorageFamily含预链接二进制，公开源码只能解释可能机制，不能虚构其运行时分拆计数。
- 已启用的RootWindowRead循环每次读一个VR_BLOCK_BYTES单元；当前宏和VerifiedRoot构建输入均为16384。transport虽有128KiB容量，但当前循环未将8个连续单元合成一次读取。实际命令数还受缓存、预读、extent、首尾请求边界影响。
- 每次UFS读取由单slot同步提交、poll、复制完成；随后VerifiedRoot逐16KiB通过普通SHA256_Init/Update/Final认证，再复制至请求缓冲。这里的SHA开销计入上述41.027秒的read_ms，不在8.431秒cs4k hash_ms内。
- 已有native_ufs_read_ticks/native_ufs_verify_ticks，但现有运行日志没有这两项的分离输出，尚不知道41秒里设备、认证和其他软件各占多少。

## 更大或一次读完

当前已经流式读完每个签名codeLimit。改成一次读完不会少读数据，也不会取消底层16KiB认证和命令循环。最大文件333,447,168字节（318MiB），上层临时缓冲会由256KiB变为318MiB；vn_rdwr的长度参数为int，也不能作为任意大文件的无界接口。

1MiB/4MiB作为上层实验可行，须保持16KiB边界及现有逐块验证，但不会自动带来相同比例加速。底层transport当前只填单个PRDT项；现有容量128KiB，UFS HCI单项上限256KiB，因此也不能只改常量到1MiB/4MiB而不重做DMA/PRDT布局。

## 下一轮可证伪的实验

1. 假设小命令提交/等待是主要成本：复用现有128KiB bounce，一次读取最多8个连续16KiB单元，逐单元认证后再向上层复制。预测实际UFS命令数下降，设备read_ticks/总转换时间降低；不承诺速度提升8倍。
2. 假设根盘认证占读取路径主要成本：先输出已有read_ticks/verify_ticks的聚合增量，再单独测试VerifiedRoot安全使用现有硬件SHA适配的可行性。预测verify_ticks占比较高且可降低；尚未实现或验证此适配。
3. 假设上层函数调用仍很贵：保持底层和哈希不变，仅比较256KiB与1MiB。预测vn_rdwr次数下降同时read_ms下降；若时间基本不变，应停止盲目增加缓冲。

底层合并必须保持窗口相对16KiB边界、512字节请求首尾、末块真实长度与物理4KiB padding边界；整个批次先认证再复制，失败不能输出未认证字节。已有RootCommands transport与RootPathBridge真实认证fixture可复用，但独立RootWindowRead.cpp仍有4KiB旧假设，需要先修正，不能称其当前路径已通过。

本次只做代码/构建/运行记录只读分析和证据计算，没有修改生产实现或设备。源码位置及哈希见同目录evidence-nova.json；两位只读探索代理分别核对XNU读取路径与NativeUfs/VerifiedRoot路径。
