/*
 * Copyright (c) 2000-2021 Apple Inc. All rights reserved.
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
/*
 * @OSF_COPYRIGHT@
 *
 */
/*
 *	File:	kern/sync_sema.c
 *	Author:	Joseph CaraDonna
 *
 *	Contains RT distributed semaphore synchronization services.
 */

#include <mach/mach_types.h>
#include <mach/mach_traps.h>
#include <mach/kern_return.h>
#include <mach/semaphore.h>
#include <mach/sync_policy.h>
#include <mach/task.h>

#include <kern/misc_protos.h>
#include <kern/sync_sema.h>
#include <kern/spl.h>
#include <kern/ipc_kobject.h>
#include <kern/ipc_tt.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <ipc/ipc_port.h>
#include <ipc/ipc_space.h>
#include <kern/host.h>
#include <kern/waitq.h>
#include <kern/zalloc.h>
#include <kern/mach_param.h>

static const uint8_t semaphore_event;
#define SEMAPHORE_EVENT CAST_EVENT64_T(&semaphore_event)

ZONE_DEFINE_ID(ZONE_ID_SEMAPHORE, "semaphores", struct semaphore,
    ZC_ZFREE_CLEARMEM);

os_refgrp_decl(static, sema_refgrp, "semaphore", NULL);

/* MEIZU_SEMA_PROBE_BEGIN
 * Three boot-lifetime slots, never reused. obj is held until retirement drain;
 * no object address is emitted. Every slot's opaque object ID is attempt+1.
 * Lock order: semaphore/task -> diagnostic; diagnostic never takes those locks.
 * splsched prevents interrupt reentry; all locked work is bounded scalar copies.
 */
#define MEIZU_SEMA_SLOTS 3
#define MEIZU_SEMA_EVENTS 64
#define MEIZU_SEMA_SIGNALS 48
static LCK_GRP_DECLARE(meizu_sema_group, "meizu-sema");
static LCK_SPIN_DECLARE(meizu_sema_lock, &meizu_sema_group);
enum { MS_SELECT = 1, MS_ASSERT, MS_BLOCK_CALL, MS_RETURN, MS_SIGNAL_BEGIN,
       MS_SIGNAL_END, MS_DESTROY, MS_CLOSE, MS_OFFCORE };
struct meizu_sema_event {
	uint64_t when, tid, generation, target, elapsed;
	int pid, result, count;
	unsigned kind, options;
};
struct meizu_sema_slot {
	uint64_t generation, tid, started, window, selected;
	semaphore_t obj;
	unsigned used, printed, signals, dropped;
	bool armed, claimed, returned, offcore;
	struct meizu_sema_event events[MEIZU_SEMA_EVENTS];
};
static struct meizu_sema_slot meizu_sema_slots[MEIZU_SEMA_SLOTS];
static uint64_t meizu_sema_tids[MEIZU_SEMA_SLOTS];
static unsigned meizu_sema_active;
extern uint64_t meizu_thread_task_generation(thread_t);
void meizu_setup_sema_capture(uint64_t, uint64_t, uint64_t, unsigned);
void meizu_setup_sema_retire(uint64_t);
void meizu_setup_sema_drain(void);

static void
meizu_sema_append(struct meizu_sema_slot *s, unsigned kind, int result,
    int count, unsigned options, uint64_t target, uint64_t elapsed)
{
	if ((kind == MS_SIGNAL_BEGIN || kind == MS_SIGNAL_END) &&
	    s->signals++ >= MEIZU_SEMA_SIGNALS) {
		/* Saturate both counters; do not wrap and reopen the quota. */
		s->signals = MEIZU_SEMA_SIGNALS;
		if (s->dropped != ~0U) s->dropped++;
		return;
	}
	if (s->used == MEIZU_SEMA_EVENTS) {
		if (s->dropped != ~0U) s->dropped++;
		return;
	}
	thread_t t = current_thread();
	s->events[s->used++] = (struct meizu_sema_event){
		.when = mach_absolute_time(), .tid = thread_tid(t),
		.generation = meizu_thread_task_generation(t),
		.pid = pid_from_task(current_task()), .target = target,
		.elapsed = elapsed, .kind = kind, .result = result,
		.count = count, .options = options };
}

void
meizu_setup_sema_capture(uint64_t gen, uint64_t tid, uint64_t started, unsigned attempt)
{
	if (!gen || !attempt || attempt > MEIZU_SEMA_SLOTS) return;
	uint64_t window;
	nanoseconds_to_absolutetime(20ULL * NSEC_PER_SEC, &window);
	spl_t spl = splsched();
	lck_spin_lock(&meizu_sema_lock);
	struct meizu_sema_slot *s = &meizu_sema_slots[attempt - 1];
	if (!s->generation) {
		s->generation = gen;
		s->tid = tid;
		s->started = started;
		s->window = window;
		s->armed = true;
		__atomic_store_n(&meizu_sema_tids[attempt - 1], tid, __ATOMIC_RELEASE);
		__atomic_fetch_or(&meizu_sema_active, 1U << (attempt - 1), __ATOMIC_RELEASE);
	}
	lck_spin_unlock(&meizu_sema_lock);
	splx(spl);
}

void
meizu_setup_sema_retire(uint64_t gen)
{
	spl_t spl = splsched();
	lck_spin_lock(&meizu_sema_lock);
	for (unsigned i = 0; i < MEIZU_SEMA_SLOTS; i++) {
		struct meizu_sema_slot *s = &meizu_sema_slots[i];
		if (s->generation == gen && s->armed) {
			meizu_sema_append(s, MS_CLOSE, 0, 0, 0, 0, 0);
			s->armed = false;
			__atomic_fetch_and(&meizu_sema_active, ~(1U << i), __ATOMIC_RELEASE);
		}
	}
	lck_spin_unlock(&meizu_sema_lock);
	splx(spl);
}

/* Caller already owns the translated semaphore reference. Its extra diagnostic
 * ref cannot become the last ref here. No faultable walk or allocation. */
static unsigned
meizu_sema_select(semaphore_t obj, uint64_t deadline, int options)
{
	if (!__atomic_load_n(&meizu_sema_active, __ATOMIC_ACQUIRE) ||
	    deadline || (options & SEMAPHORE_TIMEOUT_NOBLOCK)) return 0;
	thread_t t = current_thread();
	uint64_t gen = meizu_thread_task_generation(t), tid = thread_tid(t);
	unsigned token = 0;
	spl_t spl = splsched();
	lck_spin_lock(&meizu_sema_lock);
	for (unsigned i = 0; i < MEIZU_SEMA_SLOTS; i++) {
		struct meizu_sema_slot *s = &meizu_sema_slots[i];
		if (s->armed && !s->claimed && s->generation == gen && s->tid == tid &&
		    mach_absolute_time() - s->started < s->window) {
			semaphore_reference(obj);
			s->obj = obj;
			s->claimed = true;
			s->selected = mach_absolute_time();
			meizu_sema_append(s, MS_SELECT, 0, 0, 0, 0, 0);
			token = i + 1;
			break;
		}
	}
	lck_spin_unlock(&meizu_sema_lock);
	splx(spl);
	return token;
}

