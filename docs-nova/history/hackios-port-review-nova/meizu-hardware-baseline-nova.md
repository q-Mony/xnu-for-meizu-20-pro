# 魅族 20 Pro 本地硬件与驱动基础核验（Nova）

核验日期：2026-10-29。范围：Nova 当前源码、选中配置、已有启动日志、复制进 Nova 的设备树，以及主代理新采集的 Android 只读观察。本子任务未操作手机、未刷机、未重启、未编译、未运行历史脚本。本文件为新建文件，无旧版本；主代理统一记录 CHANGELOG。仅凭本报告不能认定某个历史镜像仍为手机当前镜像。

## 结论

当前已经具备真实的 XNU 启动、UFS 根卷和 CPU 软件显示路径；触控有实质驱动代码但当前选中配置关闭。Wi-Fi、蓝牙、电池/充电在本次检查的 Meizu 构建入口中没有可接管硬件的驱动。尤其不要把 Wi-Fi MAC 属性、bluetoothd 启动、ApplePMGRNub 发布当成功能已实现。证据见 [.config:12](<nova-root>/unixnu-main-nova/out/meizu20pro/.config:12)、[autolog-nova.txt:96](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt:96)、[autolog-nova.txt:156](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt:156)、[board.toml:45](<nova-root>/unixnu-meizu20pro-nova/board.toml:45)。

主代理在 2026-10-29 07:06:17 UTC（北京时间15:06:17）补采了 Android 只读枚举，记录明确无重启/刷机/切槽；Wi-Fi 实装为 PCI `17CB:1107` / `cnss_pci` / Kiwi，结合官方ath12k PCI ID表确认WCN7850（见下表），电池为 `pmic_glink` → `qti_battery_charger`。[本轮观察元数据](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:1)；[Wi-Fi实测绑定](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:33)；[电源实测绑定](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:45)。证据等级：**运行日志**表示已有某次设备启动记录，未在本轮复测；**静态 DT**表示保存的 Android 设备树所描述的配置，候选节点并不等于实际选中的芯片；**源码/配置**表示已实现或已选择的路径；**推断**必须补运行时枚举才能定论。README 的“nothing has been booted”与部分配置注释已经过时，以后续日志与实际配置项为准：[README.md:6](<nova-root>/unixnu-meizu20pro-nova/README.md:6)、[README-nova.md:49](<nova-root>/README-nova.md:49)、[isolation-audit-nova.json:374](<nova-root>/isolation-audit-nova.json:374)。

## 硬件与当前实现

