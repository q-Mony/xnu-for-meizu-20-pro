// SPDX-License-Identifier: BSD-2-Clause-Patent
// Meizu 20 Pro (SM8550) touch board facts and the pure helpers around them. Every constant states its source.
// Nothing here touches hardware; MeizuTouchProbe.cpp owns the MMIO.
#ifndef MEIZU_TOUCH_BOARD_H
#define MEIZU_TOUCH_BOARD_H
#include <stdint.h>

// Board DT (/soc/qcom,qupv3_1_geni_se@ac0000/i2c@a90000): engine reg 0xa90000 size 0x4000, QUP1 wrapper 0xac0000.
#define MTB_QUP_WRAPPER_BASE 0x00ac0000ULL
#define MTB_SE_BASE 0x00a90000ULL
#define MTB_SE_SIZE 0x4000ULL
// sm8550.dtsi: geniqup@ac0000 has iommus = <&apps_smmu 0xa3 0>; apps_smmu@15000000 (same geometry as sheng).
#define MTB_QUP_SMMU_SID 0xa3
#define MTB_APPS_SMMU_BASE 0x15000000ULL
#define MTB_APPS_SMMU_SIZE 0x000d0000ULL
// GCC at 0x100000 (clock-controller@100000); QUP1 = qupv3_wrap1. gcc-sm8550.c vote/halt blocks, as used by sheng.
#define MTB_GCC_BASE 0x00100000ULL
#define MTB_GCC_VOTE_ENABLE 0x52008
#define MTB_GCC_GPLL0_ENABLE 0x52018
#define MTB_GCC_GPLL0_MODE 0x0
#define MTB_VOTE_WRAP1_CORE_2X (1u << 18)     // halt 0x23294
#define MTB_VOTE_WRAP1_CORE (1u << 19)        // halt 0x23284
#define MTB_VOTE_WRAP1_M_AHB (1u << 20)       // halt 0x2327c
#define MTB_VOTE_WRAP1_S_AHB (1u << 21)       // halt 0x23280
#define MTB_VOTE_WRAP1_S4 (1u << 26)          // qup1_se4_clk, halt 0x184e4
#define MTB_VOTE_ALL (MTB_VOTE_WRAP1_CORE_2X | MTB_VOTE_WRAP1_CORE | MTB_VOTE_WRAP1_M_AHB | \
                      MTB_VOTE_WRAP1_S_AHB | MTB_VOTE_WRAP1_S4)
#define MTB_HALT_CORE_2X 0x23294
#define MTB_HALT_CORE 0x23284
#define MTB_HALT_M_AHB 0x2327c
#define MTB_HALT_S_AHB 0x23280
#define MTB_HALT_S4 0x184e4
#define MTB_CBCR_CLK_OFF (1u << 31)
// qup1_se4_clk_src RCG in DFS mode (clk-rcg2.c register layout, as read on sheng's SM8550).
#define MTB_S4_RCG 0x184f0
#define MTB_RCG_CMD 0x0
#define MTB_RCG_CFG 0x4
#define MTB_RCG_DFSR_CMD 0x14                 // bit0 DFS enable, bits 4:1 current level
#define MTB_RCG_PERF_CFG(l) (0x1c + 4 * (l))
#define MTB_RCG_PERF_M(l) (0x5c + 4 * (l))
#define MTB_RCG_PERF_N(l) (0x9c + 4 * (l))
#define MTB_DFS_LEVELS 8

// TLMM: pinctrl@f100000. ctl_reg = 0x1000 * gpio; +0x4 IN_OUT, +0x8 INTR_CFG, +0xc INTR_STATUS.
#define MTB_TLMM_BASE 0x0f100000ULL
#define MTB_GPIO_STRIDE 0x1000
#define MTB_GPIO_I2C_SDA 44                   // qupv3_se4_i2c_sda_active: gpio44, function qup1_se4_l0
#define MTB_GPIO_I2C_SCL 45                   // qupv3_se4_i2c_scl_active: gpio45, function qup1_se4_l1
#define MTB_GPIO_TS_RESET 24                  // goodix,reset-gpio = <&tlmm 24 0>, active low
#define MTB_GPIO_TS_IRQ 25                    // goodix,irq-gpio = <&tlmm 25 0x2008> (level low, one-shot)
// pinctrl-sm8550.c: [44] = PINGROUP(44, qup1_se4, ...), [45] = PINGROUP(45, qup1_se4, ...); gpio is mux 0.
#define MTB_FUNC_QUP1_SE4 1
#define MTB_FUNC_GPIO 0
// drive-strength = <2> on the I2C pins, <8> on reset/irq; the TLMM field is (mA / 2) - 1.
#define MTB_DRIVE_2MA 0
#define MTB_DRIVE_8MA 3
// Pull field bits 11:10: 0 none, 1 down, 2 keeper, 3 up. The DT asks for bias-pull-up on 44/45 and on 24/25.
#define MTB_PULL_UP 3
#define MTB_PIN_OUTPUT_ENABLE (1u << 9)
// All TLMM pin interrupts share summary SPI 208 (sm8550.dtsi tlmm interrupts), INTID 240.
#define MTB_TLMM_SPI 208
// INTR_CFG: bit0 enable, bit1 polarity, bits3:2 detection (0 level, 1 rising, 2 falling, 3 both), bit4 raw status,
// bits7:5 target (3 = application processor).
#define MTB_INTR_RAW_STATUS (1u << 4)
#define MTB_INTR_TARGET_AP (3u << 5)
#define MTB_INTR_DETECT_FALLING (2u << 2)

