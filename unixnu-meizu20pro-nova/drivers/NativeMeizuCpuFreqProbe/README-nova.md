# Nova CPU EPSS single snapshot

This is a diagnostic module for m2391, not a frequency governor or a demonstrated
boot-speed improvement. It samples domain0 once when explicitly called by the
existing autolog thread. It has no STARTUP entry, new thread, periodic MMIO,
LUT traversal, other-domain access, L3 access, or hardware writes.

## Activation and API

`nova_cpufreq_probe_dump_once()` is inert unless the NUL-terminated boot argument
string contains exactly one whitespace-delimited `nova-cpufreq-probe=1` token.
The entire string is validated within 1024 bytes (the ARM64 `BOOT_LINE_LENGTH`).
Bare keys, duplicate keys, signed/overflowing/alternate numeric forms and a
missing NUL are rejected before mapping, register sampling or printing.

`nova_cpufreq_get_snapshot(struct nova_cpufreq_snapshot *out)` returns 1 only
after the immutable snapshot is ready. Otherwise it returns 0 without modifying
`out`; a NULL output also returns 0. It never samples again. The producer claims
an atomic guard, builds a local snapshot, unmaps, copies the complete value and
publishes readiness with release ordering. Readers acquire readiness before
copying. Reentrant and concurrent producer calls return without another sample.

After opt-in the producer requires 4096-byte kernel pages, enabled interrupts,
and boot affinity (`MPIDR & 0xff00ffffff == 0`). Failure records a skip status
without mapping. The sole mapping is `0x17d91000` / `0x1000` using `VM_WIMG_IO`,
`VM_PROT_READ`, and an unmappable allocation. Four aligned 32-bit reads occur
in this order, each followed by `dmb oshld`:

| Offset | Snapshot field | Meaning retained |
| --- | --- | --- |
| 0x000 | enable_raw | Domain enable word; stop if all ones or bit0 clear |
| 0x020 | state_raw | Raw EPSS state; zero remains valid loaded data |
| 0x0b0 | dcvs_raw | Raw DCVS control |
| 0x320 | request_raw | Raw LUT index, never reported as MHz |

The `valid_mask` identifies completed raw loads, not validated hardware state.
An all-ones enable therefore has one completed read and a separate failure
status. `sample_id=1`, CNTVCT begin/end ticks, CNTFRQ `timer_hz`, both MPIDRs,
affinity-change status and the raw values are preserved. A single result line
starts with `nova: cpufreq` and labels the phase `autolog-once`.

## Interpretation limits

The physical window and EPSS offsets are grounded in the existing Nova m2391
DTB and fixed upstream/Linux source comparisons recorded in
`artifacts-nova/sheng-research-nova/cpu-boot-nova/REPORT-nova.md`. This implementation
uses the later, deliberately smaller four-read domain0 contract rather than the
report's initial three-domain/LUT proposal.

Mapping success does not prove hardware accessibility. An external abort or a
bus load that never completes cannot be recovered by this software read-count
limit. This XNU's `ml_probe_read` is a panic stub; `ml_nofault_copy` is not MMIO
fault recovery. Neither is used.

MPIDR reads do not pin the thread; matching endpoints cannot exclude intervening
migration. These are domain0 values at one autolog moment, not a measurement of
initial CPU frequency, not the generic timer's frequency interpreted as CPU
frequency, and not evidence that lsd stayed at one frequency throughout its wait.
Clock-source/LUT validation and any frequency writes belong to a later experiment.

## Validation and integration

The actual production C is compiled with a host shim under ASan/UBSan by
`artifacts-nova/boot-speed-round6-nova/cpufreq-tests-nova/run-tests-nova.py`.
It passed 48 fresh-process cases, including reentrancy and concurrent reads while
unmap is blocked. Both negative controls (disabled gate bypass and an out-of-page
read) were rejected. Tests establish software boundaries; they do not emulate
bus faults or prove true hardware accessibility.

The isolated real ARM64 object compiles using the existing Nova XNU compile
record and has no STARTUP section. Exported symbols are
`_nova_cpufreq_probe_dump_once` and `_nova_cpufreq_get_snapshot`. The inspected
object's undefined symbols are `_PAGE_SHIFT_CONST`, `_PE_boot_args`, `_io_map`,
`_ml_get_interrupts_enabled`, `_ml_io_unmap`, and `_printf`. No allocation API,
thread API or frequency-write helper is linked by this module.

Production headers are `CpuFreqReadiness.h`, `arm/machine_routines.h`,
`vm/memory_types.h`, `mach/vm_param.h`, `mach/vm_prot.h`, `pexpert/pexpert.h`,
and `kern/misc_protos.h`. The builder and parent agent own archive integration,
the autolog call site, final linked-binary audit, packaging and any device test.