| 子系统 | 本机硬件证据 | Nova 当前基础 | 判定 |
|---|---|---|---|
| SoC / CPU | 本轮Android实测SM8550；板级记录 kalama、M2391。[SoC实测](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:13)；DT 根 compatible 为 kalama-mtp/kalama/mtp。[README.md:41](<nova-root>/unixnu-meizu20pro-nova/README.md:41)；[DT 摘录](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md:51) | ArmDTPlatform、ArmCpu 已选中；八核/PSCI 描述由 ADT 生成。[.config:9](<nova-root>/unixnu-main-nova/out/meizu20pro/.config:9)；[make-meizu-adt.py:161](<nova-root>/unixnu-meizu20pro-nova/scripts/make-meizu-adt.py:161) | 已有启动平台基础。ADT CPU compatible 统一写成 cortex-a715 是生成器实现，不能据此推断每个物理核型号。 |
| GPU | DT `qcom,adreno-gpu-gen7-2-1`、KGSL @ `0x03d00000`。[DT GPU 摘录](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md:51) | 公共 `NativeAdreno` 面向 Adreno 740，README 只声明 sheng 上验证；Meizu 当前配置未设 `XNU_NATIVE_ADRENO`，构建默认值为 0。[README.md:3](<nova-root>/unixnu-main-nova/drivers/NativeAdreno/README.md:3)；[build-xnu-4k-baseline.sh:663](<nova-root>/unixnu-main-nova/scripts/build-xnu-4k-baseline.sh:663) | 有可复用同代代码；没有本机 GPU 启动/命令队列成功证据。芯片具体 revision 仍应读 Android GPU 信息。 |
| Wi-Fi | **本轮Android实测**：`wlan0`→PCI `17CB:1107`（subsystem同值）、`cnss_pci`，DT路径`/soc/qcom,pcie@1c00000/pcie0_rp/cnss_pci0`；日志加载`kiwi/bdwlan.elf`、`kiwi/regdb.bin`和`wlan/qca_cld/kiwi_v2/WCNSS_qcom_cfg.ini`。[PCI枚举](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:29)；[wlan0绑定](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:33)；[固件日志](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:49) | XNU ADT只有可选`/chosen/mac-address-wifi0`；板级archive没有CNSS/MHI/PCIe WLAN驱动。[make-meizu-adt.py](<nova-root>/unixnu-meizu20pro-nova/scripts/make-meizu-adt.py:103)；[board.toml](<nova-root>/unixnu-meizu20pro-nova/board.toml:45) | 已排除仅凭DT在0x1103/0x1107之间猜测：本机实际选0x1107 / Kiwi，PCIe可确定。Linux v6.12官方ath12k将QCOM `0x1107`定义为WCN7850，因此可确认本机Wi-Fi为**Qualcomm WCN7850**；[PCI ID定义](<nova-root>/artifacts-nova/hackios-port-review-nova/pinned-sources-nova/linux-v6.12/drivers/net/wireless/ath/ath12k/pci.c:40)、[QCOM匹配](<nova-root>/artifacts-nova/hackios-port-review-nova/pinned-sources-nova/linux-v6.12/drivers/net/wireless/ath/ath12k/pci.c:48)，[原始来源清单](<nova-root>/artifacts-nova/hackios-port-review-nova/pinned-source-manifest-nova.json:1)。硬件revision还需读取，不从驱动支持分支倒推实测值。 |
| 蓝牙 | DT `qcom,bt-qca-converged` 下同时有 `qcom,kiwi` 和 `qcom,qca6490`，BT_EN GPIO81、SW_CTRL GPIO82，与 WLAN GPIO80/83 协调。GENI HS UART @ `0x898000` 为 ok，唤醒字节 `0xfd`。[DT 蓝牙/UART 摘录](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md:98) | 没有板级 HCI transport/Qualcomm firmware loader 构建入口；已有 `bluetoothd` exec 日志只证明用户态进程启动。[board.toml:45](<nova-root>/unixnu-meizu20pro-nova/board.toml:45)；[autolog-nova.txt:903](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt:903) | 本轮`bt_bus`查询为空，不能证明无蓝牙。[BT枚举结果](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:37) UART 很可能为HCI传输，但DT未把该UART与BT直接连为父子；仍需vendor HAL/init与运行时绑定。 |
| PMIC / 电池 / 充电 | **本轮实测**电池节点由`qti_battery_charger`驱动，父级`pmic_glink`；日志可见SOC/电压/电流/温度/充电状态遥测。[电池驱动绑定](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:45)；[遥测存在证据](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:49)。DT 列有 PM8550、PM8550B、PM8550VE、PM8550VS、PM8010。电池 charger 是 PMIC GLINK 子节点，通道 `PMIC_RTR_ADSP_APPS`、远端 `lpass`、保护域 `msm/adsp/charger_pd`。[DT 电源摘录](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md:126) | 有触控供电所需的有限 RPMh voter；`ShengPlatformPower` 只是 Apple PMGR ABI 兼容发布，源码明确不触碰硬件 power state。[MeizuTouchBoard.h:77](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchBoard.h:77)；[ShengPlatformPower.cpp:2](<nova-root>/unixnu-meizu20pro-nova/package/ShengPlatformPower.cpp:2) | 本轮实测与DT共同确认应先研究GLINK/ADSP电池服务；尚不能确定独立 fuel-gauge IC 型号/地址。没有 XNU 电池遥测、充电策略或热保护闭环已实现的证据。 |
| 触控 | Goodix Berlin D `goodix,brl-d`，I²C @ `0xa90000`、地址 `0x5d`、1MHz、IRQ25/reset24、1440×3200 坐标；板级记录称 GT9916。[MeizuTouchBoard.h:8](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchBoard.h:8)；[MeizuTouchBoard.h:44](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchBoard.h:44)；[README.md:62](<nova-root>/unixnu-meizu20pro-nova/README.md:62) | GENI I²C / Goodix / RPMh / board probe / HID bridge 均已有代码；但当前board probe的`openBus()`固定使用100kHz软件GPIO I²C，源码注释称QUP FIFO不可用。[MeizuTouchProbe.cpp](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchProbe.cpp:435)；当前 `.config` 注释掉 TOUCH 与 HID，备注 workloop 可能关中断自旋。[build-native-meizu-touch.py:73](<nova-root>/unixnu-meizu20pro-nova/scripts/build-native-meizu-touch.py:73)；[.config:12](<nova-root>/unixnu-main-nova/out/meizu20pro/.config:12)；[.config:29](<nova-root>/unixnu-main-nova/out/meizu20pro/.config:29) | 不是从零开发，但不能称当前可触控；应先核对板级 MMIO/版本，再恢复受限探测。 |
| 显示 | 本机记录/DT 指向 Samsung AMOLED command+DSC，物理面板1440×3200；UEFI XML有效模式1080×2400。[README.md:45](<nova-root>/unixnu-meizu20pro-nova/README.md:45)；[Panel_meizu_amoled_samsung_dsc_cmd.xml:2](<nova-root>/uefi-sheng-nova/Binaries/meizu20pro/RawFiles/Panel_meizu_amoled_samsung_dsc_cmd.xml:2) | 当前启用 NegroniDisplay、关闭 NativeMobileDisplay；运行日志注册 AppleCLCD 1080×2400 / stride4320 / framebuffer `0xb8000000` 并有 surface swap。[.config:21](<nova-root>/unixnu-main-nova/out/meizu20pro/.config:21)；[autolog-nova.txt:96](<nova-root>/logs-nova/device-boot-optimization-nova/repeat-20261029T061747Z-nova/candidate-run2-nova/autolog-nova.txt:96)；[autolog-nova.txt:1493](<nova-root>/logs-nova/device-boot-optimization-nova/candidate-run1-nova/autolog-nova.txt:1493) | 已有软件显示路径；CPU memcpy 向前级留下的 scanout 写图，60Hz timer模拟vsync，不能等同 GPU/完整 DSI 显示电源管理。[NegroniFramebuffer.cpp:2](<nova-root>/unixnu-main-nova/drivers/NegroniDisplay/NegroniFramebuffer.cpp:2)；[NegroniFramebuffer.cpp:22](<nova-root>/unixnu-main-nova/drivers/NegroniDisplay/NegroniFramebuffer.cpp:22) |

