/*
 * Copyright (c) 2000-2007 Apple Inc. All rights reserved.
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
 */
/*
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 * NOTICE: This file was modified by McAfee Research in 2004 to introduce
 * support for mandatory and extensible security protections.  This notice
 * is included in support of clause 2.2 (b) of the Apple Public License,
 * Version 2.0.
 * Copyright (c) 2005 SPARTA, Inc.
 */
/*
 */
/*
 *  File:   ipc/mach_msg.c
 *  Author: Rich Draves
 *  Date:   1989
 *
 *  Exported message traps.  See mach/message.h.
 */

#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <mach/port.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <mach/mach_traps.h>

#include <kern/kern_types.h>
#include <kern/assert.h>
#include <kern/cpu_number.h>
#include <kern/ipc_kobject.h>
#include <kern/ipc_mig.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/exception.h>
#include <kern/misc_protos.h>
#include <kern/backtrace.h>
#include <kern/processor.h>
#include <kern/syscall_subr.h>
#include <kern/policy_internal.h>
#include <kern/mach_filter.h>

#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <libkern/crypto/sha2.h>

#include <ipc/port.h>
#include <ipc/ipc_types.h>
#include <ipc/ipc_kmsg.h>
#include <ipc/ipc_mqueue.h>
#include <ipc/ipc_object.h>
#include <ipc/ipc_notify.h>
#include <ipc/ipc_policy.h>
#include <ipc/ipc_port.h>
#include <ipc/ipc_pset.h>
#include <ipc/ipc_space.h>
#include <ipc/ipc_entry.h>
#include <ipc/ipc_importance.h>
#include <ipc/ipc_voucher.h>

#include <machine/machine_routines.h>
#include <security/mac_mach_internal.h>

#include <sys/kdebug.h>
#include <sys/proc_ro.h>

#ifndef offsetof
#define offsetof(type, member)  ((size_t)(&((type *)0)->member))
#endif /* offsetof */

/* Scalar-only, bounded diagnostics for the successfully exec'd SB thread. */
#include <kern/clock.h>
#include <kern/locks.h>
#include <os/atomic_private.h>

#define MEIZU_IPC_RECORDS 64
static LCK_GRP_DECLARE(meizu_ipc_group, "meizu ipc probe");
static LCK_SPIN_DECLARE(meizu_ipc_lock, &meizu_ipc_group);
static uint64_t meizu_ipc_started, meizu_ipc_window, meizu_ipc_sequence;
static uint64_t meizu_ipc_overwritten, meizu_ipc_dropped;
static unsigned meizu_ipc_head, meizu_ipc_count;
static bool meizu_ipc_final_dump;
enum {
	MEIZU_IPC_ENTRY = 1, MEIZU_IPC_SEND, MEIZU_IPC_SENT,
	MEIZU_IPC_WAIT, MEIZU_IPC_COPYOUT, MEIZU_IPC_DONE, MEIZU_IPC_EXIT,
	MEIZU_IPC_LINK
};
enum {
	MEIZU_IPC_PEER_UNKNOWN, MEIZU_IPC_PEER_SPACE, MEIZU_IPC_PEER_KERNEL,
	MEIZU_IPC_PEER_REPLY, MEIZU_IPC_PEER_TRANSIT, MEIZU_IPC_PEER_INACTIVE,
	MEIZU_IPC_PEER_LIMBO
};
#define MEIZU_IPC_PATH_MAX 8
enum {
	MEIZU_IPC_PATH_END = 1, MEIZU_IPC_PATH_BUSY,
	MEIZU_IPC_PATH_CYCLE, MEIZU_IPC_PATH_LIMIT
};
struct meizu_ipc_endpoint {
	uint64_t cookie, first, last, end_task_generation;
	unsigned epoch, end_epoch, kind, end_kind, hops, stop, queued, end_queued, name;
	int pid, end_pid;
};

struct meizu_ipc_record {
	struct meizu_ipc_endpoint send_endpoint, link_endpoint;
	uint64_t sequence, tid, when, options, taskgen, begin_ticks;
	int pid, peer, send_id, receive_id, initial, result;
	int link_peer, link_result;
	unsigned timeout, phase, peer_class, link_class, nested, begin_mode;
	bool special, port, pending, link_seen, send_seen;
};
static struct meizu_ipc_record meizu_ipc_latest;
#define MEIZU_WORKER_SLOTS 32
struct meizu_ipc_workers {
	struct meizu_ipc_record rows[MEIZU_WORKER_SLOTS];
	uint64_t generation, started, window, full, nested;
	int pid;
};
/* Independent capacity: SB=0, lsd=1, two exec-identified containers=2,3.
 * Container slots are assigned once per boot; no replacement/rearm quota. */
#define MEIZU_IPC_TARGETS 4
static struct meizu_ipc_workers meizu_ipc_workers[MEIZU_IPC_TARGETS];
/* Only dereferenced while meizu_ipc_lock is held. Never kernel objects. */
static struct meizu_ipc_record *meizu_ipc_current;
static struct meizu_ipc_workers *meizu_ipc_current_workers;

/*
 * R15 sblong probe: selected SpringBoard MAIN -> launchd diagnostics.
 *
 * The retained rows are scalar-only and independent of meizu_ipc_ring. They
 * are never reset or evicted by an exec capture or a ring wrap. The pure core
 * between the markers is extracted verbatim by the host harness.
 */
/* NOVA_SBLONG_CORE_BEGIN */
#define NOVA_SBLONG_ROWS 8
#define NOVA_SBLONG_START_FRAMES 16
#define NOVA_SBLONG_FINISH_FRAMES 8
#define NOVA_SBLONG_START_NEEDED 1
#define NOVA_SBLONG_BT_DONE 2
#define NOVA_SBLONG_BT_SKIPPED 3
#define NOVA_SBLONG_SUMMARY_PERIODIC_MAX 31

struct nova_sblong_obs {
	uint64_t taskgen, tid, seq;
	uint64_t begin_ticks, wait_start_ticks, finish_ticks;
	int request_id, recv_id, endpoint_pid, result;
	uint64_t endpoint_gen;
	unsigned endpoint_stop, endpoint_kind, phase, finish_phase;
	unsigned pending, send_seen, nested, window_ok;
};

struct nova_sblong_bt {
	uint64_t token, taskgen, tid, seq;
	unsigned needed, needed_finish, first;
};

struct nova_sblong_row {
	uint64_t token, taskgen, tid, seq;
	uint64_t begin_ticks, wait_start_ticks, finish_ticks, duration_ticks;
	int request_id, recv_id, endpoint_pid, result;
	uint64_t endpoint_gen;
	unsigned endpoint_stop, endpoint_kind, phase, finish_phase;
	unsigned start_bt_state, start_bt_count, start_bt_error, start_bt_trunc;
	uint64_t start_pcs[NOVA_SBLONG_START_FRAMES];
	unsigned finish_bt_state, finish_bt_count, finish_bt_error, finish_bt_trunc;
	uint64_t finish_pcs[NOVA_SBLONG_FINISH_FRAMES];
	unsigned char valid, first, retained, started, finished, start_printed, finish_printed;
	unsigned char clock_error;
};

struct nova_sblong_print {
	struct nova_sblong_row row;
	unsigned event; /* 1=start, 2=finish */
	unsigned slot;
};

struct nova_sblong_stats {
	unsigned rows_used, overflow, short_dropped, expired, mismatch, bt_skipped;
	unsigned unfinished, clock_invalid, invalid_dropped, start_bt, finish_bt, summaries;
	uint64_t next_token;
};

struct nova_sblong_summary_state {
	uint64_t armed_ticks, window_ticks, interval_ticks, next_due_ticks;
	unsigned armed, periodic, summaries, final_sent;
};

static struct nova_sblong_row nova_sblong_rows[NOVA_SBLONG_ROWS];
static struct nova_sblong_summary_state nova_sblong_summary;
static uint64_t nova_sblong_next_token;
static uint64_t nova_sblong_long_ticks;
static unsigned nova_sblong_overflow;
static unsigned nova_sblong_short;
static unsigned nova_sblong_expired;
static unsigned nova_sblong_mismatch;
static unsigned nova_sblong_bt_skipped;
static unsigned nova_sblong_clock_invalid;
static unsigned nova_sblong_invalid_dropped;
static unsigned nova_sblong_start_bt;
static unsigned nova_sblong_finish_bt;

static int
nova_sblong_summary_arm(uint64_t armed_ticks, uint64_t window_ticks,
    uint64_t interval_ticks)
{
	if (nova_sblong_summary.armed) return 0;
	nova_sblong_summary.armed_ticks = armed_ticks;
	nova_sblong_summary.window_ticks = window_ticks;
	nova_sblong_summary.interval_ticks = interval_ticks;
	nova_sblong_summary.next_due_ticks = armed_ticks + interval_ticks;
	nova_sblong_summary.periodic = 0;
	nova_sblong_summary.summaries = 0;
	nova_sblong_summary.final_sent = 0;
	/* Publish readiness last: readers hold the same probe lock. */
	nova_sblong_summary.armed = 1;
	return 1;
}

/* Pure scheduler: call only while the probe lock is held.  It never sleeps,
 * prints, or allocates; it snapshots the caller's latest scalar counters. */
static int
nova_sblong_summary_due(uint64_t now, const struct nova_sblong_stats *st,
    struct nova_sblong_stats *out, unsigned *kind)
{
	uint64_t end;
	if (!nova_sblong_summary.armed || now < nova_sblong_summary.armed_ticks) return 0;
	end = nova_sblong_summary.armed_ticks + nova_sblong_summary.window_ticks;
	if (end < nova_sblong_summary.armed_ticks) end = ~(uint64_t)0;
	if (now >= end) {
		if (nova_sblong_summary.final_sent) return 0;
		nova_sblong_summary.final_sent = 1;
		nova_sblong_summary.summaries++;
		if (out) { *out = *st; out->summaries = nova_sblong_summary.summaries; }
		if (kind) *kind = 2;
		return 1;
	}
	if (nova_sblong_summary.periodic >= NOVA_SBLONG_SUMMARY_PERIODIC_MAX) return 0;
	if (now < nova_sblong_summary.next_due_ticks) return 0;
	nova_sblong_summary.periodic++;
	nova_sblong_summary.summaries++;
	if (out) { *out = *st; out->summaries = nova_sblong_summary.summaries; }
	if (kind) *kind = 1;
	nova_sblong_summary.next_due_ticks = now + nova_sblong_summary.interval_ticks;
	if (nova_sblong_summary.next_due_ticks < now ||
	    nova_sblong_summary.next_due_ticks > end) {
		nova_sblong_summary.next_due_ticks = end;
	}
	return 1;
}

static void
nova_sblong_set_long_ticks(uint64_t ticks)
{
	nova_sblong_long_ticks = ticks;
}

static int
nova_sblong_find_free(void)
{
	for (unsigned i = 0; i < NOVA_SBLONG_ROWS; i++) {
		if (!nova_sblong_rows[i].valid) return (int)i;
	}
	return -1;
}

static struct nova_sblong_row *
nova_sblong_find(uint64_t seq, uint64_t taskgen, uint64_t tid)
{
	for (unsigned i = 0; i < NOVA_SBLONG_ROWS; i++) {
		struct nova_sblong_row *r = &nova_sblong_rows[i];
		if (r->valid && !r->finished && r->seq == seq &&
		    r->taskgen == taskgen && r->tid == tid) return r;
	}
	return NULL;
}

static struct nova_sblong_row *
nova_sblong_find_token(uint64_t token)
{
	for (unsigned i = 0; i < NOVA_SBLONG_ROWS; i++) {
		if (nova_sblong_rows[i].valid &&
		    nova_sblong_rows[i].token == token) return &nova_sblong_rows[i];
	}
	return NULL;
}

static int
nova_sblong_admit_locked(const struct nova_sblong_obs *o, struct nova_sblong_bt *bt)
{
	if (!o->pending || o->nested || !o->send_seen || o->seq == 0 ||
	    o->request_id != 6001009 || o->endpoint_pid != 1 ||
	    o->endpoint_gen == 0 || o->endpoint_stop != 1 ||
	    o->endpoint_kind != 1) return 0;
	if (!o->window_ok) {
		nova_sblong_expired++;
		return 0;
	}
	int first = nova_sblong_next_token == 0;
	int slot = nova_sblong_find_free();
	if (slot < 0) {
		nova_sblong_overflow++;
		return 0;
	}
	struct nova_sblong_row *r = &nova_sblong_rows[slot];
	*r = (struct nova_sblong_row){0};
	r->token = ++nova_sblong_next_token;
	r->taskgen = o->taskgen; r->tid = o->tid; r->seq = o->seq;
	r->begin_ticks = o->begin_ticks;
	r->wait_start_ticks = o->wait_start_ticks;
	r->request_id = o->request_id; r->recv_id = o->recv_id;
	r->endpoint_pid = o->endpoint_pid; r->endpoint_gen = o->endpoint_gen;
	r->endpoint_stop = o->endpoint_stop; r->endpoint_kind = o->endpoint_kind;
	r->phase = o->phase;
	r->valid = 1; r->first = first ? 1 : 0;
	r->retained = first ? 1 : 0;
	r->started = 1;
	bt->token = r->token; bt->taskgen = r->taskgen; bt->tid = r->tid; bt->seq = r->seq;
	bt->first = first ? 1 : 0;
	if (first) {
		bt->needed = 1;
		r->start_bt_state = NOVA_SBLONG_START_NEEDED;
	}
	return 1;
}

static void
nova_sblong_wait_locked(uint64_t seq, uint64_t taskgen, uint64_t tid, uint64_t now)
{
	struct nova_sblong_row *r = nova_sblong_find(seq, taskgen, tid);
	if (!r) return;
	if (!r->wait_start_ticks) r->wait_start_ticks = now;
	r->phase = 4; /* MEIZU_IPC_WAIT, scalar only */
}

static int
nova_sblong_finish_locked(const struct nova_sblong_obs *o, struct nova_sblong_bt *bt)
{
	struct nova_sblong_row *r = nova_sblong_find(o->seq, o->taskgen, o->tid);
	if (!r) {
		for (unsigned i = 0; i < NOVA_SBLONG_ROWS; i++) {
			if (nova_sblong_rows[i].valid && nova_sblong_rows[i].seq == o->seq) {
				nova_sblong_mismatch++;
				break;
			}
		}
		return 0;
	}
	r->finish_ticks = o->finish_ticks;
	r->recv_id = o->recv_id;
	r->finish_phase = o->finish_phase;
	r->result = o->result;
	r->phase = o->phase;
	unsigned clock_error = 0;
	if (r->begin_ticks == 0 || o->finish_ticks < r->begin_ticks ||
	    (r->wait_start_ticks != 0 &&
	    (r->wait_start_ticks < r->begin_ticks || r->wait_start_ticks > o->finish_ticks))) {
		clock_error = 1;
	}
	r->clock_error = clock_error ? 1 : 0;
	if (clock_error) {
		r->duration_ticks = 0;
		nova_sblong_clock_invalid++;
	} else {
		r->duration_ticks = o->finish_ticks - r->begin_ticks;
	}
	r->finished = 1;
	if (clock_error) {
		if (!r->first) {
			r->valid = 0;
			nova_sblong_invalid_dropped++;
			return 0;
		}
		r->retained = 1;
		return 1;
	}
	if (r->first || r->duration_ticks >= nova_sblong_long_ticks) {
		r->retained = 1;
		if (!r->first && r->start_bt_state == 0) {
			r->finish_bt_state = NOVA_SBLONG_START_NEEDED;
			bt->needed_finish = 1;
			bt->token = r->token; bt->taskgen = r->taskgen;
			bt->tid = r->tid; bt->seq = r->seq;
		}
		return 1;
	}
	/* A later short operation is dropped; the first row is never dropped. */
	r->valid = 0;
	nova_sblong_short++;
	return 0;
}

static int
nova_sblong_attach_start_locked(const struct nova_sblong_bt *bt, const uint64_t *pcs,
    unsigned n, unsigned error, unsigned truncated)
{
	struct nova_sblong_row *r = nova_sblong_find_token(bt->token);
	if (!r || r->start_bt_state != NOVA_SBLONG_START_NEEDED ||
	    r->taskgen != bt->taskgen || r->tid != bt->tid || r->seq != bt->seq) return 0;
	if (n > NOVA_SBLONG_START_FRAMES) n = NOVA_SBLONG_START_FRAMES;
	for (unsigned i = 0; i < n; i++) r->start_pcs[i] = pcs[i];
	r->start_bt_count = n; r->start_bt_error = error; r->start_bt_trunc = truncated;
	r->start_bt_state = NOVA_SBLONG_BT_DONE;
	nova_sblong_start_bt++;
	return 1;
}

static int
nova_sblong_attach_finish_locked(const struct nova_sblong_bt *bt, const uint64_t *pcs,
    unsigned n, unsigned error, unsigned truncated)
{
	struct nova_sblong_row *r = nova_sblong_find_token(bt->token);
	if (!r || r->finish_bt_state != NOVA_SBLONG_START_NEEDED ||
	    r->taskgen != bt->taskgen || r->tid != bt->tid || r->seq != bt->seq) return 0;
	if (n > NOVA_SBLONG_FINISH_FRAMES) n = NOVA_SBLONG_FINISH_FRAMES;
	for (unsigned i = 0; i < n; i++) r->finish_pcs[i] = pcs[i];
	r->finish_bt_count = n; r->finish_bt_error = error; r->finish_bt_trunc = truncated;
	r->finish_bt_state = NOVA_SBLONG_BT_DONE;
	nova_sblong_finish_bt++;
	return 1;
}

static unsigned
nova_sblong_drain_locked(struct nova_sblong_print *out, unsigned cap,
    struct nova_sblong_stats *st)
{
	unsigned n = 0;
	for (unsigned i = 0; i < NOVA_SBLONG_ROWS; i++) {
		struct nova_sblong_row *r = &nova_sblong_rows[i];
		if (!r->valid) continue;
		/* Non-first provisional rows are invisible until a LONG finish. */
		if (!r->first && !r->retained) continue;
		if (r->start_bt_state == NOVA_SBLONG_START_NEEDED) continue;
		if (!r->start_printed && n < cap) {
			out[n].row = *r; out[n].event = 1; out[n].slot = i; n++;
			r->start_printed = 1;
		}
		if (r->finish_bt_state == NOVA_SBLONG_START_NEEDED) continue;
		if (r->finished && !r->finish_printed && n < cap) {
			out[n].row = *r; out[n].event = 2; out[n].slot = i; n++;
			r->finish_printed = 1;
		}
	}
	if (st) {
		st->rows_used = 0;
		for (unsigned i = 0; i < NOVA_SBLONG_ROWS; i++) {
			if (nova_sblong_rows[i].valid) st->rows_used++;
		}
		st->overflow = nova_sblong_overflow;
		st->short_dropped = nova_sblong_short;
		st->expired = nova_sblong_expired;
		st->mismatch = nova_sblong_mismatch;
		st->bt_skipped = nova_sblong_bt_skipped;
		st->clock_invalid = nova_sblong_clock_invalid;
		st->invalid_dropped = nova_sblong_invalid_dropped;
		st->unfinished = 0;
		for (unsigned i = 0; i < NOVA_SBLONG_ROWS; i++) {
			if (nova_sblong_rows[i].valid && !nova_sblong_rows[i].finished) st->unfinished++;
		}
		st->start_bt = nova_sblong_start_bt;
		st->finish_bt = nova_sblong_finish_bt;
		st->summaries = nova_sblong_summary.summaries;
		st->next_token = nova_sblong_next_token;
	}
	return n;
}

#ifdef NOVA_SBLONG_HOST
static void
nova_sblong_core_reset(void)
{
	for (unsigned i = 0; i < NOVA_SBLONG_ROWS; i++) nova_sblong_rows[i] = (struct nova_sblong_row){0};
	nova_sblong_summary = (struct nova_sblong_summary_state){0};
	nova_sblong_next_token = 0;
	nova_sblong_long_ticks = 0;
	nova_sblong_overflow = 0;
	nova_sblong_short = 0;
	nova_sblong_expired = 0;
	nova_sblong_mismatch = 0;
	nova_sblong_bt_skipped = 0;
	nova_sblong_clock_invalid = 0;
	nova_sblong_invalid_dropped = 0;
	nova_sblong_start_bt = 0;
	nova_sblong_finish_bt = 0;
}
#endif
/* NOVA_SBLONG_CORE_END */

static void
nova_sblong_bt_start(struct nova_sblong_bt *bt)
{
	uint64_t pcs[NOVA_SBLONG_START_FRAMES] = {0};
	unsigned n = 0, error = 0, truncated = 0;
	if (!ml_get_interrupts_enabled() || ml_at_interrupt_context() ||
	    !preemption_enabled()) {
		error = 0xffffffffU;
		lck_spin_lock(&meizu_ipc_lock);
		nova_sblong_bt_skipped++;
		lck_spin_unlock(&meizu_ipc_lock);
	} else {
		uintptr_t upcs[NOVA_SBLONG_START_FRAMES] = {0};
		struct backtrace_user_info info = BTUINFO_INIT;
		n = backtrace_user(upcs, NOVA_SBLONG_START_FRAMES, NULL, &info);
		if (n > NOVA_SBLONG_START_FRAMES) n = NOVA_SBLONG_START_FRAMES;
		error = (unsigned)info.btui_error;
		truncated = (unsigned)!!(info.btui_info & BTI_TRUNCATED);
		for (unsigned i = 0; i < n; i++) pcs[i] = (uint64_t)upcs[i];
	}
	lck_spin_lock(&meizu_ipc_lock);
	nova_sblong_attach_start_locked(bt, pcs, n, error, truncated);
	lck_spin_unlock(&meizu_ipc_lock);
}

