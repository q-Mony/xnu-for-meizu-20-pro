# Round 6 lsd CPU accounting — Nova

New files, no previous version. Production backups and SHA256 manifest are in
`snapshot-nova/boot-speed-round6-nova/lsd-cpu-before-nova/`.
The parent agent owns the central CHANGELOG entry, full kernel build and device work.

## Change

`kern_exec.c` adds `nova_lsd_cpu_timing_dump(gen, ipc_ticks, dump)` under the existing
`XNU_OWNER_CACHE_MOUNT` gate. `mach_msg.c` uses the dedicated
`XNU_MEIZU_LSD_CPU_TIMING` gate and invokes it after releasing the IPC spin lock,
only when the existing lsd dump has claimed its boot-wide budget (10-second interval,
31 dumps, 300 seconds; generation churn does not reset this budget).

The successful-exec path passes `e.taskgen` to `meizu_lsd_ipc_capture`; the timing
generation is therefore the same task identity used by the exec registry, not a PID
or per-thread generation. The helper tries the registry mutex without waiting and
selects exact `taskgen` plus the complete vnode name `lsd`. It borrows an anchor thread
reference, drops the mutex, and samples `recount_task_usage` into a zeroed usage.
`recount_usage_times_mach` supplies user/system Mach ticks. Sampling checks the task
generation before and after the accounting read and releases the borrowed reference
on all acquired-reference paths before printing.

The thread reference pins task storage: thread creation takes a task internal
reference (`thread.c`, `task_reference_grp(parent_task, TASK_GRP_INTERNAL)`), and
`thread_deallocate_complete` releases it only during final thread destruction.
Task recount tracks are released by `recount_task_deinit` during final task
deallocation. Registry replacement can release its owned reference while our
borrowed reference keeps the old task safe. Exited tasks with the same identity
retain valid cumulative usage; advisory `active_before/after` flags identify them.

The other production change is the parent's requested declaration/call of
`nova_cpufreq_probe_dump_once` in the existing autolog scalar-summary section.
Both are guarded by `defined(XNU_MEIZU_CPU_FREQ_PROBE) && XNU_MEIZU_CPU_FREQ_PROBE`,
so builds that do not select the archive have no unresolved probe reference.
The parent owns the build script that adds this macro when selecting the archive.

## Log interpretation

`nova: lsdcpu` contains `gen`, `taskgen`, `taskgen_after`, `dump`, `ipc_ticks`, CPU
`begin_ticks/end_ticks`, PID/TID, `status`, `valid`, activity flags and `precise`.
Only `status=matched valid=1` lines have `user_ticks/system_ticks`. Valid cumulative
zero is preserved; busy/missing/closed registry and generation changes omit CPU
counters completely. The IPC output stays available when CPU sampling is skipped.

Use consecutive valid samples of the same generation to compute CPU deltas. The
separate begin/end ticks expose the accounting read duration and its offset from
the IPC snapshot. `PRECISE_USER_KERNEL_TIME=0` causes the real recount implementation
to attribute all measured time to user and set system time to zero, so separate
user/system interpretation is valid only when `precise=1`.

These are already charged task-lifetime totals, not an instantaneous flush of running
threads' tails. IPC and CPU sampling are not atomic together. A multi-thread task can
accumulate more CPU time than elapsed wall time. CPU deficits alone do not distinguish
sleeping from waiting in a run queue. Activity flags are lock-free advisory observations,
not a lifetime guarantee; the thread reference is the lifetime guarantee.

`recount_task_usage` reads per-track seqlock snapshots: it retries concurrent writers,
and this call has no strict retry-duration bound. It runs without IPC, registry,
task or scheduler locks; the added begin/end timestamps make observed cost measurable.
No new sampling thread, scheduler counter or runnable-time calculation is added.

## Verification

Run `python3 artifacts-nova/boot-speed-round6-nova/lsd-cpu-tests-nova/run-tests-nova.py`
from the Nova workspace. The runner extracts the actual production CPU helper,
registry declaration and IPC scalar timing/dump bodies on every run, checks source
hash stability, and compiles them with ASan+UBSan and warnings as errors.

The final run `run-20261029T180811005687Z-nova` passed 24 scenarios for each of `precise=0/1`:
generation match/mismatch before and during sampling, registry busy/closed/missing,
exact executable name, last registry slot, inactive/exit during sample, registry
replacement and reference lifetime, valid zero, 64-bit totals, cumulative rather
than per-call usage, inactive/busy IPC, correct IPC ticks/dump, interval admission,
31-dump cap, 300-second final sample and generation churn retaining budget.

Four compiled negative controls failed their assertions as expected: missing put,
accepting changed generation, keeping the registry lock held, and removing the actual
IPC-to-CPU call. The unmutated source passes both precision variants.

The host tests replace clock, lock, reference and recount mechanisms with checked
stubs. They verify real call bodies and integration placement; they do not exercise
real kernel scheduling or prove recount's duration bound. No full kernel build or
device operation was performed by this child task.

## BSD task API correction

The parent's first complete kernel attempt exposed that BSD deliberately sees an
opaque `struct task`; direct `task->active` reads in the initial version did not
compile. Both observations now use the existing `task_is_active(task)` API from
`kern/task.h`, without changing task visibility or configuration. The host model
deliberately has no `active` member, so reintroducing direct member access fails
compilation. The API remains an advisory activity observation, not a synchronization
or lifetime mechanism.

The final run also compiles/links/executes the actual autolog declaration and call
guards extracted from production with the macro undefined, zero and one; all three
pass, and disabled variants have no probe implementation linked. Additionally,
`task-api-syntax-check-nova.json` replays the real `kern_exec.o.json` compiler recipe
with output/dependency flags removed and `-fsyntax-only`. It returns 0 with 59
pre-existing-source warnings and no errors. This checks the real BSD header boundary,
without rebuilding the kernel or writing its objects. Before-fix source, tests and
reports were backed up under `snapshot-nova/boot-speed-round6-nova/lsd-cpu-before-task-api-fix-nova/`.

## IPC translation-unit guard correction

The final linked-object audit caught that `XNU_OWNER_CACHE_MOUNT` was provided to
BSD translation units but not `osfmk/ipc/mach_msg.c`. The earlier host harness
defined that macro, so its successful call test did not prove the target object
contained the call. Both IPC hook declaration and call now use the independent
`XNU_MEIZU_LSD_CPU_TIMING` macro. The parent selects it through the `mach_msg.o`
per-object compile flags, invalidates that object on mode changes, and rejects
timing mode when the owner-cache helper is not selected. This avoids exposing the
BSD registry internals or changing unrelated objects' configuration.

The final harness defines only `XNU_MEIZU_LSD_CPU_TIMING`, not the owner macro,
for its actual IPC dump body. It also extracts the exact IPC declaration/call
guards and compiles/links/runs with the new macro undefined, zero and one while
the owner macro remains undefined in all cases. All three pass; disabled variants
do not supply a CPU helper and therefore would fail linkage if a call leaked through.
Enabled mode checks the generation, IPC ticks and dump number reach the helper.
The result JSON `ipc_gate` entries retain compile commands, return codes and
extracted-source hashes for final binary-audit binding. These host tests supplement
the parent's required object/linked-kernel call audit; they do not replace it.

Before-fix source, host tests and report hashes are backed up under
`snapshot-nova/boot-speed-round6-nova/lsd-cpu-before-ipc-guard-fix-nova/`.
