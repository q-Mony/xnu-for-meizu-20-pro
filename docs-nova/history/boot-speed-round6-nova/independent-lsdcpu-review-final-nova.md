# Final lsd CPU accounting delta review — Nova

2026-10-30. New report, no previous version. This is a read-only review of the two
compatibility changes after `independent-lsdcpu-review-nova.md`; that prior report
and all production files are unchanged by this reviewer.

**PASS: both deltas preserve the prior lifecycle, lock-order, generation and
budget conclusions. No new blocking defect found.** Full kernel compilation and
linked-binary/device verification remain the parent agent's next steps.

## Frozen inputs

| Source | Reviewed SHA256 |
| --- | --- |
| actual XNU bsd/kern/kern_exec.c | `2f1fac81bf4925751da61d6f56262956c883fea5a94a4cae618769a0ee6ee14c` |
| actual XNU osfmk/ipc/mach_msg.c | `ff2e9e4241f1b434afa3f516c3e4253dc2e55dcbf9fdd8a92712f5b85d65cad0` |
| source and assembled scripts/build-xnu-4k-baseline.sh | `213998f238e48a9cf826ace3225807b35bbabf7d8e4c76a37ae899d940cc112a` |

Actual XNU is
`nova/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work`.
The before-fix backup of `kern_exec.c` has the earlier reviewed hash
`302505423a0392a2110db89b957dd68c10fa2aecbf65cf20f73af3fee60523e1`.
A direct before/after diff contains exactly the two active-state API substitutions
and two matching preprocessor guards described below. `mach_msg.c` has no delta.

## 1. Opaque task activity access

`active_before` and `active_after` now use `task_is_active(task)` instead of
dereferencing `task->active` from the BSD compilation unit.

The real XNU declaration is `osfmk/kern/task.h:1244`:
`extern boolean_t task_is_active(task_t task);`. The implementation at
`osfmk/kern/task.c:1033` returns the activity member without acquiring a lock or
changing references. The API therefore permits opaque-task callers while leaving
the existing borrowed thread/task lifetime protection unchanged.

Both calls stay outside the registry/IPC locks and inside the borrowed-reference
lifetime. They remain advisory before/after observations, not atomic state
certification, memory-ordering barriers, or object-lifetime protection. Recount,
generation checks, counter validity, reference release and output order are
unchanged. The prior report's lifetime and locking conclusions remain applicable;
its direct atomic-member-access description is superseded by this API description.

## 2. Optional archive and autolog call agreement

The local extern declaration (`kern_exec.c:8055`) and invocation (`:8071`) use the
same condition:

```c
#if defined(XNU_MEIZU_CPU_FREQ_PROBE) && XNU_MEIZU_CPU_FREQ_PROBE
```

In both source and assembled `build-xnu-4k-baseline.sh`, the selected CPU-probe
archive block (`:640`) now also appends `-DXNU_MEIZU_CPU_FREQ_PROBE=1` to
`EXTRA_CFLAGS`. That same block checks the archive file, force-loads it into the
kernel and adds it to the explicit archive dependencies. `EXTRA_CFLAGS` reaches
the build as `CFLAGS_EXTRA` (`:863`). Thus ordinary configurations without this
optional archive do not emit the call, while selecting the archive enables its
caller and link dependency together.

The compile-time guard does not replace the production module's strict runtime
`nova-cpufreq-probe=1` gate. It introduces no change to the lsd CPU accounting
helper, IPC quota or task-generation matching. It only makes the separately
optional EPSS archive and its call site agree.

## Evidence and remaining checks

The new worker run is
`lsd-cpu-tests-nova/run-20261029T175730212356Z-nova/result-nova.json`.
Its source hashes match the frozen files above. It records 24 scenarios in each
of `PRECISE_USER_KERNEL_TIME=0/1` with ASan/UBSan, plus the same four successful
negative controls. The runner now extracts the actual two autolog guard blocks,
then compiles, links and executes them with the macro undefined, 0 and 1; all
three pass, with zero calls in the first two configurations and one in the last.

`lsd-cpu-tests-nova/task-api-syntax-check-nova.json` records the real BSD compile
command converted to `-fsyntax-only` with output arguments removed, returning 0.
That syntax-check command does not contain the new opt-in macro; the host guard
matrix covers its inclusion/exclusion semantics, while the parent's ensuing
real enabled kernel build must establish the final enabled target integration.
No complete kernel build was run or claimed by this reviewer.

All prior measurement limits remain: recount can retry without a strict duration
bound; task usage is charged lifetime CPU, not a forced-current running tail;
IPC and CPU are not atomic together; a low CPU delta alone cannot distinguish
sleep from run-queue delay; the existing window can produce one late final dump;
and registry expiry may make late CPU samples unavailable. Neither compatibility
fix supplies new evidence of boot-speed improvement or hardware frequency.