static void
nova_sblong_bt_finish(struct nova_sblong_bt *bt)
{
	uint64_t pcs[NOVA_SBLONG_FINISH_FRAMES] = {0};
	unsigned n = 0, error = 0, truncated = 0;
	if (!ml_get_interrupts_enabled() || ml_at_interrupt_context() ||
	    !preemption_enabled()) {
		error = 0xffffffffU;
		lck_spin_lock(&meizu_ipc_lock);
		nova_sblong_bt_skipped++;
		lck_spin_unlock(&meizu_ipc_lock);
	} else {
		uintptr_t upcs[NOVA_SBLONG_FINISH_FRAMES] = {0};
		struct backtrace_user_info info = BTUINFO_INIT;
		n = backtrace_user(upcs, NOVA_SBLONG_FINISH_FRAMES, NULL, &info);
		if (n > NOVA_SBLONG_FINISH_FRAMES) n = NOVA_SBLONG_FINISH_FRAMES;
		error = (unsigned)info.btui_error;
		truncated = (unsigned)!!(info.btui_info & BTI_TRUNCATED);
		for (unsigned i = 0; i < n; i++) pcs[i] = (uint64_t)upcs[i];
	}
	lck_spin_lock(&meizu_ipc_lock);
	nova_sblong_attach_finish_locked(bt, pcs, n, error, truncated);
	lck_spin_unlock(&meizu_ipc_lock);
}

static void
nova_sblong_capture_arm_locked(uint64_t now, uint64_t window_ticks,
    uint64_t interval_ticks, uint64_t long_ticks)
{
	if (nova_sblong_summary.armed) return;
	/* Caller holds meizu_ipc_lock.  Publish threshold before .armed. */
	nova_sblong_set_long_ticks(long_ticks);
	(void)nova_sblong_summary_arm(now, window_ticks, interval_ticks);
}

static void
nova_sblong_drain(void)
{
	struct nova_sblong_print out[2 * NOVA_SBLONG_ROWS];
	struct nova_sblong_stats st;
	struct nova_sblong_stats summary_st = {0};
	unsigned n = 0, kind = 0;
	bool summary = false;
	uint64_t now;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) return;
	n = nova_sblong_drain_locked(out, 2 * NOVA_SBLONG_ROWS, &st);
	now = mach_absolute_time();
	if (nova_sblong_summary_due(now, &st, &summary_st, &kind)) summary = true;
	lck_spin_unlock(&meizu_ipc_lock);
	if (summary) {
		printf("nova: sblong v=15 event=summary kind=%s rows=%u overflow=%u "
		    "short_dropped=%u invalid_dropped=%u clock_invalid=%u expired=%u "
		    "mismatch=%u bt_skipped=%u unfinished=%u start_bt=%u finish_bt=%u "
		    "summaries=%u next_token=%llu\n",
		    kind == 2 ? "final" : "periodic",
		    summary_st.rows_used, summary_st.overflow, summary_st.short_dropped,
		    summary_st.invalid_dropped, summary_st.clock_invalid, summary_st.expired,
		    summary_st.mismatch, summary_st.bt_skipped, summary_st.unfinished,
		    summary_st.start_bt, summary_st.finish_bt, summary_st.summaries,
		    (unsigned long long)summary_st.next_token);
	}
	for (unsigned i = 0; i < n; i++) {
		struct nova_sblong_row *r = &out[i].row;
		unsigned slot = out[i].slot;
		if (out[i].event == 1) {
			printf("nova: sblong v=15 event=start row=%u slot=%u token=%llu "
			    "seq=%llu taskgen=%llu tid=%llu begin_ticks=%llu wait_ticks=%llu "
			    "request_id=%d recv_id=%d endpoint_pid=%d endpoint_gen=%llu "
			    "endpoint_stop=%u endpoint_kind=%u phase=%u bt_state=%u n=%u "
			    "err=%u trunc=%u\n",
			    slot, slot, (unsigned long long)r->token, (unsigned long long)r->seq,
			    (unsigned long long)r->taskgen, (unsigned long long)r->tid,
			    (unsigned long long)r->begin_ticks,
			    (unsigned long long)r->wait_start_ticks, r->request_id,
			    r->recv_id, r->endpoint_pid, (unsigned long long)r->endpoint_gen,
			    r->endpoint_stop, r->endpoint_kind, r->phase, r->start_bt_state,
			    r->start_bt_count, r->start_bt_error, r->start_bt_trunc);
			for (unsigned j = 0; j < r->start_bt_count; j++) {
				printf("nova: sblong v=15 event=pc row=%u slot=%u stage=start "
				    "frame=%u token=%llu seq=%llu taskgen=%llu tid=%llu pc=0x%llx\n",
				    slot, slot, j, (unsigned long long)r->token,
				    (unsigned long long)r->seq, (unsigned long long)r->taskgen,
				    (unsigned long long)r->tid, (unsigned long long)r->start_pcs[j]);
			}
		} else {
			printf("nova: sblong v=15 event=finish row=%u slot=%u token=%llu "
			    "seq=%llu taskgen=%llu tid=%llu begin_ticks=%llu wait_ticks=%llu "
			    "finish_ticks=%llu duration_ticks=%llu clock_error=%u request_id=%d "
			    "recv_id=%d endpoint_pid=%d endpoint_gen=%llu phase=%u finish_phase=%u "
			    "result=%x bt_state=%u n=%u err=%u trunc=%u\n",
			    slot, slot, (unsigned long long)r->token, (unsigned long long)r->seq,
			    (unsigned long long)r->taskgen, (unsigned long long)r->tid,
			    (unsigned long long)r->begin_ticks,
			    (unsigned long long)r->wait_start_ticks,
			    (unsigned long long)r->finish_ticks,
			    (unsigned long long)r->duration_ticks, r->clock_error,
			    r->request_id, r->recv_id, r->endpoint_pid,
			    (unsigned long long)r->endpoint_gen, r->phase, r->finish_phase,
			    (unsigned)r->result, r->finish_bt_state, r->finish_bt_count,
			    r->finish_bt_error, r->finish_bt_trunc);
			for (unsigned j = 0; j < r->finish_bt_count; j++) {
				printf("nova: sblong v=15 event=pc row=%u slot=%u stage=finish "
				    "frame=%u token=%llu seq=%llu taskgen=%llu tid=%llu pc=0x%llx\n",
				    slot, slot, j, (unsigned long long)r->token,
				    (unsigned long long)r->seq, (unsigned long long)r->taskgen,
				    (unsigned long long)r->tid, (unsigned long long)r->finish_pcs[j]);
			}
		}
	}
}

static void
nova_sblong_send_locked(struct meizu_ipc_record *r, const struct meizu_ipc_endpoint *e,
    struct nova_sblong_bt *bt)
{
	struct nova_sblong_obs o = {
		.taskgen = r->taskgen, .tid = r->tid, .seq = r->sequence,
		.begin_ticks = r->begin_ticks, .wait_start_ticks = 0,
		.request_id = r->send_id, .recv_id = r->receive_id,
		.endpoint_pid = e->end_pid, .endpoint_gen = e->end_task_generation,
		.endpoint_stop = e->stop, .endpoint_kind = e->end_kind,
		.phase = r->phase, .pending = r->pending, .send_seen = r->send_seen,
		.nested = r->nested,
		.window_ok = meizu_ipc_window != 0 &&
		    mach_absolute_time() - meizu_ipc_started < meizu_ipc_window,
	};
	nova_sblong_admit_locked(&o, bt);
}

static void
nova_sblong_finish(struct meizu_ipc_record *r, mach_msg_return_t result, unsigned phase,
    struct nova_sblong_bt *bt)
{
	struct nova_sblong_obs o = {
		.taskgen = r->taskgen, .tid = r->tid, .seq = r->sequence,
		.begin_ticks = r->begin_ticks, .wait_start_ticks = 0,
		.finish_ticks = mach_absolute_time(),
		.request_id = r->send_id, .recv_id = r->receive_id,
		.endpoint_pid = r->send_endpoint.end_pid,
		.endpoint_gen = r->send_endpoint.end_task_generation,
		.endpoint_stop = r->send_endpoint.stop,
		.endpoint_kind = r->send_endpoint.end_kind,
		.phase = r->phase, .finish_phase = phase, .result = result,
		.pending = r->pending, .send_seen = r->send_seen, .nested = r->nested,
		.window_ok = 1,
	};
	nova_sblong_finish_locked(&o, bt);
}

/* Trap-observation durations, not end-to-end RPC latency. All state is scalar
 * and protected by the existing probe lock. The first lsd capture anchors the
 * boot-wide budget; exec generations never extend it or replenish dump quota. */
#define NOVA_LSD_TIMING_BUCKETS 32
#define NOVA_LSD_TIMING_HISTOGRAM 8
#define NOVA_LSD_TIMING_DUMPS 31
struct nova_lsd_ipc_key {
	uint64_t peer_generation;
	int peer_pid, send_id;
	unsigned peer_kind, endpoint_source, endpoint_stop, mode;
};
struct nova_lsd_ipc_totals {
	uint64_t completed, errors, sum_ticks, max_ticks;
};
struct nova_lsd_ipc_bucket {
	struct nova_lsd_ipc_key key;
	struct nova_lsd_ipc_totals totals;
};
struct nova_lsd_ipc_timing {
	uint64_t generation, started, generation_started, window, interval, last_dump;
	uint64_t thresholds[NOVA_LSD_TIMING_HISTOGRAM - 1];
	uint64_t histogram[NOVA_LSD_TIMING_HISTOGRAM];
	struct nova_lsd_ipc_totals total, overflow;
	struct nova_lsd_ipc_bucket buckets[NOVA_LSD_TIMING_BUCKETS];
	unsigned used, dumps;
	bool armed, final_dump;
};
static struct nova_lsd_ipc_timing nova_lsd_ipc_timing;

static void
nova_lsd_ipc_timing_arm_locked(uint64_t generation, uint64_t now, uint64_t window)
{
	struct nova_lsd_ipc_timing *s = &nova_lsd_ipc_timing;
	if (!generation || (s->armed && now - s->started >= s->window)) return;
	if (!s->armed) {
		static const uint64_t limits_ns[NOVA_LSD_TIMING_HISTOGRAM - 1] = {
			NSEC_PER_MSEC, 10ULL * NSEC_PER_MSEC, 100ULL * NSEC_PER_MSEC,
			NSEC_PER_SEC, 10ULL * NSEC_PER_SEC, 60ULL * NSEC_PER_SEC,
			300ULL * NSEC_PER_SEC
		};
		s->started = now;
		s->window = window;
		nanoseconds_to_absolutetime(10ULL * NSEC_PER_SEC, &s->interval);
		for (unsigned i = 0; i < NOVA_LSD_TIMING_HISTOGRAM - 1; i++)
			nanoseconds_to_absolutetime(limits_ns[i], &s->thresholds[i]);
		s->armed = true;
	}
	if (s->generation != generation) {
		s->generation = generation;
		s->generation_started = now;
		s->used = 0;
		bzero(&s->total, sizeof(s->total));
		bzero(&s->overflow, sizeof(s->overflow));
		bzero(s->histogram, sizeof(s->histogram));
		bzero(s->buckets, sizeof(s->buckets));
	}
}

static void
nova_lsd_ipc_add_locked(struct nova_lsd_ipc_totals *t, uint64_t elapsed, bool error)
{
	t->completed++;
	t->errors += error;
	t->sum_ticks += elapsed;
	if (elapsed > t->max_ticks) t->max_ticks = elapsed;
}

static bool
nova_lsd_ipc_key_equal(struct nova_lsd_ipc_key a, struct nova_lsd_ipc_key b)
{
	return a.peer_generation == b.peer_generation && a.peer_pid == b.peer_pid &&
	    a.send_id == b.send_id && a.peer_kind == b.peer_kind &&
	    a.endpoint_source == b.endpoint_source && a.endpoint_stop == b.endpoint_stop &&
	    a.mode == b.mode;
}

/* Called before the first pending->false transition. No clock conversion,
 * allocation, task/IPC lock, endpoint lookup or printing on this path. */
static void
nova_lsd_ipc_timing_complete_locked(struct meizu_ipc_record *r,
    mach_msg_return_t result, uint64_t now)
{
	struct nova_lsd_ipc_timing *s = &nova_lsd_ipc_timing;
	if (meizu_ipc_current_workers != &meizu_ipc_workers[1] || !r->pending ||
	    !s->armed || r->taskgen != s->generation ||
	    now - s->started >= s->window || r->begin_ticks < s->generation_started ||
	    now < r->begin_ticks) return;
	uint64_t elapsed = now - r->begin_ticks;
	bool error = result != MACH_MSG_SUCCESS;
	nova_lsd_ipc_add_locked(&s->total, elapsed, error);
	unsigned bin = 0;
	while (bin < NOVA_LSD_TIMING_HISTOGRAM - 1 && elapsed >= s->thresholds[bin]) bin++;
	s->histogram[bin]++;
	struct nova_lsd_ipc_key key = { .peer_pid = -1, .mode = r->begin_mode };
	struct meizu_ipc_endpoint e = {0};
	if ((r->begin_mode & MACH64_SEND_MSG) && r->send_seen) {
		e = r->send_endpoint;
		key.endpoint_source = 1; /* This trap's observed send destination. */
		key.send_id = r->send_id;
	} else if (r->link_seen) {
		e = r->link_endpoint;
		key.endpoint_source = 2; /* This receive's observed reply linkage. */
	}
	if (key.endpoint_source) {
		key.peer_pid = e.end_pid;
		key.peer_generation = e.end_task_generation;
		key.peer_kind = e.end_kind;
		key.endpoint_stop = e.stop;
	}
	for (unsigned i = 0; i < s->used; i++) {
		if (nova_lsd_ipc_key_equal(s->buckets[i].key, key)) {
			nova_lsd_ipc_add_locked(&s->buckets[i].totals, elapsed, error);
			return;
		}
	}
	if (s->used == NOVA_LSD_TIMING_BUCKETS) {
		nova_lsd_ipc_add_locked(&s->overflow, elapsed, error);
		return;
	}
	struct nova_lsd_ipc_bucket *b = &s->buckets[s->used++];
	b->key = key;
	nova_lsd_ipc_add_locked(&b->totals, elapsed, error);
}

static bool
nova_lsd_ipc_timing_claim_dump_locked(uint64_t now)
{
	struct nova_lsd_ipc_timing *s = &nova_lsd_ipc_timing;
	if (!s->armed || s->final_dump || s->dumps == NOVA_LSD_TIMING_DUMPS ||
	    (s->dumps && now - s->last_dump < s->interval)) return false;
	s->last_dump = now;
	s->dumps++;
	if (now - s->started >= s->window) s->final_dump = true;
	return true;
}

void nova_lsd_ipc_timing_dump(void);
#if defined(XNU_MEIZU_LSD_CPU_TIMING) && XNU_MEIZU_LSD_CPU_TIMING
extern void nova_lsd_cpu_timing_dump(uint64_t, uint64_t, unsigned);
#endif
void
nova_lsd_ipc_timing_dump(void)
{
	/* Single autolog caller. Static scratch avoids a >2 KiB kernel stack frame.
	 * The complete scalar snapshot shares one sample tick; print only unlocked. */
	static struct nova_lsd_ipc_timing snapshot;
	uint64_t now, worker_full, worker_nested, pending = 0;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) return;
	now = mach_absolute_time();
	if (!nova_lsd_ipc_timing_claim_dump_locked(now)) {
		lck_spin_unlock(&meizu_ipc_lock);
		return;
	}
	snapshot = nova_lsd_ipc_timing;
	struct meizu_ipc_workers *w = &meizu_ipc_workers[1];
	bool worker_match = w->generation == snapshot.generation;
	worker_full = worker_match ? w->full : 0;
	worker_nested = worker_match ? w->nested : 0;
	if (worker_match) for (unsigned i = 0; i < MEIZU_WORKER_SLOTS; i++)
		pending += w->rows[i].pending;
	lck_spin_unlock(&meizu_ipc_lock);
#if defined(XNU_MEIZU_LSD_CPU_TIMING) && XNU_MEIZU_LSD_CPU_TIMING
	/* Same claimed dump and generation, with IPC/registry locks never nested. */
	nova_lsd_cpu_timing_dump(snapshot.generation, now, snapshot.dumps);
#endif
	uint64_t absolute_ns, age_ns, sum_ns, max_ns, overflow_sum_ns, overflow_max_ns;
	absolutetime_to_nanoseconds(now, &absolute_ns);
	absolutetime_to_nanoseconds(now - snapshot.started, &age_ns);
	absolutetime_to_nanoseconds(snapshot.total.sum_ticks, &sum_ns);
	absolutetime_to_nanoseconds(snapshot.total.max_ticks, &max_ns);
	absolutetime_to_nanoseconds(snapshot.overflow.sum_ticks, &overflow_sum_ns);
	absolutetime_to_nanoseconds(snapshot.overflow.max_ticks, &overflow_max_ns);
	printf("nova: lsdipc-trap ticks=%llu absolute_ns=%llu clock=mach_absolute gen=%llu started_ticks=%llu gen_started_ticks=%llu age_ms=%llu budget_ms=300000 dump=%u max_dumps=%u final=%u buckets=%u cap=%u completed=%llu errors=%llu sum_ns=%llu max_ns=%llu overflow=%llu overflow_errors=%llu overflow_sum_ns=%llu overflow_max_ns=%llu worker_match=%u worker_full=%llu nested=%llu pending=%llu sum_is_concurrent=1\n",
	    (unsigned long long)now, (unsigned long long)absolute_ns,
	    (unsigned long long)snapshot.generation, (unsigned long long)snapshot.started,
	    (unsigned long long)snapshot.generation_started, (unsigned long long)(age_ns / NSEC_PER_MSEC),
	    snapshot.dumps, (unsigned)NOVA_LSD_TIMING_DUMPS, snapshot.final_dump,
	    snapshot.used, (unsigned)NOVA_LSD_TIMING_BUCKETS,
	    (unsigned long long)snapshot.total.completed, (unsigned long long)snapshot.total.errors,
	    (unsigned long long)sum_ns, (unsigned long long)max_ns,
	    (unsigned long long)snapshot.overflow.completed, (unsigned long long)snapshot.overflow.errors,
	    (unsigned long long)overflow_sum_ns, (unsigned long long)overflow_max_ns,
	    worker_match, (unsigned long long)worker_full, (unsigned long long)worker_nested,
	    (unsigned long long)pending);
	printf("nova: lsdipc-trap-hist ticks=%llu gen=%llu lt1ms=%llu ms1_10=%llu ms10_100=%llu ms100_1000=%llu s1_10=%llu s10_60=%llu s60_300=%llu ge300s=%llu\n",
	    (unsigned long long)now, (unsigned long long)snapshot.generation,
	    (unsigned long long)snapshot.histogram[0], (unsigned long long)snapshot.histogram[1],
	    (unsigned long long)snapshot.histogram[2], (unsigned long long)snapshot.histogram[3],
	    (unsigned long long)snapshot.histogram[4], (unsigned long long)snapshot.histogram[5],
	    (unsigned long long)snapshot.histogram[6], (unsigned long long)snapshot.histogram[7]);
	for (unsigned i = 0; i < snapshot.used; i++) {
		const struct nova_lsd_ipc_bucket *b = &snapshot.buckets[i];
		absolutetime_to_nanoseconds(b->totals.sum_ticks, &sum_ns);
		absolutetime_to_nanoseconds(b->totals.max_ticks, &max_ns);
		printf("nova: lsdipc-trap-bucket ticks=%llu gen=%llu bucket=%u peer=%d peer_gen=%llu kind=%u endpoint_source=%u stop=%u send_id=%d mode=%u completed=%llu errors=%llu sum_ns=%llu max_ns=%llu\n",
		    (unsigned long long)now, (unsigned long long)snapshot.generation, i,
		    b->key.peer_pid, (unsigned long long)b->key.peer_generation, b->key.peer_kind,
		    b->key.endpoint_source, b->key.endpoint_stop, b->key.send_id, b->key.mode,
		    (unsigned long long)b->totals.completed, (unsigned long long)b->totals.errors,
		    (unsigned long long)sum_ns, (unsigned long long)max_ns);
	}
}
/* Dedicated main-thread row: no worker-table or shared history-ring eviction. */
static struct meizu_ipc_record meizu_setup_ipc;
static uint64_t meizu_setup_ipc_generation, meizu_setup_ipc_started, meizu_setup_ipc_window;
void meizu_setup_ipc_capture(uint64_t, uint64_t, int);
void meizu_setup_ipc_retire(uint64_t);
void meizu_setup_ipc_snapshot(uint64_t, unsigned);
static void msb_arm_locked(uint64_t, uint64_t, uint64_t);
static void msb_retire_locked(uint64_t);

void
meizu_setup_ipc_capture(uint64_t generation, uint64_t tid, int pid)
{
	uint64_t window;
	nanoseconds_to_absolutetime(20ULL * NSEC_PER_SEC, &window);
	lck_spin_lock(&meizu_ipc_lock);
	meizu_setup_ipc_started = mach_absolute_time();
	meizu_setup_ipc_window = window;
	msb_arm_locked(generation, meizu_setup_ipc_started, window);
	meizu_setup_ipc = (struct meizu_ipc_record){ .taskgen = generation,
		.tid = tid, .pid = pid, .peer = -1, .link_peer = -1 };
	os_atomic_store(&meizu_setup_ipc_generation, generation, release);
	lck_spin_unlock(&meizu_ipc_lock);
}

void
meizu_setup_ipc_retire(uint64_t generation)
{
	lck_spin_lock(&meizu_ipc_lock);
	msb_retire_locked(generation);
	if (generation == meizu_setup_ipc_generation) {
		os_atomic_store(&meizu_setup_ipc_generation, 0, release);
		bzero(&meizu_setup_ipc, sizeof(meizu_setup_ipc));
	}
	lck_spin_unlock(&meizu_ipc_lock);
}
void meizu_lsd_ipc_capture(uint64_t, int);
void meizu_lsd_ipc_snapshot(uint64_t, unsigned);
void meizu_sb_worker_ipc_snapshot(uint64_t, unsigned, uint64_t);

static void
meizu_ipc_workers_arm_locked(unsigned target, uint64_t generation, int pid,
    uint64_t window)
{
	struct meizu_ipc_workers *w = &meizu_ipc_workers[target];
	w->started = mach_absolute_time();
	w->window = window;
	w->pid = pid;
	w->full = w->nested = 0;
	bzero(w->rows, sizeof(w->rows));
	if (target == 1) nova_lsd_ipc_timing_arm_locked(generation, w->started, window);
	os_atomic_store(&w->generation, generation, release);
}

