# R9暖Data封包

新文件，基于R8已验证封包流程。固定R7/R8共同firmware/trustcache/coldseed；只替换kernel并将capture→warm启动参数切换。强制新warm源读取/媒体gate/诊断符号、拒绝GPU export写者，保持96MiB boot与90MiB FAT和其他EFI文件。已冻结R8 envelope与当前warm符号合同，最终raw/payload实际路径由build-audit另核对。设备流程使用当前Android B直接写非活动A并当场整分区校验，省去中间Android重启。未有实机结果前不声称提速。
