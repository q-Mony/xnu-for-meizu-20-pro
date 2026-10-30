// SPDX-License-Identifier: MIT
// m2391 GCC-only USB readiness snapshot. See README.md for provenance/limits.
// No controller/PHY access is implemented, even if every GCC hint is positive.
#include <stdint.h>
#ifndef M2391_USB_PROBE_HOST_TEST
#include <arm/machine_routines.h>
#include <vm/memory_types.h>
#include <kern/startup.h>
#include <mach/vm_param.h>
#include <mach/vm_prot.h>
#include <pexpert/pexpert.h>
#else
#include "probe_test_platform.h"
#endif
#include "UsbReadinessRegs.h"

static unsigned int probe_ran;

static uint32_t
probe_read32(vm_offset_t base, unsigned int offset)
{
#ifdef M2391_USB_PROBE_HOST_TEST
	return probe_test_read32(base, offset);
#else
	uint32_t value = *(volatile const uint32_t *)(base + offset);
	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
#endif
}

static const char *
branch_hint(uint32_t value, unsigned int hwcg)
{
	if (value == UINT32_MAX) {
		return "invalid-all-ones";
	}
	if (!(value & USB_BRANCH_ENABLE)) {
		return "no-local-enable";
	}
	/* A hardware-gated/voted branch is not proven running by its vote. */
	if (hwcg && (value & USB_BRANCH_HWCG)) {
		return "hw-gated-unknown";
	}
	return (value & USB_BRANCH_OFF) ? "off" : "running-hint";
}

static void
meizu_usb_readiness_probe(void)
{
	static const struct {
		const char *name;
		unsigned int offset;
		unsigned int hwcg;
	} branches[] = {
		{ "master", USB_MASTER_OFFSET, 0 },
		{ "cfg-axi", USB_CFG_AXI_OFFSET, 1 },
		{ "aggr-axi", USB_AGGR_AXI_OFFSET, 1 },
		{ "mock-utmi", USB_UTMI_OFFSET, 0 },
		{ "sleep", USB_SLEEP_OFFSET, 0 },
	};
	uint64_t enabled = 0;
	vm_offset_t base;
	uint32_t gdsc, values[5];
	unsigned int i;

	if (probe_ran) {
		return;
	}
	probe_ran = 1;
	if (!PE_parse_boot_argn("meizu-usb-probe", &enabled, sizeof(enabled)) ||
	    enabled != 1) {
		return;
	}
	kprintf("M2391_USB: GCC-only snapshot; controller/PHY access disabled\n");
	/* This board build uses 4 KiB pages. Do not widen the mapping elsewhere. */
	if (PAGE_SIZE != USB_GCC_WINDOW_SIZE) {
		kprintf("M2391_USB: skipped: requires 4KiB kernel pages\n");
		return;
	}
	kprintf("M2391_USB: reading GCC 0x139000 only; PHY rails/repeater/role unknown\n");
	/* Device memory, CPU read-only, removable kernel_map allocation. GCC is
	 * the SoC clock control plane, not the switched USB controller domain.
	 * Mapping does NOT test bus accessibility; see the explicit opt-in limit.
	 */
	base = io_map(USB_GCC_WINDOW_PA, USB_GCC_WINDOW_SIZE,
	    VM_WIMG_IO, VM_PROT_READ, true);
	if (!base) {
		kprintf("M2391_USB: skipped: GCC mapping unavailable\n");
		return;
	}
	gdsc = probe_read32(base, USB_GDSC_OFFSET);
	for (i = 0; i < 5; i++) {
		values[i] = probe_read32(base, branches[i].offset);
	}
	ml_io_unmap(base, USB_GCC_WINDOW_SIZE);

	/* DT sets qcom,support-cfg-gdscr: bit 31 alone is not the full power
	 * handshake. No undocumented adjacent CFG_GDSCR access is added here.
	 */
	kprintf("M2391_USB: gdsc=%08x pwr_on_bit=%u power-ready=unknown (cfg handshake unverified)\n",
	    gdsc, (unsigned int)((gdsc & USB_GDSC_PWR_ON) != 0));
	for (i = 0; i < 5; i++) {
		kprintf("M2391_USB: %s cbcr=%08x enable=%u clk_off=%u hwcg=%u state=%s\n",
		    branches[i].name, values[i],
		    (unsigned int)((values[i] & USB_BRANCH_ENABLE) != 0),
		    (unsigned int)((values[i] & USB_BRANCH_OFF) != 0),
		    (unsigned int)(branches[i].hwcg && (values[i] & USB_BRANCH_HWCG)),
		    branch_hint(values[i], branches[i].hwcg));
	}
	kprintf("M2391_USB: done reads=6; USB-ready=unknown; DWC3/PHY skipped\n");
}

#ifndef M2391_USB_PROBE_HOST_TEST
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, meizu_usb_readiness_probe);
#endif