## 会影响移植方案的细节

1. **Wi-Fi实装已确定为WCN7850（0x1107 / Kiwi），静态DT另一套是备用候选。** 本轮`wlan0`枚举与Kiwi固件加载相互印证；DT中0x1107对应RC0、0x1103对应RC1，供电和PDC序列不同。下一步要补实际CNSS chip/board/firmware版本、已使用文件的哈希和校准/NVM职责；无需再猜这两个PCI ID。[运行时设备绑定](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:33)；[Kiwi加载日志](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:49)；[DT两个chip_cfg](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md:67)。仅填MAC对PCIe枚举、DMA、固件启动、收发与iOS Wi-Fi用户态接口均不构成实现。[make-meizu-adt.py](<nova-root>/unixnu-meizu20pro-nova/scripts/make-meizu-adt.py:177)。
2. **电池AP端已确认是PMIC GLINK服务。** 本轮运行时绑定为`pmic_glink`与`qti_battery_charger`，静态DT指向ADSP charger_pd；[运行时驱动绑定](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:45)。GLINK debug 树还列出 SMB1394/SMB1398，但这些是 debug 描述，不能当作实际充电拓扑或 fuel gauge 型号结论。`idt9418.bin` 也只是无线充电固件属性，不能单凭它确认芯片实装。证据：[电池/充电 DT](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md:126)。应先实现只读遥测，再确认远端状态、通知与原有热/充电策略；这是依赖关系判断，未在本轮验证协议。
3. **现有 UEFI 厂商模块不等于 XNU runtime 驱动。** 本地有 PmicDxe/PmicGlinkDxe/ChargerExDxe/QcomChargerDxe，但 `DXE.inc` 用 `MEIZU_VENDOR_HW == 1` 包围，当前 board.toml 值是0；显示厂商驱动也关闭。[DXE.inc:100](<nova-root>/uefi-sheng-nova/Platforms/Meizu/meizu20proPkg/Include/DXE.inc:100)；[board.toml:147](<nova-root>/unixnu-meizu20pro-nova/board.toml:147)。旧 bring-up 记录还写出 EnvDxe/config HOB 依赖未解决。[bring-up-status.md:145](<nova-root>/unixnu-meizu20pro-nova/docs/bring-up-status.md:145)。
4. **PMGR 名称容易误导。** 当前 `sheng-pmgr=2` 只满足 AMFI 的 ApplePMGRNub 查找；provider不start，不声明clocks/sleep/reset成功，AON读回值固定全1。[ShengPlatformPower.cpp:2](<nova-root>/unixnu-meizu20pro-nova/package/ShengPlatformPower.cpp:2)；[ShengPlatformPower.cpp:170](<nova-root>/unixnu-meizu20pro-nova/package/ShengPlatformPower.cpp:170)。ADT明确 `sleep-disabled=1`，因此还没有可用休眠/恢复。[make-meizu-adt.py:232](<nova-root>/unixnu-meizu20pro-nova/scripts/make-meizu-adt.py:232)。
5. **触控板级数据有需要解释的差异。** 保存的DT TLMM总区域起点为 `0x0f000000`，代码 `MTB_TLMM_BASE=0x0f100000`，并从此基址计算 GPIO MMIO；可能涉及硬件偏移/分区，不能不核对就断言哪一方错误。[DT TLMM与Goodix](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md:225)；[MeizuTouchBoard.h:44](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchBoard.h:44)；[MeizuTouchProbe.cpp:312](<nova-root>/unixnu-meizu20pro-nova/package/MeizuTouchProbe.cpp:312)。DT含空属性`goodix,qts_en`与`vm_mode`/`pvm`；空布尔属性本身不能证明QTS关闭，应核对实际vendor驱动解析与Android日志，不能仅复述README的推断。[DT QTS属性](<nova-root>/artifacts-nova/hackios-port-review-nova/meizu-hardware-baseline-nova.md:225)；[README.md:49](<nova-root>/unixnu-meizu20pro-nova/README.md:49)。
6. **“代码存在”“归档生成”“本次镜像链接”“硬件成功”要分开。** TOUCH archive builder可能运行，但只有`MEIZU_TOUCH=1`才将touch+HID归档force_load；当前选择关闭。Adreno同样有配置门。[build-xnu-4k-baseline.sh:622](<nova-root>/unixnu-main-nova/scripts/build-xnu-4k-baseline.sh:622)；[build-xnu-4k-baseline.sh:663](<nova-root>/unixnu-main-nova/scripts/build-xnu-4k-baseline.sh:663)。本报告没有用旧对象/库存在替代链接或硬件验证。

