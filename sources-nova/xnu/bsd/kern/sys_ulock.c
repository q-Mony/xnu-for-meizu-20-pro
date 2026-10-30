/*
 * Copyright (c) 2015-2020 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#include <machine/atomic.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ioctl.h>
#include <sys/file_internal.h>
#include <sys/proc_internal.h>
#include <sys/kernel.h>
#include <sys/guarded.h>
#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/pthread_shims.h>

#include <mach/mach_types.h>

#include <kern/cpu_data.h>
#include <kern/mach_param.h>
#include <kern/kern_types.h>
#include <kern/assert.h>
#include <kern/zalloc.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <kern/ledger.h>
#include <kern/policy_internal.h>
#include <kern/task.h>
#include <kern/telemetry.h>
#include <kern/waitq.h>
#include <kern/sched_prim.h>
#include <kern/turnstile.h>
#include <kern/zalloc.h>
#include <kern/debug.h>
#include <kern/backtrace.h>
#include <machine/machine_routines.h>
#include <vm/vm_kern.h>

#include <pexpert/pexpert.h>

#define XNU_TEST_BITMAP
#include <kern/bits.h>

#include <os/hash.h>
#include <sys/ulock.h>

/*
 * How ulock promotion works:
 *
 * There’s a requested policy field on every thread called ‘promotions’, which
 * expresses which ulock promotions are happening to this thread.
 * The promotion priority saturates until the promotion count goes to 0.
 *
 * We also track effective promotion qos, which is the qos before clamping.
 * This value is used for promoting a thread that another thread is waiting on,
 * so that the lock owner reinflates to the right priority after unclamping.
 *
 * This also works for non-QoS threads, which can donate base priority to QoS
 * and non-QoS threads alike.
 *
 * ulock wait applies a promotion to the owner communicated through
 * UL_UNFAIR_LOCK as waiters block, and that promotion is saturated as long as
 * there is still an owner.  In ulock wake, if the waker is still the owner,
 * then it clears its ownership and drops the boost.  It does NOT transfer
 * ownership/priority boost to the new thread.  Instead, it selects the
 * waiting thread with the highest base priority to be woken next, and
 * relies on that thread to carry the torch for the other waiting threads.
 */

static LCK_GRP_DECLARE(ull_lck_grp, "ulocks");

/* Scalar-only observer. Never waits for its gate, allocates, copies user
 * memory, or prints on a syscall path (including while ull is locked).
 * Latest state per thread survives startup traffic. Only the three sustained
 * SB wait rounds drain it; no first-N syscall budget can consume the probe. */
#define MEIZU_ULOCK_SLOTS 64
struct meizu_ulock_row {
	uint64_t generation, tid, owner_tid;
	int pid, result;
	unsigned op, pending;
};
static struct meizu_ulock_row meizu_ulock_rows[4][MEIZU_ULOCK_SLOTS];
static uint64_t meizu_ulock_generation;
static uint64_t meizu_lsd_ulock_generation;
static uint64_t meizu_container_ulock_generation[2], meizu_container_ulock_started[2];
static uint64_t meizu_ulock_main_tid;
static unsigned meizu_ulock_gate, meizu_ulock_dropped;
extern uint64_t meizu_thread_task_generation(thread_t);
void meizu_sb_ulock_capture(uint64_t, uint64_t);
void meizu_sb_ulock_snapshot(uint64_t, unsigned);

void
meizu_sb_ulock_capture(uint64_t generation, uint64_t tid)
{
	__atomic_store_n(&meizu_ulock_main_tid, tid, __ATOMIC_RELAXED);
	__atomic_store_n(&meizu_ulock_generation, generation, __ATOMIC_RELEASE);
}

/* First capture starts one boot-wide reporting window. Exec churn cannot
 * reset the 300-second lifetime or the bounded output budget. */