void
meizu_lsd_ipc_capture(uint64_t generation, int pid)
{
	uint64_t window;
	nanoseconds_to_absolutetime(300ULL * NSEC_PER_SEC, &window);
	lck_spin_lock(&meizu_ipc_lock);
	meizu_ipc_workers_arm_locked(1, generation, pid, window);
	lck_spin_unlock(&meizu_ipc_lock);
}
unsigned meizu_container_ipc_capture(uint64_t, int);
unsigned
meizu_container_ipc_capture(uint64_t generation, int pid)
{
	uint64_t window;
	unsigned target;
	nanoseconds_to_absolutetime(300ULL * NSEC_PER_SEC, &window);
	lck_spin_lock(&meizu_ipc_lock);
	for (target = 2; target < MEIZU_IPC_TARGETS; target++) {
		if (meizu_ipc_workers[target].generation == generation) break;
		if (!meizu_ipc_workers[target].generation) {
			meizu_ipc_workers_arm_locked(target, generation, pid, window);
			break;
		}
	}
	lck_spin_unlock(&meizu_ipc_lock);
	return target;
}

/* Scalar copy only. Caller resolves identity in the exec registry AFTER unlock.
 * Inspect all 32 rows, including dependencies omitted by the 8-row print cap. */
bool meizu_lsd_ipc_dependency(uint64_t, unsigned, int *, uint64_t *, uint64_t *, uint64_t *);
bool
meizu_lsd_ipc_dependency(uint64_t generation, unsigned slot, int *pid,
    uint64_t *peer_generation, uint64_t *tid, uint64_t *sequence)
{
	bool valid = false;
	if (slot >= MEIZU_WORKER_SLOTS || !lck_spin_try_lock(&meizu_ipc_lock)) return false;
	struct meizu_ipc_workers *w = &meizu_ipc_workers[1];
	struct meizu_ipc_record *r = &w->rows[slot];
	struct meizu_ipc_endpoint e = r->link_seen ? r->link_endpoint : r->send_endpoint;
	if (generation && generation == w->generation && r->taskgen == generation &&
	    r->pending && r->sequence && mach_absolute_time() - w->started < w->window &&
	    e.stop == MEIZU_IPC_PATH_END && e.end_kind == MEIZU_IPC_PEER_SPACE &&
	    e.end_pid > 0 && e.end_task_generation) {
		*pid = e.end_pid; *peer_generation = e.end_task_generation;
		*tid = r->tid; *sequence = r->sequence; valid = true;
	}
	lck_spin_unlock(&meizu_ipc_lock);
	return valid;
}
static struct meizu_ipc_record meizu_ipc_ring[MEIZU_IPC_RECORDS];

void meizu_sb_ipc_capture(uint64_t tid, int pid, uint64_t generation);
void meizu_sb_ipc_drain(void);
bool meizu_sb_ipc_receiver(int *, uint64_t *, uint64_t *);

/* Autolog only: copy a coherent pending historical endpoint, then unlock.
 * A new operation clears the endpoint; never borrow a previous send's peer. */
bool
meizu_sb_ipc_receiver(int *pid, uint64_t *generation, uint64_t *sequence)
{
	bool valid = false;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		return false;
	}
	struct meizu_ipc_record *r = &meizu_ipc_latest;
	struct meizu_ipc_endpoint e = r->link_seen ? r->link_endpoint : r->send_endpoint;
	if (r->pending && r->sequence &&
	    mach_absolute_time() - meizu_ipc_started < meizu_ipc_window &&
	    e.stop == MEIZU_IPC_PATH_END && e.end_kind == MEIZU_IPC_PEER_SPACE &&
	    e.end_pid > 0 && e.end_task_generation != 0) {
		*pid = e.end_pid;
		*generation = e.end_task_generation;
		*sequence = r->sequence;
		valid = true;
	}
	lck_spin_unlock(&meizu_ipc_lock);
	return valid;
}

/* Successful exec arms main and workers together. Atomic task generations
 * are cheap rejection filters; selection is rechecked under the probe lock. */
void
meizu_sb_ipc_capture(uint64_t tid, int pid, uint64_t generation)
{
	uint64_t window, interval_ticks, long_ticks, now;
	nanoseconds_to_absolutetime(300ULL * NSEC_PER_SEC, &window);
	nanoseconds_to_absolutetime(10ULL * NSEC_PER_SEC, &interval_ticks);
	nanoseconds_to_absolutetime(NSEC_PER_SEC, &long_ticks);
	lck_spin_lock(&meizu_ipc_lock);
	now = mach_absolute_time();
	meizu_ipc_started = now;
	/* All shared scheduler/threshold state is initialized under this lock. */
	nova_sblong_capture_arm_locked(now, window, interval_ticks, long_ticks);
	meizu_ipc_window = window;
	meizu_ipc_head = meizu_ipc_count = 0;
	meizu_ipc_overwritten = 0;
	os_atomic_store(&meizu_ipc_dropped, 0, relaxed);
	meizu_ipc_final_dump = false;
	meizu_ipc_latest = (struct meizu_ipc_record){
		.tid = tid, .pid = pid, .taskgen = generation, .peer = -1, .link_peer = -1,
	};
	meizu_ipc_workers_arm_locked(0, generation, pid, window);
	lck_spin_unlock(&meizu_ipc_lock);
}

/* Returns with the short probe lock held, with no IPC locks acquired. */
static bool
meizu_ipc_lock_current(void)
{
	uint64_t tid = thread_tid(current_thread());
	uint64_t gen = os_atomic_load(&current_task()->task_uniqueid, relaxed);
	unsigned target;
	if (!gen) return false;
	for (target = 0; target < MEIZU_IPC_TARGETS; target++) {
		if (gen == os_atomic_load(&meizu_ipc_workers[target].generation, acquire)) break;
	}
	bool setup = gen == os_atomic_load(&meizu_setup_ipc_generation, acquire);
	if (target == MEIZU_IPC_TARGETS && !setup) return false;
	lck_spin_lock(&meizu_ipc_lock);
	if (gen == meizu_setup_ipc_generation && tid == meizu_setup_ipc.tid &&
	    mach_absolute_time() - meizu_setup_ipc_started < meizu_setup_ipc_window) {
		meizu_ipc_current_workers = NULL;
		meizu_ipc_current = &meizu_setup_ipc;
		return true;
	}
	if (target == MEIZU_IPC_TARGETS) {
		lck_spin_unlock(&meizu_ipc_lock);
		return false;
	}
	if (gen == meizu_ipc_latest.taskgen && tid == meizu_ipc_latest.tid &&
	    mach_absolute_time() - meizu_ipc_started < meizu_ipc_window) {
		meizu_ipc_current_workers = &meizu_ipc_workers[0];
		meizu_ipc_current = &meizu_ipc_latest;
		return true;
	}
	struct meizu_ipc_workers *w = &meizu_ipc_workers[target];
	if (gen == w->generation && mach_absolute_time() - w->started < w->window) {
		meizu_ipc_current_workers = w;
		for (unsigned i = 0; i < MEIZU_WORKER_SLOTS; i++) {
			if (w->rows[i].tid == tid && w->rows[i].taskgen == gen) {
				meizu_ipc_current = &w->rows[i];
				return true;
			}
		}
		for (unsigned i = 0; i < MEIZU_WORKER_SLOTS; i++) {
			if (!w->rows[i].pending) {
				w->rows[i] = (struct meizu_ipc_record){
					.tid = tid, .taskgen = gen, .pid = w->pid,
					.peer = -1, .link_peer = -1 };
				meizu_ipc_current = &w->rows[i];
				return true;
			}
		}
		w->full++;
	}
	lck_spin_unlock(&meizu_ipc_lock);
	return false;
}

static void
meizu_ipc_record_locked(unsigned phase)
{
	(*meizu_ipc_current).phase = phase;
	(*meizu_ipc_current).when = mach_absolute_time();
	if (meizu_ipc_current == &meizu_setup_ipc) return;
	if (meizu_ipc_count == MEIZU_IPC_RECORDS) {
		meizu_ipc_head = (meizu_ipc_head + 1) % MEIZU_IPC_RECORDS;
		meizu_ipc_count--;
		meizu_ipc_overwritten++;
		os_atomic_inc(&meizu_ipc_dropped, relaxed);
	}
	meizu_ipc_ring[(meizu_ipc_head + meizu_ipc_count++) % MEIZU_IPC_RECORDS] = (*meizu_ipc_current);
}

static uint64_t
meizu_ipc_begin(mach_msg_option64_t options, mach_msg_timeout_t timeout)
{
	uint64_t sequence = 0;
	if (meizu_ipc_lock_current()) {
		/* A nested entry must not replace an outer operation. Kernel nested
		 * IPC does not pass through the user trap send/wait helpers; phase
		 * guards additionally exclude cleanup/copyout callbacks. */
		if ((*meizu_ipc_current).pending) {
			if (meizu_ipc_current_workers) meizu_ipc_current_workers->nested++;
			(*meizu_ipc_current).nested++;
			lck_spin_unlock(&meizu_ipc_lock);
			return ~0ULL;
		}
		uint64_t taskgen = (*meizu_ipc_current).taskgen;
		int pid = (*meizu_ipc_current).pid;
		uint64_t tid = (*meizu_ipc_current).tid;
		sequence = ++meizu_ipc_sequence;
		(*meizu_ipc_current) = (struct meizu_ipc_record){
			.sequence = sequence, .tid = tid, .pid = pid, .peer = -1, .taskgen = taskgen,
			.link_peer = -1,
			.options = options, .timeout = timeout, .pending = true,
			.begin_ticks = mach_absolute_time(),
			.begin_mode = (unsigned)(options & (MACH64_SEND_MSG | MACH64_RCV_MSG)),
		};
		meizu_ipc_record_locked(MEIZU_IPC_ENTRY);
		lck_spin_unlock(&meizu_ipc_lock);
	}
	return sequence;
}

/* Caller owns the port lock; never acquire the probe lock here. */
static unsigned
meizu_ipc_peer_class_locked(ipc_port_t port)
{
	ip_mq_lock_held(port);
	if (!ip_active(port)) {
		return MEIZU_IPC_PEER_INACTIVE;
	}
	if (!ip_in_a_space(port)) {
		return ip_in_transit(port) ? MEIZU_IPC_PEER_TRANSIT : MEIZU_IPC_PEER_LIMBO;
	}
	if (ip_in_space(port, ipc_space_kernel)) {
		return MEIZU_IPC_PEER_KERNEL;
	}
	if (ip_in_space(port, ipc_space_reply)) {
		return MEIZU_IPC_PEER_REPLY;
	}
	return MEIZU_IPC_PEER_SPACE;
}

/* MEIZU_SETUP_BUDD_HISTORY_BEGIN
 * Independent all-thread history. The existing observer's common user-send and
 * receive-results lifecycle supplies hooks (including kevent Mach receives).
 * No payload, descriptors, selectors, task pointers, or port addresses printed.
 * Lock order: exec registry -> existing IPC probe lock. Endpoint metadata uses
 * port try-lock separately; no probe->port edge. Caller owns live message rights.
 */
#define MSB_ATTEMPTS 3
#define MSB_ENDPOINTS 128
#define MSB_BUDGET 2048
/* Retain a whole admitted attempt even if the autolog thread is delayed.
 * Admission and output remain bounded by the existing per-attempt budget. */
#define MSB_RING MSB_BUDGET
#define MSB_DRAIN_BATCH 128
struct msb_peer {
	uint64_t generation;
	int pid;
	unsigned kind, name, queued, valid;
};
struct msb_event {
	uint64_t sequence, when, generation, tid, options;
	struct msb_peer dest_peer, reply_peer;
	unsigned attempt, phase, dest, reply, bits;
	int id, result;
};
/* MEIZU_REPLY_SERVICE_BEGIN
 * Endpoint candidates are pinned by the enclosing history, never recycled.
 * Hooks borrow a live port; try-lock only, including callers holding mq locks.
 * No lock acquired by the recorder can acquire an IPC/task/scheduler lock.
 */
#include <ipc/meizu_reply_observe.h>
#define MSR_CANDIDATES 4
#define MSR_KINDS 4
#define MSR_QUOTA 16
struct msr_edge {
	uint64_t first, last, first_tid, last_tid;
	unsigned count, excess;
};
struct msr_candidate {
	unsigned endpoint;
	struct msr_edge edge[MSR_KINDS];
	unsigned owner_reserved[MSO_KINDS], owner_excess[MSO_KINDS];
	bool owner_valid[MSO_KINDS][MSO_QUOTA];
	struct meizu_reply_observation owner[MSO_KINDS][MSO_QUOTA];
};
/* MEIZU_REPLY_SERVICE_TYPES_END */

/* Fixed first/latest snapshots per stage; counts saturate, slots never recycle. */
struct msl_stage {
	struct meizu_request_observation first, last;
	unsigned count;
};
struct msl_request { struct msl_stage stages[MSL_STAGES]; };

struct msp_summary { struct meizu_selection_observation first, last; unsigned count; };
struct msb_history {
	uint64_t selection_generation, selection_loss;
	unsigned selection_target;
	struct msp_summary selection[MSP_SOURCES][MSP_DECISIONS];
	struct meizu_selection_observation antecedent[MSL_REQUESTS];
	struct msl_request requests[MSL_REQUESTS];
	unsigned request_count, request_full;
	uint64_t setup, budd, started, window;
	struct msb_event ring[MSB_RING];
	ipc_port_t endpoints[MSB_ENDPOINTS];
	unsigned head, count, admitted, overwritten, dropped, endpoint_drops;
	unsigned endpoint_count, printed, late;
	bool active, reported;
	uint64_t first_reject;
	unsigned tail[2][4], reject_begin, reject_append;
	struct msr_candidate service[MSR_CANDIDATES];
	unsigned service_count, service_full;
};
static struct msb_history msb_histories[MSB_ATTEMPTS];
static uint64_t msb_budd_generation, msb_setup_generation, msb_sequence;
static unsigned msb_attempts, msb_active;
void meizu_setup_budd_drain(void);
void meizu_setup_budd_budd(uint64_t, uint64_t);

/* MEIZU_SELECTION_RECORDER_BEGIN
 * Publication: target is written before release-publishing generation. Slots
 * never recycle. Readers acquire generation and copy scalars only. The IPC
 * lock validates active/window/target again; no source lock is acquired here.
 * loss packs closed:1,count:31,first-age-us:32 in one atomic word. Retirement
 * freezes it atomically, so a delayed contender cannot mutate closed output.
 */
uint64_t
meizu_setup_selection_generation(struct task *task)
{
 return os_atomic_load(&task->task_uniqueid, relaxed);
}
static void
msp_loss(struct msb_history *h)
{
 uint64_t age; absolutetime_to_nanoseconds(mach_absolute_time()-h->started, &age);
 uint64_t old = os_atomic_load(&h->selection_loss, relaxed), next;
 do {
  if (old >> 63) return;
  uint64_t count = (old >> 32) & 0x7fffffff;
  next = ((count < 0x7fffffff ? count+1 : count) << 32) |
      (count ? (uint32_t)old : (uint32_t)(age/1000));
  if (os_atomic_cmpxchg(&h->selection_loss, old, next, relaxed)) return;
  old = os_atomic_load(&h->selection_loss, relaxed);
 } while (true);
}
static void
msp_close(struct msb_history *h)
{
 os_atomic_or(&h->selection_loss, 1ULL << 63, relaxed);
 h->active = false;
}
uint32_t
meizu_setup_selection_armed(uint64_t generation)
{
 if (!generation) return 0;
 for (unsigned i=0; i<MSB_ATTEMPTS; i++) {
  struct msb_history *h=&msb_histories[i];
  if (os_atomic_load(&h->selection_generation, acquire)==generation &&
      !(os_atomic_load(&h->selection_loss, relaxed)>>63))
   return h->selection_target;
 }
 return 0;
}
void
meizu_setup_selection_record(uint64_t generation, unsigned source,
    unsigned decision, const struct meizu_selection_observation *row)
{
 if (!source || source>MSP_SOURCES || !decision || decision>=MSP_DECISIONS) return;
 uint32_t target=meizu_setup_selection_armed(generation);
 if (!target) return;
 struct msb_history *h=&msb_histories[(target-1)/MSL_REQUESTS];
 if (!lck_spin_try_lock(&meizu_ipc_lock)) { msp_loss(h); return; }
 if (h->active && h->setup==generation && row->target==target &&
     mach_absolute_time()-h->started<h->window) {
  struct msp_summary *s=&h->selection[source-1][decision];
  if (!s->count) s->first=*row;
  s->last=*row;
  if (s->count!=~0U) s->count++; else msp_loss(h);
 }
 lck_spin_unlock(&meizu_ipc_lock);
}
void
meizu_setup_selection_antecedent(uint32_t epoch,
    const struct meizu_selection_observation *row)
{
 if (!epoch || epoch>MSB_ATTEMPTS*MSL_REQUESTS) return;
 struct msb_history *h=&msb_histories[(epoch-1)/MSL_REQUESTS];
 if (!lck_spin_try_lock(&meizu_ipc_lock)) { msp_loss(h); return; }
 unsigned j=(epoch-1)%MSL_REQUESTS;
 if (h->active && j<h->request_count &&
     mach_absolute_time()-h->started<h->window && !h->antecedent[j].when)
  h->antecedent[j]=*row;
 lck_spin_unlock(&meizu_ipc_lock);
}
/* MEIZU_SELECTION_RECORDER_END */

/* Called while the budd registry mutex serializes replacement/retirement.
 * old=0 publishes a newly exact-path-validated generation; a stale retirement
 * cannot erase a replacement. No time limit tied to budd's earlier exec. */
void
meizu_setup_budd_budd(uint64_t old, uint64_t generation)
{
	lck_spin_lock(&meizu_ipc_lock);
	if (!old || old == msb_budd_generation) {
		os_atomic_store(&msb_budd_generation, generation, release);
		for (unsigned i = 0; i < msb_attempts; i++) {
			if (msb_histories[i].active) msb_histories[i].budd = generation;
		}
	}
	lck_spin_unlock(&meizu_ipc_lock);
}

static void
msb_arm_locked(uint64_t generation, uint64_t started, uint64_t window)
{
	if (!generation || msb_attempts == MSB_ATTEMPTS) return;
	for (unsigned i = 0; i < msb_attempts; i++) {
		if (msb_histories[i].setup == generation) return;
	}
	for (unsigned i = 0; i < msb_attempts; i++) {
		msp_close(&msb_histories[i]);
	}
	struct msb_history *h = &msb_histories[msb_attempts++];
	h->setup = generation;
	h->budd = msb_budd_generation;
	h->started = started;
	h->window = window;
	h->active = true;
	os_atomic_store(&msb_setup_generation, generation, release);
	os_atomic_store(&msb_active, 1, release);
}

static void
msb_retire_locked(uint64_t generation)
{
	for (unsigned i = 0; i < msb_attempts; i++) {
		if (msb_histories[i].setup == generation) msp_close(&msb_histories[i]);
	}
}

/* The retained object cannot be freed/reallocated while its ID is usable.
 * IDs are (immutable attempt, 1-based slot), not an address-derived cookie.
 * No slots recycled, even after detach. Zero means absent/unknown: consult
 * endpoint_drops. Retaining storage does not keep a receive right active. */
static unsigned
msb_endpoint_locked(struct msb_history *h, ipc_port_t port)
{
	if (!IP_VALID(port)) return 0;
	for (unsigned i = 0; i < h->endpoint_count; i++) {
		if (h->endpoints[i] == port) return i + 1;
	}
	if (h->endpoint_count == MSB_ENDPOINTS) {
		if (h->endpoint_drops != ~0U) h->endpoint_drops++;
		return 0;
	}
	ip_reference(port);
	h->endpoints[h->endpoint_count++] = port;
	return h->endpoint_count;
}

static struct msb_peer
msb_peer(ipc_port_t port)
{
	struct msb_peer p = {0};
	if (!IP_VALID(port) || !ip_mq_lock_try(port)) return p;
	uintptr_t receiver = 0;
	p.pid = ipc_port_get_receiver_task_locked(port, &receiver);
	p.generation = receiver ? os_atomic_load(&((task_t)receiver)->task_uniqueid, relaxed) : 0;
	p.kind = meizu_ipc_peer_class_locked(port);
	p.name = ip_get_receiver_name(port);
	p.queued = ip_active(port) ? port->ip_messages.imq_msgcount : 0;
	p.valid = 1;
	ip_mq_unlock(port);
	return p;
}

/* Called under the history lock, before the reply can be sent by budd. */
static void
msr_candidate_locked(struct msb_history *h, const struct msb_event *e)
{
	if (e->phase != 3 || e->generation != h->budd || !e->reply ||
	    !e->reply_peer.valid || e->reply_peer.generation != h->setup) return;
	for (unsigned i = 0; i < h->service_count; i++) {
		if (h->service[i].endpoint == e->reply) return;
	}
	if (h->service_count == MSR_CANDIDATES) {
		if (h->service_full != ~0U) h->service_full++;
		return;
	}
	h->service[h->service_count++].endpoint = e->reply;
}

/* Scalar-only bounded recorder; no port dereference, allocation or printf.
 * kind 1 queued, 2 direct-port filter ready, 3 direct-port process entry,
 * 4 direct delivery selected (before resume, NOT proof of runnable/oncore).
 * A lock-contention miss is deliberately unknown, never a negative edge.
 */