/* obj remains referenced by the instrumented operation. token==0 is an
 * object-wide event; wait events require their original immutable slot token. */
static void
meizu_sema_event(semaphore_t obj, unsigned token, unsigned kind, int result,
    int count, unsigned options, uint64_t target, uint64_t elapsed)
{
	if (!__atomic_load_n(&meizu_sema_active, __ATOMIC_ACQUIRE)) return;
	spl_t spl = splsched();
	lck_spin_lock(&meizu_sema_lock);
	for (unsigned i = 0; i < MEIZU_SEMA_SLOTS; i++) {
		struct meizu_sema_slot *s = &meizu_sema_slots[i];
		if ((token && token != i + 1) || !s->armed || !s->claimed || s->obj != obj ||
		    mach_absolute_time() - s->started >= s->window) continue;
		if (kind == MS_ASSERT || kind == MS_BLOCK_CALL || kind == MS_RETURN) {
			if (!token || s->returned) continue;
			if (kind == MS_RETURN) s->returned = true;
			elapsed = mach_absolute_time() - s->selected;
		}
		meizu_sema_append(s, kind, result, count, options, target, elapsed);
	}
	lck_spin_unlock(&meizu_sema_lock);
	splx(spl);
}

/* Scheduler caller owns t's sched lock with interrupts disabled. Compare only
 * pinned queue identity; never take the semaphore lock from this lock order.
 * One event confirms the selected wait was dispatched off-core still TH_WAIT. */
void meizu_setup_sema_offcore(thread_t);
void
meizu_setup_sema_offcore(thread_t t)
{
	if (!__atomic_load_n(&meizu_sema_active, __ATOMIC_ACQUIRE)) return;
	uint64_t tid = thread_tid(t);
	bool candidate = false;
	for (unsigned i = 0; i < MEIZU_SEMA_SLOTS; i++)
		candidate |= tid == __atomic_load_n(&meizu_sema_tids[i], __ATOMIC_ACQUIRE);
	if (!candidate) return;
	uint64_t gen = meizu_thread_task_generation(t);
	lck_spin_lock(&meizu_sema_lock);
	for (unsigned i = 0; i < MEIZU_SEMA_SLOTS; i++) {
		struct meizu_sema_slot *s = &meizu_sema_slots[i];
		if (s->armed && s->obj && !s->returned && !s->offcore &&
		    s->tid == tid && s->generation == gen &&
		    mach_absolute_time() - s->started < s->window &&
		    waitq_same(t->waitq, &s->obj->waitq) && t->wait_event == SEMAPHORE_EVENT) {
			s->offcore = true;
			meizu_sema_append(s, MS_OFFCORE, t->wait_result, 0,
			    (unsigned)t->state, tid, mach_absolute_time() - s->selected);
		}
	}
	lck_spin_unlock(&meizu_sema_lock);
}

/* Autolog only, with no caller locks. Bounded drain; no printing or last-ref
 * release under the diagnostic lock. Retired slots never match reused objects. */
void
meizu_setup_sema_drain(void)
{
	for (unsigned i = 0; i < MEIZU_SEMA_SLOTS; i++) {
		for (unsigned n = 0; n <= MEIZU_SEMA_EVENTS; n++) {
			struct meizu_sema_event e = {0};
			semaphore_t release = SEMAPHORE_NULL;
			uint64_t gen, tid, started;
			unsigned dropped;
			bool valid;
			spl_t spl = splsched();
			lck_spin_lock(&meizu_sema_lock);
			struct meizu_sema_slot *s = &meizu_sema_slots[i];
			if (s->armed && mach_absolute_time() - s->started >= s->window) {
				meizu_sema_append(s, MS_CLOSE, 1, 0, 0, 0, 0);
				s->armed = false;
				__atomic_fetch_and(&meizu_sema_active, ~(1U << i), __ATOMIC_RELEASE);
			}
			valid = s->printed < s->used;
			if (valid) e = s->events[s->printed++];
			gen = s->generation; tid = s->tid; started = s->started;
			dropped = s->dropped;
			if (!s->armed) { release = s->obj; s->obj = SEMAPHORE_NULL; }
			lck_spin_unlock(&meizu_sema_lock);
			splx(spl);
			if (release) semaphore_dereference(release);
			if (!valid) break;
			uint64_t age, elapsed;
			absolutetime_to_nanoseconds(e.when - started, &age);
			absolutetime_to_nanoseconds(e.elapsed, &elapsed);
			printf("meizu: setup-sema object=%u taskgen=%llu main_tid=%llu event=%u pid=%d tid=%llu actor_gen=%llu target_tid=%llu result=%d count=%d options=%x age_us=%llu elapsed_us=%llu dropped=%u\n",
			    i + 1, (unsigned long long)gen, (unsigned long long)tid, e.kind,
			    e.pid, (unsigned long long)e.tid, (unsigned long long)e.generation,
			    (unsigned long long)e.target, e.result, e.count, e.options,
			    (unsigned long long)(age / 1000), (unsigned long long)(elapsed / 1000), dropped);
		}
	}
}
/* Autolog-only scalar queries. No budd/task lock edge from semaphore or
 * scheduler paths: selection already publishes everything under this lock. */
bool meizu_setup_sema_window(unsigned, uint64_t *, uint64_t *);
bool
meizu_setup_sema_window(unsigned slot, uint64_t *generation, uint64_t *age_ns)
{
	if (slot >= MEIZU_SEMA_SLOTS) return false;
	spl_t spl = splsched();
	lck_spin_lock(&meizu_sema_lock);
	struct meizu_sema_slot *s = &meizu_sema_slots[slot];
	uint64_t now = mach_absolute_time();
	bool valid = s->armed && s->claimed && now - s->started < s->window;
	if (valid) {
		*generation = s->generation;
		*age_ns = now - s->selected;
	}
	lck_spin_unlock(&meizu_sema_lock);
	splx(spl);
	if (valid) absolutetime_to_nanoseconds(*age_ns, age_ns);
	return valid;
}

