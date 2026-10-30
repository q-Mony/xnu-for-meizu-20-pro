# R16 安装准备与只读检查报告（新文件，无旧版）

状态：`PREPARED_AWAIT_SOL_INSTALLER_REVIEW_AND_DISPATCH`。设备检查在枚举阶段停止，未写入、未重启、未切槽。固定 `deepseek-official/deepseek-flash/high`，无委派/回退；仅 Nova。

## 候选与预检

- 候选镜像：`artifacts-nova/boot-speed-round16-nova/final-build-nova/package/boot-warm-r16-diagnostic-candidate-nova/boot-warm-r16-diagnostic-candidate-nova.img`
  - SHA256 `51a0248347f0e70644a28d30f932ce59245ada3920ade777ac5835578e30bf51`，100663296 B。
- Sol 本地验收 `415fdcfb30a052f34d86624d14e7aa312cd4d3007ce1d981485d0ecbd0ed24e0`，状态 `LOCAL_DIAGNOSTIC_ACCEPTED_AWAIT_NEW_USER_INSTALL_COMMAND`；15 项 `bound_records` 全部复核匹配。
- 源码 freeze `f578ed91caf078f93cb27826aed0ae2a3f9bf4b56dfd55acbd06fefef256a60a`，6186 项；相对 R15 基线仅 `sleh.c`、`mach_msg.c` 变化。
  - sleh `17db32f399e7e527b7811ad88e606fe61507d28c13355b378ef6b031fd23d7af`
  - mach_msg `f8758befcf7d79318c0e777adb632423816bea9a9012b39d14b7912475afb2e9`
- 29 项投递 source map 从 R15 manifest 复制，仅更新上述两项（sleh 原已存在，未新增条目）；29/29 与 R16 freeze 及当前 Nova 树一致。
- 预检证据：`preflight-verification-nova.json` `b9529b35524da21321d6adcbe0b854c5944a49c7a6fe23f3cc50ecef0000fb21`。

## 安装器与测试

- R16 ROOT `device-run-nova.py` 由 R15 `7a478dae98af62d322ba1243072c426c8fdd8e3ccb3ad3f8ed25c169164efbbd` 机械生成，`69d6c154e83e37bee68e774563cd7ebcd764b57c1bc868a74ccfec5b43edf9cd`。
  - 仅 5 行变化：docstring、LOG、STAGE_DIR、BASE_A=R15 `53470de0...`、CANDIDATE=R16 `51a02483...`；BASE_B `7df205f1...`、TAG、R8 export、GPT/函数/控制流不变。
  - 全 diff `installer-diff-nova.patch` `e27c1f5a02b44d23a68042da18dbfc5af70a751ef025daf5e93023c04b015d62`；反向替换逐字节还原 R15，AST 等价，证据 `installer-comparison-nova.json` `b50b27569dd1cc745feb043645fffae965914a31cb2c04d3f46c23e75a495d00`。
- `test-device-install-nova.py` 与 R15 字节相同 `1d56111521a5e2c8218a7fe1e0e7a9dd624c9f8ef4021a73d9b1f6a7da3bf1d1`。
- 实际安装器 6 项 mocked-transport 测试一次通过，exit 0，device 未访问：`device-install-host-tests-nova.json` `00cd7f27ca91203e171dfb326dfa28bc15fd5105390c1ce2545dc9c46744e5e7`。测试不证明真机写入能力。

## 投递 manifest

- ROOT `delivery-manifest-nova.json` `653a27387db6a827540461917a3ec72ae5fce3f1fedd3720f4a85b4c92c9d461`。
- 状态 `AWAIT_SOL_INSTALLER_REVIEW_AND_DISPATCH`，`installation_allowed=false`；30 项 artifact 绑定、29 项 source 绑定；`warm_source` 为 R15 manifest 原对象逐字段复制（未打印私有身份）。
- 已记录用户命令“安装”对 exact R16 install/start 的授权，但门保持关闭，须 Sol 审查与单独派发；未自我授权。
- 未修改任何历史 R15/R16 build/package/local-acceptance 的 no-install 标志；历史状态保持原样。

## 只读设备检查（停止）

命令 `python3 artifacts-nova/boot-speed-round16-nova/device-run-nova.py check` 于 2026-10-30T09:39:25Z 在 `adb devices` 枚举阶段返回 `Expected one device`，exit 1。未重试、未重启、未写分区、未切槽。

- 记录 `logs-nova/boot-speed-round16-nova/check-device-nova.json` `23ef1dbe7dca9fc07b0ac09d436165f6097ef9a3c137f48de6d9b09b911e2e5e`；stdout 同哈希，stderr 空，exit 码 `4355a46b...`。
- 未验证：Android B、boot_a/boot_b 新鲜全哈希、96MiB 几何、boot_a 写能力/fsync。预期值仍为 boot_a `53470de0...`、boot_b `7df205f1...`，但本轮未读取。
- `check` 阶段本身只验证 boot 状态与写能力；GPT/warm export 只在 accepted install/start 函数内验证，本轮均未运行，报告不声称已做。

## 结论

准备完成，等待 Sol 审阅 R16 installer 与 manifest 后另行派发。设备可用且恰有一个已知 adb 目标时重跑 check；不得盲重试。无安装、启动、重启、切槽、分区写入、生产编辑或构建。