void meizu_setup_reply_edge(ipc_port_t, unsigned, uint64_t);
void
meizu_setup_reply_edge(ipc_port_t port, unsigned kind, uint64_t tid)
{
	if (!os_atomic_load(&msb_active, acquire) || kind < 1 || kind > MSR_KINDS) return;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) return;
	uint64_t now = mach_absolute_time();
	for (unsigned i = 0; i < msb_attempts; i++) {
		struct msb_history *h = &msb_histories[i];
		if (!h->active || now - h->started >= h->window) continue;
		for (unsigned j = 0; j < h->service_count; j++) {
			struct msr_candidate *c = &h->service[j];
			if (h->endpoints[c->endpoint - 1] != port) continue;
			struct msr_edge *e = &c->edge[kind - 1];
			if (e->count == MSR_QUOTA) {
				if (e->excess != ~0U) e->excess++;
				continue;
			}
			if (!e->count) { e->first = now; e->first_tid = tid; }
			e->last = now; e->last_tid = tid; e->count++;
		}
	}
	lck_spin_unlock(&meizu_ipc_lock);
}

/* MEIZU_REPLY_OWNER_RECORDER_BEGIN
 * No raw knote/kqueue/request pointers leave kern_event. Never recycle slots:
 * even a failed commit consumes its reservation, so stale tokens cannot alias.
 */
uint32_t
meizu_setup_reply_reserve(ipc_port_t port, unsigned kind)
{
	uint32_t token = 0;
	if (!os_atomic_load(&msb_active, acquire) || kind < 1 || kind > MSO_KINDS) return 0;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) return 0;
	uint64_t now = mach_absolute_time();
	for (unsigned i = 0; i < msb_attempts; i++) {
		struct msb_history *h = &msb_histories[i];
		if (!h->active || now - h->started >= h->window) continue;
		for (unsigned j = 0; j < h->service_count; j++) {
			struct msr_candidate *c = &h->service[j];
			if (h->endpoints[c->endpoint - 1] != port) continue;
			unsigned k = kind - 1;
			if (c->owner_reserved[k] == MSO_QUOTA) {
				if (c->owner_excess[k] != ~0U) c->owner_excess[k]++;
			} else {
				unsigned n = c->owner_reserved[k]++;
				token = 1 + (((i * MSR_CANDIDATES + j) * MSO_KINDS + k) * MSO_QUOTA + n);
			}
		}
	}
	lck_spin_unlock(&meizu_ipc_lock);
	return token;
}

void
meizu_setup_reply_commit(uint32_t token, const struct meizu_reply_observation *row)
{
	if (!token || token > MSB_ATTEMPTS * MSR_CANDIDATES * MSO_KINDS * MSO_QUOTA) return;
	unsigned v = token - 1, n = v % MSO_QUOTA;
	v /= MSO_QUOTA;
	unsigned k = v % MSO_KINDS;
	v /= MSO_KINDS;
	unsigned j = v % MSR_CANDIDATES, i = v / MSR_CANDIDATES;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) { msp_loss(&msb_histories[i]); return; }
	struct msb_history *h = &msb_histories[i];
	struct msr_candidate *c = &h->service[j];
	uint64_t now = mach_absolute_time();
	if (h->active && now - h->started < h->window &&
	    j < h->service_count && n < c->owner_reserved[k] && !c->owner_valid[k][n]) {
		c->owner[k][n] = *row;
		/* A queue can outlive observer retirement. Never join its old epoch
		 * to a new Setup/candidate generation merely because it was sampled. */
		uint32_t epoch = row->request_epoch;
		if (!epoch || (epoch - 1) / MSL_REQUESTS != i ||
		    (epoch - 1) % MSL_REQUESTS >= h->request_count) {
			c->owner[k][n].request_epoch = 0;
			c->owner[k][n].association = 0;
		}
		c->owner_valid[k][n] = true;
		/* First committed nonunique association arms only an existing epoch.
		 * Immutable per-history pair; never reread the target by pointer. */
		if (!h->selection_target && c->owner[k][n].association &&
		    c->owner[k][n].request_epoch) {
			h->selection_target = c->owner[k][n].request_epoch;
			os_atomic_store(&h->selection_generation, h->setup, release);
		}
	}
	lck_spin_unlock(&meizu_ipc_lock);
}
/* MEIZU_REPLY_OWNER_RECORDER_END */

uint32_t
meizu_setup_request_begin_task(task_t task)
{
	return meizu_setup_request_begin(os_atomic_load(&task->task_uniqueid, relaxed));
}

/* MEIZU_REQUEST_RECORDER_BEGIN
 * Never look up objects by address. Epochs name immutable history slots.
 * Try-only acquisition from either existing lock domain; scalar copies only.
 */
uint32_t
meizu_setup_request_begin(uint64_t generation)
{
	uint32_t token = 0;
	if (!generation || generation != os_atomic_load(&msb_setup_generation, acquire) ||
	    !os_atomic_load(&msb_active, acquire)) return 0;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) return 0;
	for (unsigned i = 0; i < msb_attempts; i++) {
		struct msb_history *h = &msb_histories[i];
		if (h->setup != generation || !h->active ||
		    mach_absolute_time() - h->started >= h->window) continue;
		if (h->request_count == MSL_REQUESTS) {
			if (h->request_full != ~0U) h->request_full++;
		} else {
			token = 1 + i * MSL_REQUESTS + h->request_count++;
		}
	}
	lck_spin_unlock(&meizu_ipc_lock);
	return token;
}

void
meizu_setup_request_record(uint32_t token, unsigned stage,
    const struct meizu_request_observation *row)
{
	if (!token || token > MSB_ATTEMPTS * MSL_REQUESTS ||
	    !stage || stage > MSL_STAGES) return;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) return;
	unsigned i = (token - 1) / MSL_REQUESTS, j = (token - 1) % MSL_REQUESTS;
	struct msb_history *h = &msb_histories[i];
	if (h->active && j < h->request_count &&
	    mach_absolute_time() - h->started < h->window) {
		struct msl_stage *s = &h->requests[j].stages[stage - 1];
		if (!s->count) s->first = *row;
		s->last = *row;
		if (s->count != ~0U) s->count++;
	}
	lck_spin_unlock(&meizu_ipc_lock);
}
/* MEIZU_REQUEST_RECORDER_END */

/* Rejections are probe calls, not messages. Begin and append are separate.
 * Fixed [Setup,budd][phase1..4] counters saturate and never reset on drain. */
static void
msb_reject_locked(struct msb_history *h, uint64_t gen, unsigned phase, bool begin)
{
	if (!h->first_reject) h->first_reject = mach_absolute_time();
	if (h->dropped != ~0U) h->dropped++;
	unsigned actor = gen == h->setup ? 0 : 1;
	if (phase >= 1 && phase <= 4 && h->tail[actor][phase - 1] != ~0U)
		h->tail[actor][phase - 1]++;
	unsigned *n = begin ? &h->reject_begin : &h->reject_append;
	if (*n != ~0U) (*n)++;
}
/* MEIZU_REPLY_SERVICE_END */

static void
msb_append_locked(struct msb_history *h, struct msb_event e)
{
	if (h->admitted == MSB_BUDGET) {
		msb_reject_locked(h, e.generation, e.phase, false);
		return;
	}
	h->admitted++;
	if (h->count == MSB_RING) {
		h->head = (h->head + 1) % MSB_RING;
		h->count--;
		h->overwritten++;
	}
	h->ring[(h->head + h->count++) % MSB_RING] = e;
}

/* Begin has no dependency on main TID, worker slots, or a pending Mach trap.
 * phase=1 send (after successful copyin), 3 receive-results (after dequeue).
 * A receive error does not authorize dereferencing ith_kmsg. Local-port means
 * the header reply right before copyout swaps the header's interpretation.
 */
static struct msb_event
msb_begin(unsigned phase, ipc_kmsg_t kmsg, int result, uint64_t options)
{
	struct msb_event e = {0};
	if (!os_atomic_load(&msb_active, acquire)) return e;
	uint64_t gen = os_atomic_load(&current_task()->task_uniqueid, relaxed);
	if (!gen || (gen != os_atomic_load(&msb_setup_generation, acquire) &&
	    gen != os_atomic_load(&msb_budd_generation, acquire))) return e;
	lck_spin_lock(&meizu_ipc_lock);
	for (unsigned i = 0; i < msb_attempts; i++) {
		struct msb_history *h = &msb_histories[i];
		if (h->active && gen && (gen == h->setup || gen == h->budd) &&
		    mach_absolute_time() - h->started < h->window) {
			if (h->admitted == MSB_BUDGET) {
				msb_reject_locked(h, gen, phase, true);
				break;
			}
			e.attempt = i + 1;
			e.sequence = ++msb_sequence;
			e.generation = gen;
			e.tid = thread_tid(current_thread());
			e.when = mach_absolute_time();
			e.phase = phase;
			e.options = options;
			e.result = result;
			break;
		}
	}
	lck_spin_unlock(&meizu_ipc_lock);
	if (!e.attempt) return e;
	ipc_port_t dest = IP_NULL, reply = IP_NULL;
	if (result == MACH_MSG_SUCCESS && kmsg != IKM_NULL) {
		mach_msg_header_t *hdr = ikm_header(kmsg);
		dest = hdr->msgh_remote_port;
		reply = hdr->msgh_local_port;
		e.id = hdr->msgh_id;
		e.bits = hdr->msgh_bits;
		e.dest_peer = msb_peer(dest);
		e.reply_peer = msb_peer(reply);
	}
	lck_spin_lock(&meizu_ipc_lock);
	struct msb_history *h = &msb_histories[e.attempt - 1];
	if (h->active && (gen == h->setup || gen == h->budd) &&
	    mach_absolute_time() - h->started < h->window) {
		e.dest = msb_endpoint_locked(h, dest);
		e.reply = msb_endpoint_locked(h, reply);
		msr_candidate_locked(h, &e);
		msb_append_locked(h, e);
	} else {
		if (h->late != ~0U) h->late++;
		e.attempt = 0;
	}
	lck_spin_unlock(&meizu_ipc_lock);
	return e;
}

static void
msb_end(struct msb_event e, int result)
{
	if (!e.attempt) return;
	lck_spin_lock(&meizu_ipc_lock);
	struct msb_history *h = &msb_histories[e.attempt - 1];
	if (h->active && (e.generation == h->setup || e.generation == h->budd) &&
	    mach_absolute_time() - h->started < h->window) {
		e.phase++;
		e.result = result;
		e.when = mach_absolute_time();
		msb_append_locked(h, e);
	} else if (h->late != ~0U) h->late++;
	lck_spin_unlock(&meizu_ipc_lock);
}

/* Autolog only. At most MSB_DRAIN_BATCH records per attempt/tick,
 * MSB_BUDGET per attempt/boot. A full backlog drains in at most 16 ticks.
 * No printf, release, user access, allocation, or port-lock acquisition under
 * the probe lock. Late completions cannot reinsert detached endpoints. */
void
meizu_setup_budd_drain(void)
{
	for (unsigned i = 0; i < MSB_ATTEMPTS; i++) {
		lck_spin_lock(&meizu_ipc_lock);
		bool skip = !msb_histories[i].setup || msb_histories[i].reported;
		lck_spin_unlock(&meizu_ipc_lock);
		if (skip) continue;
		for (unsigned n = 0; n < MSB_DRAIN_BATCH; n++) {
			struct msb_event e = {0};
			uint64_t started = 0, setup = 0;
			lck_spin_lock(&meizu_ipc_lock);
			struct msb_history *h = &msb_histories[i];
			if (h->active && mach_absolute_time() - h->started >= h->window) msp_close(h);
			if (h->count) {
				e = h->ring[h->head];
				h->head = (h->head + 1) % MSB_RING;
				h->count--; h->printed++;
				started = h->started; setup = h->setup;
			}
			lck_spin_unlock(&meizu_ipc_lock);
			if (!e.attempt) break;
			uint64_t age;
			absolutetime_to_nanoseconds(e.when - started, &age);
			printf("meizu: setup-budd-ipc attempt=%u setup=%llu seq=%llu gen=%llu tid=%llu phase=%u age_us=%llu id=%d result=%x options=%llx dest=%u reply=%u bits=%x dvalid=%u dkind=%u dpid=%d dgen=%llu dname=%x dq=%u rvalid=%u rkind=%u rpid=%d rgen=%llu rname=%x rq=%u\n",
			    e.attempt, setup, e.sequence, e.generation, e.tid, e.phase, age / 1000,
			    e.id, (unsigned)e.result, e.options, e.dest, e.reply, e.bits,
			    e.dest_peer.valid, e.dest_peer.kind, e.dest_peer.pid, e.dest_peer.generation,
			    e.dest_peer.name, e.dest_peer.queued, e.reply_peer.valid, e.reply_peer.kind,
			    e.reply_peer.pid, e.reply_peer.generation, e.reply_peer.name, e.reply_peer.queued);
		}
		/* Detach one ref at a time; never a large kernel-stack array. */
		for (unsigned j = 0; j < MSB_ENDPOINTS; j++) {
			ipc_port_t port = IP_NULL;
			lck_spin_lock(&meizu_ipc_lock);
			struct msb_history *h = &msb_histories[i];
			if (!h->active) { port = h->endpoints[j]; h->endpoints[j] = IP_NULL; }
			lck_spin_unlock(&meizu_ipc_lock);
			if (IP_VALID(port)) ip_release(port);
		}
		unsigned admitted = 0, printed = 0, overwritten = 0, dropped = 0, endpoints = 0, late = 0;
		bool report = false;
		uint64_t first_reject = 0;
		unsigned tail[2][4] = {{0}}, reject_begin = 0, reject_append = 0, service_count = 0, service_full = 0;
		lck_spin_lock(&meizu_ipc_lock);
		struct msb_history *h = &msb_histories[i];
		if (h->setup && !h->active && !h->count && !h->reported) {
			h->reported = report = true;
			admitted = h->admitted; printed = h->printed; overwritten = h->overwritten;
			dropped = h->dropped; endpoints = h->endpoint_drops; late = h->late;
			first_reject = h->first_reject ? h->first_reject - h->started : 0;
			memcpy(tail, h->tail, sizeof(tail));
			reject_begin = h->reject_begin; reject_append = h->reject_append;
			service_count = h->service_count; service_full = h->service_full;
		}
		unsigned active = 0;
		for (unsigned j = 0; j < msb_attempts; j++) active |= msb_histories[j].active;
		os_atomic_store(&msb_active, active, release);
		lck_spin_unlock(&meizu_ipc_lock);
		if (report) printf("meizu: setup-budd-ipc-close attempt=%u admitted=%u printed=%u overwritten=%u dropped=%u endpoint_drops=%u late_at_close=%u\n",
		    i + 1, admitted, printed, overwritten, dropped, endpoints, late);
		if (report) {

            printf("meizu: selection-tail attempt=%u target=%u loss=%u first_loss_us=%u target_state=unknown worker_attribution=unknown\n",
                i+1,h->selection_target,(unsigned)((os_atomic_load(&h->selection_loss, relaxed)>>32)&0x7fffffff),(unsigned)os_atomic_load(&h->selection_loss, relaxed));
            for (unsigned source=0;source<=MSP_SOURCES;source++) {
                unsigned size=source ? MSP_DECISIONS : MSL_REQUESTS;
                for (unsigned d=0;d<size;d++) {
                    struct msp_summary *ss=source ? &h->selection[source-1][d] : NULL;
                    unsigned count=ss ? ss->count : (h->antecedent[d].when!=0);
                    for (unsigned last=0;last<(count>1 ? 2U : count);last++) {
                        struct meizu_selection_observation o=ss ? (last ? ss->last : ss->first) : h->antecedent[d];
                        uint64_t age; absolutetime_to_nanoseconds(o.when-h->started,&age);
                        printf("meizu: selection attempt=%u source=%u decision=%u last=%u count=%u age_us=%llu target=%u root=%u rq=%u rf=%x chosen=%u qos=%u flags=%x creator=%llu returning=%llu reqcount=%u scheduled=%u idle=%u allowance=%u coop=%u priority=%u manager=%u timer=%u result=%u cause=%u excluded=%u bucket=%u active=%u,%u,%u,%u,%u,%u,%u counts=%u,%u,%u,%u,%u,%u,%u\n",
                            i+1,source,source ? d : 0,last,count,age/1000,o.target,o.root,o.root_qos,o.root_flags,o.chosen,o.qos,o.flags,o.creator,o.returning,o.reqcount,o.scheduled,o.idle,o.allowance,o.cooperative,o.priority,o.manager,o.timer,o.timer_result,o.cause,o.excluded,o.return_bucket,
                            o.active[0],o.active[1],o.active[2],o.active[3],o.active[4],o.active[5],o.active[6],o.counts[0],o.counts[1],o.counts[2],o.counts[3],o.counts[4],o.counts[5],o.counts[6]);
                        printf("meizu: selection-pools attempt=%u source=%u decision=%u last=%u special=%u,%u,%x overcommit=%u,%u,%x cooperative=%u,%u,%x manager=%u,%u,%x priority_kind=%u active_sum=%u busy=%u parallelism=%u scheduled_excluded=%u\n",
                            i+1,source,source ? d : 0,last,o.special,o.special_qos,o.special_flags,o.overcommit,o.overcommit_qos,o.overcommit_flags,o.coop,o.coop_qos,o.coop_flags,o.manager_epoch,o.manager_qos,o.manager_flags,o.priority_kind,o.active_sum,o.busy,o.parallelism,o.scheduled_excluded);
                    }
                }
            }
			/* Closed immutable history: copy one small scalar row at a time.
			 * Never print while holding a source or probe lock. */
			printf("meizu: setup-request-tail attempt=%u setup=%llu requests=%u full=%u association=nonunique-process-scope\n",
			    i + 1, h->setup, h->request_count, h->request_full);
			for (unsigned r = 0; r < h->request_count; r++) {
				for (unsigned st = 0; st < MSL_STAGES; st++) {
					struct msl_stage *s = &h->requests[r].stages[st];
					if (!s->count) continue;
					for (unsigned last = 0; last < (s->count > 1 ? 2U : 1U); last++) {
						struct meizu_request_observation o = last ? s->last : s->first;
						uint64_t age;
						absolutetime_to_nanoseconds(o.when - h->started, &age);
						printf("meizu: setup-request attempt=%u setup=%llu epoch=%u stage=%u last=%u count=%u age_us=%llu actor=%llu state=%u qos=%u flags=%x reason=%u allowance=%u active=%u busy=%u parallelism=%u scheduled=%u limit=%u threads=%u idle=%u bound_valid=%u bound=%llu association=0\n",
						    i + 1, h->setup, 1 + i * MSL_REQUESTS + r, st + 1, last, s->count,
						    age / 1000, o.actor, o.state, o.qos, o.flags, o.reason, o.allowance,
						    o.active, o.busy, o.parallelism, o.scheduled, o.limit, o.threads,
						    o.idle, o.bound_valid, o.bound);
					}
				}
			}
			absolutetime_to_nanoseconds(first_reject, &first_reject);
			printf("meizu: setup-budd-ipc-tail attempt=%u first_reject_valid=%u first_reject_age_us=%llu begin=%u append=%u setup_p1=%u setup_p2=%u setup_p3=%u setup_p4=%u budd_p1=%u budd_p2=%u budd_p3=%u budd_p4=%u candidates=%u candidate_full=%u\n",
			    i + 1, (unsigned)(dropped != 0), first_reject / 1000, reject_begin, reject_append,
			    tail[0][0], tail[0][1], tail[0][2], tail[0][3],
			    tail[1][0], tail[1][1], tail[1][2], tail[1][3], service_count, service_full);
			for (unsigned j = 0; j < service_count; j++) {
				/* Closed history: endpoints detached, numeric IDs remain immutable. */
				struct msr_candidate c;
				uint64_t started, setup;
				lck_spin_lock(&meizu_ipc_lock);
				c = h->service[j]; started = h->started; setup = h->setup;
				lck_spin_unlock(&meizu_ipc_lock);
				for (unsigned k = 0; k < MSO_KINDS; k++) {
					unsigned committed = 0;
					for (unsigned n = 0; n < c.owner_reserved[k]; n++) {
						if (!c.owner_valid[k][n]) continue;
						committed++;
						struct meizu_reply_observation *o = &c.owner[k][n];
						uint64_t age = 0;
						absolutetime_to_nanoseconds(o->when - started, &age);
						printf("meizu: setup-reply-owner attempt=%u setup=%llu endpoint=%u kind=%u sample=%u age_us=%llu actor=%llu status=%x flags=%x queuekind=%u bucket=%u qos=%x override=%u request_state=%u request_qos=%u request_override=%u bound_valid=%u bound=%llu owner_valid=%u owner=%llu request_epoch=%u association=%u\n",
						    i + 1, setup, c.endpoint, k + 1, n + 1, age / 1000, o->actor,
						    o->status, o->flags, o->queuekind, o->bucket, o->qos, o->override,
						    o->request_state, o->request_qos, o->request_override,
						    o->bound_valid, o->bound, o->owner_valid, o->owner,
						    o->request_epoch, o->association);
					}
					printf("meizu: setup-reply-owner-tail attempt=%u setup=%llu endpoint=%u kind=%u reserved=%u committed=%u excess=%u\n",
					    i + 1, setup, c.endpoint, k + 1, c.owner_reserved[k], committed, c.owner_excess[k]);
				}
				for (unsigned k = 0; k < MSR_KINDS; k++) {
					struct msr_edge *e = &c.edge[k];
					uint64_t first = 0, last = 0;
					if (e->count) {
						absolutetime_to_nanoseconds(e->first - started, &first);
						absolutetime_to_nanoseconds(e->last - started, &last);
					}
					printf("meizu: setup-reply-edge attempt=%u setup=%llu endpoint=%u kind=%u count=%u excess=%u first_us=%llu last_us=%llu first_tid=%llu last_tid=%llu\n",
					    i + 1, setup, c.endpoint, k + 1, e->count, e->excess,
					    first / 1000, last / 1000, e->first_tid, e->last_tid);
				}
			}
		}
	}
}
/* MEIZU_SETUP_BUDD_HISTORY_END */

/* Each sample is historical: locks are dropped between edges, so this is NOT
 * an atomic route or proof of the final server. Caller owns a root reference.
 * ip_get_destination's reference protects next until we take our own under
 * the current lock (ipc_port.h). Keep visited refs until done to prevent ABA
 * during cycle detection. No nested port locks, no blocking lock acquisition.
 */
