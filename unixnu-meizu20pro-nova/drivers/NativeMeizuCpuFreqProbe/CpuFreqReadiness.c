// SPDX-License-Identifier: MIT
// Nova m2391: one explicitly enabled domain0 EPSS raw snapshot at autolog time.
#include "CpuFreqReadiness.h"
#ifndef NOVA_CPUFREQ_HOST_TEST
#include <arm/machine_routines.h>
#include <vm/memory_types.h>
#include <mach/vm_param.h>
#include <mach/vm_prot.h>
#include <pexpert/pexpert.h>
#include <kern/misc_protos.h>
#else
#include "cpufreq_test_platform.h"
#endif

static unsigned int probe_claimed;
static unsigned int snapshot_ready;
static struct nova_cpufreq_snapshot published_snapshot;

static int
probe_space(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
	       ch == '\v' || ch == '\f';
}

static int
probe_bootarg_enabled(const char *args)
{
	static const char key[] = "nova-cpufreq-probe";
	const unsigned int key_len = sizeof(key) - 1U;
	unsigned int length = 0, cursor = 0, count = 0;
	int enabled = 0;

	if (!args) {
		return 0;
	}
	/* ARM64 XNU boot.h sets BOOT_LINE_LENGTH to 1024. Validate the whole
	 * bounded string before accepting any token, including an early '=1'.
	 */
	while (length < NOVA_CPUFREQ_BOOTARGS_LIMIT && args[length] != '\0') {
		length++;
	}
	if (length == NOVA_CPUFREQ_BOOTARGS_LIMIT) {
		return 0;
	}
	while (cursor < length) {
		unsigned int start, size, i;
		while (cursor < length && probe_space(args[cursor])) {
			cursor++;
		}
		start = cursor;
		while (cursor < length && !probe_space(args[cursor])) {
			cursor++;
		}
		size = cursor - start;
		if (size < key_len) {
			continue;
		}
		for (i = 0; i < key_len && args[start + i] == key[i]; i++) {
		}
		if (i != key_len || (size != key_len && args[start + key_len] != '=')) {
			continue;
		}
		/* Count bare keys and keys with invalid values as occurrences too.
		 * No numeric parser: leading zero, signs and overflow are rejected.
		 */
		count++;
		enabled = size == key_len + 2U && args[start + key_len] == '=' &&
		    args[start + key_len + 1U] == '1';
	}
	return count == 1U && enabled;
}

static uint64_t
probe_ticks(void)
{
#ifdef NOVA_CPUFREQ_HOST_TEST
	return cpufreq_test_ticks();
#else
	uint64_t value;
	__asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(value) :: "memory");
	return value;
#endif
}

static uint64_t
probe_timer_hz(void)
{
#ifdef NOVA_CPUFREQ_HOST_TEST
	return cpufreq_test_timer_hz();
#else
	uint64_t value;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
	return value;
#endif
}

static uint64_t
probe_mpidr(void)
{
#ifdef NOVA_CPUFREQ_HOST_TEST
	return cpufreq_test_mpidr();
#else
	uint64_t value;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(value) :: "memory");
	return value;
#endif
}

static uint32_t
probe_read32(vm_offset_t base, unsigned int offset)
{
#ifdef NOVA_CPUFREQ_HOST_TEST
	return cpufreq_test_read32(base, offset);
#else
	uint32_t value = *(volatile const uint32_t *)(base + offset);
	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
#endif
}

static const char *
probe_status_name(uint32_t status)
{
	switch (status) {
	case NOVA_CPUFREQ_SAMPLED: return "sampled";
	case NOVA_CPUFREQ_SKIP_PAGE_SIZE: return "skip-page-size";
	case NOVA_CPUFREQ_SKIP_INTERRUPTS: return "skip-interrupts-disabled";
	case NOVA_CPUFREQ_SKIP_AFFINITY: return "skip-nonboot-affinity";
	case NOVA_CPUFREQ_MAP_UNAVAILABLE: return "map-unavailable";
	case NOVA_CPUFREQ_ENABLE_ALL_ONES: return "enable-all-ones";
	case NOVA_CPUFREQ_DOMAIN_DISABLED: return "domain-disabled";
	default: return "unknown";
	}
}

int
nova_cpufreq_get_snapshot(struct nova_cpufreq_snapshot *out)
{
	if (!out || !__atomic_load_n(&snapshot_ready, __ATOMIC_ACQUIRE)) {
		return 0;
	}
	*out = published_snapshot;
	return 1;
}

