/* Bounded Setup reply observation: scalars only across probe lock domains. */
#ifndef _IPC_MEIZU_REPLY_OBSERVE_H_
#define _IPC_MEIZU_REPLY_OBSERVE_H_
#include <stdint.h>
struct ipc_port;
#define MSO_KINDS 2
#define MSO_QUOTA 4
#define MSO_UNKNOWN (~0U)
/* kind 1: after activation/enqueue; kind 2: processing before suppression.
 * queuekind 1=file, 2=workq, 3=workloop.
 * request_state is a kqlock observation (QUEUED/BINDING may race).
 * bound_valid=0 means unknown; =1 with tid=0 means no bound servicer.
 * owner_valid=0 means unsupported; =1 with tid=0 means no workloop owner.
 */
struct meizu_reply_observation {
	uint64_t when, actor, bound, owner;
	unsigned status, flags, queuekind, bucket, qos, override;
	unsigned request_state, request_qos, request_override;
	unsigned bound_valid, owner_valid;
	unsigned request_epoch, association;
};
/* Caller pins port. Reserve compares only under the existing IPC probe lock.
 * Token names immutable attempt/candidate/kind/slot, never an object address.
 * Reservation consumes quota even if commit loses contention or retirement.
 */
uint32_t meizu_setup_reply_reserve(struct ipc_port *, unsigned);
void meizu_setup_reply_commit(uint32_t, const struct meizu_reply_observation *);
/* Scoped Setup request epochs: association=0 is process scope only;
 * association=1 on owner rows is a point-in-time, NONUNIQUE endpoint link.
 * No endpoint/knote/request/thread pointer is retained by this interface. */
/* 015432 closed windows reached 64 + 13/14 quota-qualified initiations.
 * 96 leaves 18 slots above the measured maximum; this is a finite coverage
 * budget, NOT a target reservation or a guarantee against future exhaustion.
 * Allocate only at real initiation; never fabricate a late NEW/epoch. */
#define MSL_REQUESTS 96
#define MSL_STAGES 8
#define MSL_NEW 1
#define MSL_QUEUED 2
#define MSL_ADMISSION 3
#define MSL_PREPOST 4
#define MSL_BOUND 5
#define MSL_PROCESS 6
#define MSL_UNBIND 7
#define MSL_CANCEL 8
struct meizu_request_observation {
	uint64_t when, actor, bound;
	unsigned state, qos, flags, reason, allowance, active, busy, parallelism;
	unsigned scheduled, limit, threads, idle, bound_valid;
};
uint32_t meizu_setup_request_begin(uint64_t);
struct task;
uint32_t meizu_setup_request_begin_task(struct task *);
void meizu_setup_request_record(uint32_t, unsigned,
    const struct meizu_request_observation *);
/* Selection scalars; no source object lifetime crosses this interface.
 * decision: 1 manager eligible, 2 manager busy, 3 priority, 4 cooperative,
 * 5 overcommit, 6 constrained pass, 7 constrained reject, 8 none,
 * 9 turnstile fallback; creator outcomes 10..18; 19 reason3 counters.
 * source: 1 creator selector, 2 returning selector, 3 creator action,
 * 4 admission. cause: 1 bucket,2 exit,3 thread-call,4 initiate,5 modify,
 * 6 AST,7 turnstile,8 dequeue. 0 unknown. target state remains unknown.
 */
#define MSP_DECISIONS 22
#define MSP_SOURCES 4
struct meizu_selection_observation {
 uint64_t when, creator, returning;
 unsigned target, root, root_qos, root_flags, chosen, qos, flags;
 unsigned reqcount, scheduled, idle, allowance, cooperative, priority;
 unsigned special, special_qos, special_flags, overcommit, overcommit_qos, overcommit_flags;
 unsigned coop, coop_qos, coop_flags, manager_epoch, manager_qos, manager_flags;
 unsigned priority_kind, active_sum, busy, parallelism, scheduled_excluded;
 unsigned manager, timer, timer_result, cause, excluded, return_bucket;
 unsigned active[7], counts[7];
};
uint64_t meizu_setup_selection_generation(struct task *);
uint32_t meizu_setup_selection_armed(uint64_t);
void meizu_setup_selection_record(uint64_t, unsigned, unsigned,
    const struct meizu_selection_observation *);
void meizu_setup_selection_antecedent(uint32_t,
    const struct meizu_selection_observation *);

#endif