/* A salted SHA digest, never an address (including for static addresses).
 * Include the global port-death epoch: equal cookies on active snapshots are
 * correlation evidence only within a death-free epoch, modulo 32-bit wrap
 * and hash collision. Unrelated port deaths intentionally break correlation.
 * No persistent references or address-to-ID table extend port lifetime.
 */
static uint64_t
meizu_ipc_cookie(ipc_port_t port, unsigned epoch)
{
	SHA256_CTX ctx;
	uint64_t digest[SHA256_DIGEST_LENGTH / sizeof(uint64_t)];
	uintptr_t address = (uintptr_t)port;
	uint64_t salt = vm_kernel_addrhash_salt;
	assert(salt != 0);
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, &salt, sizeof(salt));
	SHA256_Update(&ctx, &address, sizeof(address));
	SHA256_Update(&ctx, &epoch, sizeof(epoch));
	SHA256_Final(digest, &ctx);
	return digest[0];
}

static struct meizu_ipc_endpoint
meizu_ipc_endpoint(ipc_port_t root)
{
	struct meizu_ipc_endpoint e = { .pid = -1, .end_pid = -1 };
	ipc_port_t seen[MEIZU_IPC_PATH_MAX];
	unsigned count = 1;
	if (!IP_VALID(root)) {
		return e;
	}
	seen[0] = root; /* caller's reference remains live throughout */
	for (;;) {
		ipc_port_t port = seen[count - 1];
		if (!ip_mq_lock_try(port)) {
			e.stop = MEIZU_IPC_PATH_BUSY;
			break;
		}
		unsigned epoch = os_atomic_load(&ipc_port_timestamp_data, relaxed);
		unsigned kind = meizu_ipc_peer_class_locked(port);
		uint64_t cookie = meizu_ipc_cookie(port, epoch);
		uintptr_t receiver = 0;
		int pid = ipc_port_get_receiver_task_locked(port, &receiver);
		/* The locked receive right keeps its space/task alive through this
		 * scalar load. Never retain receiver beyond this lock. task_uniqueid
		 * is the task generation, NOT get_task_uniqueid's BSD process ID.
		 * No task/proc lock or lookup is allowed on this path. */
		uint64_t task_generation = receiver ?
		    os_atomic_load(&((task_t)receiver)->task_uniqueid, relaxed) : 0;
		unsigned queued = ip_active(port) ? port->ip_messages.imq_msgcount : 0;
		if (count == 1) {
			e.cookie = cookie;
			e.epoch = epoch;
			e.kind = kind;
			e.pid = pid;
			e.queued = queued;
		}
		e.last = cookie;
		e.end_epoch = epoch;
		e.end_kind = kind;
		e.end_pid = pid;
		e.end_task_generation = task_generation;
		e.end_queued = queued;
		e.name = ip_get_receiver_name(port);
		e.hops = count - 1;
		if (!ip_in_transit(port)) {
			e.stop = MEIZU_IPC_PATH_END;
			ip_mq_unlock(port);
			break;
		}
		ipc_port_t next = ip_get_destination(port);
		/* next is owned by the locked port, even if next is inactive. */
		if (count == 1) {
			e.first = meizu_ipc_cookie(next, epoch);
		}
		for (unsigned i = 0; i < count; i++) {
			if (seen[i] == next) {
				e.stop = MEIZU_IPC_PATH_CYCLE;
				break;
			}
		}
		if (!e.stop && count == MEIZU_IPC_PATH_MAX) {
			e.stop = MEIZU_IPC_PATH_LIMIT;
		}
		if (e.stop) {
			ip_mq_unlock(port);
			break;
		}
		ip_reference(next);
		ip_mq_unlock(port);
		seen[count++] = next;
	}
	/* All puts are outside all locks, including the diagnostic ring lock. */
	while (count > 1) {
		ip_release(seen[--count]);
	}
	return e;
}

/*
 * R16 first-request lifecycle: one fixed record, only the first selected
 * SpringBoard 6001009 request (sblong cap 1).  It pins exactly one reply-port
 * reference while the request kmsg owns a valid right, never retains a kmsg
 * after send/free, and never logs a pointer.  Event hooks do a cheap scalar
 * check and try_lock; they drop instead of blocking.  Printing happens only
 * at retirement, from a scalar snapshot taken under meizu_ipc_lock.
 */
/* NOVA_LIFE_CORE_BEGIN */
#define NOVA_LIFE_REPLY_NS (300ULL * NSEC_PER_SEC)
#define NOVA_LIFE_GRACE_NS (2ULL * NSEC_PER_SEC)
struct nova_life_row {
	uint64_t seq, taskgen, tid, deadline_ticks, grace_ticks, retire_ticks;
	uint64_t request_ticks, recv_ticks, copyout_ticks;
	uint64_t reply_begin_ticks, reply_end_ticks, finish_ticks;
	uint64_t reply_cookie, recv_token, reply_token;
	uint64_t recv_server_taskgen, recv_server_tid;
	uint64_t reply_server_taskgen, reply_server_tid;
	uintptr_t reply_port; /* identity only; never dereferenced here */
	int pid, recv_server_pid, reply_server_pid;
	int recv_id, reply_id, recv_result, reply_result, finish_result;
	unsigned reply_epoch;
	unsigned active, used, pin_owned, request_seen, recv_seen, copyout_seen;
	unsigned reply_seen, reply_end_seen, finish_seen, printed;
};
struct nova_life_stats {
	unsigned dropped, invalid, duplicate_release;
	unsigned recv_begin_drops, recv_end_drops, reply_begin_drops;
	unsigned reply_end_drops, finish_drops, drain_drops;
};

/* Admission is all-or-nothing; the caller owns/releases the actual port ref.
 * The retire deadline and the finish grace are both precomputed here once. */
static int
nova_life_admit_locked(struct nova_life_row *r, uint64_t seq, uint64_t taskgen,
    uint64_t tid, int pid, int id, uintptr_t reply_key, uint64_t now,
    uint64_t retire, uint64_t grace, uint64_t cookie, unsigned epoch)
{
	if (id != 6001009 || reply_key == 0 || !seq || !taskgen || !tid) return 0;
	if (r->used || r->active || r->pin_owned) return 0;
	*r = (struct nova_life_row){0};
	r->seq = seq; r->taskgen = taskgen; r->tid = tid; r->pid = pid;
	r->request_ticks = now;
	r->deadline_ticks = now + retire;
	if (r->deadline_ticks < now) r->deadline_ticks = ~(uint64_t)0;
	r->grace_ticks = grace;
	r->retire_ticks = r->deadline_ticks;
	r->reply_port = reply_key; r->reply_cookie = cookie; r->reply_epoch = epoch;
	r->used = 1;
	r->active = 1; r->pin_owned = 1; r->request_seen = 1;
	return 1;
}

/* 1 = matched delivery, 0 = not expecting, -1 = identity mismatch. */
static int
nova_life_receive_locked(struct nova_life_row *r, uintptr_t reply_key,
    int server_pid, uint64_t server_taskgen, uint64_t server_tid,
    uint64_t now, int id, uint64_t token)
{
	if (!r->active || !r->pin_owned) return 0;
	if (reply_key != r->reply_port) return -1;
	if (r->recv_seen || !token) return 0;
	r->recv_ticks = now; r->recv_id = id; r->recv_token = token;
	r->recv_server_pid = server_pid; r->recv_server_taskgen = server_taskgen;
	r->recv_server_tid = server_tid; r->recv_seen = 1;
	return 1;
}

static int
nova_life_copyout_locked(struct nova_life_row *r, uint64_t token,
    uint64_t server_taskgen, uint64_t server_tid, uint64_t now, int result)
{
	if (!r->active || !r->recv_seen || r->copyout_seen || r->recv_token != token) return 0;
	if (r->recv_server_taskgen != server_taskgen || r->recv_server_tid != server_tid) return 0;
	r->copyout_ticks = now; r->recv_result = result; r->copyout_seen = 1;
	return 1;
}

/* 1 = matched reply send start, 0 = not expecting, -1 = identity mismatch. */
static int
nova_life_reply_locked(struct nova_life_row *r, uintptr_t reply_key,
    int server_pid, uint64_t server_taskgen, uint64_t server_tid,
    uint64_t now, int id, uint64_t token)
{
	if (!r->active || !r->pin_owned) return 0;
	if (reply_key != r->reply_port) return -1;
	if (r->reply_seen || !token) return 0;
	r->reply_begin_ticks = now; r->reply_id = id; r->reply_token = token;
	r->reply_server_pid = server_pid; r->reply_server_taskgen = server_taskgen;
	r->reply_server_tid = server_tid; r->reply_seen = 1;
	return 1;
}

static int
nova_life_reply_end_locked(struct nova_life_row *r, uint64_t token, uint64_t now,
    int result)
{
	if (!r->active || !r->reply_seen || r->reply_end_seen) return 0;
	if (!token || r->reply_token != token) return 0;
	r->reply_end_ticks = now; r->reply_result = result; r->reply_end_seen = 1;
	return 1;
}

/* Sender identity is validated too: seq alone cannot join unrelated work. */
static int
nova_life_finish_locked(struct nova_life_row *r, uint64_t seq, uint64_t taskgen,
    uint64_t tid, uint64_t now, int result)
{
	if (!r->active || !seq || r->seq != seq || r->taskgen != taskgen ||
	    r->tid != tid || r->finish_seen) return 0;
	r->finish_ticks = now; r->finish_result = result; r->finish_seen = 1;
	/* The sender syscall can return before the server's reply send does.
	 * Keep the row alive for one precomputed grace interval, never past the
	 * hard 300 s admission deadline. */
	r->retire_ticks = now + r->grace_ticks;
	if (r->retire_ticks < now || r->retire_ticks > r->deadline_ticks) {
		r->retire_ticks = r->deadline_ticks;
	}
	return 1;
}

/* Retire once the sender finished plus grace, or the hard deadline elapsed.
 * `release` is non-zero at most once; clearing pin_owned makes a second call
 * a no-op.  Missing events stay unknown, never a negative conclusion. */
static int
nova_life_retire_locked(struct nova_life_row *r, uint64_t now,
    uintptr_t *release, const char **why)
{
	if (!r->active) return 0;
	if (r->finish_seen) {
		if (now < r->retire_ticks) return 0;
		if (why) *why = r->reply_end_seen ? "finish" : "finish-grace-unknown";
	} else {
		if (now < r->deadline_ticks) return 0;
		if (why) *why = "timeout-unknown";
	}
	if (release) *release = r->pin_owned ? r->reply_port : 0;
	r->pin_owned = 0;
	r->reply_port = 0;
	r->active = 0;
	r->recv_token = 0;
	r->reply_token = 0;
	r->printed = 1;
	return 1;
}
/* NOVA_LIFE_CORE_END */

static struct nova_life_row nova_life;
static struct nova_life_stats nova_life_stats;
static uint64_t nova_life_next_token;
/* Lock-free cheap gates: 0 means no live receive/reply invocation. */
static uint64_t nova_life_recv_pub, nova_life_reply_pub;

static void
nova_life_print(const struct nova_life_row *r, const char *why,
    const struct nova_life_stats *st)
{
	char line[2048];
	int n;

	/* One bounded line, one encoder string argument. The complete scalar
	 * schema is unchanged; the encoder never sees the 42 scalar commands. */
	n = snprintf(line, sizeof(line),
	    "nova: life v=16 why=%s missing=unknown seq=%llu taskgen=%llu tid=%llu "
	    "pid=%d request_id=6001009 recv_id=%d reply_id=%d reply_cookie=%llx "
	    "reply_epoch=%u req_ticks=%llu recv_ticks=%llu copyout_ticks=%llu "
	    "reply_begin_ticks=%llu reply_end_ticks=%llu finish_ticks=%llu "
	    "recv_token=%llu reply_token=%llu "
	    "recv_server_pid=%d recv_server_taskgen=%llu recv_server_tid=%llu "
	    "reply_server_pid=%d reply_server_taskgen=%llu reply_server_tid=%llu "
	    "recv_result=%x reply_result=%x finish_result=%x "
	    "flags=req:%u,recv:%u,copyout:%u,reply:%u,reply_end:%u,finish:%u "
	    "pin_owned:%u dropped=%u invalid_identity=%u duplicate_release=%u "
	    "lock_drops=recv_begin:%u,recv_end:%u,reply_begin:%u,reply_end:%u,"
	    "finish:%u,drain:%u\n",
	    why, (unsigned long long)r->seq, (unsigned long long)r->taskgen,
	    (unsigned long long)r->tid, r->pid, r->recv_id, r->reply_id,
	    (unsigned long long)r->reply_cookie, r->reply_epoch,
	    (unsigned long long)r->request_ticks, (unsigned long long)r->recv_ticks,
	    (unsigned long long)r->copyout_ticks,
	    (unsigned long long)r->reply_begin_ticks,
	    (unsigned long long)r->reply_end_ticks,
	    (unsigned long long)r->finish_ticks,
	    (unsigned long long)r->recv_token, (unsigned long long)r->reply_token,
	    r->recv_server_pid, (unsigned long long)r->recv_server_taskgen,
	    (unsigned long long)r->recv_server_tid, r->reply_server_pid,
	    (unsigned long long)r->reply_server_taskgen,
	    (unsigned long long)r->reply_server_tid,
	    (unsigned)r->recv_result, (unsigned)r->reply_result,
	    (unsigned)r->finish_result, r->request_seen, r->recv_seen,
	    r->copyout_seen, r->reply_seen, r->reply_end_seen, r->finish_seen,
	    r->pin_owned, st->dropped, st->invalid, st->duplicate_release,
	    st->recv_begin_drops, st->recv_end_drops, st->reply_begin_drops,
	    st->reply_end_drops, st->finish_drops, st->drain_drops);
	if (n < 0) {
		printf("nova: life v=16 event=fmt_error why=%s\n",
		    why ? why : "unknown");
		return;
	}
	if ((size_t)n >= sizeof(line)) {
		printf("nova: life v=16 event=fmt_trunc why=%s\n",
		    why ? why : "unknown");
	}
	printf("%s", line);
}

/* Called before ipc_kmsg_send with a live kmsg.  The reply right is the
 * kmsg's local port; pin it with a plain reference so the later reply send and
 * receiver can be compared by identity without ever dereferencing kmsg again.
 * Cookie/epoch are computed while the local pin is certainly owned, before the
 * row can be retired and the pin transferred. */
static void
nova_life_request(ipc_kmsg_t kmsg, uint64_t seq, uint64_t taskgen, uint64_t tid,
    int pid, int id, unsigned first)
{
	ipc_port_t reply;
	uint64_t now, retire, grace;
	unsigned epoch;
	uint64_t cookie;

	if (!first || !kmsg || id != 6001009) return;
	reply = (ipc_port_t)ikm_header(kmsg)->msgh_local_port;
	if (!IP_VALID(reply)) {
		__atomic_add_fetch(&nova_life_stats.invalid, 1, __ATOMIC_RELAXED);
		return;
	}
	ip_reference(reply);
	now = mach_absolute_time();
	nanoseconds_to_absolutetime(NOVA_LIFE_REPLY_NS, &retire);
	nanoseconds_to_absolutetime(NOVA_LIFE_GRACE_NS, &grace);
	epoch = os_atomic_load(&ipc_port_timestamp_data, relaxed);
	cookie = meizu_ipc_cookie(reply, epoch);
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		__atomic_add_fetch(&nova_life_stats.dropped, 1, __ATOMIC_RELAXED);
		ip_release(reply);
		return;
	}
	if (!nova_life_admit_locked(&nova_life, seq, taskgen, tid, pid, id,
	    (uintptr_t)reply, now, retire, grace, cookie, epoch)) {
		__atomic_add_fetch(&nova_life_stats.dropped, 1, __ATOMIC_RELAXED);
		lck_spin_unlock(&meizu_ipc_lock);
		ip_release(reply);
		return;
	}
	lck_spin_unlock(&meizu_ipc_lock);
}

/* Receiver kernel delivery begins with a live kmsg before copyout.  Returns
 * the invocation token that must accompany the matching end call. */
static uint64_t
nova_life_receive_begin(ipc_kmsg_t kmsg)
{
	ipc_port_t reply;
	thread_t thread;
	int matched;
	uint64_t token;
	if (!kmsg || ikm_header(kmsg)->msgh_id != 6001009) return 0;
	reply = (ipc_port_t)ikm_header(kmsg)->msgh_local_port;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		__atomic_add_fetch(&nova_life_stats.recv_begin_drops, 1, __ATOMIC_RELAXED);
		return 0;
	}
	thread = current_thread();
	token = ++nova_life_next_token;
	matched = nova_life_receive_locked(&nova_life, (uintptr_t)reply,
	    pid_from_task(get_threadtask(thread)), get_threadtask(thread)->task_uniqueid,
	    thread_tid(thread), mach_absolute_time(), ikm_header(kmsg)->msgh_id, token);
	if (matched < 0) {
		__atomic_add_fetch(&nova_life_stats.invalid, 1, __ATOMIC_RELAXED);
		token = 0;
	} else if (matched == 0) {
		token = 0;
	} else {
		os_atomic_store(&nova_life_recv_pub, token, release);
	}
	lck_spin_unlock(&meizu_ipc_lock);
	return token;
}

/* Receiver copyout ended (success or error).  Cheap token check first; kmsg is
 * already freed and the token ties this end to the exact invocation. */
static void
nova_life_receive_end(uint64_t token, mach_msg_return_t mr)
{
	thread_t thread;
	if (!token || os_atomic_load(&nova_life_recv_pub, acquire) != token) return;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		__atomic_add_fetch(&nova_life_stats.recv_end_drops, 1, __ATOMIC_RELAXED);
		return;
	}
	thread = current_thread();
	(void)nova_life_copyout_locked(&nova_life, token,
	    get_threadtask(thread)->task_uniqueid, thread_tid(thread),
	    mach_absolute_time(), mr);
	os_atomic_store(&nova_life_recv_pub, 0, release);
	lck_spin_unlock(&meizu_ipc_lock);
}

/* Generic send of the reply.  Match the reply port pointer, not the id. */
static uint64_t
nova_life_reply_begin(ipc_kmsg_t kmsg)
{
	ipc_port_t dest;
	thread_t thread;
	int matched;
	uint64_t token;
	if (!kmsg || ikm_header(kmsg)->msgh_id != 6001109) return 0;
	dest = (ipc_port_t)ikm_header(kmsg)->msgh_remote_port;
	if (!IP_VALID(dest)) return 0;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		__atomic_add_fetch(&nova_life_stats.reply_begin_drops, 1, __ATOMIC_RELAXED);
		return 0;
	}
	thread = current_thread();
	token = ++nova_life_next_token;
	matched = nova_life_reply_locked(&nova_life, (uintptr_t)dest,
	    pid_from_task(get_threadtask(thread)), get_threadtask(thread)->task_uniqueid,
	    thread_tid(thread), mach_absolute_time(), ikm_header(kmsg)->msgh_id, token);
	if (matched < 0) {
		__atomic_add_fetch(&nova_life_stats.invalid, 1, __ATOMIC_RELAXED);
		token = 0;
	} else if (matched == 0) {
		token = 0;
	} else {
		os_atomic_store(&nova_life_reply_pub, token, release);
	}
	lck_spin_unlock(&meizu_ipc_lock);
	return token;
}

static void
nova_life_reply_end(uint64_t token, mach_msg_return_t mr)
{
	if (!token || os_atomic_load(&nova_life_reply_pub, acquire) != token) return;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		__atomic_add_fetch(&nova_life_stats.reply_end_drops, 1, __ATOMIC_RELAXED);
		return;
	}
	if (nova_life_reply_end_locked(&nova_life, token, mach_absolute_time(), mr)) {
		os_atomic_store(&nova_life_reply_pub, 0, release);
	}
	lck_spin_unlock(&meizu_ipc_lock);
}

static void
nova_life_sender_finish(uint64_t seq, uint64_t taskgen, uint64_t tid,
    mach_msg_return_t result)
{
	if (!seq || !taskgen || !tid) return;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		__atomic_add_fetch(&nova_life_stats.finish_drops, 1, __ATOMIC_RELAXED);
		return;
	}
	(void)nova_life_finish_locked(&nova_life, seq, taskgen, tid,
	    mach_absolute_time(), result);
	lck_spin_unlock(&meizu_ipc_lock);
}

/* Retire once the sender finished plus grace or 300 s elapsed, releasing the
 * pin outside the lock and exactly once.  Counters are snapshotted under lock. */
static void
nova_life_drain(void)
{
	struct nova_life_row snap;
	struct nova_life_stats st;
	uintptr_t release = 0;
	const char *why = NULL;
	unsigned retired = 0;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		__atomic_add_fetch(&nova_life_stats.drain_drops, 1, __ATOMIC_RELAXED);
		return;
	}
	snap = nova_life;
	memset(&st, 0, sizeof(st));
	st.dropped = __atomic_load_n(&nova_life_stats.dropped, __ATOMIC_RELAXED);
	st.invalid = __atomic_load_n(&nova_life_stats.invalid, __ATOMIC_RELAXED);
	st.duplicate_release = __atomic_load_n(&nova_life_stats.duplicate_release, __ATOMIC_RELAXED);
	st.recv_begin_drops = __atomic_load_n(&nova_life_stats.recv_begin_drops, __ATOMIC_RELAXED);
	st.recv_end_drops = __atomic_load_n(&nova_life_stats.recv_end_drops, __ATOMIC_RELAXED);
	st.reply_begin_drops = __atomic_load_n(&nova_life_stats.reply_begin_drops, __ATOMIC_RELAXED);
	st.reply_end_drops = __atomic_load_n(&nova_life_stats.reply_end_drops, __ATOMIC_RELAXED);
	st.finish_drops = __atomic_load_n(&nova_life_stats.finish_drops, __ATOMIC_RELAXED);
	st.drain_drops = __atomic_load_n(&nova_life_stats.drain_drops, __ATOMIC_RELAXED);
	if (nova_life_retire_locked(&nova_life, mach_absolute_time(), &release, &why)) {
		retired = 1;
		os_atomic_store(&nova_life_recv_pub, 0, release);
		os_atomic_store(&nova_life_reply_pub, 0, release);
		__atomic_add_fetch(&nova_life_stats.duplicate_release, release ? 0 : 1, __ATOMIC_RELAXED);
		st.duplicate_release = __atomic_load_n(&nova_life_stats.duplicate_release, __ATOMIC_RELAXED);
		st.dropped = __atomic_load_n(&nova_life_stats.dropped, __ATOMIC_RELAXED);
		st.invalid = __atomic_load_n(&nova_life_stats.invalid, __ATOMIC_RELAXED);
	}
	lck_spin_unlock(&meizu_ipc_lock);
	if (release) {
		ip_release((ipc_port_t)release);
	}
	if (retired) nova_life_print(&snap, why, &st);
}