void
nova_cpufreq_probe_dump_once(void)
{
	struct nova_cpufreq_snapshot sample = { 0 };
	vm_offset_t base;

	if (!probe_bootarg_enabled(PE_boot_args())) {
		return;
	}
	/* Claiming is distinct from publishing: recursive/concurrent callers
	 * return immediately; readers cannot observe the producer's partial data.
	 */
	if (__atomic_exchange_n(&probe_claimed, 1U, __ATOMIC_ACQ_REL)) {
		return;
	}
	sample.sample_id = 1;
	sample.sample_begin_ticks = probe_ticks();
	sample.timer_hz = probe_timer_hz();
	sample.mpidr_before = probe_mpidr();
	if (PAGE_SIZE != NOVA_CPUFREQ_WINDOW_SIZE) {
		sample.status = NOVA_CPUFREQ_SKIP_PAGE_SIZE;
		goto publish;
	}
	if (!ml_get_interrupts_enabled()) {
		sample.status = NOVA_CPUFREQ_SKIP_INTERRUPTS;
		goto publish;
	}
	if ((sample.mpidr_before & NOVA_CPUFREQ_AFFINITY_MASK) != 0) {
		sample.status = NOVA_CPUFREQ_SKIP_AFFINITY;
		goto publish;
	}
	/* Read-only, removable device mapping. Mapping success is NOT proof of
	 * bus accessibility. An external abort or a load that never returns is
	 * not recoverable by this software read-count bound. No ml_probe_read:
	 * this XNU implements it as panic; ml_nofault_copy is not MMIO recovery.
	 */
	base = io_map(NOVA_CPUFREQ_WINDOW_PA, NOVA_CPUFREQ_WINDOW_SIZE,
	    VM_WIMG_IO, VM_PROT_READ, true);
	if (!base) {
		sample.status = NOVA_CPUFREQ_MAP_UNAVAILABLE;
		goto publish;
	}
	sample.enable_raw = probe_read32(base, NOVA_CPUFREQ_ENABLE_OFFSET);
	sample.read_count++;
	sample.valid_mask |= NOVA_CPUFREQ_VALID_ENABLE;
	if (sample.enable_raw == UINT32_MAX) {
		sample.status = NOVA_CPUFREQ_ENABLE_ALL_ONES;
		goto unmap;
	}
	if (!(sample.enable_raw & 1U)) {
		sample.status = NOVA_CPUFREQ_DOMAIN_DISABLED;
		goto unmap;
	}
	sample.state_raw = probe_read32(base, NOVA_CPUFREQ_STATE_OFFSET);
	sample.read_count++;
	sample.valid_mask |= NOVA_CPUFREQ_VALID_STATE;
	sample.dcvs_raw = probe_read32(base, NOVA_CPUFREQ_DCVS_OFFSET);
	sample.read_count++;
	sample.valid_mask |= NOVA_CPUFREQ_VALID_DCVS;
	sample.request_raw = probe_read32(base, NOVA_CPUFREQ_REQUEST_OFFSET);
	sample.read_count++;
	sample.valid_mask |= NOVA_CPUFREQ_VALID_REQUEST;
	sample.status = NOVA_CPUFREQ_SAMPLED;
unmap:
	ml_io_unmap(base, NOVA_CPUFREQ_WINDOW_SIZE);
publish:
	sample.mpidr_after = probe_mpidr();
	sample.sample_end_ticks = probe_ticks();
	sample.affinity_changed =
	    ((sample.mpidr_before ^ sample.mpidr_after) & NOVA_CPUFREQ_AFFINITY_MASK) != 0;
	/* Reading MPIDR does not pin this thread. Even unchanged endpoints cannot
	 * rule out intervening migration. Values describe domain0 at autolog time,
	 * never the initial CPU frequency or the subsequent lsd waiting interval.
	 */
	published_snapshot = sample;
	__atomic_store_n(&snapshot_ready, 1U, __ATOMIC_RELEASE);
	printf("nova: cpufreq sample_id=%u phase=autolog-once domain=0 status=%s "
	    "begin_ticks=%llu end_ticks=%llu timer_hz=%llu "
	    "mpidr_before=%llx mpidr_after=%llx affinity_changed=%u "
	    "reads=%u valid_mask=%x enable_raw=%08x state_raw=%08x "
	    "dcvs_raw=%08x request_index_raw=%08x cpu_frequency=unknown\n",
	    sample.sample_id, probe_status_name(sample.status),
	    (unsigned long long)sample.sample_begin_ticks,
	    (unsigned long long)sample.sample_end_ticks,
	    (unsigned long long)sample.timer_hz,
	    (unsigned long long)sample.mpidr_before,
	    (unsigned long long)sample.mpidr_after, sample.affinity_changed,
	    sample.read_count, sample.valid_mask, sample.enable_raw, sample.state_raw,
	    sample.dcvs_raw, sample.request_raw);
}