// Goodix Berlin D on i2c@a90000 at 1 MHz (qcom,clk-freq-out = <1000000>).
#define MTB_I2C_ADDR 0x5d
#define MTB_I2C_HZ 1000000u
#define MTB_PANEL_MAX_X 1440                  // goodix,panel-max-x = <0x5a0>
#define MTB_PANEL_MAX_Y 3200                  // goodix,panel-max-y = <0xc80>
// NativeHidInput maps the published set from these maxima to the digitizer's 0..32767 range.
#define MTB_HID_MAX_X 30480
#define MTB_HID_MAX_Y 20320

// Power. The board DT has no always-on/boot-on on either supply: Android's touch driver is the consumer
// (regulator_summary: 5-005d-avdd on pm_humu_l9 at 2960 mV, 5-005d-iovdd on pm_humu_l12 at 1800 mV), so
// XNU has to place the same votes itself or the controller has no rails when it leaves reset.
#define MTB_RAIL_AVDD "ldob9"                 // /sys/kernel/debug/regulator/regulator_summary
#define MTB_RAIL_AVDD_MV 2960
#define MTB_RAIL_IOVDD "ldob12"
#define MTB_RAIL_IOVDD_MV 1800
// DT /soc/apps_rsc@17a00000: drv-0 0x17a00000, drv-1 0x17a10000, drv-2 0x17a20000 (reg-names), and
// drv@2 carries qcom,drv-id = <2> with qcom,tcs-offset = <0xd00>. The AP's active votes use DRV 2.
#define MTB_RSC_BASE 0x17a20000ULL
#define MTB_RSC_SIZE 0x10000ULL
#define MTB_RSC_TCS_OFFSET 0xd00u
// DT /reserved-memory/aop_cmd_db_region@81c60000, reg = <0 0x81c60000 0 0x20000>.
#define MTB_CMD_DB_BASE 0x81c60000ULL
#define MTB_CMD_DB_SIZE 0x20000ULL

static inline uint32_t mtb_tlmm_ctl(unsigned gpio) { return (uint32_t)gpio * MTB_GPIO_STRIDE; }
static inline uint32_t mtb_tlmm_io(unsigned gpio) { return (uint32_t)gpio * MTB_GPIO_STRIDE + 4; }
static inline uint32_t mtb_tlmm_intr_cfg(unsigned gpio) { return (uint32_t)gpio * MTB_GPIO_STRIDE + 8; }
static inline uint32_t mtb_tlmm_intr_status(unsigned gpio) { return (uint32_t)gpio * MTB_GPIO_STRIDE + 0xc; }
// bits 5:2 function, 8:6 drive, 9 output enable, 11:10 pull.
// TLMM GPIO_CFG: bits 2:0 function, 5:4 drive strength, 7:6 pull, 9 output enable.
static inline uint32_t mtb_pin_cfg(unsigned func, unsigned drive_code, unsigned pull) {
    return ((func & 15u) << 2) | ((drive_code & 7u) << 4) | ((pull & 3u) << 6);
}

// Source clock rates by RCG source-select value (gcc_parent_map_0: 0 = TCXO 19.2 MHz, 1 = GPLL0 main, 6 = GPLL0 even).
static inline uint64_t mtb_src_hz(unsigned sel) {
    return sel == 0 ? 19200000ULL : sel == 1 ? 600000000ULL : sel == 6 ? 300000000ULL : 0;
}
// clk-rcg2.c calc_rate: half-integer divider, then M/N.
static inline uint64_t mtb_calc_rate(uint64_t prate, uint32_t m, uint32_t n, unsigned mode, unsigned hid) {
    if (hid) { prate *= 2; prate /= hid + 1; }
    if (mode && n) { prate *= m; prate /= n; }
    return prate;
}
// Rate of DFS level l from its CFG (src 10:8, mode 13:12, hid 4:0), M register and N register (stored inverted,
// n = (~N & mask) + m, mnd_width 16).
static inline uint64_t mtb_dfs_level_hz(uint32_t cfg, uint32_t mreg, uint32_t nreg) {
    unsigned src = (cfg >> 8) & 7, mode = (cfg >> 12) & 3, hid = cfg & 0x1f;
    uint32_t m = 0, n = 0;
    if (mode) { m = mreg & 0xffff; n = ((~nreg) & 0xffff) + m; }
    return mtb_calc_rate(mtb_src_hz(src), m, n, mode, hid);
}

// Pick the DFS level that GENI_CLK_SEL should select for an I2C clock derived from a 19.2 MHz source. Returns 0 on
// success, -1 when no level is within 1% of 19.2 MHz (the caller then leaves the level the bootloader chose).
static inline int mtb_choose_xo_level(const uint64_t *level_hz, unsigned n, unsigned *out) {
    for (unsigned l = 0; l < n; ++l) {
        uint64_t f = level_hz[l];
        if (f >= 19200000ULL - 192000ULL && f <= 19200000ULL + 192000ULL) { *out = l; return 0; }
    }
    return -1;
}
#endif