## 剩余最小补采方案（本子任务未执行设备操作）

主代理已完成第一轮Android只读枚举：Wi-Fi PCI ID/driver/Kiwi固件路径、电池GLINK driver和遥测存在性已有证据，BT class查询为空。[第一轮枚举](<nova-root>/artifacts-nova/hackios-port-review-nova/android-hardware-observation-nova.json:27)。下面只补尚缺部分；复用现有ADB/日志通道前由主代理核对当时授权与设备状态。

| 要补的信息 | 最小只读来源 | 接受标准 |
|---|---|---|
| Wi-Fi芯片revision/board ID（总线与PCI ID已完成） | 现有CNSS启动日志中chip/board/firmware版本白名单字段；必要时核对只读PCI revision与driver版本 | 保留已确认WCN7850/`17CB:1107`/RC0/Kiwi，进一步确定硬件revision与board data版本；无需收集MAC、SSID、扫描历史 |
| 无线固件与依赖 | 只列实际使用的`/vendor/firmware*`、`/vendor/etc/wifi`文件名/长度/哈希；读取加载模块清单中的CNSS/WLAN/MHI/PCIe名称；读取对应vendor驱动source版本 | 固件、board data、校准/NVM职责可区分；私有校准内容保留本地，不纳入报告 |
| BT芯片和传输 | vendor bluetooth SOC属性；BT HAL/init配置里UART设备名；`/sys/class/bluetooth/hci*/device`链接（若存在）；已有BT初始化日志中的chip/ROM/patch版本 | 明确是否GENI UART@898000、HCI/H4/IBS流控/唤醒方案以及实际firmware；不能只看到bluetoothd进程 |
| 电池/充电拓扑 | GLINK与qti_battery_charger绑定已确认；补字段单位/通知机制、远端charger_pd健康状态与vendor协议源码，白名单读`capacity/status/voltage_now/current_now/temp/charge_full/charge_full_design/technology`；PMIC GLINK/charger_pd现有启动日志；vendor电源模块名 | AP端GLINK已确认；继续查远端fuel-gauge/charger拓扑并明确单位、有效性、更新通知；不读取电池型号长标识或序列号 |
| 触控运行状态 | Android driver链接、Goodix version/firmware/config与已启动的I²C adapter号，现有regulator summary中两条touch rails，TLMM region的vendor源码或只读资源信息 | 解释TLMM基址与QTS属性，确认GT9916/实际IC版本和电源状态；不做I²C扫描或寄存器写入 |
| 显示/GPU | 当前panel mode/compatible、KGSL GPU chip_id/gpu_model、固件名、实际GOP handoff/UEFI boot日志 | 区分物理1440×3200与当前1080×2400显示；确认GPU revision与电源/固件依赖；不改显示模式 |

