# Independent final diagnostic hook review — Nova

2026-10-30. New report, no prior version. This reviewer changed no production
file or prior report. The only execution performed for this review was a small
extracted build-script block against simulated artifact roots; no complete
builder or device command was executed by the reviewer.

**PASS for the corrected source, mode-switch behavior and final static binding.**
The final kernel now contains the real IPC-to-CPU accounting call. Earlier
source-level reviews did not establish that this call survived the actual
per-object preprocessing; this report supplies that missing final-artifact check.
This is diagnostic readiness, not a device-success or boot-speed claim.

## Change and frozen identity

`XNU_OWNER_CACHE_MOUNT` is supplied to the BSD `kern_exec.o` compilation but was
not supplied to `mach_msg.o`. Guarding the latter's call with that macro removed
the hook from the first built candidate. The parent rejected that candidate.

The final `mach_msg.c:316` and `:340` guards use the independent
`XNU_MEIZU_LSD_CPU_TIMING` macro. The build script validates its mode, passes it
explicitly through `mach_msg.o_CFLAGS_ADD`, and invalidates the previous object
when that mode changes. The current diagnostic `.config` explicitly selects
`CONFIG_XNU_MEIZU_LSD_CPU_TIMING=y` together with `CONFIG_XNU_OWNER_CACHE_MOUNT=y`.

| Reviewed input | SHA256 |
| --- | --- |
| actual XNU bsd/kern/kern_exec.c | `2f1fac81bf4925751da61d6f56262956c883fea5a94a4cae618769a0ee6ee14c` |
| actual XNU osfmk/ipc/mach_msg.c | `79a1e53bd724f27faf3f5b579458f175b3370f3da72ba05e8dfb1001fb6a2741` |
| source and assembled build-xnu-4k-baseline.sh | `20707bf44fbb2d769f91f804a906802900ae18d25764c644c633894a673e6838` |
| out/meizu20pro/.config | `58d98383fe11857e3e40ee6a62d2e20118730ac9c42927980205ca936e6435f7` |

## Default-off and dependency checks

The source and assembled build scripts match. The new block at
`scripts/build-xnu-4k-baseline.sh:395` defaults an unset/empty mode to 0, permits
only 0 or 1, and rejects mode 1 unless the owner exec registry is selected.
The per-object argument is supplied for both enabled and disabled modes, so it
does not depend on unrelated BSD flags. In source, an undefined or zero macro
removes both the declaration and call from `mach_msg.c`; the helper can remain
present in an owner-enabled BSD object without being invoked by this feature.

The new host macro matrix extracts the actual IPC declaration/call guards and
compiles, links and executes them in undefined/0/1 modes without defining
`XNU_OWNER_CACHE_MOUNT` at all. All three pass. This directly covers the earlier
false assumption that the BSD macro would be visible to the IPC object.

## Mode-switch invalidation: executed isolated tests

The reviewer extracted the actual new block, without reimplementing its shell
logic, into a wrapper whose ROOT always points under this report's artifact
directory. Full builder execution and active build mutation are excluded.

Runner: `independent-hook-tests-nova/test-mode-stamp-nova.py`.
Result:
`independent-hook-tests-nova/run-20261029T181106590559Z-nova/result-nova.json`.
**14 of 14 cases passed.**

| Scenario | Observed outcome |
| --- | --- |
| Mode unset or empty | Defaults to 0; emits explicit per-object mode 0 |
| Stamp absent or empty with existing object | Deletes old `mach_msg.o`, records selected mode |
| 0 → 1 and 1 → 0 | Deletes old object in both directions and updates stamp |
| Same mode 0 or same mode 1 | Preserves existing object |
| Mode 2, -1, y or 01 | Exits 1 with mode error before changing object/stamp |
| Mode 1 with owner disabled | Exits 1 with dependency error before changing object/stamp |
| Object directory absent | Succeeds without creating it, emits enabled compile flag; cold build must create the object |

Every case preserved unrelated objects and the dependency file. The script
contains one deletion target, the simulated `mach_msg.o`. The stamp is written
after its removal and before the real build: if a build later fails before
recreating the object, the next make still sees the missing object. If compilation
already recreated it, the stamp corresponds to that selected mode. No global
clean, task-layout change or registry/budget alteration is introduced.

## Actual compiler and final kernel evidence

The successful corrected build is
`build-result-20261029T180917125947Z-nova.json`: exit code 0, source hashes stable.
The actual `mach_msg.o.json` contains `XNU_MEIZU_LSD_CPU_TIMING=1` independently of
the BSD owner macro. The final audit also binds that compile record by SHA256
`937d21c3457d67c76ec7478743584866b20bb1433c95000b9f7a032e388bfca6`.

The packaging agent's completed audit was read and its hash checked:
`linked-diagnostics-audit-nova/audit-nova.json`, status `PASS_STATIC_BINDING`, SHA256
`e45ff4d60bf9261657798ddcc47d654337c886926ee48ddd749b26ad4e7f87d1`.
It compares actual objects to the linked raw kernel modulo relocation words,
resolves the relevant branch destinations, and verifies the audited function
bytes are unchanged in the packaged payload. The relevant static link addresses
are:

| Real BL edge | Instruction address |
| --- | --- |
| autolog → lsd IPC timing dump | `0xfffffff0086a6d50` |
| lsd IPC timing dump → lsd CPU timing dump | `0xfffffff00821db14` |
| lsd CPU timing dump → recount_task_usage | `0xfffffff00869c32c` |
| lsd CPU timing dump → recount_usage_times_mach | `0xfffffff00869c334` |
| autolog → one-shot EPSS probe | `0xfffffff0086a6d5c` |

The same audit verifies the registry unlock dominates recount in the final
machine code, both `task_is_active` calls are present, and real thread reference
acquisition/release calls are linked. The final artifact hashes are:

- Raw kernel: `9a5d91cda01888313607128a26d9b2e5d2f8ceb485b8a67dc9a2e6e4c601c47d`
- Packaged kernel: `28b5299bcf853c1972e78044d8a7ab80f61da664972f1129a731c833a4cc5865`
- Final boot image: `a2114a6acbfe136b3793d1ec2cbf80c4ff7358c444cde9b344f0704479b5bd1e`

The final package is `packaging-nova/cpufreq-candidate-final-nova/`.

## Test and interpretation limits

The final lsd host result is
`lsd-cpu-tests-nova/run-20261029T180811005687Z-nova/result-nova.json`: 24 scenarios
for each precision mode under ASan/UBSan, four negative controls, three EPSS
autolog guard modes and three independent IPC guard modes all pass. The final
binary audit verifies those tested source hashes still match the built inputs.

Lifecycle, lock-order, exact generation pairing and the existing 31-dump/window
budget conclusions from the prior reviews still apply; the new change affects
whether the already-reviewed helper is actually called. Recount has no strict
duration bound, cumulative CPU samples are not atomic with IPC, and a single
EPSS snapshot is not a frequency trace over the whole wait. Static calls prove
the hook survived compilation and packaging; device logs must establish actual
execution and any conclusion about the long Boot-to-Hello interval.
