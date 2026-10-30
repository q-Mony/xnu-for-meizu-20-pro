// SPDX-License-Identifier: MIT
#ifndef M2391_USB_READINESS_REGS_H
#define M2391_USB_READINESS_REGS_H
/* m2391 kernel dfc9fd58332e3f44402c7d05a260ff89bd6dc36a:
 * arch/arm64/boot/dts/meizu/m2391.dts:8095,19872 (GCC, USB GDSC)
 * drivers/clk/qcom/gcc-kalama.c:1734,1844,3410-3457 (five CBCRs)
 * drivers/clk/qcom/clk-branch.c:47 (CLK_OFF); .c:100 (HWCG caveat)
 * drivers/clk/qcom/gdsc.c:20 (PWR_ON bit, treated only as a raw hint)
 * USB dependency nodes agree with the local DTB: usb-log-research/REPORT.md.
 * Offsets here are relative to the single mapped GCC page, NOT GCC base.
 */
#define USB_GCC_WINDOW_PA   0x00139000UL
#define USB_GCC_WINDOW_SIZE 0x1000UL
#define USB_GDSC_OFFSET     0x004U
#define USB_MASTER_OFFSET   0x018U
#define USB_SLEEP_OFFSET    0x024U
#define USB_UTMI_OFFSET     0x028U
#define USB_CFG_AXI_OFFSET  0x088U
#define USB_AGGR_AXI_OFFSET 0x08cU
#define USB_GDSC_PWR_ON     (1U << 31)
#define USB_BRANCH_ENABLE  (1U << 0)
#define USB_BRANCH_HWCG    (1U << 1)
#define USB_BRANCH_OFF     (1U << 31)
#endif