bool meizu_setup_sema_finished(void);
bool
meizu_setup_sema_finished(void)
{
	spl_t spl = splsched();
	lck_spin_lock(&meizu_sema_lock);
	bool finished = meizu_sema_slots[MEIZU_SEMA_SLOTS - 1].generation != 0;
	uint64_t now = mach_absolute_time();
	for (unsigned i = 0; i < MEIZU_SEMA_SLOTS; i++) {
		struct meizu_sema_slot *s = &meizu_sema_slots[i];
		if (s->armed && now - s->started < s->window) finished = false;
	}
	lck_spin_unlock(&meizu_sema_lock);
	splx(spl);
	return finished;
}
/* MEIZU_SEMA_PROBE_END */

/* Forward declarations */

static inline bool
semaphore_active(semaphore_t semaphore)
{
	return semaphore->owner != TASK_NULL;
}

static __inline__ uint64_t
semaphore_deadline(
	unsigned int            sec,
	clock_res_t             nsec)
{
	uint64_t abstime;

	nanotime_to_absolutetime(sec, nsec, &abstime);
	clock_absolutetime_interval_to_deadline(abstime, &abstime);

	return abstime;
}

/*
 *	Routine:	semaphore_create
 *
 *	Creates a semaphore.
 *	The port representing the semaphore is returned as a parameter.
 */
kern_return_t
semaphore_create(
	task_t                  task,
	semaphore_t             *new_semaphore,
	int                     policy,
	int                     value)
{
	semaphore_t s = SEMAPHORE_NULL;

	*new_semaphore = SEMAPHORE_NULL;
	if (task == TASK_NULL || value < 0 || (policy & ~SYNC_POLICY_USER_MASK)) {
		return KERN_INVALID_ARGUMENT;
	}

	s = zalloc_id(ZONE_ID_SEMAPHORE, Z_ZERO | Z_WAITOK | Z_NOFAIL);

	/*
	 *  Associate the new semaphore with the task by adding
	 *  the new semaphore to the task's semaphore list.
	 */
	task_lock(task);
	/* Check for race with task_terminate */
	if (!task->active) {
		task_unlock(task);
		zfree_id(ZONE_ID_SEMAPHORE, s);
		return KERN_INVALID_TASK;
	}

	waitq_init(&s->waitq, WQT_QUEUE, policy | SYNC_POLICY_INIT_LOCKED);

	/* init everything under both the task and semaphore locks */
	os_ref_init_raw(&s->ref_count, &sema_refgrp);
	s->count = value;
	s->owner = task;
	enqueue_head(&task->semaphore_list, &s->task_link);
	task->semaphores_owned++;

	semaphore_unlock(s);

	task_unlock(task);

	*new_semaphore = s;

	return KERN_SUCCESS;
}

/*
 *	Routine:	semaphore_destroy_internal
 *
 *	Disassociate a semaphore from its owning task, mark it inactive,
 *	and set any waiting threads running with THREAD_RESTART.
 *
 *	Conditions:
 *			task is locked
 *			semaphore is owned by the specified task
 *			if semaphore is locked, interrupts are disabled
 *	Returns:
 *			with semaphore unlocked, interrupts enabled
 */
static void
semaphore_destroy_internal(
	task_t                  task,
	semaphore_t             semaphore,
	bool                    semaphore_locked)
{
	int old_count;

	/* unlink semaphore from owning task */
	assert(semaphore->owner == task);
	remqueue(&semaphore->task_link);
	task->semaphores_owned--;

	spl_t spl_level = 0;

	if (semaphore_locked) {
		spl_level = 1;
	} else {
		spl_level = splsched();
		semaphore_lock(semaphore);
	}

	/*
	 * deactivate semaphore under both locks
	 * and then wake up all waiters.
	 */

	meizu_sema_event(semaphore, 0, MS_DESTROY, 0, semaphore->count, 0, 0, 0);
	semaphore->owner = TASK_NULL;
	old_count = semaphore->count;
	semaphore->count = 0;

	if (old_count < 0) {
		waitq_wakeup64_all_locked(&semaphore->waitq,
		    SEMAPHORE_EVENT, THREAD_RESTART,
		    waitq_flags_splx(spl_level) | WAITQ_UNLOCK);
		/* waitq/semaphore is unlocked, splx handled */
		assert(ml_get_interrupts_enabled());
	} else {
		assert(circle_queue_empty(&semaphore->waitq.waitq_queue));
		semaphore_unlock(semaphore);
		splx(spl_level);
		assert(ml_get_interrupts_enabled());
	}
}

/*
 *	Routine:	semaphore_free
 *
 *	Free a semaphore that hit a 0 refcount.
 *
 *	Conditions:
 *			Nothing is locked.
 */
__attribute__((noinline))
static void
semaphore_free(
	semaphore_t             semaphore)
{
	ipc_port_t port;
	task_t task;

	/*
	 * Last ref, clean up the port [if any]
	 * associated with the semaphore, destroy
	 * it (if still active) and then free
	 * the semaphore.
	 */
	port = semaphore->port;
	if (IP_VALID(port)) {
		assert(!port->ip_srights);
		ipc_kobject_dealloc_port(port, 0, IKOT_SEMAPHORE);
	}

	/*
	 * If the semaphore owned by the current task,
	 * we know the current task can't go away,
	 * so we can take locks in the right order.
	 *
	 * Else we try to take locks in the "wrong" order
	 * but if we fail to, we take a task ref and do it "right".
	 */
	task = current_task();
	if (semaphore->owner == task) {
		task_lock(task);
		if (semaphore->owner == task) {
			semaphore_destroy_internal(task, semaphore, false);
		} else {
			assert(semaphore->owner == TASK_NULL);
		}
		task_unlock(task);
	} else {
		spl_t spl = splsched();

		/* semaphore_destroy_internal will always enable, can't nest */
		assert(spl);

		semaphore_lock(semaphore);

		task = semaphore->owner;
		if (task == TASK_NULL) {
			semaphore_unlock(semaphore);
			splx(spl);
		} else if (task_lock_try(task)) {
			semaphore_destroy_internal(task, semaphore, true);
			/* semaphore unlocked, interrupts enabled */
			task_unlock(task);
		} else {
			task_reference(task);
			semaphore_unlock(semaphore);
			splx(spl);

			task_lock(task);
			if (semaphore->owner == task) {
				semaphore_destroy_internal(task, semaphore, false);
			}
			task_unlock(task);

			task_deallocate(task);
		}
	}

	waitq_deinit(&semaphore->waitq);
	zfree_id(ZONE_ID_SEMAPHORE, semaphore);
}

/*
 *	Routine:	semaphore_destroy
 *
 *	Destroys a semaphore and consume the caller's reference on the
 *	semaphore.
 */