static unsigned nova_lsd_bt_timing_armed;
static uint64_t nova_lsd_bt_timing_started;
void meizu_lsd_ulock_capture(uint64_t);
void
meizu_lsd_ulock_capture(uint64_t generation)
{
	unsigned unarmed = 0;
	if (generation && __atomic_compare_exchange_n(&nova_lsd_bt_timing_armed,
	    &unarmed, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		nova_lsd_bt_timing_started = mach_absolute_time();
		__atomic_store_n(&nova_lsd_bt_timing_armed, 2, __ATOMIC_RELEASE);
	}
	__atomic_store_n(&meizu_lsd_ulock_generation, generation, __ATOMIC_RELEASE);
}

/* Entry-only, faultable lsd stack observer. Slots and the trace budget are
 * boot-lifetime (never reset by exec). No gate is held across backtrace_user
 * or printf. The busy bit remains set across both to suppress recursion. */
#define MEIZU_LSD_BT_THREADS 16
#define MEIZU_LSD_BT_TRACES 32
#define MEIZU_LSD_BT_FRAMES 16
struct meizu_lsd_bt_slot {
	uint64_t generation, tid;
	unsigned count[7]; /* 0: semaphore_wait; 1..6: ulock opcode */
	unsigned busy;
	/* The admitted trace is this slot's sole writer until busy is cleared.
	 * Atomic version/data pairs let the reader reject an overlapping publish
	 * without waiting or holding the admission gate across the stack walk. */
	unsigned timing_version, completed_count;
	uint64_t total_ticks, max_ticks;
};
static struct meizu_lsd_bt_slot meizu_lsd_bt_slots[MEIZU_LSD_BT_THREADS];
static unsigned meizu_lsd_bt_gate, meizu_lsd_bt_total;

void meizu_lsd_wait_backtrace(unsigned, unsigned, uint64_t, uint64_t);
void
meizu_lsd_wait_backtrace(unsigned kind, unsigned operation, uint64_t address,
    uint64_t timeout)
{
	thread_t self = current_thread();
	uint64_t generation = meizu_thread_task_generation(self);
	if (!generation || generation != __atomic_load_n(
	    &meizu_lsd_ulock_generation, __ATOMIC_ACQUIRE)) return;
	/* Both callers are user trap entries, before IPC/ulock/waitq locks.
	 * Refuse contexts where the default faultable copy cannot be used. */
	if (!ml_get_interrupts_enabled() || ml_at_interrupt_context() ||
	    !preemption_enabled()) return;
	unsigned opcode = operation & UL_OPCODE_MASK;
	if (kind > 1 || (kind == 0 && opcode != UL_COMPARE_AND_WAIT &&
	    opcode != UL_UNFAIR_LOCK && opcode != UL_COMPARE_AND_WAIT_SHARED &&
	    opcode != UL_COMPARE_AND_WAIT64 && opcode != UL_COMPARE_AND_WAIT64_SHARED)) return;
	unsigned bucket = kind ? 0 : opcode;
	uint64_t tid = thread_tid(self);
	if (__atomic_exchange_n(&meizu_lsd_bt_gate, 1, __ATOMIC_ACQUIRE)) return;
	struct meizu_lsd_bt_slot *slot = NULL, *empty = NULL;
	unsigned sequence = 0;
	if (generation != __atomic_load_n(&meizu_lsd_ulock_generation, __ATOMIC_ACQUIRE) ||
	    meizu_lsd_bt_total >= MEIZU_LSD_BT_TRACES) goto unlock;
	for (unsigned i = 0; i < MEIZU_LSD_BT_THREADS; i++) {
		struct meizu_lsd_bt_slot *s = &meizu_lsd_bt_slots[i];
		if (s->generation == generation && s->tid == tid) { slot = s; break; }
		if (!s->generation && !empty) empty = s;
	}
	if (!slot) slot = empty;
	if (!slot || __atomic_load_n(&slot->busy, __ATOMIC_ACQUIRE) ||
	    slot->count[bucket] >= 2) goto unlock;
	slot->generation = generation;
	slot->tid = tid;
	slot->count[bucket]++;
	__atomic_store_n(&slot->busy, 1, __ATOMIC_RELEASE);
	sequence = ++meizu_lsd_bt_total;
unlock:
	__atomic_store_n(&meizu_lsd_bt_gate, 0, __ATOMIC_RELEASE);
	if (!sequence) return;

	uintptr_t pcs[MEIZU_LSD_BT_FRAMES] = { 0 };
	struct backtrace_user_info info = BTUINFO_INIT;
	uint64_t timing_start = mach_absolute_time();
	/* NULL control selects current_thread and the standard faultable copyin.
	 * Only returned PCs are emitted, never frame addresses or stack bytes. */
	unsigned n = backtrace_user(pcs, MEIZU_LSD_BT_FRAMES, NULL, &info);
	if (n > MEIZU_LSD_BT_FRAMES) n = MEIZU_LSD_BT_FRAMES;
	/* Keep even invalid syscall addresses out of the hash helper's kernel
	 * unslide branch. Valid ARM64 user addresses/port names have bit 63 clear. */
	uint64_t cookie = vm_kernel_addrhash((vm_offset_t)(address & 0x7fffffffffffffffULL));
	printf("meizu: lsdbt seq=%u pid=%d taskgen=%llu tid=%llu kind=%u op=0x%x timeout=%llu timeout_unit=%s cookie=0x%llx n=%u error=%d flags=0x%x truncated=%u\n",
	    sequence, proc_selfpid(), (unsigned long long)generation,
	    (unsigned long long)tid, kind, operation, (unsigned long long)timeout,
	    kind ? "infinite" : (operation & ULF_DEADLINE) ? "mach_absolute" : "ns",
	    (unsigned long long)cookie, n, info.btui_error, (unsigned)info.btui_info,
	    (unsigned)!!(info.btui_info & BTI_TRUNCATED));
	for (unsigned i = 0; i < n; i++) {
		printf("meizu: lsdbt-pc seq=%u frame=%u pc=0x%llx\n",
		    sequence, i, (unsigned long long)pcs[i]);
	}
	uint64_t elapsed = mach_absolute_time() - timing_start;
	/* Only 32 traces can reach this publication during a boot. Sequentially
	 * consistent scalars keep the bounded reader's version check sufficient
	 * under the C memory model; no new lock or retry loop enters this path. */
	__atomic_add_fetch(&slot->timing_version, 1, __ATOMIC_SEQ_CST);
	__atomic_add_fetch(&slot->total_ticks, elapsed, __ATOMIC_SEQ_CST);
	if (elapsed > __atomic_load_n(&slot->max_ticks, __ATOMIC_SEQ_CST)) {
		__atomic_store_n(&slot->max_ticks, elapsed, __ATOMIC_SEQ_CST);
	}
	__atomic_add_fetch(&slot->completed_count, 1, __ATOMIC_SEQ_CST);
	__atomic_add_fetch(&slot->timing_version, 1, __ATOMIC_SEQ_CST);
	__atomic_store_n(&slot->busy, 0, __ATOMIC_RELEASE);
}

/* Single autolog caller, never a syscall caller. At most one changed summary
 * per 10 seconds, 31 outputs, and 300 seconds after the first lsd capture.
 * Slots retain their immutable generation and are never recycled. Individual
 * slot reads are coherent; the aggregate spans different instants and its
 * elapsed sum is not exclusive CPU time or wall-clock startup time. */
void nova_lsd_bt_timing_dump(void);
void
nova_lsd_bt_timing_dump(void)
{
	struct nova_lsd_bt_summary {
		uint64_t generation, total_ticks, max_ticks;
		unsigned admitted, completed, busy;
	};
	static struct nova_lsd_bt_summary previous;
	static unsigned reports, checked;
	static uint64_t last_check;
	uint64_t now = mach_absolute_time(), age_ns, interval_ns;
	if (__atomic_load_n(&nova_lsd_bt_timing_armed, __ATOMIC_ACQUIRE) != 2 ||
	    reports >= 31) return;
	absolutetime_to_nanoseconds(now - nova_lsd_bt_timing_started, &age_ns);
	if (age_ns > 300ULL * NSEC_PER_SEC) return;
	absolutetime_to_nanoseconds(now - last_check, &interval_ns);
	if (checked && interval_ns < 10ULL * NSEC_PER_SEC) return;
	if (__atomic_exchange_n(&meizu_lsd_bt_gate, 1, __ATOMIC_ACQUIRE)) return;
	struct nova_lsd_bt_summary summary = {0};
	summary.generation = __atomic_load_n(&meizu_lsd_ulock_generation, __ATOMIC_ACQUIRE);
	bool stable = summary.generation != 0;
	for (unsigned i = 0; stable && i < MEIZU_LSD_BT_THREADS; i++) {
		struct meizu_lsd_bt_slot *slot = &meizu_lsd_bt_slots[i];
		if (slot->generation != summary.generation) continue;
		unsigned version = __atomic_load_n(&slot->timing_version, __ATOMIC_SEQ_CST);
		if (version & 1) { stable = false; break; }
		unsigned completed = __atomic_load_n(&slot->completed_count, __ATOMIC_SEQ_CST);
		uint64_t total = __atomic_load_n(&slot->total_ticks, __ATOMIC_SEQ_CST);
		uint64_t maximum = __atomic_load_n(&slot->max_ticks, __ATOMIC_SEQ_CST);
		unsigned busy = __atomic_load_n(&slot->busy, __ATOMIC_ACQUIRE);
		if (version != __atomic_load_n(&slot->timing_version, __ATOMIC_SEQ_CST)) {
			stable = false; break;
		}
		for (unsigned op = 0; op < 7; op++) summary.admitted += slot->count[op];
		summary.completed += completed;
		summary.total_ticks += total;
		if (maximum > summary.max_ticks) summary.max_ticks = maximum;
		summary.busy += !!busy;
	}
	if (summary.generation != __atomic_load_n(&meizu_lsd_ulock_generation, __ATOMIC_ACQUIRE)) {
		stable = false;
	}
	__atomic_store_n(&meizu_lsd_bt_gate, 0, __ATOMIC_RELEASE);
	if (!stable) return;
	checked = 1; last_check = now;
	if (!summary.admitted || (summary.generation == previous.generation &&
	    summary.admitted == previous.admitted && summary.completed == previous.completed &&
	    summary.busy == previous.busy && summary.total_ticks == previous.total_ticks &&
	    summary.max_ticks == previous.max_ticks)) return;
	previous = summary;
	reports++;
	uint64_t total_ns, max_ns;
	absolutetime_to_nanoseconds(summary.total_ticks, &total_ns);
	absolutetime_to_nanoseconds(summary.max_ticks, &max_ns);
	printf("nova: lsdbt-time generation=%llu admitted=%u completed=%u pending=%u busy_slots=%u total_ticks=%llu max_ticks=%llu total_ms=%llu max_ms=%llu snapshot_nonatomic=1 elapsed_sum_not_wall=1\n",
	    (unsigned long long)summary.generation, summary.admitted, summary.completed,
	    summary.admitted - summary.completed, summary.busy,
	    (unsigned long long)summary.total_ticks, (unsigned long long)summary.max_ticks,
	    (unsigned long long)(total_ns / NSEC_PER_MSEC), (unsigned long long)(max_ns / NSEC_PER_MSEC));
}

/* Setup-only analogue of the lsd entry observer. Each successful exact-path
 * exec publishes one immutable slot before user return; slots are never reused.
 * Retirement closes admission, but an already admitted current-thread walk may
 * finish. No task/thread references or user stack bytes are retained here. */
#define MEIZU_SETUP_BT_ATTEMPTS 3
#define MEIZU_SETUP_BT_TRACES 8
#define MEIZU_SETUP_BT_PER_OP 4
#define MEIZU_SETUP_BT_FRAMES 24
struct meizu_setup_bt_slot {
	uint64_t generation, tid, started;
	unsigned retired, busy, total, count[7];
};
static struct meizu_setup_bt_slot meizu_setup_bt_slots[MEIZU_SETUP_BT_ATTEMPTS];
static unsigned meizu_setup_bt_gate;

void meizu_setup_bt_capture(uint64_t, uint64_t, uint64_t, unsigned);
void meizu_setup_bt_retire(uint64_t);
void meizu_setup_wait_backtrace(unsigned, unsigned, uint64_t, uint64_t);

/* Arm/retire callers serialize with meizu_setup_lock; no reverse lock edge. */
void
meizu_setup_bt_capture(uint64_t generation, uint64_t tid, uint64_t started,
    unsigned attempt)
{
	if (!generation || !tid || !attempt || attempt > MEIZU_SETUP_BT_ATTEMPTS) return;
	struct meizu_setup_bt_slot *slot = &meizu_setup_bt_slots[attempt - 1];
	if (__atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE)) return;
	slot->tid = tid;
	slot->started = started;
	__atomic_store_n(&slot->generation, generation, __ATOMIC_RELEASE);
}

void
meizu_setup_bt_retire(uint64_t generation)
{
	for (unsigned i = 0; i < MEIZU_SETUP_BT_ATTEMPTS; i++) {
		struct meizu_setup_bt_slot *slot = &meizu_setup_bt_slots[i];
		if (generation && generation == __atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE)) {
			__atomic_store_n(&slot->retired, 1, __ATOMIC_RELEASE);
		}
	}
}

