# CPU probe implementation validation — Nova

2026-10-30. New module and host-test files; no previous versions existed.
The printf header correction and test-boundary literal correction were backed
up to `snapshot-nova/boot-speed-round6-nova/cpufreq-impl-before-compile-fixes-nova/`.
The parent agent owns the central changelog. No device was accessed and this
subtask did not build the full kernel or change board/builder configuration.

## Result

**PASS: 48 real-production-function cases with AddressSanitizer and
UndefinedBehaviorSanitizer; 2 of 2 deliberate negative controls rejected.**

Run from this directory: `python3 run-tests-nova.py`.
Machine-readable commands, outputs and negative-control failures are recorded
in `test-results-nova.json`. Each test runs in a fresh process, so production
static state needs no reset hook. The harness compiles the actual production
`CpuFreqReadiness.c`, replacing only platform access and output with a shim.
It does not copy the parser or sample/publication logic into the test.

| Area | Verified behavior |
| --- | --- |
| Strict gate | Missing/NULL/empty/bare/0/2/negative/plus/leading zero/hex/overflow/suffix/impostor/duplicate tokens cause zero map, MMIO, log, or system-register sample |
| Full bounded string | Exactly 1024 bytes without NUL rejected even with an early valid token; a valid token ending with NUL at byte 1023 accepted; all six ASCII whitespace separators supported |
| Read preconditions | Non-4KiB pages, disabled interrupts and nonzero affinity at Aff0/Aff1/Aff2/Aff3 cause zero mapping |
| Mapping | Exact domain0 PA, 4KiB size, device memory, read-only and unmappable; failure records status with no reads/unmap |
| Reads | Exact whitelist/order 0x000, 0x020, 0x0b0, 0x320; maximum four reads; all-ones/disabled enable only one read plus cleanup |
| Raw values | Zero state/request remain loaded valid data; all raw values retained; mask records completed loads |
| Idempotence | 1000 repeated calls/readbacks preserve one log, one sample and original ticks; map/read/unmap reentry cannot sample again |
| Publication | Getter leaves output untouched until ready; while producer is blocked inside unmap another thread performs 10000 unpublished reads and a redundant dump safely; output occurs only after complete publication and unmap |
| Affinity | MPIDR U/MT/non-affinity flags do not alter affinity status; Aff1/Aff3 changes are reported; raw endpoints retained |
| Log semantics | Single prefixed line with timer_hz, autolog-once phase and cpu_frequency=unknown |

Negative controls compile separate mutated source copies under the same
sanitizers. Bypassing the bootarg gate fails the zero mapping/read/print
assertion. Replacing the request offset with `0x1000` fails the shim's MMIO
window check. Both exit with the expected harness failure code 70 and specific
assertion text; incidental compiler or sanitizer crashes are not counted as
successful negative controls.

Production C SHA256 tested:
`820e379fa22902d43cb822889d1f10d38bf9842bf7b36be13328a1ca42f0f13a`.
Header SHA256:
`19c5dd230a43aa77fe011a9a54f888efe5ba892e17c6ae3f492b3d0073d55fb7`.

The packaging agent separately compiled the same source into a real ARM64 iOS
17.0 object using this workspace's existing XNU compile record. Its report is
`../packaging-nova/isolated-probe-build-nova/build-report.json`; source hashes
match the host-tested file. That report confirms no STARTUP section and the
two expected exports. A direct `xcrun nm -u` inspection found exactly
`_PAGE_SHIFT_CONST`, `_PE_boot_args`, `_io_map`, `_ml_get_interrupts_enabled`,
`_ml_io_unmap`, `_printf`. Real-object compilation is distinct from a final
kernel or device validation.

## Limits

Host shims cannot establish EL1 MMIO accessibility, implement bus-fault recovery,
or prove physical CPU frequency. The concurrency case validates the exposed
publication boundary; the underlying acquire/release protocol also depends on
the target compiler and ARM64 memory model. Final object/linked-kernel MMIO
load/store audit, autolog integration, packaged bootargs, and device behavior
remain the parent/build agent's checks. This is evidence collection for the
startup investigation, with no claimed startup improvement yet.
