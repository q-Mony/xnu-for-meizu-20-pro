Sol 验收要求定点修订，仅改本任务 DSH-RESEARCH-nova.md 和 dsh-findings-nova.json。固定 deepseek-official/deepseek-flash high，不派代理，不运行测试/构建/上机。模型/授权沿用原任务。
先将两份现报告备份到 <nova-root>/snapshot-nova/before-pudding-pandora-review-revision-nova/，该目录新建且无旧版。最多12次工具调用，不再扩大研究范围。
1. 删掉“两仓都没有提交原始启动日志、测量记录或截图”等全称断言。docs已有嵌入日志和测量记录；改为“本次检查以源码和作者文档内嵌日志为主，未独立复现；在本次所查tracked文件中未发现对应独立原始日志/屏幕证据”，如果未穷尽检查就留未知。最终限制段同步。USB串口可表述源码/编译接线存在，端到端运行未确认，不能写已可实时USB日志。
2. data-a-live最新HEAD的 pandora_sink.c 是很相关的新实现，需加入重点，最多三项可合并SHA测试到cs4k项。核查 tools/sink/pandora_sink.c:35-43、210-271 与 xnu/patches/0330-crash-stop-sink.patch。它用替代daemon接Mach服务，MIG回复错误、XPC持有约1秒后销毁；注释记载立即丢弃曾使客户端重连重试、CPU高占用。明确这是兼容stub，未实现真实服务；当前只说明避免无限排队/忙重试的设计，不能拿来解决Nova lsd或证明Boot提速，不能笼统关闭服务。给确切固定SHA证据。
3. 批量读项禁止用主机read_ms阈值去比较手机744ms，也不能断言“1MiB不改变3-4分钟”。允许说“没有证据能消除分钟级等待”。已有R7总计量可引用 artifacts-nova/boot-speed-round7-nova/device-analysis-nova/storage-logio-nova/analysis-run-nova.txt（cs4k合计19.344秒/读10.911秒/hash8.405秒；是累计函数计量，不等于独立阶段墙钟）。现有同报告lsd等待age最大90.060秒，只能视为等待观察，不等于Hello。CPU项保留依赖警告，不作为直接修复。
4. 唯一建议后续动作改为：离线将Nova已有R7/R8等待日志中的端口/服务/调用者/等待年龄整理成依赖清单，与launchd服务配置、data-a-live sink对应的问题类型对照，区分缺失/崩溃重启/已经在运行但无回复。只做有证据归属，无证据留unknown；此步骤不实施stub、不改策略、不上机。验收为每个结论都有本机日志与源码位置，优先定位最长lsd等待的调用双方和具体请求；不以对方CommCenter故事推断本机同根因。256KiB/1MiB等价可作为未来备选，不作为本轮优先后续动作。
5. 所有Bluetooth缺失等负面结论用“本次未发现”，README的WiFi/cellular行不能拿来直接证明BT。pudding有历史文档与当前构建版本不一致，表格未知保持。
6. 修复无效PLAN.md#239链接为#L239。JSON和Markdown结论/后续步骤保持一致。保留e22是最新暖Data候选首个已知阻塞；stat ABI假设已排除；R12未装。改完做JSON与引用范围静态检查后结束。
