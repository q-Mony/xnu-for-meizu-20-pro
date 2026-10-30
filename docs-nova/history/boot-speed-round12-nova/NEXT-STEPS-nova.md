# R12 已完成本地交付，等待安装命令 — Nova

新文件，无旧版。状态BUILT_AWAIT_USER_INSTALL_COMMAND。用户明确要求改完不要直接安装；本轮没有任何设备操作。手机最后核对为Android B，A仍R11，B为原Android。

本轮仅新增policy-metadata probe12的mkdir结果、目录属性和失败文件属性。R11 core版本标记及nova-r11 panic文字保留。目录/文件判断和errno都不变，尚无属性根因、Hello或提速结论。

收到明确安装命令后，备份并更新授权/manifest放行状态，先新鲜核对设备模式、匿名身份和完整A/B及固定export，再按当前Android B直接写非活动A/fsync/同会话完整回读流程安装；不添加额外Android校验启动。若设备不在B，先依实际状态处理。未经命令不得调用install或start；当前manifest状态已通过实际verify_delivery拒绝测试。

获准运行后无需用户手动计时，失败则回B读取正式ring长度内probe12目录字段，与固定fixture比较，修正有证据的具体原因。