/* Fresh lookup of a historical endpoint. Anchor reference pins task/space.
 * Probe, space, and port locks never overlap; no printf or puts under locks. */
void meizu_sb_queue_snapshot(thread_t, int, uint64_t, uint64_t, unsigned);
void
meizu_sb_queue_snapshot(thread_t anchor, int pid, uint64_t generation,
    uint64_t sequence, unsigned round)
{
	task_t task = get_threadtask(anchor);
	ipc_space_t space = task->itk_space;
	ipc_port_t port = IP_NULL;
	struct meizu_ipc_endpoint e = { 0 };
	const char *status = "probe-busy", *continuity = "unknown";
	unsigned qcount = 0, seqno = 0, epoch = 0;
	uint64_t cookie = 0;
	int head = 0, head_id = 0, knotes = -1, valid = 0;
	bool selected;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		goto report_queue;
	}
	struct meizu_ipc_record *r = &meizu_ipc_latest;
	e = r->link_seen ? r->link_endpoint : r->send_endpoint;
	selected = r->pending && r->sequence == sequence &&
	    mach_absolute_time() - meizu_ipc_started < meizu_ipc_window &&
	    e.stop == MEIZU_IPC_PATH_END && e.end_kind == MEIZU_IPC_PEER_SPACE &&
	    e.end_pid == pid && e.end_task_generation == generation;
	lck_spin_unlock(&meizu_ipc_lock);
	status = "selection-changed";
	if (!selected) {
		goto report_queue;
	}
	status = "generation-changed";
	if (os_atomic_load(&task->task_uniqueid, relaxed) != generation) {
		goto report_queue;
	}
	status = "space-busy";
	if (!lck_ticket_lock_try(&space->is_lock, &ipc_lck_grp)) {
		goto report_queue;
	}
	status = "space-inactive";
	if (is_active(space)) {
		ipc_entry_t entry = ipc_entry_lookup(space, e.name);
		status = "no-receive-right";
		if (entry != IE_NULL && (entry->ie_bits & MACH_PORT_TYPE_RECEIVE)) {
			port = (ipc_port_t)entry->ie_object;
			ip_reference(port);
		}
	}
	is_read_unlock(space);
	if (port == IP_NULL) {
		goto report_queue;
	}
	status = "port-busy";
	if (!ip_mq_lock_try(port)) {
		goto release_queue;
	}
	status = "receiver-changed";
	if (ip_in_space(port, space) && ip_get_receiver_name(port) == e.name) {
		epoch = os_atomic_load(&ipc_port_timestamp_data, relaxed);
		cookie = meizu_ipc_cookie(port, epoch);
		qcount = port->ip_messages.imq_msgcount;
		seqno = port->ip_messages.imq_seqno;
		head = !ipc_kmsg_queue_empty(&port->ip_messages.imq_messages);
		if (head) {
			ipc_kmsg_t kmsg = ipc_kmsg_queue_first(&port->ip_messages.imq_messages);
			head_id = ikm_header(kmsg)->msgh_id;
		}
		if (ipc_port_has_klist(port)) {
			knotes = !SLIST_EMPTY(&port->ip_klist);
		}
		valid = 1;
		status = "sampled";
		continuity = epoch == e.end_epoch && cookie == e.last ?
		    "correlated" : "unknown";
	}
	ip_mq_unlock(port);
release_queue:
	ip_release(port);
	if (os_atomic_load(&task->task_uniqueid, relaxed) != generation) {
		status = "generation-changed";
		valid = head = head_id = 0;
		qcount = seqno = 0;
		knotes = -1;
		continuity = "unknown";
	}
report_queue:
	printf("meizu: receiver-queue seq=%llu pid=%d taskgen=%llu round=%u name=%x status=%s valid=%d qcount=%u seqno=%u head=%d head_id=%d knotes=%d old_cookie=%llx cookie=%llx old_epoch=%u epoch=%u continuity=%s\n",
	    (unsigned long long)sequence, pid, (unsigned long long)generation,
	    round, e.name, status, valid, qcount, seqno, head, head_id, knotes,
	    (unsigned long long)e.last, (unsigned long long)cookie,
	    e.end_epoch, epoch, continuity);
}

/* Claim only this thread's pending operation before receive wait setup. */
static uint64_t
meizu_ipc_link_start(void)
{
	uint64_t sequence = 0;
	if (meizu_ipc_lock_current()) {
		if (!(*meizu_ipc_current).nested && (*meizu_ipc_current).pending &&
		    ((*meizu_ipc_current).phase == MEIZU_IPC_ENTRY ||
		    (*meizu_ipc_current).phase == MEIZU_IPC_SENT)) {
			sequence = (*meizu_ipc_current).sequence;
		}
		lck_spin_unlock(&meizu_ipc_lock);
	}
	return sequence;
}

/* Translation has released its port lock; retain scalar results only. */
static void
meizu_ipc_link_record(uint64_t sequence, kern_return_t result,
    struct meizu_ipc_endpoint endpoint)
{
	if (sequence && meizu_ipc_lock_current()) {
		if (!(*meizu_ipc_current).nested && (*meizu_ipc_current).sequence == sequence && (*meizu_ipc_current).pending &&
		    ((*meizu_ipc_current).phase == MEIZU_IPC_ENTRY ||
		    (*meizu_ipc_current).phase == MEIZU_IPC_SENT)) {
			(*meizu_ipc_current).link_seen = true;
			(*meizu_ipc_current).link_result = result;
			(*meizu_ipc_current).link_peer = endpoint.pid;
			(*meizu_ipc_current).link_class = endpoint.kind;
			(*meizu_ipc_current).link_endpoint = endpoint;
			meizu_ipc_record_locked(MEIZU_IPC_LINK);
		}
		lck_spin_unlock(&meizu_ipc_lock);
	}
}

static unsigned
meizu_ipc_send(ipc_kmsg_t kmsg)
{
	uint64_t sequence = 0, life_taskgen = 0, life_tid = 0;
	int life_pid = 0;
	/* Cheap identity check first; the server's own meizu record is unrelated. */
	uint64_t life_reply = nova_life_reply_begin(kmsg);
	if (meizu_ipc_lock_current()) {
		if (!(*meizu_ipc_current).nested && (*meizu_ipc_current).phase == MEIZU_IPC_ENTRY) {
			sequence = (*meizu_ipc_current).sequence;
		}
		lck_spin_unlock(&meizu_ipc_lock);
	}
	if (!sequence) {
		return life_reply;
	}
	/* Copyin succeeded: kmsg owns the root reference during the snapshot.
	 * Never retain/dereference kmsg after ipc_kmsg_send. */
	int id = ikm_header(kmsg)->msgh_id;
	ipc_port_t dest = ikm_header(kmsg)->msgh_remote_port;
	struct meizu_ipc_endpoint endpoint = meizu_ipc_endpoint(dest);
	struct nova_sblong_bt sblong_bt = {0};
	if (meizu_ipc_lock_current()) {
		if (!(*meizu_ipc_current).nested && (*meizu_ipc_current).sequence == sequence) {
			(*meizu_ipc_current).send_id = id;
			(*meizu_ipc_current).send_seen = true;
			(*meizu_ipc_current).peer = endpoint.pid;
			(*meizu_ipc_current).peer_class = endpoint.kind;
			(*meizu_ipc_current).send_endpoint = endpoint;
			meizu_ipc_record_locked(MEIZU_IPC_SEND);
			if (meizu_ipc_current == &meizu_ipc_latest) {
				nova_sblong_send_locked(meizu_ipc_current, &endpoint, &sblong_bt);
				if (sblong_bt.first) {
					life_taskgen = (*meizu_ipc_current).taskgen;
					life_tid = (*meizu_ipc_current).tid;
					life_pid = (*meizu_ipc_current).pid;
				}
			}
		}
		lck_spin_unlock(&meizu_ipc_lock);
	}
	if (sblong_bt.first) {
		nova_life_request(kmsg, sequence, life_taskgen, life_tid, life_pid, id, 1);
	}
	if (sblong_bt.needed) {
		nova_sblong_bt_start(&sblong_bt);
	}
	return life_reply;
}

static void
meizu_ipc_wait(ipc_object_t object, mach_msg_option64_t options, unsigned timeout)
{
	if (meizu_ipc_lock_current()) {
		if (!(*meizu_ipc_current).nested && (*meizu_ipc_current).pending &&
		    ((*meizu_ipc_current).phase == MEIZU_IPC_ENTRY ||
		    (*meizu_ipc_current).phase == MEIZU_IPC_SENT ||
		    (*meizu_ipc_current).phase == MEIZU_IPC_LINK)) {
			(*meizu_ipc_current).port = io_otype(object) == IOT_PORT;
			(*meizu_ipc_current).special = (*meizu_ipc_current).port &&
			    ip_object_to_port(object) == current_thread()->ith_special_reply_port;
			(*meizu_ipc_current).options = options;
			(*meizu_ipc_current).timeout = timeout;
			meizu_ipc_record_locked(MEIZU_IPC_WAIT);
			if (meizu_ipc_current == &meizu_ipc_latest &&
			    (*meizu_ipc_current).send_seen &&
			    (*meizu_ipc_current).send_id == 6001009) {
				nova_sblong_wait_locked((*meizu_ipc_current).sequence,
				    (*meizu_ipc_current).taskgen, (*meizu_ipc_current).tid,
				    mach_absolute_time());
			}
		}
		lck_spin_unlock(&meizu_ipc_lock);
	}
}

/* Claim the outer receive before cleanup/copyout can trigger nested IPC. */
static uint64_t
meizu_ipc_receive_start(mach_msg_return_t initial, ipc_kmsg_t kmsg)
{
	uint64_t sequence = 0;
	if (meizu_ipc_lock_current()) {
		if (!(*meizu_ipc_current).nested && (*meizu_ipc_current).phase == MEIZU_IPC_WAIT) {
			sequence = (*meizu_ipc_current).sequence;
			(*meizu_ipc_current).initial = initial;
			(*meizu_ipc_current).receive_id = initial == MACH_MSG_SUCCESS ?
			    ikm_header(kmsg)->msgh_id : 0;
			meizu_ipc_record_locked(MEIZU_IPC_COPYOUT);
		}
		lck_spin_unlock(&meizu_ipc_lock);
	}
	return sequence;
}

static void
meizu_ipc_finish(uint64_t sequence, mach_msg_return_t result, unsigned phase)
{
	struct nova_sblong_bt sblong_bt = {0};
	uint64_t finish_taskgen = 0, finish_tid = 0;
	if (sequence && meizu_ipc_lock_current()) {
		if (sequence == ~0ULL) {
			if ((*meizu_ipc_current).nested) (*meizu_ipc_current).nested--;
			lck_spin_unlock(&meizu_ipc_lock);
			return;
		}
		if (!(*meizu_ipc_current).nested && (*meizu_ipc_current).sequence == sequence &&
		    (phase != MEIZU_IPC_EXIT || (*meizu_ipc_current).pending)) {
			finish_taskgen = (*meizu_ipc_current).taskgen;
			finish_tid = (*meizu_ipc_current).tid;
			nova_lsd_ipc_timing_complete_locked(meizu_ipc_current, result, mach_absolute_time());
			(*meizu_ipc_current).result = result;
			if (meizu_ipc_current == &meizu_ipc_latest &&
			    (*meizu_ipc_current).send_seen &&
			    (*meizu_ipc_current).send_id == 6001009) {
				nova_sblong_finish(meizu_ipc_current, result, phase, &sblong_bt);
			}
			(*meizu_ipc_current).pending = false;
			meizu_ipc_record_locked(phase);
		}
		lck_spin_unlock(&meizu_ipc_lock);
	}
	if (sblong_bt.needed_finish) {
		nova_sblong_bt_finish(&sblong_bt);
	}
	if (sequence && sequence != ~0ULL && finish_taskgen && finish_tid) {
		nova_life_sender_finish(sequence, finish_taskgen, finish_tid, result);
	}
}

static void
meizu_ipc_sent(mach_msg_return_t result)
{
	if (meizu_ipc_lock_current()) {
		if (!(*meizu_ipc_current).nested && (*meizu_ipc_current).phase == MEIZU_IPC_SEND) {
			(*meizu_ipc_current).result = result;
			meizu_ipc_record_locked(MEIZU_IPC_SENT);
		}
		lck_spin_unlock(&meizu_ipc_lock);
	}
}

static void
meizu_ipc_print(const char *kind, struct meizu_ipc_record r)
{
	uint64_t age;
	struct meizu_ipc_endpoint e = r.link_seen ? r.link_endpoint : r.send_endpoint;
	absolutetime_to_nanoseconds(mach_absolute_time() - r.when, &age);
	printf("meizu: sbipc %s seq=%llu pid=%d tid=%llu phase=%u pending=%u "
	    "age_ms=%llu opt=%llx timeout=%u peer=%d send_id=%d recv_id=%d "
	    "port=%u special=%u initial=%x result=%x peer_class=%u "
	    "link_seen=%u link_peer=%d link_class=%u link_kr=%x "
	    "ep=%llx epoch=%u hist_first=%llx hist_last=%llx hops=%u stop=%u "
	    "end_class=%u end_pid=%d taskgen=%llu rname=%x q=%u end_q=%u\n", kind,
	    (unsigned long long)r.sequence, r.pid, (unsigned long long)r.tid,
	    r.phase, r.pending, (unsigned long long)(age / NSEC_PER_MSEC),
	    (unsigned long long)r.options, r.timeout, r.peer, r.send_id,
	    r.receive_id, r.port, r.special, (unsigned)r.initial, (unsigned)r.result,
	    r.peer_class, r.link_seen, r.link_peer, r.link_class, (unsigned)r.link_result,
	    (unsigned long long)e.cookie, e.epoch, (unsigned long long)e.first,
	    (unsigned long long)e.last, e.hops, e.stop, e.end_kind, e.end_pid,
	    (unsigned long long)e.end_task_generation, e.name, e.queued, e.end_queued);
}

/* Scalar copy under lock; endpoint traversal happened in the target's own
 * Mach IPC hook. No retained ports/tasks, cross-thread lookup, or user read. */
void
meizu_setup_ipc_snapshot(uint64_t generation, unsigned round)
{
	struct meizu_ipc_record r = {0};
	const char *status = "busy";
	bool valid = false;
	if (lck_spin_try_lock(&meizu_ipc_lock)) {
		valid = generation && generation == meizu_setup_ipc_generation &&
		    mach_absolute_time() - meizu_setup_ipc_started < meizu_setup_ipc_window;
		if (valid) r = meizu_setup_ipc;
		status = !valid ? "retired" : !r.sequence ? "no-mach-ipc" :
		    r.pending ? "pending" : "completed";
		lck_spin_unlock(&meizu_ipc_lock);
	}
	printf("meizu: setup-ipc taskgen=%llu round=%u status=%s nested=%u\n",
	    (unsigned long long)generation, round, status, r.nested);
	if (valid && r.sequence) meizu_ipc_print("setup-main", r);
}

/* Selected receiver rounds only: cap output independently of event traffic.
 * Pending rows are never evicted by completed traffic or ring wrap. */
static void
meizu_worker_ipc_snapshot(unsigned target, uint64_t generation, unsigned round,
    uint64_t owner)
{
	struct meizu_ipc_record rows[8];
	unsigned omitted = 0, count = 0;
	uint64_t full, nested, overwritten, sample_now;
	const char *label = target >= 2 ? "containeripc" : target ? "lsdipc" : "sbworkeripc";
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		printf("meizu: %s taskgen=%llu round=%u busy=1\n", label,
		    (unsigned long long)generation, round);
		return;
	}
	struct meizu_ipc_workers *w = &meizu_ipc_workers[target];
	if (!generation || generation != w->generation) {
		lck_spin_unlock(&meizu_ipc_lock);
		return;
	}
	sample_now = mach_absolute_time();
	/* The main thread's observed ulock owner gets first output priority.
	 * This is scalar historical correlation, not present ownership proof. */
	for (unsigned pass = 0; pass < 2; pass++) {
		for (unsigned i = 0; i < MEIZU_WORKER_SLOTS; i++) {
			struct meizu_ipc_record *r = &w->rows[i];
			if (r->pending && ((r->tid == owner) == (pass == 0))) {
				if (count < 8) rows[count++] = *r;
				else omitted++;
			}
		}
	}
	full = w->full; nested = w->nested;
	overwritten = meizu_ipc_overwritten;
	lck_spin_unlock(&meizu_ipc_lock);
	printf("meizu: %s taskgen=%llu round=%u shown=%u omitted=%u cap=%u full=%llu nested=%llu ring_overwrite=%llu ownerTID=%llu\n",
	    label, (unsigned long long)generation, round, count, omitted, (unsigned)MEIZU_WORKER_SLOTS,
	    (unsigned long long)full, (unsigned long long)nested,
	    (unsigned long long)overwritten, (unsigned long long)owner);
	for (unsigned i = 0; i < count; i++) {
		struct meizu_ipc_record r = rows[i];
		struct meizu_ipc_endpoint e = r.link_seen ? r.link_endpoint : r.send_endpoint;
		uint64_t op_age, phase_age;
		absolutetime_to_nanoseconds(sample_now - r.begin_ticks, &op_age);
		absolutetime_to_nanoseconds(sample_now - r.when, &phase_age);
		printf("meizu: %s pid=%d taskgen=%llu tid=%llu seq=%llu phase=%u op_age_ms=%llu phase_age_ms=%llu opt=%llx send=%d peer=%d peer_gen=%llu stop=%u class=%u recv=%d initial=%x result=%x special=%u owner=%u\n",
		    label, r.pid, (unsigned long long)r.taskgen, (unsigned long long)r.tid,
		    (unsigned long long)r.sequence, r.phase,
		    (unsigned long long)(op_age / NSEC_PER_MSEC), (unsigned long long)(phase_age / NSEC_PER_MSEC),
		    (unsigned long long)r.options,
		    r.send_id, e.end_pid,
		    (unsigned long long)e.end_task_generation, e.stop, e.end_kind,
		    r.receive_id, (unsigned)r.initial, (unsigned)r.result, r.special,
		    (unsigned)(r.tid == owner));
	}
}

void meizu_container_ipc_snapshot(uint64_t, unsigned);
void
meizu_container_ipc_snapshot(uint64_t generation, unsigned round)
{
	for (unsigned target = 2; target < MEIZU_IPC_TARGETS; target++) {
		if (generation == os_atomic_load(&meizu_ipc_workers[target].generation, acquire)) {
			meizu_worker_ipc_snapshot(target, generation, round, 0);
			return;
		}
	}
}

void
meizu_lsd_ipc_snapshot(uint64_t generation, unsigned round)
{
	meizu_worker_ipc_snapshot(1, generation, round, 0);
}

void
meizu_sb_worker_ipc_snapshot(uint64_t generation, unsigned round, uint64_t owner)
{
	meizu_worker_ipc_snapshot(0, generation, round, owner);
}

/* Autolog prints pending SB latest at most once per ten seconds. The ring
 * retains bounded completion history without flooding the persistent log.
 * Latest per-thread state survives ring overwrite. No printing under locks. */
void
meizu_sb_ipc_drain(void)
{
	nova_sblong_drain();
	nova_life_drain();
	struct meizu_ipc_record r;
	uint64_t dropped, overwritten;
	bool report, expired;
	/* Single autolog caller: bound printing to once per second. */
	static uint64_t last_drain;
	static uint64_t last_latest;
	static uint64_t last_dropped, last_overwritten;
	uint64_t now = mach_absolute_time(), elapsed;
	absolutetime_to_nanoseconds(now - last_drain, &elapsed);
	if (last_drain && elapsed < NSEC_PER_SEC) {
		return;
	}
	last_drain = now;
	if (!lck_spin_try_lock(&meizu_ipc_lock)) {
		return;
	}
	r = meizu_ipc_latest;
	dropped = os_atomic_load(&meizu_ipc_dropped, relaxed);
	overwritten = meizu_ipc_overwritten;
	/* Capture may have reset the window while this drain printed events. */
	now = mach_absolute_time();
	expired = now - meizu_ipc_started >= meizu_ipc_window;
	absolutetime_to_nanoseconds(now - last_latest, &elapsed);
	report = r.sequence != 0 && !meizu_ipc_final_dump &&
	    (r.pending && (!last_latest || elapsed >= 10ULL * NSEC_PER_SEC || expired));
	if (expired) {
		meizu_ipc_final_dump = true;
	}
	lck_spin_unlock(&meizu_ipc_lock);
	if (report) {
		meizu_ipc_print("latest", r);
		last_latest = now;
	}
	if (report && (dropped != last_dropped || overwritten != last_overwritten)) {
		printf("meizu: sbipc loss dropped=%llu overwritten=%llu\n",
		    (unsigned long long)dropped, (unsigned long long)overwritten);
		last_dropped = dropped;
		last_overwritten = overwritten;
	}
}

/*
 * Forward declarations - kernel internal routines
 */

static mach_msg_return_t
mach_msg_rcv_link_special_reply_port(
	ipc_port_t special_reply_port,
	mach_port_name_t dest_name_port);

void
mach_msg_receive_results_complete(ipc_object_t object);

const security_token_t KERNEL_SECURITY_TOKEN = KERNEL_SECURITY_TOKEN_VALUE;
const audit_token_t KERNEL_AUDIT_TOKEN = KERNEL_AUDIT_TOKEN_VALUE;

#define mach_copyout_field(kaddr, uaddr, type_t, field) \
	mach_copyout((kaddr), (uaddr) + offsetof(type_t, field), \
	    sizeof(((type_t *)0)->field))