kern_return_t
semaphore_destroy(
	task_t                  task,
	semaphore_t             semaphore)
{
	if (semaphore == SEMAPHORE_NULL) {
		return KERN_INVALID_ARGUMENT;
	}

	if (task == TASK_NULL) {
		semaphore_dereference(semaphore);
		return KERN_INVALID_ARGUMENT;
	}

	if (semaphore->owner == task) {
		task_lock(task);
		if (semaphore->owner == task) {
			semaphore_destroy_internal(task, semaphore, false);
		}
		task_unlock(task);
	}

	semaphore_dereference(semaphore);
	return KERN_SUCCESS;
}

/*
 *	Routine:	semaphore_destroy_all
 *
 *	Destroy all the semaphores associated with a given task.
 */

void
semaphore_destroy_all(
	task_t                  task)
{
	semaphore_t semaphore;

	task_lock(task);

	qe_foreach_element_safe(semaphore, &task->semaphore_list, task_link) {
		semaphore_destroy_internal(task, semaphore, false);
	}

	task_unlock(task);
}

/*
 *	Routine:	semaphore_signal_internal
 *
 *		Signals the semaphore as direct.
 *	Assumptions:
 *		Semaphore is locked.
 */
static kern_return_t
semaphore_signal_internal_body(
	semaphore_t             semaphore,
	thread_t                thread,
	int                     options)
{
	kern_return_t kr;

	spl_t spl_level = splsched();
	semaphore_lock(semaphore);
	meizu_sema_event(semaphore, 0, MS_SIGNAL_BEGIN, 0, semaphore->count,
	    (unsigned)options, thread ? thread_tid(thread) : 0, 0);

	if (!semaphore_active(semaphore)) {
		semaphore_unlock(semaphore);
		splx(spl_level);
		return KERN_TERMINATED;
	}

	if (thread != THREAD_NULL) {
		if (semaphore->count < 0) {
			kr = waitq_wakeup64_thread_and_unlock(
				&semaphore->waitq, SEMAPHORE_EVENT,
				thread, THREAD_AWAKENED);
			/* waitq/semaphore is unlocked */
			splx(spl_level);
		} else {
			kr = KERN_NOT_WAITING;
			semaphore_unlock(semaphore);
			splx(spl_level);
		}
		return kr;
	}

	if (options & SEMAPHORE_SIGNAL_ALL) {
		int old_count = semaphore->count;

		kr = KERN_NOT_WAITING;
		if (old_count < 0) {
			semaphore->count = 0;  /* always reset */

			kr = waitq_wakeup64_all_locked(&semaphore->waitq,
			    SEMAPHORE_EVENT, THREAD_AWAKENED,
			    WAITQ_UNLOCK | waitq_flags_splx(spl_level));
			/* waitq / semaphore is unlocked, splx handled */
		} else {
			if (options & SEMAPHORE_SIGNAL_PREPOST) {
				semaphore->count++;
			}
			kr = KERN_SUCCESS;
			semaphore_unlock(semaphore);
			splx(spl_level);
		}
		return kr;
	}

	if (semaphore->count < 0) {
		waitq_wakeup_flags_t flags = WAITQ_KEEP_LOCKED;

		if (options & SEMAPHORE_THREAD_HANDOFF) {
			flags |= WAITQ_HANDOFF;
		}
		kr = waitq_wakeup64_one_locked(&semaphore->waitq,
		    SEMAPHORE_EVENT, THREAD_AWAKENED, flags);
		if (kr == KERN_SUCCESS) {
			semaphore_unlock(semaphore);
			splx(spl_level);
			return KERN_SUCCESS;
		} else {
			semaphore->count = 0;  /* all waiters gone */
		}
	}

	if (options & SEMAPHORE_SIGNAL_PREPOST) {
		semaphore->count++;
	}

	semaphore_unlock(semaphore);
	splx(spl_level);
	return KERN_NOT_WAITING;
}

/* All signal APIs (including wait-signal and explicit thread targeting) funnel
 * here. Begin is serialized under the semaphore lock in the body; end is after
 * unlock, while the caller still owns its reference. Result is the INTERNAL
 * wake result before public APIs normalize KERN_NOT_WAITING to success. */
static kern_return_t
semaphore_signal_internal(semaphore_t semaphore, thread_t thread, int options)
{
	uint64_t began = mach_absolute_time();
	kern_return_t kr = semaphore_signal_internal_body(semaphore, thread, options);
	meizu_sema_event(semaphore, 0, MS_SIGNAL_END, kr, 0, (unsigned)options,
	    thread ? thread_tid(thread) : 0, mach_absolute_time() - began);
	return kr;
}

/*
 *	Routine:	semaphore_signal_thread
 *
 *	If the specified thread is blocked on the semaphore, it is
 *	woken up.  If a NULL thread was supplied, then any one
 *	thread is woken up.  Otherwise the caller gets KERN_NOT_WAITING
 *	and the	semaphore is unchanged.
 */
kern_return_t
semaphore_signal_thread(
	semaphore_t     semaphore,
	thread_t        thread)
{
	if (semaphore == SEMAPHORE_NULL) {
		return KERN_INVALID_ARGUMENT;
	}

	return semaphore_signal_internal(semaphore, thread,
	           SEMAPHORE_OPTION_NONE);
}

/*
 *	Routine:	semaphore_signal_thread_trap
 *
 *	Trap interface to the semaphore_signal_thread function.
 */
kern_return_t
semaphore_signal_thread_trap(
	struct semaphore_signal_thread_trap_args *args)
{
	mach_port_name_t sema_name = args->signal_name;
	mach_port_name_t thread_name = args->thread_name;
	semaphore_t      semaphore;
	thread_t         thread;
	kern_return_t    kr;

	/*
	 * MACH_PORT_NULL is not an error. It means that we want to
	 * select any one thread that is already waiting, but not to
	 * pre-post the semaphore.
	 */
	if (thread_name != MACH_PORT_NULL) {
		thread = port_name_to_thread(thread_name, PORT_INTRANS_OPTIONS_NONE);
		if (thread == THREAD_NULL) {
			return KERN_INVALID_ARGUMENT;
		}
	} else {
		thread = THREAD_NULL;
	}

	kr = port_name_to_semaphore(sema_name, &semaphore);
	if (kr == KERN_SUCCESS) {
		kr = semaphore_signal_internal(semaphore,
		    thread,
		    SEMAPHORE_OPTION_NONE);
		semaphore_dereference(semaphore);
	}
	if (thread != THREAD_NULL) {
		thread_deallocate(thread);
	}
	return kr;
}