第一轮已拿到的PCIe/GLINK绑定使主线明确；剩余交付只需driver/firmware版本、远端电源协议与UART绑定。它们用于细化 Wi-Fi/BT/电池驱动分别在哪一层接入。后续实施顺序建议：保留已验证的软件显示/UFS基线；先补总线与电源服务的可观测性；再分别做BT HCI版本读取、Wi-Fi PCIe/固件握手、电池只读遥测；最终才接iOS服务接口和休眠/充电策略。这是根据当前缺口提出的工程计划，未声称已完成。

## 静态 DT 证据摘录

唯一输入：`<nova-root>/inputs-nova/meizu20pro-dt.dtb`，SHA-256 `a28fd23de748395474cc8723272854c61883dcd06d19aa85286045164fddf720`。`inputs-nova/meizu20pro.dtb`本轮只读计算得到相同哈希。源为二进制没有文本行号；下面由本轮`dtc -I dtb -O dts`输出筛选，括号内DTS行号对应完整反编译输出，路径和属性值保持原样。报告行号可作为可点击证据；未保存额外DTS文件。未抽取chosen中的设备身份/启动私有参数。

### SoC / GPU

```text
/  (DTS:3)
  [DTS:4] model = "Meizu M2391(kalama MTP) meizu,board-id = <0x2 0xf>";
  [DTS:5] compatible = "qcom,kalama-mtp", "qcom,kalama", "qcom,mtp";
  [DTS:11] meizu,board-id = <0x02 0x0f>;
/soc/qcom,kgsl-3d0@3d00000  (DTS:14948)
  [DTS:14958] clock-names = "gcc_gpu_memnoc_gfx", "gcc_gpu_snoc_dvm_gfx", "gpu_cc_ahb", "apb_pclk";
  [DTS:14961] interrupts = <0x00 0x12c 0x04>;
  [DTS:14962] reg-names = "kgsl_3d0_reg_memory", "cx_dbgc", "rscc", "isense_cntl", "cx_misc", "qdss_gfx", "qdss_etr", "qdss_tmc";
  [DTS:14963] reg = <0x3d00000 0x40000 0x3d61000 0x800 0x3d50000 0x10000 0x3d8b000 0x2000 0x3d9e000 0x1000 0x10900000 0x80000 0x10048000 0x8000 0x10b05000 0x1000>;
  [DTS:14964] status = "ok";
  [DTS:14965] compatible = "qcom,adreno-gpu-gen7-2-1", "qcom,kgsl-3d0";
```

### Wi-Fi candidates