void
meizu_setup_wait_backtrace(unsigned kind, unsigned operation, uint64_t address,
    uint64_t timeout)
{
	thread_t self = current_thread();
	uint64_t generation = meizu_thread_task_generation(self), tid = thread_tid(self);
	if (!generation) return;
	struct meizu_setup_bt_slot *slot = NULL;
	unsigned attempt = 0;
	for (unsigned i = 0; i < MEIZU_SETUP_BT_ATTEMPTS; i++) {
		struct meizu_setup_bt_slot *s = &meizu_setup_bt_slots[i];
		if (generation == __atomic_load_n(&s->generation, __ATOMIC_ACQUIRE) &&
		    tid == s->tid && !__atomic_load_n(&s->retired, __ATOMIC_ACQUIRE)) {
			slot = s; attempt = i + 1; break;
		}
	}
	if (!slot || !ml_get_interrupts_enabled() || ml_at_interrupt_context() ||
	    !preemption_enabled()) return;
	unsigned opcode = operation & UL_OPCODE_MASK;
	if (kind > 1 || (kind == 0 && opcode != UL_COMPARE_AND_WAIT &&
	    opcode != UL_UNFAIR_LOCK && opcode != UL_COMPARE_AND_WAIT_SHARED &&
	    opcode != UL_COMPARE_AND_WAIT64 && opcode != UL_COMPARE_AND_WAIT64_SHARED)) return;
	unsigned bucket = kind ? 0 : opcode;
	if (__atomic_exchange_n(&meizu_setup_bt_gate, 1, __ATOMIC_ACQUIRE)) return;
	unsigned sequence = 0;
	uint64_t age;
	absolutetime_to_nanoseconds(mach_absolute_time() - slot->started, &age);
	if (__atomic_load_n(&slot->retired, __ATOMIC_ACQUIRE) ||
	    meizu_thread_task_generation(self) != generation || thread_tid(self) != slot->tid ||
	    age >= 20ULL * NSEC_PER_SEC || __atomic_load_n(&slot->busy, __ATOMIC_ACQUIRE) ||
	    slot->total >= MEIZU_SETUP_BT_TRACES || slot->count[bucket] >= MEIZU_SETUP_BT_PER_OP) goto unlock;
	slot->count[bucket]++;
	__atomic_store_n(&slot->busy, 1, __ATOMIC_RELEASE);
	sequence = ++slot->total;
unlock:
	__atomic_store_n(&meizu_setup_bt_gate, 0, __ATOMIC_RELEASE);
	if (!sequence) return;

	/* Same safe API/context as lsd: NULL control walks only current_thread,
	 * with faultable copyin, outside every probe/IPC/ulock/semaphore lock.
	 * busy covers both walking and printing, including recursive entry. */
	uintptr_t pcs[MEIZU_SETUP_BT_FRAMES] = { 0 };
	struct backtrace_user_info info = BTUINFO_INIT;
	unsigned n = backtrace_user(pcs, MEIZU_SETUP_BT_FRAMES, NULL, &info);
	if (n > MEIZU_SETUP_BT_FRAMES) n = MEIZU_SETUP_BT_FRAMES;
	uint64_t cookie = vm_kernel_addrhash((vm_offset_t)(address & 0x7fffffffffffffffULL));
	printf("meizu: setupbt attempt=%u seq=%u pid=%d taskgen=%llu tid=%llu age_ms=%llu kind=%u op=0x%x timeout=%llu timeout_unit=%s cookie=0x%llx n=%u error=%d flags=0x%x truncated=%u\n",
	    attempt, sequence, proc_selfpid(), (unsigned long long)generation,
	    (unsigned long long)tid, (unsigned long long)(age / NSEC_PER_MSEC),
	    kind, operation, (unsigned long long)timeout,
	    kind ? "infinite" : (operation & ULF_DEADLINE) ? "mach_absolute" : "ns",
	    (unsigned long long)cookie, n, info.btui_error, (unsigned)info.btui_info,
	    (unsigned)!!(info.btui_info & BTI_TRUNCATED));
	for (unsigned i = 0; i < n; i++) {
		printf("meizu: setupbt-pc attempt=%u seq=%u frame=%u pc=0x%llx\n",
		    attempt, sequence, i, (unsigned long long)pcs[i]);
	}
	__atomic_store_n(&slot->busy, 0, __ATOMIC_RELEASE);
}

/* Exact Setup-generation ulock entry observer, including every worker.
 * Shares only the immutable exec registration above, not main-thread quotas.
 * At most 16 TIDs, 2 entries per TID/opcode, 48 entries per attempt, 12 PCs.
 * A missed/failed walk consumes its reservation; no slot is ever recycled.
 * Entry is the only faultable context. Completion only writes scalars to its
 * exclusively reserved record, then release-publishes for autolog output. */
#define MEIZU_SWL_THREADS 16
#define MEIZU_SWL_TRACES 48
#define MEIZU_SWL_PER_OP 2
#define MEIZU_SWL_FRAMES 12
struct meizu_swl_thread {
	uint64_t tid;
	unsigned count[7], busy;
};
struct meizu_swl_record {
	uint64_t tid, owner_tid;
	unsigned state; /* 0 unused, 1 reserved, 2 complete, 3 drain claimed */
	unsigned operation, asserted;
	int result, waiting;
};
struct meizu_swl_attempt {
	struct meizu_swl_thread threads[MEIZU_SWL_THREADS];
	struct meizu_swl_record records[MEIZU_SWL_TRACES];
	unsigned total;
};
static struct meizu_swl_attempt meizu_swl_attempts[MEIZU_SETUP_BT_ATTEMPTS];
static unsigned meizu_swl_gate;
void meizu_setup_worker_lock_drain(void);

static struct meizu_swl_record *
meizu_setup_worker_lock_enter(unsigned operation, uint64_t address,
    uint64_t value, uint64_t timeout)
{
	thread_t self = current_thread();
	uint64_t gen = meizu_thread_task_generation(self), tid = thread_tid(self);
	struct meizu_setup_bt_slot *registration = NULL;
	unsigned attempt = 0, opcode = operation & UL_OPCODE_MASK;
	if (!gen || !tid) return NULL;
	for (unsigned i = 0; i < MEIZU_SETUP_BT_ATTEMPTS; i++) {
		struct meizu_setup_bt_slot *s = &meizu_setup_bt_slots[i];
		if (gen == __atomic_load_n(&s->generation, __ATOMIC_ACQUIRE) &&
		    !__atomic_load_n(&s->retired, __ATOMIC_ACQUIRE)) {
			registration = s; attempt = i; break;
		}
	}
	if (!registration || !ml_get_interrupts_enabled() ||
	    ml_at_interrupt_context() || !preemption_enabled()) return NULL;
	if (opcode != UL_COMPARE_AND_WAIT && opcode != UL_UNFAIR_LOCK &&
	    opcode != UL_COMPARE_AND_WAIT_SHARED && opcode != UL_COMPARE_AND_WAIT64 &&
	    opcode != UL_COMPARE_AND_WAIT64_SHARED) return NULL;
	uint64_t age;
	absolutetime_to_nanoseconds(mach_absolute_time() - registration->started, &age);
	if (age >= 20ULL * NSEC_PER_SEC) return NULL;
	if (__atomic_exchange_n(&meizu_swl_gate, 1, __ATOMIC_ACQUIRE)) return NULL;
	struct meizu_swl_attempt *a = &meizu_swl_attempts[attempt];
	struct meizu_swl_thread *t = NULL, *empty = NULL;
	struct meizu_swl_record *r = NULL;
	unsigned sequence = 0;
	if (__atomic_load_n(&registration->retired, __ATOMIC_ACQUIRE) ||
	    meizu_thread_task_generation(self) != gen ||
	    a->total >= MEIZU_SWL_TRACES) goto unlock;
	for (unsigned i = 0; i < MEIZU_SWL_THREADS; i++) {
		struct meizu_swl_thread *s = &a->threads[i];
		if (s->tid == tid) { t = s; break; }
		if (!s->tid && !empty) empty = s;
	}
	if (!t) t = empty;
	if (!t || __atomic_load_n(&t->busy, __ATOMIC_ACQUIRE) ||
	    t->count[opcode] >= MEIZU_SWL_PER_OP) goto unlock;
	t->tid = tid;
	t->count[opcode]++;
	__atomic_store_n(&t->busy, 1, __ATOMIC_RELEASE);
	r = &a->records[a->total++];
	sequence = a->total;
	r->tid = tid;
	r->operation = operation;
	__atomic_store_n(&r->state, 1, __ATOMIC_RELEASE);
unlock:
	__atomic_store_n(&meizu_swl_gate, 0, __ATOMIC_RELEASE);
	if (!r) return NULL;

	uintptr_t pcs[MEIZU_SWL_FRAMES] = { 0 };
	struct backtrace_user_info info = BTUINFO_INIT;
	/* Current-thread standard faultable unwind, no observer/ulock locks held.
	 * Busy remains set through printing, suppressing reentrant instrumentation. */
	unsigned n = backtrace_user(pcs, MEIZU_SWL_FRAMES, NULL, &info);
	if (n > MEIZU_SWL_FRAMES) n = MEIZU_SWL_FRAMES;
	uint64_t cookie = vm_kernel_addrhash((vm_offset_t)(address & 0x7fffffffffffffffULL));
	printf("meizu: setup-worker-lock-entry attempt=%u seq=%u taskgen=%llu tid=%llu main=%u age_ms=%llu op=0x%x opcode=%u expected=0x%llx timeout=%llu timeout_unit=%s cookie=0x%llx n=%u error=%d flags=0x%x truncated=%u\n",
	    attempt + 1, sequence, (unsigned long long)gen, (unsigned long long)tid,
	    (unsigned)(tid == registration->tid), (unsigned long long)(age / NSEC_PER_MSEC),
	    operation, opcode, (unsigned long long)value, (unsigned long long)timeout,
	    (operation & ULF_DEADLINE) ? "mach_absolute" : "ns",
	    (unsigned long long)cookie, n, info.btui_error, (unsigned)info.btui_info,
	    (unsigned)!!(info.btui_info & BTI_TRUNCATED));
	for (unsigned i = 0; i < n; i++) {
		printf("meizu: setup-worker-lock-pc attempt=%u seq=%u frame=%u pc=0x%llx\n",
		    attempt + 1, sequence, i, (unsigned long long)pcs[i]);
	}
	__atomic_store_n(&t->busy, 0, __ATOMIC_RELEASE);
	return r;
}