/*
 *	Routine:	semaphore_signal
 *
 *		Traditional (in-kernel client and MIG interface) semaphore
 *		signal routine.  Most users will access the trap version.
 *
 *		This interface in not defined to return info about whether
 *		this call found a thread waiting or not.  The internal
 *		routines (and future external routines) do.  We have to
 *		convert those into plain KERN_SUCCESS returns.
 */
kern_return_t
semaphore_signal(
	semaphore_t             semaphore)
{
	kern_return_t           kr;

	if (semaphore == SEMAPHORE_NULL) {
		return KERN_INVALID_ARGUMENT;
	}

	kr = semaphore_signal_internal(semaphore,
	    THREAD_NULL,
	    SEMAPHORE_SIGNAL_PREPOST);
	if (kr == KERN_NOT_WAITING) {
		return KERN_SUCCESS;
	}
	return kr;
}

/*
 *	Routine:	semaphore_signal_trap
 *
 *	Trap interface to the semaphore_signal function.
 */
kern_return_t
semaphore_signal_trap(
	struct semaphore_signal_trap_args *args)
{
	mach_port_name_t sema_name = args->signal_name;

	return semaphore_signal_internal_trap(sema_name);
}

kern_return_t
semaphore_signal_internal_trap(mach_port_name_t sema_name)
{
	semaphore_t   semaphore;
	kern_return_t kr;

	kr = port_name_to_semaphore(sema_name, &semaphore);
	if (kr == KERN_SUCCESS) {
		kr = semaphore_signal_internal(semaphore,
		    THREAD_NULL,
		    SEMAPHORE_SIGNAL_PREPOST);
		semaphore_dereference(semaphore);
		if (kr == KERN_NOT_WAITING) {
			kr = KERN_SUCCESS;
		}
	}
	return kr;
}

/*
 *	Routine:	semaphore_signal_all
 *
 *	Awakens ALL threads currently blocked on the semaphore.
 *	The semaphore count returns to zero.
 */
kern_return_t
semaphore_signal_all(
	semaphore_t             semaphore)
{
	kern_return_t kr;

	if (semaphore == SEMAPHORE_NULL) {
		return KERN_INVALID_ARGUMENT;
	}

	kr = semaphore_signal_internal(semaphore,
	    THREAD_NULL,
	    SEMAPHORE_SIGNAL_ALL);
	if (kr == KERN_NOT_WAITING) {
		return KERN_SUCCESS;
	}
	return kr;
}

/*
 *	Routine:	semaphore_signal_all_trap
 *
 *	Trap interface to the semaphore_signal_all function.
 */
kern_return_t
semaphore_signal_all_trap(
	struct semaphore_signal_all_trap_args *args)
{
	mach_port_name_t sema_name = args->signal_name;
	semaphore_t     semaphore;
	kern_return_t kr;

	kr = port_name_to_semaphore(sema_name, &semaphore);
	if (kr == KERN_SUCCESS) {
		kr = semaphore_signal_internal(semaphore,
		    THREAD_NULL,
		    SEMAPHORE_SIGNAL_ALL);
		semaphore_dereference(semaphore);
		if (kr == KERN_NOT_WAITING) {
			kr = KERN_SUCCESS;
		}
	}
	return kr;
}

/*
 *	Routine:	semaphore_convert_wait_result
 *
 *	Generate the return code after a semaphore wait/block.  It
 *	takes the wait result as an input and coverts that to an
 *	appropriate result.
 */
static kern_return_t
semaphore_convert_wait_result(int wait_result)
{
	switch (wait_result) {
	case THREAD_AWAKENED:
		return KERN_SUCCESS;

	case THREAD_TIMED_OUT:
		return KERN_OPERATION_TIMED_OUT;

	case THREAD_INTERRUPTED:
		return KERN_ABORTED;

	case THREAD_RESTART:
		return KERN_TERMINATED;

	default:
		panic("semaphore_block");
		return KERN_FAILURE;
	}
}

/*
 *	Routine:	semaphore_wait_continue
 *
 *	Common continuation routine after waiting on a semphore.
 *	It returns directly to user space.
 */
static void
semaphore_wait_continue(void *arg, wait_result_t wr)
{
	thread_t self = current_thread();
	semaphore_cont_t caller_cont = self->sth_continuation;

	meizu_sema_event(self->sth_waitsemaphore, (unsigned)(uintptr_t)arg,
	    MS_RETURN, semaphore_convert_wait_result(wr), 0, (unsigned)wr, 0, 0);
	assert(self->sth_waitsemaphore != SEMAPHORE_NULL);
	semaphore_dereference(self->sth_waitsemaphore);
	if (self->sth_signalsemaphore != SEMAPHORE_NULL) {
		semaphore_dereference(self->sth_signalsemaphore);
	}

	assert(self->handoff_thread == THREAD_NULL);
	assert(caller_cont != NULL);
	(*caller_cont)(semaphore_convert_wait_result(wr));
}

/*
 *	Routine:	semaphore_wait_internal
 *
 *		Decrements the semaphore count by one.  If the count is
 *		negative after the decrement, the calling thread blocks
 *		(possibly at a continuation and/or with a timeout).
 *
 *	Assumptions:
 *		The reference
 *		A reference is held on the signal semaphore.
 */
