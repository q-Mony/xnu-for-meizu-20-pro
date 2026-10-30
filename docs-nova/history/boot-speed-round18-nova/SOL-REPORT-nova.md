# R18 本地候选验收（Sol，新增无旧版）

本轮已完成暖 Data 流式 SHA 加速的实现、独立源码审查、测试证据验收、完整内核构建和封包。执行器两轮实际 request/header 均核对为 deepseek-official/deepseek-flash/high；运行日志分析由Sol直接进行。未使用Codex子智能体。未操作任何设备，手机最后已知状态Android B，新安装需用户明确命令。

暖Data局部Hash改接native流式SHA，复用既有逐batch CPU特性核对和抢占保护的硬件compress；保留软件fallback、header/payload/logical全部摘要、取消/预算及失败释放。普通SHA接口与全局descriptor未改。6186项源码中恰好三文件修改；硬件汇编、UFS、显示、输入、策略和R17日志修复对象均保持一致。

21,019个流式摘要用例、144个feature/chunk直压用例、385个真实WarmLoad模板用例通过，含ASan/UBSan；两个production ARM64 TU与完整构建对象逐字节一致，build exit0。41个封包检查通过，Sol另核对实际镜像SHA、Android v0 SHA1、R17封套、重提取FAT/内核及EFI文件集合，只有kernel载荷改变。

候选：build-package-nova/package/boot-warm-r18-streamsha-candidate-nova/boot-warm-r18-streamsha-candidate-nova.img
SHA256：be40edfedadbc161e3e86864c9d8803243c34426f78007f983213c07c879c21b
96MiB，FAT90MiB。未硬件测试，不能承诺节省8.67秒；该值是R17暖Data哈希已测成本。

36秒接收前等待、Hello黑屏尚未修复，后续方案在receiver-blackout-plan-sol-nova/。新增确认现有workers采样只对lsd开启，且目标请求等待期endpoint仍是launchd，所以仅扩展名称过滤不能采到backboardd关键窗口，需要独立exact-exec anchor。已有selector首次日志之间约30.58秒只是线索，不证明其阻塞。Boot/Hello精确计时仍缺两个端点，Setup armed不可替代；当前DarwinBoot拒绝传入FDT，实际读ADT，起点必须显式传递。

证据流程缺口：dsh首次GREEN失败目录被其删除后原名重跑。Sol从tool transcript保存初版fixture和失败记录，报告如实披露原始完整失败目录已丢失；最终GREEN和生产修改前RED保留。未把缺失证据伪造成旧日志。此后任务已明确要求保留所有失败目录、修改前备份。

正式验收绑定见build-package-nova/SOL-FINAL-ACCEPTANCE-nova.json。新候选完成，等待用户安装命令。