/*
 *	Routine:	mach_msg_receive_too_large()
 *	Purpose:
 *		Helper for mach_msg_receive_results() to handle
 *		the MACH_RCV_TOO_LARGE error when the MACH64_RCV_LARGE
 *		option is set.
 *	Returns:
 *		MACH_RCV_INVALID_DATA or the passed in mr.
 */
static mach_msg_return_t
mach_msg_receive_too_large(
	mach_msg_recv_bufs_t   *recv_bufs,
	mach_msg_recv_result_t *msgr,
	mach_msg_option64_t     options,
	mach_msg_return_t       mr)
{
	assert(mr == MACH_RCV_TOO_LARGE);
	assert(options & MACH_RCV_LARGE);

	if (options & MACH64_RCV_LINEAR_VECTOR) {
		/*
		 * If MACH64_RCV_LINEAR_VECTOR is set, kevent is calling
		 * from filt_machportprocess() and the reporting of name
		 * and sizes happen via the knote in filt_machportprocess()
		 * rather than a receive operation, there's nothing for us
		 * to do here.
		 */
		return mr;
	}

	/*
	 * For the regular case, just copyout the size
	 * (and optional port name) in a fake header.
	 */
	if ((options & MACH64_RCV_LARGE_IDENTITY) &&
	    recv_bufs->recv_msg_size >=
	    offsetof(mach_msg_user_header_t, msgh_voucher_port)) {
		/*
		 * If MACH64_RCV_LARGE_IDENTITY is set,
		 * we monkey patch the msgh_local_port field.
		 */
		if (mach_copyout_field(&msgr->msgr_recv_name,
		    recv_bufs->recv_msg_addr,
		    mach_msg_user_header_t, msgh_local_port)) {
			mr = MACH_RCV_INVALID_DATA;
		}
	}

	if (recv_bufs->recv_msg_size >=
	    offsetof(mach_msg_user_header_t, msgh_remote_port)) {
		/*
		 * For all cases, we monkey patch the msgh_size
		 * field with how much size is needed.
		 */
		if (mach_copyout_field(&msgr->msgr_msg_size,
		    recv_bufs->recv_msg_addr,
		    mach_msg_user_header_t, msgh_size)) {
			mr = MACH_RCV_INVALID_DATA;
		}
	}

	if (recv_bufs->recv_aux_addr) {
		/*
		 * Then we report the incoming aux size as well,
		 * if the caller has an aux buffer.
		 */
		assert(recv_bufs->recv_aux_size >= sizeof(mach_msg_aux_header_t));
		if (mach_copyout_field(&msgr->msgr_aux_size,
		    recv_bufs->recv_aux_addr,
		    mach_msg_aux_header_t, msgdh_size)) {
			mr = MACH_RCV_INVALID_DATA;
		}
	}

	return mr;
}

/*
 *  Routine:    mach_msg_receive_error   [internal]
 *  Purpose:
 *      Builds a minimal header/trailer and copies it to
 *      the user message buffer.  Invoked when in the case of a
 *      MACH_RCV_TOO_LARGE or MACH_RCV_BODY_ERROR error.
 *  Conditions:
 *      Nothing locked. kmsg is freed upon return.
 *  Returns:
 *      the incoming "mr"       minimal header/trailer copied
 *      MACH_RCV_INVALID_DATA   copyout to user buffer failed
 */
static mach_msg_return_t
mach_msg_receive_error(
	ipc_kmsg_t              kmsg,
	mach_msg_recv_bufs_t   *recv_bufs,
	mach_msg_recv_result_t *msgr,
	mach_msg_option64_t     options,
	ipc_space_t             space,
	vm_map_t                map,
	mach_msg_return_t       mr)
{
	/*
	 * Copy out the destination port in the message.
	 *
	 * Destroy all other rights and memory in the message,
	 * and turn it into a very simple bare message with just a header.
	 */
	ipc_kmsg_copyout_dest_to_user(kmsg, space);

	/*
	 * Copy the message to user space and return the size
	 * (note that ipc_kmsg_put_to_user may also adjust the actual
	 * msg and aux size copied out to user-space).
	 */
	return ipc_kmsg_put_to_user(kmsg, recv_bufs, msgr, options, map, mr);
}

/*
 *  Routine:    mach_msg_receive_results
 *  Purpose:
 *      Receive a message.
 *  Conditions:
 *      Nothing locked.
 *
 *  Returns:
 *          sizep (out): copied out size of message proper
 *          aux_sizep (out): copied out size of aux data
 *      MACH_MSG_SUCCESS    Received a message.
 *      MACH_RCV_INVALID_NAME   The name doesn't denote a right,
 *          or the denoted right is not receive or port set.
 *      MACH_RCV_IN_SET     Receive right is a member of a set.
 *      MACH_RCV_TOO_LARGE  Message wouldn't fit into buffer.
 *      MACH_RCV_TIMED_OUT  Timeout expired without a message.
 *      MACH_RCV_INTERRUPTED    Reception interrupted.
 *      MACH_RCV_PORT_DIED  Port/set died while receiving.
 *      MACH_RCV_PORT_CHANGED   Port moved into set while receiving.
 *      MACH_RCV_INVALID_DATA   Couldn't copy to user buffer.
 *      MACH_RCV_INVALID_NOTIFY Bad notify port.
 *      MACH_RCV_HEADER_ERROR
 */
mach_msg_return_t
mach_msg_receive_results(
	mach_msg_recv_result_t *msgr_out)
{
	thread_t                self    = current_thread();
	ipc_space_t             space   = current_space();
	vm_map_t                map     = current_map();

	/*
	 * /!\IMPORTANT/!\: Pull out values we stashed on thread struct now.
	 * Values may be stomped over if copyio operations in this function
	 * trigger kernel IPC calls.
	 */
	ipc_object_t            object  = self->ith_object;
	mach_msg_return_t       mr      = self->ith_state;
	mach_msg_option64_t     options = self->ith_option;
	ipc_kmsg_t              kmsg    = self->ith_kmsg;
	struct msb_event msb_receive = msb_begin(3, kmsg, mr, options);
	uint64_t meizu_receive_sequence = meizu_ipc_receive_start(mr, kmsg);
	uint64_t nova_life_receive_token = nova_life_receive_begin(kmsg);

	mach_msg_recv_bufs_t recv_bufs = self->ith_recv_bufs;
	mach_msg_recv_result_t msgr = {
		.msgr_seqno     = self->ith_seqno,
		.msgr_context   = 0,
	};

	/*
	 * unlink the special_reply_port before releasing reference to object.
	 * get the thread's turnstile, if the thread donated it's turnstile to the port
	 */
	mach_msg_receive_results_complete(object);
	io_release(object);

	if (options & MACH64_RCV_LINEAR_VECTOR) {
		assert(recv_bufs.recv_aux_addr == 0);
		assert(recv_bufs.recv_aux_size == 0);
	}

	if (mr == MACH_RCV_TOO_LARGE) {
		/* these ith_* fields are only set for MACH_RCV_TOO_LARGE */
		msgr.msgr_msg_size  = self->ith_msize;
		msgr.msgr_aux_size  = self->ith_asize;
		msgr.msgr_recv_name = self->ith_receiver_name;

		/*
		 * If the receive operation occurs with MACH_RCV_LARGE set
		 * then no message was extracted from the queue, and the size
		 * and (optionally) receiver names were the only thing captured.
		 */
		if (options & MACH64_RCV_LARGE) {
			mr = mach_msg_receive_too_large(&recv_bufs, &msgr,
			    options, mr);
		} else {
			/* discard importance in message */
			ipc_importance_clean(kmsg);
			mr = mach_msg_receive_error(kmsg, &recv_bufs, &msgr,
			    options, space, map, mr);
			/* kmsg freed */
		}
	}
	if (mr != MACH_MSG_SUCCESS) {
		goto out;
	}

#if IMPORTANCE_INHERITANCE

	/* adopt/transform any importance attributes carried in the message */
	ipc_importance_receive(kmsg, options);

#endif  /* IMPORTANCE_INHERITANCE */

	/* auto redeem the voucher in the message */
	ipc_voucher_receive_postprocessing(kmsg, options);

	/* Save destination port context for the trailer before copyout */
	msgr.msgr_context = ikm_header(kmsg)->msgh_remote_port->ip_context;

	/*
	 * restore the recv_bufs values that are used by
	 * ipc_kmsg_copyout_guarded_port_descriptor()
	 */
	self->ith_recv_bufs = recv_bufs;
	mr = ipc_kmsg_copyout(kmsg, space, map, options);

	if (mr != MACH_MSG_SUCCESS) {
		/* already received importance, so have to undo that here */
		ipc_importance_unreceive(kmsg, options);

		/* if we had a body error copyout what we have, otherwise a simple header/trailer */
		if ((mr & ~MACH_MSG_MASK) == MACH_RCV_BODY_ERROR) {
			mr = ipc_kmsg_put_to_user(kmsg, &recv_bufs, &msgr,
			    options, map, mr);
		} else {
			mr = mach_msg_receive_error(kmsg, &recv_bufs, &msgr,
			    options, space, map, mr);
		}
	} else {
		msgr.msgr_priority = kmsg->ikm_ppriority;
		msgr.msgr_qos_ovrd = kmsg->ikm_qos_override;
		mr = ipc_kmsg_put_to_user(kmsg, &recv_bufs, &msgr,
		    options, map, mr);
	}
	/* kmsg freed */

out:
	nova_life_receive_end(nova_life_receive_token, mr);
	msb_end(msb_receive, mr);
	meizu_ipc_finish(meizu_receive_sequence, mr, MEIZU_IPC_DONE);
	if (msgr_out) {
		*msgr_out = msgr;
	}
	return mr;
}

void
mach_msg_receive_continue(void)
{
	mach_msg_return_t mr;

	ipc_port_thread_group_unblocked();

#if MACH_FLIPC
	if (current_thread()->ith_state == MACH_PEEK_READY) {
		thread_syscall_return(MACH_PEEK_READY);
		__builtin_unreachable();
	}
#endif /* MACH_FLIPC */

	mr = mach_msg_receive_results(NULL);
	thread_syscall_return(mr);
}

/*
 *  Routine:    mach_msg_validate_data_vectors
 *  Purpose:
 *      Perform validations on message and auxiliary data vectors
 *      we have copied in.
 */
static mach_msg_return_t
mach_msg_validate_data_vectors(
	mach_msg_vector_t       *msg_vec,
	mach_msg_vector_t       *aux_vec,
	mach_msg_size_t         vec_count,
	__unused mach_msg_option64_t     option64,
	bool                    sending)
{
	mach_msg_size_t msg_size = 0, aux_size = 0; /* user size */

	assert(vec_count <= MACH_MSGV_MAX_COUNT);
	assert(option64 & MACH64_MSG_VECTOR);

	assert(msg_vec != NULL);
	assert(aux_vec != NULL);

	if (vec_count == 0) {
		/*
		 * can't use MACH_RCV_TOO_LARGE or MACH_RCV_INVALID_DATA here because
		 * they imply a message has been dropped. use a new error code that
		 * suggests an early error and that message is still queued.
		 */
		return sending ? MACH_SEND_MSG_TOO_SMALL : MACH_RCV_INVALID_ARGUMENTS;
	}

	/*
	 * Validate first (message proper) data vector.
	 *
	 * Since we are using mach_msg2_trap() to shim existing mach_msg() calls,
	 * we unfortunately cannot validate message rcv address or message rcv size
	 * at this point for compatibility reasons.
	 *
	 * (1) If rcv address is invalid, we will destroy the incoming message during
	 * ipc_kmsg_put_to_user(), instead of returning an error before receive
	 * is attempted.
	 * (2) If rcv size is smaller than the minimal message header and trailer
	 * that mach_msg_receive_error() builds, we will truncate the message
	 * and copy out a partial message.
	 *
	 * See: ipc_kmsg_put_vector_to_user().
	 */
	if (sending) {
		if (msg_vec->msgv_data == 0) {
			return MACH_SEND_INVALID_DATA;
		}
		msg_size = msg_vec->msgv_send_size;
		if ((msg_size < sizeof(mach_msg_user_header_t)) || (msg_size & 3)) {
			return MACH_SEND_MSG_TOO_SMALL;
		}
		if (msg_size > IPC_KMSG_MAX_BODY_SPACE) {
			return MACH_SEND_TOO_LARGE;
		}
	}

	/* Validate second (optional auxiliary) data vector */
	if (vec_count == MACH_MSGV_MAX_COUNT) {
		if (sending) {
			aux_size = aux_vec->msgv_send_size;
			if (aux_size != 0 && aux_vec->msgv_data == 0) {
				return MACH_SEND_INVALID_DATA;
			}
			if (aux_size != 0 && aux_size < sizeof(mach_msg_aux_header_t)) {
				return MACH_SEND_AUX_TOO_SMALL;
			}
			if (aux_size > IPC_KMSG_MAX_AUX_DATA_SPACE) {
				return MACH_SEND_AUX_TOO_LARGE;
			}
		} else {
			mach_vm_address_t rcv_addr = aux_vec->msgv_rcv_addr ?
			    aux_vec->msgv_rcv_addr : aux_vec->msgv_data;

			if (rcv_addr == 0) {
				return MACH_RCV_INVALID_ARGUMENTS;
			}
			/*
			 * We are using this aux vector to receive, kernel will at
			 * least copy out an empty aux data header.
			 *
			 * See: ipc_kmsg_put_vector_to_user()
			 */
			aux_size = aux_vec->msgv_rcv_size;
			if (aux_size < sizeof(mach_msg_aux_header_t)) {
				return MACH_RCV_INVALID_ARGUMENTS;
			}
		}
	} else {
		if (sending) {
			/*
			 * Not sending aux data vector, but we still might have copied it
			 * in if doing a combined send/receive. Nil out the send size.
			 */
			aux_vec->msgv_send_size = 0;
		} else {
			/* Do the same for receive */
			aux_vec->msgv_rcv_size = 0;
		}
	}

	return MACH_MSG_SUCCESS;
}

/*
 *  Routine:    mach_msg_copyin_data_vectors
 *  Purpose:
 *      Copy in and message user data vectors.
 */
static mach_msg_return_t
mach_msg_copyin_data_vectors(
	mach_vm_address_t   data_addr,/* user address */
	mach_msg_size_t     cpin_count,
	mach_msg_option64_t option64,
	mach_msg_vector_t   *msg_vecp,/* out */
	mach_msg_vector_t   *aux_vecp)/* out */
{
	mach_msg_vector_t data_vecs[MACH_MSGV_MAX_COUNT] = {};

	static_assert(MACH_MSGV_MAX_COUNT == 2);
	assert(option64 & MACH64_MSG_VECTOR);

	if (cpin_count > MACH_MSGV_MAX_COUNT) {
		return (option64 & MACH64_SEND_MSG) ?
		       MACH_SEND_INVALID_DATA : MACH_RCV_INVALID_ARGUMENTS;
	}

	if (cpin_count == 0) {
		return (option64 & MACH64_SEND_MSG) ?
		       MACH_SEND_MSG_TOO_SMALL : MACH_RCV_INVALID_ARGUMENTS;
	}

	if (mach_copyin(data_addr, data_vecs,
	    cpin_count * sizeof(mach_msg_vector_t))) {
		return (option64 & MACH64_SEND_MSG) ?
		       MACH_SEND_INVALID_DATA : MACH_RCV_INVALID_ARGUMENTS;
	}

	memcpy(msg_vecp, &data_vecs[MACH_MSGV_IDX_MSG], sizeof(mach_msg_vector_t));

	if (cpin_count == MACH_MSGV_MAX_COUNT) {
		memcpy(aux_vecp, &data_vecs[MACH_MSGV_IDX_AUX], sizeof(mach_msg_vector_t));
	}

	return MACH_MSG_SUCCESS;
}

#if IPC_HAS_LEGACY_MACH_MSG_TRAP
/*
 *  Routine:    mach_msg_copyin_user_header
 *  Purpose:
 *      Copy in the message header, or up until message body if message is
 *      large enough. Returns the header of the message and number of descriptors.
 *      Used for mach_msg_overwrite_trap() only. Not available on embedded.
 *  Returns:
 *      MACH_MSG_SUCCESS - Copyin succeeded, msg_addr and msg_size are validated.
 *      MACH_SEND_MSG_TOO_SMALL
 *      MACH_SEND_TOO_LARGE
 *      MACH_SEND_INVALID_DATA
 */
static mach_msg_return_t
mach_msg_copyin_user_header(
	mach_msg_send_uctx_t   *send_uctx,
	mach_msg_option64_t     options)
{
	mach_msg_return_t       mr = MACH_MSG_SUCCESS;

	if (send_uctx->send_msg_size < sizeof(mach_msg_user_header_t) ||
	    (send_uctx->send_msg_size & 3)) {
		return MACH_SEND_MSG_TOO_SMALL;
	}

	if (send_uctx->send_msg_size > IPC_KMSG_MAX_BODY_SPACE) {
		return MACH_SEND_TOO_LARGE;
	}

	if (send_uctx->send_msg_size < sizeof(mach_msg_user_base_t)) {
		static_assert(offsetof(mach_msg_send_uctx_t, send_dsc_count) ==
		    offsetof(mach_msg_user_base_t, body.msgh_descriptor_count));

		mr = copyinmsg(send_uctx->send_msg_addr,
		    &send_uctx->send_header, sizeof(mach_msg_user_header_t));
	} else {
		mr = copyinmsg(send_uctx->send_msg_addr,
		    &send_uctx->send_header, sizeof(mach_msg_user_base_t));
	}
	if (mr != KERN_SUCCESS) {
		return MACH_SEND_INVALID_DATA;
	}

	/*
	 * If the message claims to be complex, it must at least
	 * have the length of a "base" message (header + dsc_count).
	 */
	if (send_uctx->send_header.msgh_bits & MACH_MSGH_BITS_COMPLEX) {
		if (send_uctx->send_msg_size < sizeof(mach_msg_user_base_t)) {
			return MACH_SEND_MSG_TOO_SMALL;
		}
	} else {
		send_uctx->send_dsc_count = 0;
	}
	send_uctx->send_header.msgh_size = send_uctx->send_msg_size;

	return ipc_policy_allow_legacy_send_trap(send_uctx->send_header.msgh_id,
	           options);
}
#endif /* IPC_HAS_LEGACY_MACH_MSG_TRAP */


__attribute__((noinline, cold))
static mach_msg_return_t
mach_msg_receive_pseudo(
	ipc_kmsg_t              kmsg,
	mach_msg_send_uctx_t   *send_uctx,
	mach_msg_option64_t     options,
	ipc_space_t             space,
	vm_map_t                map)
{
	mach_msg_recv_bufs_t recv_bufs = {
		.recv_msg_addr  = send_uctx->send_msg_addr,
		.recv_aux_addr  = send_uctx->send_aux_addr,
		.recv_msg_size  = send_uctx->send_msg_size,
		.recv_aux_size  = send_uctx->send_aux_size,
	};
	mach_msg_recv_result_t msgr = {
		.msgr_recv_name = MSGR_PSEUDO_RECEIVE,
	};
	mach_msg_return_t mr;

	/*
	 * set the recv_bufs values that are used by
	 * ipc_kmsg_copyout_guarded_port_descriptor()
	 */
	current_thread()->ith_recv_bufs = recv_bufs;
	mr = ipc_kmsg_copyout_pseudo(kmsg, space, map);
	(void)ipc_kmsg_put_to_user(kmsg, &recv_bufs, &msgr, options, map, mr);
	return mr;
}

/*
 *  Routine:    mach_msg_trap_send [internal]
 *  Purpose:
 *      Send a message.
 *  Conditions:
 *      MACH_SEND_MSG is set. aux_send_size is bound checked.
 *      send_aux_{addr, size} are 0 if not vector send.
 *      send_msg_size needs additional bound checks.
 *  Returns:
 *      All of mach_msg_send error codes.
 */
static mach_msg_return_t
mach_msg_trap_send(
	/* shared args between send and receive */
	mach_msg_send_uctx_t   *send_uctx,
	mach_msg_option64_t     options,
	mach_msg_timeout_t      msg_timeout,
	mach_msg_priority_t     priority)
{
	ipc_space_t       space = current_space();
	vm_map_t          map = current_map();
	ipc_kmsg_t        kmsg;
	mach_msg_return_t mr;

	assert(options & MACH64_SEND_MSG);

	/*
	 * Validate the send sizes and header carefuly.
	 */
	if (send_uctx->send_msg_size < sizeof(mach_msg_user_header_t) ||
	    (send_uctx->send_msg_size & 3)) {
		return MACH_SEND_MSG_TOO_SMALL;
	}
	if (send_uctx->send_msg_size > IPC_KMSG_MAX_BODY_SPACE) {
		return MACH_SEND_TOO_LARGE;
	}
	/*
	 * Complex message must have a body, also do a bound check on descriptor count
	 * (more in ikm_check_descriptors()).
	 */
	if (send_uctx->send_header.msgh_bits & MACH_MSGH_BITS_COMPLEX) {
		if (send_uctx->send_msg_size < sizeof(mach_msg_user_base_t)) {
			return MACH_SEND_MSG_TOO_SMALL;
		}
		if (send_uctx->send_dsc_count >
		    (send_uctx->send_msg_size - sizeof(mach_msg_user_base_t)) /
		    sizeof(mach_msg_type_descriptor_t)) {
			return MACH_SEND_MSG_TOO_SMALL;
		}
	} else if (send_uctx->send_dsc_count != 0) {
		/*
		 * Simple message cannot contain descriptors.
		 *
		 * This invalid config can only happen from mach_msg2_trap()
		 * since desc_count is passed as its own trap argument.
		 */
		assert(options & MACH64_MACH_MSG2);
		return MACH_SEND_TOO_LARGE;
	}

	/*
	 * Now that we have validated send_msg_size, send_aux_size and
	 * send_dsc_count, copy in the message.
	 */
	kmsg = ipc_kmsg_alloc(send_uctx->send_msg_size, send_uctx->send_aux_size,
	    send_uctx->send_dsc_count, IPC_KMSG_ALLOC_USER);
	if (kmsg == IKM_NULL) {
		return MACH_SEND_NO_BUFFER;
	}

	KERNEL_DEBUG_CONSTANT(MACHDBG_CODE(DBG_MACH_IPC, MACH_IPC_KMSG_LINK) | DBG_FUNC_NONE,
	    (uintptr_t)send_uctx->send_msg_addr, VM_KERNEL_ADDRPERM((uintptr_t)kmsg),
	    0, 0, 0);

	/*
	 * holding kmsg ref
	 * may add MACH64_SEND_ALWAYS to options
	 */
	mr = ipc_kmsg_copyin_from_user(kmsg, send_uctx, space, map, priority, &options);
	if (mr != MACH_MSG_SUCCESS) {
		ipc_kmsg_free(kmsg);
		return mr;
	}

	struct msb_event msb_send = msb_begin(1, kmsg, MACH_MSG_SUCCESS, options);
	unsigned life_reply = meizu_ipc_send(kmsg);
	mr = ipc_kmsg_send(kmsg, options, msg_timeout);
	nova_life_reply_end(life_reply, mr);
	msb_end(msb_send, mr);
	meizu_ipc_sent(mr);

	if (mr != MACH_MSG_SUCCESS) {
		mr |= mach_msg_receive_pseudo(kmsg, send_uctx, options, space, map);
		/* kmsg is freed */
	}

	return mr;
}