static kern_return_t
semaphore_wait_internal(
	semaphore_t             wait_semaphore,
	semaphore_t             signal_semaphore,
	uint64_t                deadline,
	int                     option,
	semaphore_cont_t        caller_cont)
{
	int           wait_result;
	spl_t         spl_level;
	kern_return_t kr = KERN_ALREADY_WAITING;
	thread_t      self = current_thread();
	thread_t      handoff_thread = THREAD_NULL;
	int           semaphore_signal_options = SEMAPHORE_SIGNAL_PREPOST;
	thread_handoff_option_t handoff_option = THREAD_HANDOFF_NONE;
	unsigned probe = meizu_sema_select(wait_semaphore, deadline, option);
	int probe_assert = -1;

	spl_level = splsched();
	semaphore_lock(wait_semaphore);

	if (!semaphore_active(wait_semaphore)) {
		kr = KERN_TERMINATED;
	} else if (wait_semaphore->count > 0) {
		wait_semaphore->count--;
		kr = KERN_SUCCESS;
	} else if (option & SEMAPHORE_TIMEOUT_NOBLOCK) {
		kr = KERN_OPERATION_TIMED_OUT;
	} else {
		wait_semaphore->count = -1;  /* we don't keep an actual count */

		thread_set_pending_block_hint(self, kThreadWaitSemaphore);
		probe_assert = waitq_assert_wait64_locked(
			&wait_semaphore->waitq,
			SEMAPHORE_EVENT,
			THREAD_ABORTSAFE,
			TIMEOUT_URGENCY_USER_NORMAL,
			deadline, TIMEOUT_NO_LEEWAY,
			self);

		semaphore_signal_options |= SEMAPHORE_THREAD_HANDOFF;
	}
	if (probe) meizu_sema_event(wait_semaphore, probe, MS_ASSERT,
	    probe_assert, wait_semaphore->count, (unsigned)kr, 0, 0);
	semaphore_unlock(wait_semaphore);
	splx(spl_level);

	/*
	 * wait_semaphore is unlocked so we are free to go ahead and
	 * signal the signal_semaphore (if one was provided).
	 */
	if (signal_semaphore != SEMAPHORE_NULL) {
		kern_return_t signal_kr;

		/*
		 * lock the signal semaphore reference we got and signal it.
		 * This will NOT block (we cannot block after having asserted
		 * our intention to wait above).
		 */
		signal_kr = semaphore_signal_internal(signal_semaphore,
		    THREAD_NULL, semaphore_signal_options);

		if (signal_kr == KERN_NOT_WAITING) {
			assert(self->handoff_thread == THREAD_NULL);
			signal_kr = KERN_SUCCESS;
		} else if (signal_kr == KERN_TERMINATED) {
			/*
			 * Uh!Oh!  The semaphore we were to signal died.
			 * We have to get ourselves out of the wait in
			 * case we get stuck here forever (it is assumed
			 * that the semaphore we were posting is gating
			 * the decision by someone else to post the
			 * semaphore we are waiting on).  People will
			 * discover the other dead semaphore soon enough.
			 * If we got out of the wait cleanly (someone
			 * already posted a wakeup to us) then return that
			 * (most important) result.  Otherwise,
			 * return the KERN_TERMINATED status.
			 */
			assert(self->handoff_thread == THREAD_NULL);
			clear_wait(self, THREAD_INTERRUPTED);
			kr = semaphore_convert_wait_result(self->wait_result);
			if (kr == KERN_ABORTED) {
				kr = KERN_TERMINATED;
			}
		}
	}

	/*
	 * If we had an error, or we didn't really need to wait we can
	 * return now that we have signalled the signal semaphore.
	 */
	if (kr != KERN_ALREADY_WAITING) {
		if (probe) meizu_sema_event(wait_semaphore, probe, MS_RETURN, kr, 0, 0, 0, 0);
		assert(self->handoff_thread == THREAD_NULL);
		return kr;
	}

	if (self->handoff_thread) {
		handoff_thread = self->handoff_thread;
		self->handoff_thread = THREAD_NULL;
		handoff_option = THREAD_HANDOFF_SETRUN_NEEDED;
	}

	/*
	 * Now, we can block.  If the caller supplied a continuation
	 * pointer of his own for after the block, block with the
	 * appropriate semaphore continuation.  This will gather the
	 * semaphore results, release references on the semaphore(s),
	 * and then call the caller's continuation.
	 */
	if (probe) meizu_sema_event(wait_semaphore, probe, MS_BLOCK_CALL, 0, 0,
	    caller_cont != NULL, 0, 0);
	if (caller_cont) {
		self->sth_continuation = caller_cont;
		self->sth_waitsemaphore = wait_semaphore;
		self->sth_signalsemaphore = signal_semaphore;

		thread_handoff_parameter(handoff_thread, semaphore_wait_continue,
		    (void *)(uintptr_t)probe, handoff_option);
	} else {
		wait_result = thread_handoff_deallocate(handoff_thread, handoff_option);
	}

	assert(self->handoff_thread == THREAD_NULL);
	kr = semaphore_convert_wait_result(wait_result);
	if (probe) meizu_sema_event(wait_semaphore, probe, MS_RETURN, kr, 0,
	    (unsigned)wait_result, 0, 0);
	return kr;
}


/*
 *	Routine:	semaphore_wait
 *
 *	Traditional (non-continuation) interface presented to
 *      in-kernel clients to wait on a semaphore.
 */
kern_return_t
semaphore_wait(
	semaphore_t             semaphore)
{
	if (semaphore == SEMAPHORE_NULL) {
		return KERN_INVALID_ARGUMENT;
	}

	return semaphore_wait_internal(semaphore, SEMAPHORE_NULL,
	           0ULL, SEMAPHORE_OPTION_NONE, SEMAPHORE_CONT_NULL);
}

kern_return_t
semaphore_wait_noblock(
	semaphore_t             semaphore)
{
	if (semaphore == SEMAPHORE_NULL) {
		return KERN_INVALID_ARGUMENT;
	}

	return semaphore_wait_internal(semaphore, SEMAPHORE_NULL,
	           0ULL, SEMAPHORE_TIMEOUT_NOBLOCK, SEMAPHORE_CONT_NULL);
}

kern_return_t
semaphore_wait_deadline(
	semaphore_t             semaphore,
	uint64_t                deadline)
{
	if (semaphore == SEMAPHORE_NULL) {
		return KERN_INVALID_ARGUMENT;
	}

	return semaphore_wait_internal(semaphore, SEMAPHORE_NULL,
	           deadline, SEMAPHORE_OPTION_NONE, SEMAPHORE_CONT_NULL);
}

/*
 *	Trap:	semaphore_wait_trap
 *
 *	Trap version of semaphore wait.  Called on behalf of user-level
 *	clients.
 */

/* Shared exact-exec-generation gate and boot-lifetime budget in sys_ulock.c. */
extern void meizu_lsd_wait_backtrace(unsigned, unsigned, uint64_t, uint64_t);
extern void meizu_setup_wait_backtrace(unsigned, unsigned, uint64_t, uint64_t);

kern_return_t
semaphore_wait_trap(
	struct semaphore_wait_trap_args *args)
{
	/* User entry only: before port translation and all IPC/semaphore locks.
	 * kind=1, op=0, timeout=0 denotes an indefinite semaphore wait. */
	meizu_lsd_wait_backtrace(1, 0, args->wait_name, 0);
	meizu_setup_wait_backtrace(1, 0, args->wait_name, 0);
	return semaphore_wait_trap_internal(args->wait_name, thread_syscall_return);
}

kern_return_t
semaphore_wait_trap_internal(
	mach_port_name_t name,
	semaphore_cont_t caller_cont)
{
	semaphore_t   semaphore;
	kern_return_t kr;

	kr = port_name_to_semaphore(name, &semaphore);
	if (kr == KERN_SUCCESS) {
		kr = semaphore_wait_internal(semaphore,
		    SEMAPHORE_NULL,
		    0ULL, SEMAPHORE_OPTION_NONE,
		    caller_cont);
		semaphore_dereference(semaphore);
	}
	return kr;
}

