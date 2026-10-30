// SPDX-License-Identifier: MIT
#ifndef NOVA_CPUFREQ_READINESS_H
#define NOVA_CPUFREQ_READINESS_H

#include <stdint.h>

#define NOVA_CPUFREQ_BOOTARGS_LIMIT 1024U
#define NOVA_CPUFREQ_WINDOW_PA UINT64_C(0x17d91000)
#define NOVA_CPUFREQ_WINDOW_SIZE 0x1000U
#define NOVA_CPUFREQ_AFFINITY_MASK UINT64_C(0xff00ffffff)
#define NOVA_CPUFREQ_ENABLE_OFFSET 0x000U
#define NOVA_CPUFREQ_STATE_OFFSET 0x020U
#define NOVA_CPUFREQ_DCVS_OFFSET 0x0b0U
#define NOVA_CPUFREQ_REQUEST_OFFSET 0x320U

enum nova_cpufreq_status {
	NOVA_CPUFREQ_SAMPLED = 1,
	NOVA_CPUFREQ_SKIP_PAGE_SIZE,
	NOVA_CPUFREQ_SKIP_INTERRUPTS,
	NOVA_CPUFREQ_SKIP_AFFINITY,
	NOVA_CPUFREQ_MAP_UNAVAILABLE,
	NOVA_CPUFREQ_ENABLE_ALL_ONES,
	NOVA_CPUFREQ_DOMAIN_DISABLED
};

enum nova_cpufreq_valid {
	NOVA_CPUFREQ_VALID_ENABLE = 1U << 0,
	NOVA_CPUFREQ_VALID_STATE = 1U << 1,
	NOVA_CPUFREQ_VALID_DCVS = 1U << 2,
	NOVA_CPUFREQ_VALID_REQUEST = 1U << 3
};

struct nova_cpufreq_snapshot {
	uint64_t sample_begin_ticks;
	uint64_t sample_end_ticks;
	uint64_t timer_hz;
	uint64_t mpidr_before;
	uint64_t mpidr_after;
	uint32_t sample_id;
	uint32_t status;
	uint32_t read_count;
	/* A set bit means the raw load completed, not hardware validity. */
	uint32_t valid_mask;
	uint32_t enable_raw;
	uint32_t state_raw;
	uint32_t dcvs_raw;
	uint32_t request_raw;
	uint32_t affinity_changed;
	uint32_t reserved;
};

/* Explicit opt-in, one producer, one immutable publication, one log line.
 * Called from the existing autolog thread; no startup hook or new thread.
 */
void nova_cpufreq_probe_dump_once(void);

/* Returns 1 only after complete publication; otherwise leaves out untouched.
 * Cached reads never perform MMIO or obtain a new sample time. NULL returns 0.
 */
int nova_cpufreq_get_snapshot(struct nova_cpufreq_snapshot *out);

#endif
