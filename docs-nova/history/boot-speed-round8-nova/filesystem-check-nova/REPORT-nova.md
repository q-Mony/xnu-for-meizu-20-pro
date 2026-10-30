# R8 Data快照只读核查

2026-10-30。新增报告，无旧版。原始镜像始终只读挂载；本报告没有更改Data内容或开启恢复。

- export 256MiB设备与主机SHA一致，capture ID 4013558640。头、payload、DBRDISK1全部边界与日志成功回执一致。
- packed 231,147,936字节，logical 536,870,912字节。展开SHA `372549a9fcb74ba062fbd871a370199c7d32657cfa4e436040c120b6dbfcbd43`。
- `fsck_apfs -n /dev/rdisk8`返回0；Data卷UUID `691978E3-CC8E-4F14-9BF9-D55DAB2D1A4C`，checkpoint xid112。设备号是本次挂载值，未来操作必须重新按backing path查询。
- 可读取目录中找到1,474个普通文件、169,939,406字节、599份容器metadata。仍有41个受限目录读取失败，因此不是全文件清单；noowners只读重挂没有消除这些读取限制。见inventory中的errors，不能把未见文件当不存在。
- LaunchServices两个csstore分别5,201,920与5,198,208字节，开头magic `bdsl`（小端形式）。已记录SHA和头，未用私有格式解析器验证其全部语义，不声称LS下次必接受。
- MobileContainerManager数据库及WAL复制到独立本地目录后以SQLite mode=ro检查：integrity_check=ok，code_signing_info与code_signing_data各558行，child_bundles135行。
- LS SettingsStore SQLite integrity_check=ok。SQLite结构有效不等于跨文件/跨服务事务一致。
- 两份Setup ips已交稳定性子代理保存并分析；快照时点以前的报告可用于解释超时，不能据快照未见后续SB报告断言其未崩溃。

大小的主要来源是诊断符号/日志数据：一个db/uuidtext/dsc文件约101.69MB，coresymbolicationd缓存约15.25MB；并非LaunchServices数据库自身有220MB。本轮保留完整字节，没有删文件来强行塞入boot。gzip离线试验仍48,103,236字节，超过当前FAT暖seed约19,627,008字节预算。

下一步验证复用已初始化Data能否减少重复注册和SB→Setup时间。加载必须在APFS首次探测之前完成；保持mode0、原Data UUID、OS/SystemOS签名链，不直接启用现有持久化模式。优先比较UEFI加载已有export与内核加载方案，选择可确定顺序且能完整验证的最小改动。成功挂载、文件存在和SQLite检查不能代替下一轮实机启动对照。
