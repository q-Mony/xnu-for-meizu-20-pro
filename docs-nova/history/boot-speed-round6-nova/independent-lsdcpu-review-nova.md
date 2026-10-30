# Independent lsd CPU accounting review — Nova

2026-10-30. New report, no previous version. Reviewer implemented the separate
EPSS module but did not write the lsd CPU accounting changes reviewed here.
This review made no production edits, did not rerun an unchanged test suite,
did not operate the device, and did not modify the central changelog.

**Result: no new blocking lifecycle, lock-order, generation-matching or budget
defect found in the round-6 lsd CPU helper and its IPC call site.** The accounting
read itself has no strict time bound; this is explicitly documented and must not
be advertised as a hard-duration-bounded operation.

## Reviewed source identity

The actual files match the implementation worker's tested source manifest:

| File in actual XNU tree | SHA256 |
| --- | --- |
| bsd/kern/kern_exec.c | `302505423a0392a2110db89b957dd68c10fa2aecbf65cf20f73af3fee60523e1` |
| osfmk/ipc/mach_msg.c | `ff2e9e4241f1b434afa3f516c3e4253dc2e55dcbf9fdd8a92712f5b85d65cad0` |

Actual XNU tree:
`<nova-root>/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work`.
Review scope was the new `nova_lsd_cpu_timing_dump` body at
`bsd/kern/kern_exec.c:3590` and the guarded IPC pairing at
`osfmk/ipc/mach_msg.c:342`, with their real registry, thread/task and recount
dependencies checked for the guarantees below.

## Lifecycle and identity

- The selected registry entry must have both the exact `taskgen` and full vnode
  component name `lsd`; neither PID reuse nor a short name prefix can select it.
  The generation originates at successful exec and is passed to the IPC capture
  from that same registry input (`kern_exec.c:3955`, `:3992`).
- While the registry mutex protects its owned thread reference, the helper takes
  one extra `thread_reference`. Replacement or expiry can then remove the
  registry's own reference without freeing the borrowed anchor. Every path after
  acquiring that extra reference reaches `thread_deallocate(anchor)` once,
  including either generation mismatch; pre-acquisition skips take no reference.
- The real lifetime chain supports the comment: thread creation takes an
  internal task reference (`osfmk/kern/thread.c:1468`), final thread destruction
  drops it (`:840`), and final task deallocation alone frees recount tracks
  (`osfmk/kern/task.c:2065`, `:2192`; `recount.c:1438`). An exited but retained task
  remains a valid source of cumulative charged CPU usage.
- Generation is checked before and after recount. The helper omits counters on
  mismatch instead of presenting an invalid sample as zero. `active_before` and
  `active_after` are explicitly advisory; object lifetime comes from the reference.
  Registry replacement after borrowing can still produce a valid old-generation
  sample paired with the already-captured IPC generation, which is correct.

## Locks and publication pairing

- `nova_lsd_ipc_timing_dump` claims and copies its scalar state under the existing
  IPC spin lock, releases that lock, then invokes the CPU helper. No IPC-to-registry
  lock nesting is added (`mach_msg.c:325`–`:342`).
- The CPU helper uses `lck_mtx_try_lock`, so a busy registry produces a skip line
  without waiting. It releases that mutex before task lookup, recount, reference
  release or printing (`kern_exec.c:3608`–`:3667`). No task/scheduler lock is added.
- The pairing carries exactly the captured generation, IPC sample tick and claimed
  dump number. CPU begin/end ticks are separate, and logs mark
  `non_atomic_with_ipc=1`; there is no assertion that the two snapshots are atomic.
- The pre-existing static IPC scratch remains valid under its single-autolog-caller
  contract. Searches of the actual changed files found one IPC dump call in the
  autolog loop, one CPU helper call in the IPC dump, and one autolog start invocation
  on the init path (`kern_exec.c:8066`, `:8168`, `:9586`). No new parallel caller was
  introduced by this change.

## Budget and measurement meaning

- The CPU helper is reached only after the existing IPC dump claim succeeds. It
  inherits the 10-second interval and 31-dump boot-wide quota rather than creating
  its own periodic activity. Generation changes reset scalar IPC totals but not
  `started`, `last_dump`, `dumps` or `final_dump` (`mach_msg.c:209`, `:304`).
- The existing 300-second window admits one final snapshot at the first eligible
  call at/after expiry; it is not a guarantee that the final sample occurs exactly
  at 300 seconds. Delayed autolog scheduling can delay that final snapshot. The
  exec registry also has its own earlier first-exec-relative expiry, so late CPU
  rows can correctly say `skip-registry-closed` while the IPC summary still prints.
- Recount sums lifetime task usage into a freshly zeroed struct and uses the real
  `recount_usage_times_mach` conversion. These are already charged totals; they do
  not force the current execution tail to be charged. Per-track consistency does
  not make the entire task or the IPC/CPU pair one simultaneous observation.
- The real `_fix_time_precision` path was checked (`recount.c:530`). The helper
  records `PRECISE_USER_KERNEL_TIME`; on the current non-precise mode, separate
  user/system interpretation must follow the real recount behavior, not infer that
  a zero system counter means no kernel work occurred.
- A low CPU delta establishes little charged CPU progress over the sample interval.
  It alone cannot distinguish sleeping, waiting for an IPC reply, or being runnable
  but waiting for CPU. A high delta supports active computation but does not identify
  the responsible code. Concurrent thread totals may exceed elapsed wall time.
- `recount_read_track` uses seqlock retry loops (`recount.c:200`–`:305`); no timeout
  is added. The helper runs them without probe locks, and begin/end timestamps expose
  their observed cost. This is a recorded limitation, not proof of bounded latency.

## Evidence checked

The implementation's `lsd-cpu-tests-nova/run-20261029T174741168752Z-nova/result-nova.json`
reports 24 cases for each precision mode under ASan/UBSan. The runner extracts the
actual production CPU helper and IPC scalar timing/dump bodies; its recorded hashes
match the reviewed source. The four negative-control log files were inspected:
missing reference release failed reference balance; changed-generation acceptance
failed invalid-output checks; holding the registry lock failed the lock-order stub;
removing the real IPC-to-CPU call failed the paired-log assertion. These are the
intended assertion failures, not incidental sanitizer crashes.

Host tests model locks, references, clocks and recount; they do not exercise real
kernel scheduling or hardware. Real target compilation, final linked symbol/call
inspection and the device log remain separate validation steps owned by the parent.