```text
/soc/qcom,cnss-qca-converged  (DTS:29792)
  [DTS:29794] qcom,wlan-sw-ctrl-gpio = <0xcd 0x53 0x00>;
  [DTS:29795] qcom,converged-dt;
  [DTS:29796] reg-names = "smmu_iova_ipa";
  [DTS:29797] reg = <0xb0000000 0x10000>;
  [DTS:29798] compatible = "qcom,cnss-qca-converged";
/soc/qcom,cnss-qca-converged/chip_cfg@0  (DTS:29800)
  [DTS:29805] interconnect-names = "pcie_to_memnoc", "memnoc_to_ddr";
  [DTS:29817] qcom,vreg_ol_cpr = "s2g";
  [DTS:29818] mboxes = <0x63 0x00>;
  [DTS:29819] use-pm-domain;
  [DTS:29820] use-nv-mac;
  [DTS:29825] qcom,wlan-rc-num = <0x01>;
  [DTS:29830] qcom,sw-ctrl-gpio = <0xcd 0x52 0x00>;
  [DTS:29831] qcom,bt-en-gpio = <0xcd 0x51 0x00>;
  [DTS:29832] wlan-en-gpio = <0xcd 0x50 0x00>;
  [DTS:29833] supported-ids = <0x1103>;
/soc/qcom,cnss-qca-converged/chip_cfg@1  (DTS:29836)
  [DTS:29843] interconnect-names = "pcie_to_memnoc", "memnoc_to_ddr";
  [DTS:29857] mboxes = <0x63 0x00>;
  [DTS:29858] use-pm-domain;
  [DTS:29862] qcom,wlan-rc-num = <0x00>;
  [DTS:29867] qcom,sw-ctrl-gpio = <0xcd 0x52 0x00>;
  [DTS:29868] qcom,bt-en-gpio = <0xcd 0x51 0x00>;
  [DTS:29869] wlan-en-gpio = <0xcd 0x50 0x00>;
  [DTS:29870] supported-ids = <0x1107>;
```

### Bluetooth candidates / enabled UART

```text
/soc/qcom,qupv3_2_geni_se@8c0000/qcom,qup_uart@898000  (DTS:21423)
  [DTS:21424] compatible = "qcom,msm-geni-serial-hs";
  [DTS:21425] reg = <0x898000 0x4000>;
  [DTS:21427] interrupts-extended = <0x01 0x00 0x1cd 0x04 0xcd 0x4f 0x04>;
  [DTS:21428] clock-names = "se-clk";
  [DTS:21437] qcom,wakeup-byte = <0xfd>;
  [DTS:21438] status = "ok";
/soc/bt-qca-converged  (DTS:27159)
  [DTS:27161] qcom,wlan-sw-ctrl-gpio = <0xcd 0x53 0x00>;
  [DTS:27162] qcom,converged-dt;
  [DTS:27163] compatible = "qcom,bt-qca-converged";
/soc/bt-qca-converged/bt_kiwi  (DTS:27165)
  [DTS:27179] mboxes = <0x63 0x00>;
  [DTS:27180] qcom,bt-sw-ctrl-gpio = <0xcd 0x52 0x00>;
  [DTS:27181] qcom,wl-reset-gpio = <0xcd 0x50 0x00>;
  [DTS:27182] qcom,bt-reset-gpio = <0xcd 0x51 0x00>;
  [DTS:27185] compatible = "qcom,kiwi";
/soc/bt-qca-converged/bt_qca6490  (DTS:27188)
  [DTS:27202] mboxes = <0x63 0x00>;
  [DTS:27203] qcom,bt-sw-ctrl-gpio = <0xcd 0x52 0x00>;
  [DTS:27204] qcom,wl-reset-gpio = <0xcd 0x50 0x00>;
  [DTS:27205] qcom,bt-reset-gpio = <0xcd 0x51 0x00>;
  [DTS:27208] compatible = "qcom,qca6490";
```

### PMIC / GLINK / charger