/*
 *	Routine:	semaphore_timedwait
 *
 *	Traditional (non-continuation) interface presented to
 *      in-kernel clients to wait on a semaphore with a timeout.
 *
 *	A timeout of {0,0} is considered non-blocking.
 */
kern_return_t
semaphore_timedwait(
	semaphore_t             semaphore,
	mach_timespec_t         wait_time)
{
	int      option = SEMAPHORE_OPTION_NONE;
	uint64_t deadline = 0;

	if (semaphore == SEMAPHORE_NULL) {
		return KERN_INVALID_ARGUMENT;
	}

	if (BAD_MACH_TIMESPEC(&wait_time)) {
		return KERN_INVALID_VALUE;
	}

	if (wait_time.tv_sec == 0 && wait_time.tv_nsec == 0) {
		option = SEMAPHORE_TIMEOUT_NOBLOCK;
	} else {
		deadline = semaphore_deadline(wait_time.tv_sec, wait_time.tv_nsec);
	}

	return semaphore_wait_internal(semaphore, SEMAPHORE_NULL,
	           deadline, option, SEMAPHORE_CONT_NULL);
}

/*
 *	Trap:	semaphore_timedwait_trap
 *
 *	Trap version of a semaphore_timedwait.  The timeout parameter
 *	is passed in two distinct parts and re-assembled on this side
 *	of the trap interface (to accomodate calling conventions that
 *	pass structures as pointers instead of inline in registers without
 *	having to add a copyin).
 *
 *	A timeout of {0,0} is considered non-blocking.
 */
kern_return_t
semaphore_timedwait_trap(
	struct semaphore_timedwait_trap_args *args)
{
	return semaphore_timedwait_trap_internal(args->wait_name,
	           args->sec, args->nsec, thread_syscall_return);
}


kern_return_t
semaphore_timedwait_trap_internal(
	mach_port_name_t name,
	unsigned int     sec,
	clock_res_t      nsec,
	semaphore_cont_t caller_cont)
{
	semaphore_t semaphore;
	mach_timespec_t wait_time;
	kern_return_t kr;

	wait_time.tv_sec = sec;
	wait_time.tv_nsec = nsec;
	if (BAD_MACH_TIMESPEC(&wait_time)) {
		return KERN_INVALID_VALUE;
	}

	kr = port_name_to_semaphore(name, &semaphore);
	if (kr == KERN_SUCCESS) {
		int      option = SEMAPHORE_OPTION_NONE;
		uint64_t deadline = 0;

		if (sec == 0 && nsec == 0) {
			option = SEMAPHORE_TIMEOUT_NOBLOCK;
		} else {
			deadline = semaphore_deadline(sec, nsec);
		}

		kr = semaphore_wait_internal(semaphore,
		    SEMAPHORE_NULL,
		    deadline, option,
		    caller_cont);
		semaphore_dereference(semaphore);
	}
	return kr;
}

/*
 *	Routine:	semaphore_wait_signal
 *
 *	Atomically register a wait on a semaphore and THEN signal
 *	another.  This is the in-kernel entry point that does not
 *	block at a continuation and does not free a signal_semaphore
 *      reference.
 */
kern_return_t
semaphore_wait_signal(
	semaphore_t             wait_semaphore,
	semaphore_t             signal_semaphore)
{
	if (wait_semaphore == SEMAPHORE_NULL) {
		return KERN_INVALID_ARGUMENT;
	}

	return semaphore_wait_internal(wait_semaphore, signal_semaphore,
	           0ULL, SEMAPHORE_OPTION_NONE, SEMAPHORE_CONT_NULL);
}

/*
 *	Trap:	semaphore_wait_signal_trap
 *
 *	Atomically register a wait on a semaphore and THEN signal
 *	another.  This is the trap version from user space.
 */
kern_return_t
semaphore_wait_signal_trap(
	struct semaphore_wait_signal_trap_args *args)
{
	return semaphore_wait_signal_trap_internal(args->wait_name,
	           args->signal_name, thread_syscall_return);
}

kern_return_t
semaphore_wait_signal_trap_internal(
	mach_port_name_t wait_name,
	mach_port_name_t signal_name,
	semaphore_cont_t caller_cont)
{
	semaphore_t wait_semaphore;
	semaphore_t signal_semaphore;
	kern_return_t kr;

	kr = port_name_to_semaphore(signal_name, &signal_semaphore);
	if (kr == KERN_SUCCESS) {
		kr = port_name_to_semaphore(wait_name, &wait_semaphore);
		if (kr == KERN_SUCCESS) {
			kr = semaphore_wait_internal(wait_semaphore,
			    signal_semaphore,
			    0ULL, SEMAPHORE_OPTION_NONE,
			    caller_cont);
			semaphore_dereference(wait_semaphore);
		}
		semaphore_dereference(signal_semaphore);
	}
	return kr;
}


/*
 *	Routine:	semaphore_timedwait_signal
 *
 *	Atomically register a wait on a semaphore and THEN signal
 *	another.  This is the in-kernel entry point that does not
 *	block at a continuation.
 *
 *	A timeout of {0,0} is considered non-blocking.
 */
kern_return_t
semaphore_timedwait_signal(
	semaphore_t             wait_semaphore,
	semaphore_t             signal_semaphore,
	mach_timespec_t         wait_time)
{
	int      option = SEMAPHORE_OPTION_NONE;
	uint64_t deadline = 0;

	if (wait_semaphore == SEMAPHORE_NULL) {
		return KERN_INVALID_ARGUMENT;
	}

	if (BAD_MACH_TIMESPEC(&wait_time)) {
		return KERN_INVALID_VALUE;
	}

	if (wait_time.tv_sec == 0 && wait_time.tv_nsec == 0) {
		option = SEMAPHORE_TIMEOUT_NOBLOCK;
	} else {
		deadline = semaphore_deadline(wait_time.tv_sec, wait_time.tv_nsec);
	}

	return semaphore_wait_internal(wait_semaphore, signal_semaphore,
	           deadline, option, SEMAPHORE_CONT_NULL);
}

/*
 *	Trap:	semaphore_timedwait_signal_trap
 *
 *	Atomically register a timed wait on a semaphore and THEN signal
 *	another.  This is the trap version from user space.
 */
kern_return_t
semaphore_timedwait_signal_trap(
	struct semaphore_timedwait_signal_trap_args *args)
{
	return semaphore_timedwait_signal_trap_internal(args->wait_name,
	           args->signal_name, args->sec, args->nsec, thread_syscall_return);
}

