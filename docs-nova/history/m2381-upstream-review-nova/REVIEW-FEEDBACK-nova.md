Sol 验收后要求你对现有报告做以下定点修订。不要重新研究，不扩大范围，不派子智能体，不动生产源码，不调用设备，不构建或安装。两个报告的旧版已由 Sol 备份到 snapshot-nova，允许只修改 artifacts-nova/m2381-upstream-review-nova/DSH-RESEARCH-nova.md 与 dsh-findings-nova.json。继续使用原会话和固定 deepseek-official/deepseek-flash/high，最多完成一轮必要读证据+编辑+格式检查后结束。

必须修正并在Markdown/JSON保持一致：
1. 摘要“对方没有实际到 Hello/桌面”改为“在本次固定提交及公开记录中未找到到达 Hello/桌面的证据；最新已记录实机结果为进度条冻结”。负面证据只对研究范围负责，不能断言对方未公开的新运行状态。
2. RAM 的11501072 kB约11.5GiB单位计算错误。保留原始11501072 kB及“12 GB级设备”即可，或按Linux MemTotal单位计算约10.97GiB；不要写11.5GiB。
3. warm e22 未知段重新提出stat ABI不正确。R12已排除该假设，证据 artifacts-nova/boot-speed-round12-nova/stat-abi-review-nova/REPORT-nova.md：PID1=64，stat64/user64_stat64/user32_stat64的mode@4、uid@16、gid@20一致。明确该mode/uid/gid偏移假设已排除；实际mode/owner或APFS呈现仍未知。推荐行动中的“修stat映射”等若暗指已排除的布局也收紧。
4. “若R12没有目录行则问题就在stat/copyin”不能由缺日志推出。改为先核对镜像身份、日志保存/有效ring、执行是否到达probe，再判断是否需定位更早路径；不能直接归因。
5. 本机最新warm候选最早已知阻塞是policy e22；不能声称它是唯一启动瓶颈或R12是唯一可做行动。冷Data/lsd长期耗时等问题仍未解决。保留等用户命令再安装R12的边界，不催促、不请求安装。
6. Sol已新增并核验主/设备仓最新提交的真实file list：本目录main-latest-commit-detail-nova.json显示最新主仓只改AGENTS.md、README.md、docs/STATE.md、docs/UPSTREAM.md；device-latest-commit-detail-nova.json显示设备最新只改board.toml的7行注释。把此前“最新变更只根据message，没每提交file list”的限制更新为“最新主/设备提交file list已核验；更早历史和全树未逐文件diff”。UEFI仍partial snapshot，原范围限制保留。
7. 本机UEFI HEAD已由Sol核对为0eb4514db0744b8136a3897ab1af93b713c2c27b，与引用上游基座一致。无需再扫描整个UEFI。
8. 本次只修改报告，文件链接若引用本机请用 /绝对路径:行号 形式，不用GitHub式本机#L行号；GitHub固定commit链接保留。

完成后检查JSON可解析、上述错误文字已修正，然后直接结束并给出两文件路径，不追加新调查。