```text
/soc/qcom,spmi@c42d000/qcom,pm8550@1  (DTS:13516)
  [DTS:13517] compatible = "qcom,spmi-pmic";
  [DTS:13518] reg = <0x01 0x00>;
/soc/qcom,spmi@c42d000/qcom,pm8550b@7  (DTS:13826)
  [DTS:13827] compatible = "qcom,spmi-pmic";
  [DTS:13828] reg = <0x07 0x00>;
/soc/qcom,spmi@c42d000/qcom,pm8550ve_f@5  (DTS:14059)
  [DTS:14060] compatible = "qcom,spmi-pmic";
  [DTS:14061] reg = <0x05 0x00>;
/soc/qcom,spmi@c42d000/qcom,pm8550vs@2  (DTS:14086)
  [DTS:14087] compatible = "qcom,spmi-pmic";
  [DTS:14088] reg = <0x02 0x00>;
/soc/qcom,spmi@c42d000/qcom,pm8550vs@3  (DTS:14114)
  [DTS:14115] compatible = "qcom,spmi-pmic";
  [DTS:14116] reg = <0x03 0x00>;
/soc/qcom,spmi@c42d000/qcom,pm8550vs@4  (DTS:14157)
  [DTS:14158] compatible = "qcom,spmi-pmic";
  [DTS:14159] reg = <0x04 0x00>;
/soc/qcom,spmi@c42d000/qcom,pm8550vs@6  (DTS:14186)
  [DTS:14187] compatible = "qcom,spmi-pmic";
  [DTS:14188] reg = <0x06 0x00>;
/soc/qcom,spmi@c42d000/qcom,pm8010@c  (DTS:14672)
  [DTS:14673] compatible = "qcom,spmi-pmic";
  [DTS:14674] reg = <0x0c 0x00>;
/soc/qcom,spmi@c42d000/qcom,pm8010@d  (DTS:14688)
  [DTS:14689] compatible = "qcom,spmi-pmic";
  [DTS:14690] reg = <0x0d 0x00>;
/soc/qcom,spmi-debug@10b14000/qcom,pm8550-debug@1  (DTS:14744)
  [DTS:14745] compatible = "qcom,spmi-pmic";
  [DTS:14746] reg = <0x01 0x00>;
/soc/qcom,spmi-debug@10b14000/qcom,pm8550vs-debug@2  (DTS:14752)
  [DTS:14753] compatible = "qcom,spmi-pmic";
  [DTS:14754] reg = <0x02 0x00>;
/soc/qcom,spmi-debug@10b14000/qcom,pm8550vs-debug@3  (DTS:14760)
  [DTS:14761] compatible = "qcom,spmi-pmic";
  [DTS:14762] reg = <0x03 0x00>;
/soc/qcom,spmi-debug@10b14000/qcom,pm8550vs-debug@4  (DTS:14768)
  [DTS:14769] compatible = "qcom,spmi-pmic";
  [DTS:14770] reg = <0x04 0x00>;
/soc/qcom,spmi-debug@10b14000/qcom,pm8550ve-debug@5  (DTS:14776)
  [DTS:14777] compatible = "qcom,spmi-pmic";
  [DTS:14778] reg = <0x05 0x00>;
/soc/qcom,spmi-debug@10b14000/qcom,pm8550vs-debug@6  (DTS:14784)
  [DTS:14785] compatible = "qcom,spmi-pmic";
  [DTS:14786] reg = <0x06 0x00>;
/soc/qcom,spmi-debug@10b14000/qcom,pm8550b-debug@7  (DTS:14792)
  [DTS:14793] compatible = "qcom,spmi-pmic";
  [DTS:14794] reg = <0x07 0x00>;
/soc/qcom,pmic_glink  (DTS:14853)
  [DTS:14854] compatible = "qcom,pmic-glink";
  [DTS:14855] qcom,pmic-glink-channel = "PMIC_RTR_ADSP_APPS";
  [DTS:14856] qcom,subsys-name = "lpass";
  [DTS:14857] qcom,protection-domain = "tms/servreg", "msm/adsp/charger_pd";
/soc/qcom,pmic_glink/qcom,battery_charger  (DTS:14860)
  [DTS:14861] compatible = "qcom,battery-charger";
  [DTS:14863] qcom,wireless-fw-name = "idt9418.bin";
  [DTS:14865] qcom,display-panels = <0x747>;
/soc/qcom,pmic_glink/qcom,ucsi  (DTS:14868)
  [DTS:14869] compatible = "qcom,ucsi-glink";
/soc/qcom,pmic_glink/qcom,altmode  (DTS:14884)
  [DTS:14885] compatible = "qcom,altmode-glink";
/soc/qcom,pmic_glink_log  (DTS:14891)
  [DTS:14892] compatible = "qcom,pmic-glink";
  [DTS:14893] qcom,pmic-glink-channel = "PMIC_LOGS_ADSP_APPS";
/soc/qcom,pmic_glink_log/qcom,battery_debug  (DTS:14895)
  [DTS:14896] compatible = "qcom,battery-debug";
/soc/qcom,pmic_glink_log/qcom,charger_ulog_glink  (DTS:14899)
  [DTS:14900] compatible = "qcom,charger-ulog-glink";
/soc/qcom,pmic_glink_log/qcom,spmi_glink_debug  (DTS:14903)
  [DTS:14904] compatible = "qcom,spmi-glink-debug";
/soc/qcom,pmic_glink_log/qcom,spmi_glink_debug/spmi@0  (DTS:14909)
  [DTS:14910] reg = <0x00>;
/soc/qcom,pmic_glink_log/qcom,spmi_glink_debug/spmi@0/qcom,pm8550b-debug@7  (DTS:14914)
  [DTS:14915] compatible = "qcom,spmi-pmic";
  [DTS:14916] reg = <0x07 0x00>;
/soc/qcom,pmic_glink_log/qcom,spmi_glink_debug/spmi@1  (DTS:14921)
  [DTS:14922] reg = <0x01>;
/soc/qcom,pmic_glink_log/qcom,spmi_glink_debug/spmi@1/qcom,smb1394-debug@9  (DTS:14926)
  [DTS:14927] compatible = "qcom,spmi-pmic";
  [DTS:14928] reg = <0x09 0x00>;
/soc/qcom,pmic_glink_log/qcom,spmi_glink_debug/spmi@1/qcom,smb1394-debug@b  (DTS:14933)
  [DTS:14934] compatible = "qcom,spmi-pmic";
  [DTS:14935] reg = <0x0b 0x00>;
/soc/qcom,pmic_glink_log/qcom,spmi_glink_debug/spmi@1/qcom,smb1398-debug@d  (DTS:14939)
  [DTS:14940] compatible = "qcom,spmi-pmic";
  [DTS:14941] reg = <0x0d 0x00>;
/soc/xcharge_notify_subsys  (DTS:30031)
  [DTS:30032] compatible = "xcharge,notify_subsys";
/soc/mz_mbattery_logger  (DTS:30041)
  [DTS:30042] compatible = "xcharge,mbattery_logger";
/soc/xcharge_chg_interface  (DTS:30046)
  [DTS:30047] compatible = "xcharge,xcharge_chg_interface";
/soc/xcharge_chg_manager  (DTS:30051)
  [DTS:30052] compatible = "xcharge,xcharge_chg_manager";
```