/*
 *  Routine:    mach_msg_trap_receive [internal]
 *  Purpose:
 *      Receive a message.
 *  Conditions:
 *      MACH_RCV_MSG is set.
 *      max_{msg, aux}_rcv_size are already validated.
 *  Returns:
 *      All of mach_msg_receive error codes.
 */
static mach_msg_return_t
mach_msg_trap_receive(
	/* shared args between send and receive */
	mach_vm_address_t   msg_addr,
	mach_vm_address_t   aux_addr,        /* 0 if not vector send/rcv */
	mach_msg_option64_t option64,
	mach_msg_timeout_t  msg_timeout,
	mach_port_name_t    sync_send,
	/* msg receive args */
	mach_msg_size_t     max_msg_rcv_size,
	mach_msg_size_t     max_aux_rcv_size,        /* 0 if not vector send/rcv */
	mach_port_name_t    rcv_name)
{
	ipc_object_t object;

	thread_t           self = current_thread();
	ipc_space_t        space = current_space();
	mach_msg_return_t  mr = MACH_MSG_SUCCESS;

	assert(option64 & MACH64_RCV_MSG);

	mr = ipc_mqueue_copyin(space, rcv_name, &object);
	if (mr != MACH_MSG_SUCCESS) {
		return mr;
	}
	/* hold ref for object */

	if (sync_send != MACH_PORT_NULL) {
		ipc_port_t special_reply_port = ip_object_to_port(object);
		/* link the special reply port to the destination */
		mr = mach_msg_rcv_link_special_reply_port(special_reply_port, sync_send);
		if (mr != MACH_MSG_SUCCESS) {
			io_release(object);
			return mr;
		}
	}

	/* Set up message proper receive params on thread */
	bzero(&self->ith_receive, sizeof(self->ith_receive));
	self->ith_recv_bufs = (mach_msg_recv_bufs_t){
		.recv_msg_addr = msg_addr,
		.recv_msg_size = max_msg_rcv_size,
		.recv_aux_addr = max_aux_rcv_size ? aux_addr : 0,
		.recv_aux_size = max_aux_rcv_size,
	};
	self->ith_object = object;
	self->ith_option = option64;
	self->ith_knote  = ITH_KNOTE_NULL; /* not part of ith_receive */

	meizu_ipc_wait(object, option64, msg_timeout);
	ipc_mqueue_receive(io_waitq(object), msg_timeout, THREAD_ABORTSAFE,
	    self, /* continuation ? */ true);
	/* NOTREACHED if thread started waiting */

	if ((option64 & MACH_RCV_TIMEOUT) && msg_timeout == 0) {
		thread_poll_yield(self);
	}

	mr = mach_msg_receive_results(NULL);
	/* release ref on ith_object */

	return mr;
}

/*
 *  Routine:    mach_msg_overwrite_trap [mach trap]
 *  Purpose:
 *      Possibly send a message; possibly receive a message.
 *
 *		/!\ Deprecated /!\
 *      No longer supported on embedded and will be removed from macOS.
 *      Use mach_msg2_trap() instead.
 *  Conditions:
 *      Nothing locked.
 *      The 'priority' is only a QoS if MACH_SEND_OVERRIDE is passed -
 *      otherwise, it is a port name.
 *  Returns:
 *      All of mach_msg_send and mach_msg_receive error codes.
 */
mach_msg_return_t
mach_msg_overwrite_trap(
	struct mach_msg_overwrite_trap_args *args)
{
#if IPC_HAS_LEGACY_MACH_MSG_TRAP
	mach_msg_option64_t     options = args->option;
	mach_msg_return_t       mr = MACH_MSG_SUCCESS;

	options = ipc_current_user_policy(current_task(), options);

	KDBG(MACHDBG_CODE(DBG_MACH_IPC, MACH_IPC_KMSG_INFO) | DBG_FUNC_START);

	if (options & MACH_SEND_MSG) {
		mach_msg_send_uctx_t send_uctx = {
			.send_msg_addr = args->msg,
			.send_msg_size = args->send_size,
		};

		/*
		 * Perform a crude copyin of the mach_msg_user_header_t,
		 * including the next 4 bytes in case it is a complex message,
		 * in order to mimic mach_msg2_trap() behavior of synthesizing
		 * the mach_msg_user_base_t via the trap arguments.
		 *
		 * mach_msg_trap_send() will do more thorough validation of
		 * sizes and header.
		 */

		mr = mach_msg_copyin_user_header(&send_uctx, options);
		if (mr != MACH_MSG_SUCCESS) {
			return mr;
		}

		mr = mach_msg_trap_send(&send_uctx, options,
		    args->timeout, args->priority);
	}

	/* If send failed, skip receive */
	if (mr != MACH_MSG_SUCCESS) {
		KDBG(MACHDBG_CODE(DBG_MACH_IPC, MACH_IPC_KMSG_INFO) | DBG_FUNC_END, mr);
		goto end;
	}

	if (options & MACH_RCV_MSG) {
		mach_vm_address_t msg_addr = args->rcv_msg ?: args->msg;
		mach_port_name_t  sync_send = MACH_PORT_NULL;

		/*
		 * Although just the presence of MACH_RCV_SYNC_WAIT and absence of
		 * MACH_SEND_OVERRIDE should imply that 'priority' is a valid port name
		 * to link, in practice older userspace is dependent on
		 * MACH_SEND_SYNC_OVERRIDE also excluding this path.
		 */
		if ((options & MACH_RCV_SYNC_WAIT) &&
		    !(options & (MACH_SEND_OVERRIDE | MACH_SEND_MSG)) &&
		    !(options & MACH_SEND_SYNC_OVERRIDE)) {
			sync_send = (mach_port_name_t)args->priority;
		}

		mr = mach_msg_trap_receive(msg_addr, 0, options, args->timeout,
		    sync_send, args->rcv_size, 0, args->rcv_name);
	}

end:
	/* unblock call is idempotent */
	ipc_port_thread_group_unblocked();
	return mr;
#else
	(void)args;
	return KERN_NOT_SUPPORTED;
#endif /* IPC_HAS_LEGACY_MACH_MSG_TRAP */
}

/*
 *  Routine:    mach_msg_trap [mach trap]
 *  Purpose:
 *      Possibly send a message; possibly receive a message.
 *
 *		/!\ Deprecated /!\
 *      No longer supported on embedded and will be removed from macOS.
 *      Use mach_msg2_trap() instead.
 *  Conditions:
 *      Nothing locked.
 *  Returns:
 *      All of mach_msg_send and mach_msg_receive error codes.
 */
mach_msg_return_t
mach_msg_trap(
	struct mach_msg_overwrite_trap_args *args)
{
	extern struct proc *current_proc(void);
	extern int proc_getpid(struct proc *);
	extern void meizu_autolog_flush(void);
	struct proc *meizu_p = current_proc();
	mach_msg_return_t meizu_mr;

	args->rcv_msg = (mach_vm_address_t)0;
	if (meizu_p && proc_getpid(meizu_p) == 22) {
		printf("meizu: msg1 pid=22 opt=%x send=%u rcv=%u\n",
		    (unsigned)args->option, (unsigned)args->send_size,
		    (unsigned)args->rcv_size);
		meizu_autolog_flush();
	}
	meizu_mr = mach_msg_overwrite_trap(args);
	if (meizu_p && proc_getpid(meizu_p) == 22) {
		printf("meizu: msg1 ret mr=%d\n", meizu_mr);
		meizu_autolog_flush();
	}
	return meizu_mr;
}

/*
 *  Routine:    mach_msg2_trap [mach trap]
 *  Purpose:
 *      Modern mach_msg_trap() with vector message and CFI support.
 *  Conditions:
 *      Nothing locked.
 *  Returns:
 *      All of mach_msg_send and mach_msg_receive error codes.
 */
mach_msg_return_t
mach_msg2_trap_internal(
	struct mach_msg2_trap_args *args)
{
	/* packed arguments, LO_BITS_and_HI_BITS */
	uint64_t mb_ss = args->msgh_bits_and_send_size;
	uint64_t mr_lp = args->msgh_remote_and_local_port;
	uint64_t mv_id = args->msgh_voucher_and_id;
	uint64_t dc_rn = args->desc_count_and_rcv_name;
	uint64_t rs_pr = args->rcv_size_and_priority;

	mach_msg_timeout_t  msg_timeout = (mach_msg_timeout_t)args->timeout;
	mach_msg_vector_t   msg_vec = {}, aux_vec = {};
	mach_msg_size_t     vec_snd_count = 0;
	mach_msg_size_t     vec_rcv_count = 0;
	mach_msg_option64_t option64;
	mach_msg_return_t   mr = MACH_MSG_SUCCESS;

	option64 = ipc_current_user_policy(current_task(),
	    args->options) | MACH64_MACH_MSG2;
	uint64_t meizu_sequence = meizu_ipc_begin(option64, msg_timeout);

	{
		extern struct proc *current_proc(void);
		extern int proc_getpid(struct proc *);
		extern void meizu_autolog_flush(void);
		struct proc *meizu_p2 = current_proc();
		if (meizu_p2 && proc_getpid(meizu_p2) == 22) {
			printf("meizu: msg2 pid=22 opt=%llx id=%d rport=%x lport=%x "
			    "to=%u rcvsz=%u\n", (unsigned long long)option64,
			    (int)(mv_id >> 32), (unsigned)(mr_lp), (unsigned)(mr_lp >> 32),
			    (unsigned)msg_timeout, (unsigned)(rs_pr & 0xffffffff));
			meizu_autolog_flush();
		}
	}

	KDBG(MACHDBG_CODE(DBG_MACH_IPC, MACH_IPC_KMSG_INFO) | DBG_FUNC_START);

	mr = ipc_preflight_msg_option64(option64);
	if (mr != MACH_MSG_SUCCESS) {
		goto send_fail;
	}

	if (option64 & MACH64_MSG_VECTOR) {
		vec_snd_count = (mach_msg_size_t)(mb_ss >> 32);
		vec_rcv_count = (mach_msg_size_t)rs_pr;

		mr = mach_msg_copyin_data_vectors(args->data,
		    MAX(vec_snd_count, vec_rcv_count), option64, &msg_vec, &aux_vec);
		if (mr != MACH_MSG_SUCCESS) {
			goto send_fail;
		}
	}

	if (option64 & MACH64_SEND_MSG) {
		mach_msg_send_uctx_t send_uctx = {
			.send_header = {
				.msgh_bits         = (mach_msg_bits_t) (mb_ss),
				.msgh_size         = 0,
				.msgh_remote_port  = (mach_port_name_t)(mr_lp),
				.msgh_local_port   = (mach_port_name_t)(mr_lp >> 32),
				.msgh_voucher_port = (mach_port_name_t)(mv_id),
				.msgh_id           = (mach_msg_id_t)   (mv_id >> 32),
			},
			.send_dsc_count = (mach_msg_size_t)dc_rn,
		};
		mach_msg_priority_t priority = (mach_msg_priority_t)(rs_pr >> 32);

		if (option64 & MACH64_MSG_VECTOR) {
			/*
			 * only validate msg send related arguments.
			 *
			 * bad receive args do not stop us from sending
			 * during combined send/rcv.
			 */
			mr = mach_msg_validate_data_vectors(&msg_vec, &aux_vec,
			    vec_snd_count, option64, /* sending? */ TRUE);
			if (mr != MACH_MSG_SUCCESS) {
				goto send_fail;
			}

			send_uctx.send_msg_addr = msg_vec.msgv_data;
			send_uctx.send_msg_size = msg_vec.msgv_send_size;
			send_uctx.send_aux_size = aux_vec.msgv_send_size;
			if (send_uctx.send_aux_size) {
				send_uctx.send_aux_addr = aux_vec.msgv_data;
			}
		} else {
			send_uctx.send_msg_addr = args->data;
			send_uctx.send_msg_size = (mach_msg_size_t)(mb_ss >> 32);
		}

		mr = mach_msg_trap_send(&send_uctx, option64, msg_timeout, priority);
	}

send_fail:
	/* if send failed, skip receive */
	if (mr != MACH_MSG_SUCCESS) {
		KDBG(MACHDBG_CODE(DBG_MACH_IPC, MACH_IPC_KMSG_INFO) | DBG_FUNC_END, mr);
		goto end;
	}

	if (option64 & MACH64_RCV_MSG) {
		mach_port_name_t    rcv_name   = (mach_port_name_t)(dc_rn >> 32);
		mach_port_name_t    sync_send  = MACH_PORT_NULL;
		mach_vm_address_t   msg_addr   = 0;
		mach_msg_size_t     msg_size   = 0;
		mach_vm_address_t   aux_addr   = 0;
		mach_msg_size_t     aux_size   = 0;

		if (option64 & MACH64_MSG_VECTOR) {
			/* only validate msg receive related arguments */
			mr = mach_msg_validate_data_vectors(&msg_vec, &aux_vec,
			    vec_rcv_count, option64, /* sending? */ FALSE);
			if (mr != MACH_MSG_SUCCESS) {
				goto end;
			}

			msg_addr = msg_vec.msgv_rcv_addr ?: msg_vec.msgv_data;
			msg_size = msg_vec.msgv_rcv_size;
			aux_size = aux_vec.msgv_rcv_size;
			if (aux_size) {
				aux_addr = aux_vec.msgv_rcv_addr ?: aux_vec.msgv_data;
			}
		} else {
			msg_addr = args->data;
			msg_size = (mach_msg_size_t)rs_pr;
		}

		if (option64 & MACH64_RCV_SYNC_WAIT) {
			/* use msgh_remote_port as sync send boosting port */
			sync_send = (mach_port_name_t)mr_lp;
		}

		mr = mach_msg_trap_receive(msg_addr, aux_addr, option64,
		    msg_timeout, sync_send, msg_size, aux_size, rcv_name);
	}

end:
	/* unblock call is idempotent */
	ipc_port_thread_group_unblocked();
	meizu_ipc_finish(meizu_sequence, mr, MEIZU_IPC_EXIT);
	return mr;
}


mach_msg_return_t
mach_msg2_trap(
	struct mach_msg2_trap_args *args)
{
	extern struct proc *current_proc(void);
	extern int proc_getpid(struct proc *);
	extern void meizu_autolog_flush(void);
	struct proc *meizu_p = current_proc();
	int meizu_pid = meizu_p ? proc_getpid(meizu_p) : -1;
	int meizu_id = (int)(args->msgh_voucher_and_id >> 32);
	mach_msg_return_t meizu_mr;

	/* No destination-port translation probe here: translate_send returns
	 * a locked, borrowed port, not an owned send right. */

	meizu_mr = mach_msg2_trap_internal(args);

	if (meizu_pid == 21 || meizu_pid == 22) {
		printf("meizu: msg2 ret pid=%d mr=%d id=%d\n",
		    meizu_pid, meizu_mr, meizu_id);
		meizu_autolog_flush();
	}
	return meizu_mr;
}

/*
 *  Routine:    mach_msg_rcv_link_special_reply_port
 *  Purpose:
 *      Link the special reply port(rcv right) to the
 *      other end of the sync ipc channel.
 *  Conditions:
 *      Nothing locked.
 *  Returns:
 *      None.
 */
static mach_msg_return_t
mach_msg_rcv_link_special_reply_port(
	ipc_port_t special_reply_port,
	mach_port_name_t dest_name_port)
{
	ipc_port_t dest_port = IP_NULL;
	kern_return_t kr;
	uint64_t meizu_sequence;
	struct meizu_ipc_endpoint meizu_endpoint = { .pid = -1, .end_pid = -1 };

	if (current_thread()->ith_special_reply_port != special_reply_port) {
		return MACH_RCV_INVALID_NOTIFY;
	}

	/* Copyin the destination port */
	if (!MACH_PORT_VALID(dest_name_port)) {
		return MACH_RCV_INVALID_NOTIFY;
	}

	meizu_sequence = meizu_ipc_link_start();
	kr = ipc_port_translate_send(current_space(), dest_name_port, &dest_port);
	if (kr == KERN_SUCCESS) {
		/* translate_send returns an active, locked, borrowed port. */
		ip_reference(dest_port);
		ip_mq_unlock(dest_port);
		if (meizu_sequence) {
			meizu_endpoint = meizu_ipc_endpoint(dest_port);
		}
		meizu_ipc_link_record(meizu_sequence, kr, meizu_endpoint);

		/*
		 * The receive right of dest port might have gone away,
		 * do not fail the receive in that case.
		 */
		ipc_port_link_special_reply_port(special_reply_port,
		    dest_port, FALSE);

		ip_release(dest_port);
	} else {
		/* Translation failure is diagnostic only; preserve receive semantics. */
		meizu_ipc_link_record(meizu_sequence, kr, meizu_endpoint);
	}
	return MACH_MSG_SUCCESS;
}

/*
 *  Routine:    mach_msg_receive_results_complete
 *  Purpose:
 *      Get thread's turnstile back from the object and
 *              if object is a special reply port then reset its
 *      linkage.
 *  Condition:
 *      Nothing locked.
 *  Returns:
 *      None.
 */
void
mach_msg_receive_results_complete(ipc_object_t object)
{
	thread_t self = current_thread();
	ipc_port_t port = IP_NULL;
	boolean_t get_turnstile = (self->turnstile == TURNSTILE_NULL);

	if (io_otype(object) == IOT_PORT) {
		port = ip_object_to_port(object);
	} else {
		assert(self->turnstile != TURNSTILE_NULL);
		return;
	}

	uint8_t flags = IPC_PORT_ADJUST_SR_ALLOW_SYNC_LINKAGE;

	/*
	 * Don't clear the ip_srp_msg_sent bit if...
	 */
	if (!((self->ith_state == MACH_RCV_TOO_LARGE && self->ith_option & MACH_RCV_LARGE) ||         //msg was too large and the next receive will get it
	    self->ith_state == MACH_RCV_INTERRUPTED ||
	    self->ith_state == MACH_RCV_TIMED_OUT ||
#if MACH_FLIPC
	    self->ith_state == MACH_PEEK_READY ||
#endif /* MACH_FLIPC */
	    self->ith_state == MACH_RCV_PORT_CHANGED)) {
		flags |= IPC_PORT_ADJUST_SR_RECEIVED_MSG;
	}

	if (port->ip_specialreply || get_turnstile) {
		ip_mq_lock(port);
		ipc_port_adjust_special_reply_port_locked(port, NULL,
		    flags, get_turnstile);
		/* port unlocked */
	}
	assert(self->turnstile != TURNSTILE_NULL);
	/* thread now has a turnstile */
}


SECURITY_READ_ONLY_LATE(struct mach_msg_filter_callbacks) mach_msg_filter_callbacks;

kern_return_t
mach_msg_filter_register_callback(
	const struct mach_msg_filter_callbacks *callbacks)
{
	size_t size = 0;

	if (callbacks == NULL) {
		return KERN_INVALID_ARGUMENT;
	}
	if (mach_msg_filter_callbacks.fetch_filter_policy != NULL) {
		/* double init */
		return KERN_FAILURE;
	}

	if (callbacks->version >= MACH_MSG_FILTER_CALLBACKS_VERSION_0) {
		/* check for missing v0 callbacks */
		if (callbacks->fetch_filter_policy == NULL) {
			return KERN_INVALID_ARGUMENT;
		}
		size = offsetof(struct mach_msg_filter_callbacks, alloc_service_port_sblabel);
	}

	if (callbacks->version >= MACH_MSG_FILTER_CALLBACKS_VERSION_1) {
		if (callbacks->alloc_service_port_sblabel == NULL ||
		    callbacks->dealloc_service_port_sblabel == NULL ||
		    callbacks->derive_sblabel_from_service_port == NULL ||
		    callbacks->get_connection_port_filter_policy == NULL ||
		    callbacks->retain_sblabel == NULL) {
			return KERN_INVALID_ARGUMENT;
		}
		size = sizeof(struct mach_msg_filter_callbacks);
	}

	if (callbacks->version > MACH_MSG_FILTER_CALLBACKS_VERSION_1) {
		/* invalid version */
		return KERN_INVALID_ARGUMENT;
	}

	memcpy(&mach_msg_filter_callbacks, callbacks, size);
	return KERN_SUCCESS;
}

/* This function should only be called if the task and port allow message filtering */
boolean_t
mach_msg_fetch_filter_policy(
	void *port_label,
	mach_msg_id_t msgh_id,
	mach_msg_filter_id *fid)
{
	boolean_t ret = TRUE;

	if (mach_msg_fetch_filter_policy_callback == NULL) {
		*fid = MACH_MSG_FILTER_POLICY_ALLOW;
		return true;
	}
	ret = mach_msg_fetch_filter_policy_callback(current_task(), port_label, msgh_id, fid);

	return ret;
}