kern_return_t
semaphore_timedwait_signal_trap_internal(
	mach_port_name_t wait_name,
	mach_port_name_t signal_name,
	unsigned int sec,
	clock_res_t nsec,
	semaphore_cont_t caller_cont)
{
	semaphore_t wait_semaphore;
	semaphore_t signal_semaphore;
	mach_timespec_t wait_time;
	kern_return_t kr;

	wait_time.tv_sec = sec;
	wait_time.tv_nsec = nsec;
	if (BAD_MACH_TIMESPEC(&wait_time)) {
		return KERN_INVALID_VALUE;
	}

	kr = port_name_to_semaphore(signal_name, &signal_semaphore);
	if (kr == KERN_SUCCESS) {
		kr = port_name_to_semaphore(wait_name, &wait_semaphore);
		if (kr == KERN_SUCCESS) {
			int      option = SEMAPHORE_OPTION_NONE;
			uint64_t deadline = 0;

			if (sec == 0 && nsec == 0) {
				option = SEMAPHORE_TIMEOUT_NOBLOCK;
			} else {
				deadline = semaphore_deadline(sec, nsec);
			}

			kr = semaphore_wait_internal(wait_semaphore,
			    signal_semaphore,
			    deadline, option,
			    caller_cont);
			semaphore_dereference(wait_semaphore);
		}
		semaphore_dereference(signal_semaphore);
	}
	return kr;
}


/*
 *	Routine:	semaphore_reference
 *
 *	Take out a reference on a semaphore.  This keeps the data structure
 *	in existence (but the semaphore may be deactivated).
 */
void
semaphore_reference(
	semaphore_t             semaphore)
{
	zone_id_require(ZONE_ID_SEMAPHORE, sizeof(*semaphore), semaphore);
	os_ref_retain_raw(&semaphore->ref_count, &sema_refgrp);
}

/*
 *	Routine:	semaphore_dereference
 *
 *	Release a reference on a semaphore.  If this is the last reference,
 *	the semaphore data structure is deallocated.
 */
void
semaphore_dereference(
	semaphore_t             semaphore)
{
	if (semaphore == NULL) {
		return;
	}

	if (os_ref_release_raw(&semaphore->ref_count, &sema_refgrp) == 0) {
		return semaphore_free(semaphore);
	}
}

void
kdp_sema_find_owner(struct waitq *waitq, __assert_only event64_t event, thread_waitinfo_t * waitinfo)
{
	semaphore_t sem = __container_of(waitq, struct semaphore, waitq);
	assert(event == SEMAPHORE_EVENT);

	zone_id_require(ZONE_ID_SEMAPHORE, sizeof(*sem), sem);

	waitinfo->context = VM_KERNEL_UNSLIDE_OR_PERM(sem->port);
	if (sem->owner) {
		waitinfo->owner = pid_from_task(sem->owner);
	}
}

/*
 *	Routine:	port_name_to_semaphore
 *	Purpose:
 *		Convert from a port name in the current space to a semaphore.
 *		Produces a semaphore ref, which may be null.
 *	Conditions:
 *		Nothing locked.
 */
kern_return_t
port_name_to_semaphore(
	mach_port_name_t        name,
	semaphore_t             *semaphorep)
{
	ipc_port_t port;
	kern_return_t kr;

	if (!MACH_PORT_VALID(name)) {
		*semaphorep = SEMAPHORE_NULL;
		return KERN_INVALID_NAME;
	}

	kr = ipc_port_translate_send(current_space(), name, &port);
	if (kr != KERN_SUCCESS) {
		*semaphorep = SEMAPHORE_NULL;
		return kr;
	}
	/* have the port locked */

	*semaphorep = convert_port_to_semaphore(port);
	if (*semaphorep == SEMAPHORE_NULL) {
		/* the port is valid, but doesn't denote a semaphore */
		kr = KERN_INVALID_CAPABILITY;
	} else {
		kr = KERN_SUCCESS;
	}
	ip_mq_unlock(port);

	return kr;
}

/*
 *	Routine:	convert_port_to_semaphore
 *	Purpose:
 *		Convert from a port to a semaphore.
 *		Doesn't consume the port [send-right] ref;
 *		produces a semaphore ref, which may be null.
 *	Conditions:
 *		Caller has a send-right reference to port.
 *		Port may or may not be locked.
 */
semaphore_t
convert_port_to_semaphore(ipc_port_t port)
{
	semaphore_t semaphore = SEMAPHORE_NULL;

	if (IP_VALID(port)) {
		semaphore = ipc_kobject_get_stable(port, IKOT_SEMAPHORE);
		if (semaphore != SEMAPHORE_NULL) {
			semaphore_reference(semaphore);
		}
	}

	return semaphore;
}


/*
 *	Routine:	convert_semaphore_to_port
 *	Purpose:
 *		Convert a semaphore reference to a send right to a
 *		semaphore port.
 *
 *		Consumes the semaphore reference.  If the semaphore
 *		port currently has no send rights (or doesn't exist
 *		yet), the reference is donated to the port to represent
 *		all extant send rights collectively.
 */
ipc_port_t
convert_semaphore_to_port(semaphore_t semaphore)
{
	if (semaphore == SEMAPHORE_NULL) {
		return IP_NULL;
	}

	/*
	 * make a send right and donate our reference for
	 * semaphore_no_senders if this is the first send right
	 */
	if (!ipc_kobject_make_send_lazy_alloc_port(&semaphore->port,
	    semaphore, IKOT_SEMAPHORE, IPC_KOBJECT_ALLOC_NONE)) {
		semaphore_dereference(semaphore);
	}
	return semaphore->port;
}

/*
 * Routine:	semaphore_no_senders
 * Purpose:
 *	Called whenever the Mach port system detects no-senders
 *	on the semaphore port.
 *
 *	When a send-right is first created, a no-senders
 *	notification is armed (and a semaphore reference is donated).
 *
 *	A no-senders notification will be posted when no one else holds a
 *	send-right (reference) to the semaphore's port. This notification function
 *	will consume the semaphore reference donated to the extant collection of
 *	send-rights.
 */
static void
semaphore_no_senders(ipc_port_t port, __unused mach_port_mscount_t mscount)
{
	semaphore_t semaphore = ipc_kobject_get_stable(port, IKOT_SEMAPHORE);

	assert(semaphore != SEMAPHORE_NULL);
	assert(semaphore->port == port);

	semaphore_dereference(semaphore);
}

IPC_KOBJECT_DEFINE(IKOT_SEMAPHORE,
    .iko_op_stable     = true,
    .iko_op_no_senders = semaphore_no_senders);