### TLMM / touch / panel

```text
/soc/pinctrl@f000000  (DTS:2935)
  [DTS:2936] compatible = "qcom,kalama-pinctrl-v2";
  [DTS:2937] reg = <0xf000000 0x1000000>;
/soc/qcom,qupv3_1_geni_se@ac0000/i2c@a90000  (DTS:20910)
  [DTS:20911] compatible = "qcom,i2c-geni";
  [DTS:20912] reg = <0xa90000 0x4000>;
  [DTS:20925] status = "ok";
  [DTS:20928] qcom,clk-freq-out = <0xf4240>;
/soc/qcom,qupv3_1_geni_se@ac0000/i2c@a90000/goodix-berlin@5d  (DTS:20930)
  [DTS:20931] compatible = "goodix,brl-d";
  [DTS:20932] reg = <0x5d>;
  [DTS:20935] goodix,reset-gpio = <0xcd 0x18 0x00>;
  [DTS:20936] goodix,irq-gpio = <0xcd 0x19 0x2008>;
  [DTS:20938] goodix,panel-max-x = <0x5a0>;
  [DTS:20939] goodix,panel-max-y = <0xc80>;
  [DTS:20942] goodix,firmware-name = "goodix_firmware.bin";
  [DTS:20943] goodix,config-name = "goodix_cfg_group.bin";
  [DTS:20946] avdd-supply = <0x2f1>;
  [DTS:20947] iovdd-supply = <0x2d>;
  [DTS:20949] goodix,qts_en;
  [DTS:20950] qts,trusted-touch-mode = "vm_mode";
  [DTS:20951] qts,touch-environment = "pvm";
  [DTS:20958] goodix,touch-environment = "pvm";
/soc/qcom,mdss_mdp@ae00000/qcom,mdss_dsi_meizu_amoled_samsung_cmd  (DTS:35101)
  [DTS:35111] qcom,mdss-dsi-bl-pmic-control-type = "bl_ctrl_dcs";
  [DTS:35148] qcom,mdss-dsi-panel-physical-type = "oled";
  [DTS:35149] qcom,mdss-dsi-panel-type = "dsi_cmd_mode";
  [DTS:35150] qcom,mdss-dsi-panel-name = "meizu amoled cmd mode dsi samsung panel with DSC";
```