/* No lock acquisition, print, allocation, user access, or borrowed pointer
 * retention here. owner_tid comes only from the existing +1 owner reference
 * after the kernel's expected-value validation, never from userspace bits. */
static void
meizu_setup_worker_lock_complete(struct meizu_swl_record *r,
    uint64_t owner_tid, unsigned asserted, int result, int waiting)
{
	if (!r || __atomic_load_n(&r->state, __ATOMIC_ACQUIRE) != 1) return;
	r->owner_tid = (r->operation & UL_OPCODE_MASK) == UL_UNFAIR_LOCK ? owner_tid : 0;
	r->asserted = asserted;
	r->result = result;
	r->waiting = waiting;
	__atomic_store_n(&r->state, 2, __ATOMIC_RELEASE);
}

void
meizu_setup_worker_lock_drain(void)
{
	/* Claim records once; immutable slots also accept completion after retire.
	 * No reads of unpublished fields, and no gate held across printf. */
	for (unsigned i = 0; i < MEIZU_SETUP_BT_ATTEMPTS; i++) {
		uint64_t gen = __atomic_load_n(&meizu_setup_bt_slots[i].generation, __ATOMIC_ACQUIRE);
		if (!gen) continue;
		for (unsigned j = 0; j < MEIZU_SWL_TRACES; j++) {
			struct meizu_swl_record *r = &meizu_swl_attempts[i].records[j];
			unsigned state = 2;
			if (!__atomic_compare_exchange_n(&r->state, &state, 3, false,
			    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) continue;
			bool applicable = (r->operation & UL_OPCODE_MASK) == UL_UNFAIR_LOCK;
			printf("meizu: setup-worker-lock-result attempt=%u seq=%u taskgen=%llu tid=%llu op=0x%x asserted=%u waiting=%u result=%d owner_applicable=%u owner_valid=%u ownerTID=%llu\n",
			    i + 1, j + 1, (unsigned long long)gen, (unsigned long long)r->tid,
			    r->operation, r->asserted, (unsigned)r->waiting, r->result, applicable,
			    (unsigned)(applicable && r->owner_tid != 0), (unsigned long long)r->owner_tid);
		}
	}
}

void meizu_container_ulock_capture(unsigned, uint64_t);
void
meizu_container_ulock_capture(unsigned target, uint64_t generation)
{
	if (target < 2 || target >= 4) return;
	/* Called once per accepted container exec; never reset an occupied slot. */
	if (__atomic_load_n(&meizu_container_ulock_generation[target - 2], __ATOMIC_ACQUIRE)) return;
	meizu_container_ulock_started[target - 2] = mach_absolute_time();
	__atomic_store_n(&meizu_container_ulock_generation[target - 2], generation, __ATOMIC_RELEASE);
}

static unsigned
meizu_ulock_target(uint64_t generation)
{
	if (!generation) return 4;
	if (generation == __atomic_load_n(&meizu_ulock_generation, __ATOMIC_ACQUIRE)) return 0;
	if (generation == __atomic_load_n(&meizu_lsd_ulock_generation, __ATOMIC_ACQUIRE)) return 1;
	for (unsigned i = 0; i < 2; i++) {
		if (generation == __atomic_load_n(&meizu_container_ulock_generation[i], __ATOMIC_ACQUIRE)) {
			uint64_t age;
			absolutetime_to_nanoseconds(mach_absolute_time() - meizu_container_ulock_started[i], &age);
			if (age < 300ULL * NSEC_PER_SEC) return i + 2;
		}
	}
	return 4;
}

static void
meizu_ulock_record(int pid, unsigned op, uint64_t owner_tid, int result,
    unsigned pending)
{
	thread_t self = current_thread();
	uint64_t generation = meizu_thread_task_generation(self);
	unsigned target = meizu_ulock_target(generation);
	if (target == 4) {
		return;
	}
	if (__atomic_exchange_n(&meizu_ulock_gate, 1, __ATOMIC_ACQUIRE)) {
		__atomic_fetch_add(&meizu_ulock_dropped, 1, __ATOMIC_RELAXED);
		return;
	}
	/* Recheck publication after taking the gate; old exec cannot overwrite new. */
	if (target != meizu_ulock_target(generation)) {
		__atomic_store_n(&meizu_ulock_gate, 0, __ATOMIC_RELEASE);
		return;
	}
	struct meizu_ulock_row *table = meizu_ulock_rows[target];
	uint64_t tid = thread_tid(self);
	/* Slot zero is reserved for the captured main thread. Recycle completed
	 * worker records, but never evict a pending worker for startup churn. */
	bool main = !target && tid == __atomic_load_n(&meizu_ulock_main_tid, __ATOMIC_RELAXED);
	unsigned slot = main ? 0 : MEIZU_ULOCK_SLOTS;
	for (unsigned i = target ? 0 : 1; !main && i < MEIZU_ULOCK_SLOTS; i++) {
		if (table[i].generation == generation && table[i].tid == tid) {
			slot = i;
			break;
		}
		if (slot == MEIZU_ULOCK_SLOTS && (table[i].generation != generation ||
		    !table[i].pending)) {
			slot = i;
		}
	}
	if (slot < MEIZU_ULOCK_SLOTS) {
		table[slot] = (struct meizu_ulock_row){
			generation, tid, owner_tid, pid, result, op, pending };
	} else {
		__atomic_fetch_add(&meizu_ulock_dropped, 1, __ATOMIC_RELAXED);
	}
	__atomic_store_n(&meizu_ulock_gate, 0, __ATOMIC_RELEASE);
}

/* Scalar historical owner of the captured SB main thread. No IPC lock is
 * held here; release the observer gate before the caller snapshots IPC. */
uint64_t meizu_sb_ulock_owner(uint64_t);
uint64_t
meizu_sb_ulock_owner(uint64_t generation)
{
	uint64_t owner = 0;
	if (__atomic_exchange_n(&meizu_ulock_gate, 1, __ATOMIC_ACQUIRE)) {
		return 0;
	}
	struct meizu_ulock_row *r = &meizu_ulock_rows[0][0];
	if (generation && generation == __atomic_load_n(&meizu_ulock_generation, __ATOMIC_ACQUIRE) &&
	    r->generation == generation && r->pending &&
	    r->tid == __atomic_load_n(&meizu_ulock_main_tid, __ATOMIC_RELAXED)) {
		owner = r->owner_tid;
	}
	__atomic_store_n(&meizu_ulock_gate, 0, __ATOMIC_RELEASE);
	return owner;
}

/* Autolog only, called outside target/registry/ulock locks, three rounds. */
void
meizu_sb_ulock_snapshot(uint64_t generation, unsigned round)
{
	struct meizu_ulock_row rows[MEIZU_ULOCK_SLOTS];
	unsigned count = 0;
	unsigned target = meizu_ulock_target(generation);
	if (target == 4) return;
	struct meizu_ulock_row *table = meizu_ulock_rows[target];
	bool busy = __atomic_exchange_n(&meizu_ulock_gate, 1, __ATOMIC_ACQUIRE);
	if (!busy) {
		for (unsigned i = 0; i < MEIZU_ULOCK_SLOTS; i++) {
			if (table[i].generation == generation && table[i].pending) {
				rows[count++] = table[i];
			}
		}
		__atomic_store_n(&meizu_ulock_gate, 0, __ATOMIC_RELEASE);
	}
	printf("meizu: ulocks taskgen=%llu round=%u count=%u dropped=%u status=%s\n",
	    (unsigned long long)generation, round, count,
	    __atomic_load_n(&meizu_ulock_dropped, __ATOMIC_RELAXED),
	    busy ? "busy-unavailable" : count ? "sampled" : "unavailable");
	for (unsigned i = 0; i < count; i++) {
		struct meizu_ulock_row *r = &rows[i];
		printf("meizu: ulock pid=%d taskgen=%llu tid=%llu ownerTID=%llu op=%u result=%d pending=%u owner=%s\n",
		    r->pid, (unsigned long long)r->generation, (unsigned long long)r->tid, (unsigned long long)r->owner_tid,
		    r->op, r->result, r->pending, r->owner_tid ? "observed-at-wait" : "unavailable");
	}
}


#if ULL_TICKET_LOCK
typedef lck_ticket_t ull_lock_t;
#define ull_lock_init(ull)      lck_ticket_init(&ull->ull_lock, &ull_lck_grp)
#define ull_lock_destroy(ull)   lck_ticket_destroy(&ull->ull_lock, &ull_lck_grp)
#define ull_lock(ull)           lck_ticket_lock(&ull->ull_lock, &ull_lck_grp)
#define ull_unlock(ull)         lck_ticket_unlock(&ull->ull_lock)
#define ull_assert_owned(ull)   lck_ticket_assert_owned(&ull->ull_lock)
#define ull_assert_notwned(ull) lck_ticket_assert_not_owned(&ull->ull_lock)
#else
typedef lck_spin_t ull_lock_t;
#define ull_lock_init(ull)      lck_spin_init(&ull->ull_lock, &ull_lck_grp, NULL)
#define ull_lock_destroy(ull)   lck_spin_destroy(&ull->ull_lock, &ull_lck_grp)
#define ull_lock(ull)           lck_spin_lock_grp(&ull->ull_lock, &ull_lck_grp)
#define ull_unlock(ull)         lck_spin_unlock(&ull->ull_lock)
#define ull_assert_owned(ull)   LCK_SPIN_ASSERT(&ull->ull_lock, LCK_ASSERT_OWNED)
#define ull_assert_notwned(ull) LCK_SPIN_ASSERT(&ull->ull_lock, LCK_ASSERT_NOTOWNED)
#endif /* ULL_TICKET_LOCK */

#define ULOCK_TO_EVENT(ull)   ((event_t)ull)
#define EVENT_TO_ULOCK(event) ((ull_t *)event)

typedef enum {
	ULK_INVALID = 0,
	ULK_UADDR,
	ULK_XPROC,
} ulk_type;

typedef struct {
	union {
		struct __attribute__((packed)) {
			user_addr_t     ulk_addr;
			/*
			 * We use the task address as a hashing key,
			 * so that ulock wakes across exec can't
			 * be confused.
			 */
			task_t          ulk_task __kernel_data_semantics;
		};
		struct __attribute__((packed)) {
			uint64_t        ulk_object;
			uint64_t        ulk_offset;
		};
	};
	ulk_type        ulk_key_type;
} ulk_t;

#define ULK_UADDR_LEN   (sizeof(user_addr_t) + sizeof(task_t))
#define ULK_XPROC_LEN   (sizeof(uint64_t) + sizeof(uint64_t))

inline static bool
ull_key_match(ulk_t *a, ulk_t *b)
{
	if (a->ulk_key_type != b->ulk_key_type) {
		return false;
	}

	if (a->ulk_key_type == ULK_UADDR) {
		return (a->ulk_task == b->ulk_task) &&
		       (a->ulk_addr == b->ulk_addr);
	}

	assert(a->ulk_key_type == ULK_XPROC);
	return (a->ulk_object == b->ulk_object) &&
	       (a->ulk_offset == b->ulk_offset);
}

typedef struct ull {
	/*
	 * ull_owner is the most recent known value for the owner of this ulock
	 * i.e. it may be out of date WRT the real value in userspace.
	 */
	thread_t        ull_owner; /* holds +1 thread reference */
	ulk_t           ull_key;
	ull_lock_t      ull_lock;
	uint            ull_bucket_index;
	int32_t         ull_nwaiters;
	int32_t         ull_refcount;
	uint8_t         ull_opcode;
	struct turnstile *ull_turnstile;
	queue_chain_t   ull_hash_link;
} ull_t;

#define ULL_MUST_EXIST  0x0001
static void ull_put(ull_t *);

static uint32_t ulock_adaptive_spin_usecs = 20;

SYSCTL_INT(_kern, OID_AUTO, ulock_adaptive_spin_usecs, CTLFLAG_RW | CTLFLAG_LOCKED,
    &ulock_adaptive_spin_usecs, 0, "ulock adaptive spin duration");

#if DEVELOPMENT || DEBUG
static int ull_simulate_copyin_fault = 0;

static void
ull_dump(ull_t *ull)
{
	kprintf("ull\t%p\n", ull);
	switch (ull->ull_key.ulk_key_type) {
	case ULK_UADDR:
		kprintf("ull_key.ulk_key_type\tULK_UADDR\n");
		kprintf("ull_key.ulk_task\t%p\n", ull->ull_key.ulk_task);
		kprintf("ull_key.ulk_addr\t%p\n", (void *)(ull->ull_key.ulk_addr));
		break;
	case ULK_XPROC:
		kprintf("ull_key.ulk_key_type\tULK_XPROC\n");
		kprintf("ull_key.ulk_object\t%p\n", (void *)(ull->ull_key.ulk_object));
		kprintf("ull_key.ulk_offset\t%p\n", (void *)(ull->ull_key.ulk_offset));
		break;
	default:
		kprintf("ull_key.ulk_key_type\tUNKNOWN %d\n", ull->ull_key.ulk_key_type);
		break;
	}
	kprintf("ull_nwaiters\t%d\n", ull->ull_nwaiters);
	kprintf("ull_refcount\t%d\n", ull->ull_refcount);
	kprintf("ull_opcode\t%d\n\n", ull->ull_opcode);
	kprintf("ull_owner\t0x%llx\n\n", thread_tid(ull->ull_owner));
	kprintf("ull_turnstile\t%p\n\n", ull->ull_turnstile);
}
#endif

typedef struct ull_bucket {
	queue_head_t ulb_head;
#if ULL_TICKET_LOCK
	lck_ticket_t ulb_lock;
#else
	lck_spin_t   ulb_lock;
#endif /* ULL_TICKET_LOCK */
} ull_bucket_t;

static SECURITY_READ_ONLY_LATE(int) ull_hash_buckets;
static SECURITY_READ_ONLY_LATE(ull_bucket_t *) ull_bucket;
static uint32_t ull_nzalloc = 0;
static KALLOC_TYPE_DEFINE(ull_zone, ull_t, KT_DEFAULT);

#if ULL_TICKET_LOCK
#define ull_bucket_lock(i)       lck_ticket_lock(&ull_bucket[i].ulb_lock, &ull_lck_grp)
#define ull_bucket_unlock(i)     lck_ticket_unlock(&ull_bucket[i].ulb_lock)
#else
#define ull_bucket_lock(i)       lck_spin_lock_grp(&ull_bucket[i].ulb_lock, &ull_lck_grp)
#define ull_bucket_unlock(i)     lck_spin_unlock(&ull_bucket[i].ulb_lock)
#endif /* ULL_TICKET_LOCK */
static __inline__ uint32_t
ull_hash_index(const void *key, size_t length)
{
	uint32_t hash = os_hash_jenkins(key, length);

	hash &= (ull_hash_buckets - 1);

	return hash;
}

#define ULL_INDEX(keyp) ull_hash_index(keyp, keyp->ulk_key_type == ULK_UADDR ? ULK_UADDR_LEN : ULK_XPROC_LEN)

static void
ulock_initialize(void)
{
	assert(thread_max > 16);
	/* Size ull_hash_buckets based on thread_max.
	 * Round up to nearest power of 2, then divide by 4
	 */
	ull_hash_buckets = (1 << (bit_ceiling(thread_max) - 2));

	kprintf("%s>thread_max=%d, ull_hash_buckets=%d\n", __FUNCTION__, thread_max, ull_hash_buckets);
	assert(ull_hash_buckets >= thread_max / 4);

	ull_bucket = zalloc_permanent(sizeof(ull_bucket_t) * ull_hash_buckets,
	    ZALIGN_PTR);
	assert(ull_bucket != NULL);

	for (int i = 0; i < ull_hash_buckets; i++) {
		queue_init(&ull_bucket[i].ulb_head);
#if ULL_TICKET_LOCK
		lck_ticket_init(&ull_bucket[i].ulb_lock, &ull_lck_grp);
#else
		lck_spin_init(&ull_bucket[i].ulb_lock, &ull_lck_grp, NULL);
#endif /* ULL_TICKET_LOCK */
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_FIRST, ulock_initialize);

#if DEVELOPMENT || DEBUG
/* Count the number of hash entries for a given task address.
 * if task==0, dump the whole table.
 */
static int
ull_hash_dump(task_t task)
{
	int count = 0;
	if (task == TASK_NULL) {
		kprintf("%s>total number of ull_t allocated %d\n", __FUNCTION__, ull_nzalloc);
		kprintf("%s>BEGIN\n", __FUNCTION__);
	}
	for (int i = 0; i < ull_hash_buckets; i++) {
		ull_bucket_lock(i);
		if (!queue_empty(&ull_bucket[i].ulb_head)) {
			ull_t *elem;
			if (task == TASK_NULL) {
				kprintf("%s>index %d:\n", __FUNCTION__, i);
			}
			qe_foreach_element(elem, &ull_bucket[i].ulb_head, ull_hash_link) {
				if ((task == TASK_NULL) || ((elem->ull_key.ulk_key_type == ULK_UADDR)
				    && (task == elem->ull_key.ulk_task))) {
					ull_dump(elem);
					count++;
				}
			}
		}
		ull_bucket_unlock(i);
	}
	if (task == TASK_NULL) {
		kprintf("%s>END\n", __FUNCTION__);
		ull_nzalloc = 0;
	}
	return count;
}
#endif

static ull_t *
ull_alloc(ulk_t *key)
{
	ull_t *ull = (ull_t *)zalloc_flags(ull_zone, Z_SET_NOTSHARED);
	assert(ull != NULL);

	ull->ull_refcount = 1;
	ull->ull_key = *key;
	ull->ull_bucket_index = ULL_INDEX(key);
	ull->ull_nwaiters = 0;
	ull->ull_opcode = 0;

	ull->ull_owner = THREAD_NULL;
	ull->ull_turnstile = TURNSTILE_NULL;

	ull_lock_init(ull);

	ull_nzalloc++;
	return ull;
}

static void
ull_free(ull_t *ull)
{
	assert(ull->ull_owner == THREAD_NULL);
	assert(ull->ull_turnstile == TURNSTILE_NULL);

	ull_assert_notwned(ull);

	ull_lock_destroy(ull);

	zfree(ull_zone, ull);
}

/* Finds an existing ulock structure (ull_t), or creates a new one.
 * If MUST_EXIST flag is set, returns NULL instead of creating a new one.
 * The ulock structure is returned with ull_lock locked
 */
static ull_t *
ull_get(ulk_t *key, uint32_t flags, ull_t **unused_ull)
{
	ull_t *ull = NULL;
	uint i = ULL_INDEX(key);
	ull_t *new_ull = (flags & ULL_MUST_EXIST) ? NULL : ull_alloc(key);
	ull_t *elem;

	ull_bucket_lock(i);
	qe_foreach_element(elem, &ull_bucket[i].ulb_head, ull_hash_link) {
		ull_lock(elem);
		if (ull_key_match(&elem->ull_key, key)) {
			ull = elem;
			break;
		} else {
			ull_unlock(elem);
		}
	}
	if (ull == NULL) {
		if (flags & ULL_MUST_EXIST) {
			/* Must already exist (called from wake) */
			ull_bucket_unlock(i);
			assert(new_ull == NULL);
			assert(unused_ull == NULL);
			return NULL;
		}

		if (new_ull == NULL) {
			/* Alloc above failed */
			ull_bucket_unlock(i);
			return NULL;
		}

		ull = new_ull;
		ull_lock(ull);
		enqueue(&ull_bucket[i].ulb_head, &ull->ull_hash_link);
	} else if (!(flags & ULL_MUST_EXIST)) {
		assert(new_ull);
		assert(unused_ull);
		assert(*unused_ull == NULL);
		*unused_ull = new_ull;
	}

	ull->ull_refcount++;

	ull_bucket_unlock(i);

	return ull; /* still locked */
}

/*
 * Must be called with ull_lock held
 */
static void
ull_put(ull_t *ull)
{
	ull_assert_owned(ull);
	int refcount = --ull->ull_refcount;
	assert(refcount == 0 ? (ull->ull_key.ulk_key_type == ULK_INVALID) : 1);
	ull_unlock(ull);

	if (refcount > 0) {
		return;
	}

	ull_bucket_lock(ull->ull_bucket_index);
	remqueue(&ull->ull_hash_link);
	ull_bucket_unlock(ull->ull_bucket_index);

	ull_free(ull);
}

extern kern_return_t vm_map_page_info(vm_map_t map, vm_map_offset_t offset, vm_page_info_flavor_t flavor, vm_page_info_t info, mach_msg_type_number_t *count);
extern vm_map_t current_map(void);
extern boolean_t machine_thread_on_core(thread_t thread);

static int
uaddr_findobj(user_addr_t uaddr, uint64_t *objectp, uint64_t *offsetp)
{
	kern_return_t ret;
	vm_page_info_basic_data_t info;
	mach_msg_type_number_t count = VM_PAGE_INFO_BASIC_COUNT;
	ret = vm_map_page_info(current_map(), uaddr, VM_PAGE_INFO_BASIC, (vm_page_info_t)&info, &count);
	if (ret != KERN_SUCCESS) {
		return EINVAL;
	}

	if (objectp != NULL) {
		*objectp = (uint64_t)info.object_id;
	}
	if (offsetp != NULL) {
		*offsetp = (uint64_t)info.offset;
	}

	return 0;
}

static void ulock_wait_continue(void *, wait_result_t);
static void ulock_wait_cleanup(ull_t *, thread_t, thread_t, int32_t *);

inline static int
wait_result_to_return_code(wait_result_t wr)
{
	int ret = 0;

	switch (wr) {
	case THREAD_AWAKENED:
		break;
	case THREAD_TIMED_OUT:
		ret = ETIMEDOUT;
		break;
	case THREAD_INTERRUPTED:
	case THREAD_RESTART:
	default:
		ret = EINTR;
		break;
	}

	return ret;
}

static int
ulock_resolve_owner(uint32_t value, thread_t *owner)
{
	mach_port_name_t owner_name = ulock_owner_value_to_port_name(value);

	*owner = port_name_to_thread(owner_name,
	    PORT_INTRANS_THREAD_IN_CURRENT_TASK |
	    PORT_INTRANS_THREAD_NOT_CURRENT_THREAD);
	if (*owner == THREAD_NULL) {
		/*
		 * Translation failed - even though the lock value is up to date,
		 * whatever was stored in the lock wasn't actually a thread port.
		 */
		return owner_name == MACH_PORT_DEAD ? ESRCH : EOWNERDEAD;
	}
	return 0;
}

int
sys_ulock_wait(struct proc *p, struct ulock_wait_args *args, int32_t *retval)
{
	struct ulock_wait2_args args2;

	args2.operation = args->operation;
	args2.addr      = args->addr;
	args2.value     = args->value;
	args2.timeout   = (uint64_t)(args->timeout) * NSEC_PER_USEC;
	args2.value2    = 0;

	return sys_ulock_wait2(p, &args2, retval);
}

int
sys_ulock_wait2(struct proc *p, struct ulock_wait2_args *args, int32_t *retval)
{
	/* Entry context: before cancellation, address lookup, and ull locks. */
	meizu_lsd_wait_backtrace(0, args->operation, args->addr, args->timeout);
	meizu_setup_wait_backtrace(0, args->operation, args->addr, args->timeout);
	struct meizu_swl_record *worker_record = meizu_setup_worker_lock_enter(
	    args->operation, args->addr, args->value, args->timeout);
	uint8_t opcode = (uint8_t)(args->operation & UL_OPCODE_MASK);
	uint flags = args->operation & UL_FLAGS_MASK;

	if (flags & ULF_WAIT_CANCEL_POINT) {
		__pthread_testcancel(1);
	}

	int ret = 0;
	thread_t self = current_thread();
	uint64_t diagnostic_owner_tid = 0;
	ulk_t key;

	/* involved threads - each variable holds +1 ref if not null */
	thread_t owner_thread   = THREAD_NULL;
	thread_t old_owner      = THREAD_NULL;

	ull_t *unused_ull = NULL;

	if ((flags & ULF_WAIT_MASK) != flags) {
		ret = EINVAL;
		goto munge_retval;
	}

	bool set_owner = false;
	bool xproc = false;
	size_t lock_size = sizeof(uint32_t);
	int copy_ret;

	switch (opcode) {
	case UL_UNFAIR_LOCK:
		set_owner = true;
		break;
	case UL_COMPARE_AND_WAIT:
		break;
	case UL_COMPARE_AND_WAIT64:
		lock_size = sizeof(uint64_t);
		break;
	case UL_COMPARE_AND_WAIT_SHARED:
		xproc = true;
		break;
	case UL_COMPARE_AND_WAIT64_SHARED:
		xproc = true;
		lock_size = sizeof(uint64_t);
		break;
	default:
		ret = EINVAL;
		goto munge_retval;
	}

	uint64_t value = 0;

	if ((args->addr == 0) || (args->addr & (lock_size - 1))) {
		ret = EINVAL;
		goto munge_retval;
	}

	if (xproc) {
		uint64_t object = 0;
		uint64_t offset = 0;

		ret = uaddr_findobj(args->addr, &object, &offset);
		if (ret) {
			ret = EINVAL;
			goto munge_retval;
		}
		key.ulk_key_type = ULK_XPROC;
		key.ulk_object = object;
		key.ulk_offset = offset;
	} else {
		key.ulk_key_type = ULK_UADDR;
		key.ulk_task = proc_task(p);
		key.ulk_addr = args->addr;
	}

	if ((flags & ULF_WAIT_ADAPTIVE_SPIN) && set_owner) {
		/*
		 * Attempt the copyin outside of the lock once,
		 *
		 * If it doesn't match (which is common), return right away.
		 *
		 * If it matches, resolve the current owner, and if it is on core,
		 * spin a bit waiting for the value to change. If the owner isn't on
		 * core, or if the value stays stable, then go on with the regular
		 * blocking code.
		 */
		uint64_t end = 0;
		uint32_t u32;

		ret = copyin_atomic32(args->addr, &u32);
		if (ret || u32 != args->value) {
			goto munge_retval;
		}
		for (;;) {
			if (owner_thread == NULL && ulock_resolve_owner(u32, &owner_thread) != 0) {
				break;
			}

			/* owner_thread may have a +1 starting here */

			if (!machine_thread_on_core(owner_thread)) {
				break;
			}
			if (end == 0) {
				clock_interval_to_deadline(ulock_adaptive_spin_usecs,
				    NSEC_PER_USEC, &end);
			} else if (mach_absolute_time() > end) {
				break;
			}
			if (copyin_atomic32_wait_if_equals(args->addr, u32) != 0) {
				goto munge_retval;
			}
		}
	}

	ull_t *ull = ull_get(&key, 0, &unused_ull);
	if (ull == NULL) {
		ret = ENOMEM;
		goto munge_retval;
	}
	/* ull is locked */

	ull->ull_nwaiters++;

	if (ull->ull_opcode == 0) {
		ull->ull_opcode = opcode;
	} else if (ull->ull_opcode != opcode) {
		ret = EDOM;
		goto out_locked;
	}

	/*
	 * We don't want this copyin to get wedged behind VM operations,
	 * but we have to read the userspace value under the ull lock for correctness.
	 *
	 * Until <rdar://problem/24999882> exists,
	 * holding the ull spinlock across copyin forces any
	 * vm_fault we encounter to fail.
	 */

	/* copyin_atomicXX always checks alignment */

	if (lock_size == 4) {
		uint32_t u32;
		copy_ret = copyin_atomic32(args->addr, &u32);
		value = u32;
	} else {
		copy_ret = copyin_atomic64(args->addr, &value);
	}

#if DEVELOPMENT || DEBUG
	/* Occasionally simulate copyin finding the user address paged out */
	if (((ull_simulate_copyin_fault == proc_getpid(p)) || (ull_simulate_copyin_fault == 1)) && (copy_ret == 0)) {
		static _Atomic int fault_inject = 0;
		if (os_atomic_inc_orig(&fault_inject, relaxed) % 73 == 0) {
			copy_ret = EFAULT;
		}
	}
#endif
	if (copy_ret != 0) {
		/* copyin() will return an error if the access to the user addr would have faulted,
		 * so just return and let the user level code fault it in.
		 */
		ret = copy_ret;
		goto out_locked;
	}

	if (value != args->value) {
		/* Lock value has changed from expected so bail out */
		goto out_locked;
	}

	if (set_owner) {
		if (owner_thread == THREAD_NULL) {
			ret = ulock_resolve_owner((uint32_t)args->value, &owner_thread);
			if (ret == EOWNERDEAD) {
				/*
				 * Translation failed - even though the lock value is up to date,
				 * whatever was stored in the lock wasn't actually a thread port.
				 */
				goto out_locked;
			}
			/* HACK: don't bail on MACH_PORT_DEAD, to avoid blowing up the no-tsd pthread lock */
			ret = 0;
		}
		/* owner_thread has a +1 reference */

		/*
		 * At this point, I know:
		 * a) owner_thread is definitely the current owner, because I just read the value
		 * b) owner_thread is either:
		 *      i) holding the user lock or
		 *      ii) has just unlocked the user lock after I looked
		 *              and is heading toward the kernel to call ull_wake.
		 *              If so, it's going to have to wait for the ull mutex.
		 *
		 * Therefore, I can ask the turnstile to promote its priority, and I can rely
		 * on it to come by later to issue the wakeup and lose its promotion.
		 */

		/* Return the +1 ref from the ull_owner field */
		old_owner = ull->ull_owner;
		ull->ull_owner = THREAD_NULL;

		if (owner_thread != THREAD_NULL) {
			/* The ull_owner field now owns a +1 ref on owner_thread */
			thread_reference(owner_thread);
			ull->ull_owner = owner_thread;
		}
	}

	/* Owner resolution above owns a reference; never infer from user bits. */
	diagnostic_owner_tid = owner_thread ? thread_tid(owner_thread) : 0;
	wait_result_t wr;
	uint64_t timeout = args->timeout; /* nanoseconds */
	uint64_t deadline = TIMEOUT_WAIT_FOREVER;
	wait_interrupt_t interruptible = THREAD_ABORTSAFE;
	struct turnstile *ts;

	ts = turnstile_prepare((uintptr_t)ull, &ull->ull_turnstile,
	    TURNSTILE_NULL, TURNSTILE_ULOCK);
	thread_set_pending_block_hint(self, kThreadWaitUserLock);

	if (flags & ULF_WAIT_WORKQ_DATA_CONTENTION) {
		interruptible |= THREAD_WAIT_NOREPORT;
	}

	turnstile_update_inheritor(ts, owner_thread,
	    (TURNSTILE_DELAYED_UPDATE | TURNSTILE_INHERITOR_THREAD));

	if (timeout) {
		if (flags & ULF_DEADLINE) {
			deadline = timeout;
		} else {
			nanoseconds_to_deadline(timeout, &deadline);
		}
	}

	wr = waitq_assert_wait64(&ts->ts_waitq, CAST_EVENT64_T(ULOCK_TO_EVENT(ull)),
	    interruptible, deadline);
	meizu_ulock_record(proc_getpid(p), opcode, diagnostic_owner_tid,
	    wr == THREAD_WAITING ? 0 : wait_result_to_return_code(wr), wr == THREAD_WAITING);
	meizu_setup_worker_lock_complete(worker_record, diagnostic_owner_tid, 1,
	    wr == THREAD_WAITING ? 0 : wait_result_to_return_code(wr), wr == THREAD_WAITING);

	if (wr == THREAD_WAITING) {
		uthread_t uthread = (uthread_t)get_bsdthread_info(self);
		uthread->uu_save.uus_ulock_wait_data.ull = ull;
		uthread->uu_save.uus_ulock_wait_data.retval = retval;
		uthread->uu_save.uus_ulock_wait_data.flags = flags;
		uthread->uu_save.uus_ulock_wait_data.owner_thread = owner_thread;
		uthread->uu_save.uus_ulock_wait_data.old_owner = old_owner;
	}

	ull_unlock(ull);

	if (unused_ull) {
		ull_free(unused_ull);
		unused_ull = NULL;
	}

	turnstile_update_inheritor_complete(ts, TURNSTILE_INTERLOCK_NOT_HELD);

	if (wr == THREAD_WAITING) {
		if (set_owner && owner_thread != THREAD_NULL) {
			thread_handoff_parameter(owner_thread, ulock_wait_continue, ull, THREAD_HANDOFF_NONE);
		} else {
			assert(owner_thread == THREAD_NULL);
			thread_block_parameter(ulock_wait_continue, ull);
		}
		/* NOT REACHED */
	}

	ret = wait_result_to_return_code(wr);

	ull_lock(ull);
	turnstile_complete((uintptr_t)ull, &ull->ull_turnstile, NULL, TURNSTILE_ULOCK);

out_locked:
	ulock_wait_cleanup(ull, owner_thread, old_owner, retval);
	owner_thread = NULL;

	if (unused_ull) {
		ull_free(unused_ull);
		unused_ull = NULL;
	}

	assert(*retval >= 0);

munge_retval:
	meizu_setup_worker_lock_complete(worker_record, 0, 0, ret, 0);
	meizu_ulock_record(proc_getpid(p), opcode, diagnostic_owner_tid, ret, 0);
	if (owner_thread) {
		thread_deallocate(owner_thread);
	}
	if (ret == ESTALE) {
		ret = 0;
	}
	if ((flags & ULF_NO_ERRNO) && (ret != 0)) {
		*retval = -ret;
		ret = 0;
	}
	return ret;
}

/*
 * Must be called with ull_lock held
 */
static void
ulock_wait_cleanup(ull_t *ull, thread_t owner_thread, thread_t old_owner, int32_t *retval)
{
	ull_assert_owned(ull);

	thread_t old_lingering_owner = THREAD_NULL;

	*retval = --ull->ull_nwaiters;
	if (ull->ull_nwaiters == 0) {
		/*
		 * If the wait was canceled early, we might need to
		 * clear out the lingering owner reference before
		 * freeing the ull.
		 */
		old_lingering_owner = ull->ull_owner;
		ull->ull_owner = THREAD_NULL;

		memset(&ull->ull_key, 0, sizeof ull->ull_key);
		ull->ull_refcount--;
		assert(ull->ull_refcount > 0);
	}
	ull_put(ull);

	/* Need to be called after dropping the interlock */
	turnstile_cleanup();

	if (owner_thread != THREAD_NULL) {
		thread_deallocate(owner_thread);
	}

	if (old_owner != THREAD_NULL) {
		thread_deallocate(old_owner);
	}

	if (old_lingering_owner != THREAD_NULL) {
		thread_deallocate(old_lingering_owner);
	}

	assert(*retval >= 0);
}

__attribute__((noreturn))
static void
ulock_wait_continue(__unused void * parameter, wait_result_t wr)
{
	uthread_t uthread = current_uthread();
	int ret = 0;

	ull_t *ull = uthread->uu_save.uus_ulock_wait_data.ull;
	int32_t *retval = uthread->uu_save.uus_ulock_wait_data.retval;
	uint flags = uthread->uu_save.uus_ulock_wait_data.flags;
	thread_t owner_thread = uthread->uu_save.uus_ulock_wait_data.owner_thread;
	thread_t old_owner = uthread->uu_save.uus_ulock_wait_data.old_owner;

	ret = wait_result_to_return_code(wr);

	ull_lock(ull);
	turnstile_complete((uintptr_t)ull, &ull->ull_turnstile, NULL, TURNSTILE_ULOCK);

	/* Save only scalars before cleanup drops the owner and ull references. */
	unsigned diagnostic_op = ull->ull_opcode;
	uint64_t diagnostic_owner_tid = owner_thread ? thread_tid(owner_thread) : 0;
	ulock_wait_cleanup(ull, owner_thread, old_owner, retval);
	meizu_ulock_record(proc_getpid(current_proc()), diagnostic_op,
	    diagnostic_owner_tid, ret, 0);

	if ((flags & ULF_NO_ERRNO) && (ret != 0)) {
		*retval = -ret;
		ret = 0;
	}

	unix_syscall_return(ret);
}

int
sys_ulock_wake(struct proc *p, struct ulock_wake_args *args, int32_t *retval)
{
	int ret = 0;
#if DEVELOPMENT || DEBUG
	uint8_t opcode = (uint8_t)(args->operation & UL_OPCODE_MASK);

	if (opcode == UL_DEBUG_HASH_DUMP_PID) {
		*retval = ull_hash_dump(proc_task(p));
		return ret;
	} else if (opcode == UL_DEBUG_HASH_DUMP_ALL) {
		*retval = ull_hash_dump(TASK_NULL);
		return ret;
	} else if (opcode == UL_DEBUG_SIMULATE_COPYIN_FAULT) {
		ull_simulate_copyin_fault = (int)(args->wake_value);
		return ret;
	}
#endif
	ret = ulock_wake(proc_task(p), args->operation, args->addr, args->wake_value);

	if ((args->operation & ULF_NO_ERRNO) && (ret != 0)) {
		*retval = -ret;
		ret = 0;
	}

	return ret;
}

int
ulock_wake(task_t task, uint32_t operation, user_addr_t addr, uint64_t wake_value)
{
	uint8_t opcode = (uint8_t)(operation & UL_OPCODE_MASK);
	uint flags = operation & UL_FLAGS_MASK;
	int ret = 0;
	ulk_t key;

	/* involved threads - each variable holds +1 ref if not null */
	thread_t wake_thread    = THREAD_NULL;

	bool set_owner = false;
	bool allow_non_owner = false;
	bool xproc = false;

	switch (opcode) {
	case UL_UNFAIR_LOCK:
		set_owner = true;
		break;
	case UL_COMPARE_AND_WAIT:
	case UL_COMPARE_AND_WAIT64:
		break;
	case UL_COMPARE_AND_WAIT_SHARED:
	case UL_COMPARE_AND_WAIT64_SHARED:
		xproc = true;
		break;
	default:
		ret = EINVAL;
		goto munge_retval;
	}

	if ((flags & ULF_WAKE_MASK) != flags) {
		ret = EINVAL;
		goto munge_retval;
	}

	if ((flags & ULF_WAKE_THREAD) && ((flags & ULF_WAKE_ALL) || set_owner)) {
		ret = EINVAL;
		goto munge_retval;
	}

	if (flags & ULF_WAKE_ALLOW_NON_OWNER) {
		if (!set_owner) {
			ret = EINVAL;
			goto munge_retval;
		}

		allow_non_owner = true;
	}

	if (addr == 0) {
		ret = EINVAL;
		goto munge_retval;
	}

	if (xproc) {
		uint64_t object = 0;
		uint64_t offset = 0;

		ret = uaddr_findobj(addr, &object, &offset);
		if (ret) {
			ret = EINVAL;
			goto munge_retval;
		}
		key.ulk_key_type = ULK_XPROC;
		key.ulk_object = object;
		key.ulk_offset = offset;
	} else {
		key.ulk_key_type = ULK_UADDR;
		key.ulk_task = task;
		key.ulk_addr = addr;
	}

	if (flags & ULF_WAKE_THREAD) {
		mach_port_name_t wake_thread_name = (mach_port_name_t)(wake_value);
		wake_thread = port_name_to_thread(wake_thread_name,
		    PORT_INTRANS_THREAD_IN_CURRENT_TASK |
		    PORT_INTRANS_THREAD_NOT_CURRENT_THREAD);
		if (wake_thread == THREAD_NULL) {
			ret = ESRCH;
			goto munge_retval;
		}
	}

	ull_t *ull = ull_get(&key, ULL_MUST_EXIST, NULL);
	thread_t new_owner = THREAD_NULL;
	struct turnstile *ts = TURNSTILE_NULL;
	thread_t cleanup_thread = THREAD_NULL;

	if (ull == NULL) {
		ret = ENOENT;
		goto munge_retval;
	}
	/* ull is locked */

	if (opcode != ull->ull_opcode) {
		ret = EDOM;
		goto out_ull_put;
	}

	if (set_owner) {
		if ((ull->ull_owner != current_thread()) && !allow_non_owner) {
			/*
			 * If the current thread isn't the known owner,
			 * then this wake call was late to the party,
			 * and the kernel already knows who owns the lock.
			 *
			 * This current owner already knows the lock is contended
			 * and will redrive wakes, just bail out.
			 */
			goto out_ull_put;
		}
	} else {
		assert(ull->ull_owner == THREAD_NULL);
	}

	ts = turnstile_prepare((uintptr_t)ull, &ull->ull_turnstile,
	    TURNSTILE_NULL, TURNSTILE_ULOCK);
	assert(ts != TURNSTILE_NULL);

	if (flags & ULF_WAKE_THREAD) {
		kern_return_t kr = waitq_wakeup64_thread(&ts->ts_waitq,
		    CAST_EVENT64_T(ULOCK_TO_EVENT(ull)),
		    wake_thread, THREAD_AWAKENED);
		if (kr != KERN_SUCCESS) {
			assert(kr == KERN_NOT_WAITING);
			ret = EALREADY;
		}
	} else if (flags & ULF_WAKE_ALL) {
		waitq_wakeup64_all(&ts->ts_waitq, CAST_EVENT64_T(ULOCK_TO_EVENT(ull)),
		    THREAD_AWAKENED,
		    set_owner ? WAITQ_UPDATE_INHERITOR : WAITQ_WAKEUP_DEFAULT);
	} else if (set_owner) {
		/*
		 * The turnstile waitq is priority ordered,
		 * and will wake up the highest priority waiter
		 * and set it as the inheritor for us.
		 */
		new_owner = waitq_wakeup64_identify(&ts->ts_waitq,
		    CAST_EVENT64_T(ULOCK_TO_EVENT(ull)),
		    THREAD_AWAKENED, WAITQ_UPDATE_INHERITOR);
	} else {
		waitq_wakeup64_one(&ts->ts_waitq, CAST_EVENT64_T(ULOCK_TO_EVENT(ull)),
		    THREAD_AWAKENED, WAITQ_WAKEUP_DEFAULT);
	}

	if (set_owner) {
		turnstile_update_inheritor_complete(ts, TURNSTILE_INTERLOCK_HELD);
		cleanup_thread = ull->ull_owner;
		ull->ull_owner = new_owner;
	}

	turnstile_complete((uintptr_t)ull, &ull->ull_turnstile, NULL, TURNSTILE_ULOCK);

out_ull_put:
	ull_put(ull);

	if (ts != TURNSTILE_NULL) {
		/* Need to be called after dropping the interlock */
		turnstile_cleanup();
	}

	if (cleanup_thread != THREAD_NULL) {
		thread_deallocate(cleanup_thread);
	}

munge_retval:
	if (wake_thread != THREAD_NULL) {
		thread_deallocate(wake_thread);
	}

	return ret;
}

void
kdp_ulock_find_owner(__unused struct waitq * waitq, event64_t event, thread_waitinfo_t * waitinfo)
{
	ull_t *ull = EVENT_TO_ULOCK(event);

	zone_require(ull_zone->kt_zv.zv_zone, ull);

	switch (ull->ull_opcode) {
	case UL_UNFAIR_LOCK:
	case UL_UNFAIR_LOCK64_SHARED:
		waitinfo->owner   = thread_tid(ull->ull_owner);
		waitinfo->context = ull->ull_key.ulk_addr;
		break;
	case UL_COMPARE_AND_WAIT:
	case UL_COMPARE_AND_WAIT64:
	case UL_COMPARE_AND_WAIT_SHARED:
	case UL_COMPARE_AND_WAIT64_SHARED:
		waitinfo->owner   = 0;
		waitinfo->context = ull->ull_key.ulk_addr;
		break;
	default:
		panic("%s: Invalid ulock opcode %d addr %p", __FUNCTION__, ull->ull_opcode, (void*)ull);
		break;
	}
	return;
}
