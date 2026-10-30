# R15 probe correction: final local candidate

Status: **corrected diagnostic built and packaged; installation not authorized; awaiting a
new explicit user install command**. No device, adb/fastboot, mount/unmount, install,
reboot, slot change, or global config repair was performed.

## Review closure (six findings)

The rejected R15 source was `8a7fbe46…c030`. The corrected source is
`f4969ca5a6d8a1d7475866e7cbc072b062a1b5b35cf6d40922cabbcec40f4047`; only
`osfmk/ipc/mach_msg.c` differs from the R14 freeze. The old source/tests/build/image/
report are preserved under
`snapshot-nova/boot-speed-round15-nova/sblong-probe-nova/rejected-initial-r15-nova/`.
The initial image `6887a6d0…` is explicitly **REJECTED/SUPERSEDED**, not an installation
choice (`REJECTED-SUPERSEDED-nova.json`, `installation_allowed=false`).

| finding | correction | evidence |
|---|---|---|
| R15-1 receive_id | `nova_sblong_finish_locked` copies `o->recv_id` into the retained row at completion | green test SEND0→finish `6001109` survives start/finish snapshots |
| R15-2 stable slot | `nova_sblong_print.slot` is the actual table index; every event/PC line carries `row=slot`, token, seq, taskgen, tid | cap=1 separate drains; same slot/token for start and finish |
| R15-3 provisional rows | non-first rows are invisible until a valid LONG finish (`retained`); later short rows free/count without start/PC | 100 later shorts with drain before/after finish emit 0 events; first record unaffected |
| R15-4 summary scheduler | pure armed scheduler: 31 periodic max + one final in 300 s, interval 10 s, no rearm; summary snapshots latest counters | pre-arm, due/not-due, late drain, overflow-after-first, final once, cap32, repeated arm tests |
| R15-5 clock validity | `clock_error` + `clock_invalid`/`invalid_dropped`; invalid first row retained/flagged; invalid later rows dropped explicitly; no unsigned underflow | zero begin, equal begin/end valid zero, reversed wait, backward finish tests |
| R15-6 hot-path conversion | 1 s long threshold, 10 s interval and 300 s window precomputed once at first SB arm; finish observation gated by `send_seen && send_id==6001009`; no conversion in finish hook | linked `_meizu_sb_ipc_capture` has 3 conversions; `_nova_sblong_finish` has none; send gate in source |

Corrected core starts at L187; summary scheduler L274; finish L387; drain L474; capture arm
L590; drain L602; send/finish hooks L2119/L2184/L2192. The drain calls the scheduler only
after a successful `lck_spin_try_lock`; failed try-lock consumes no due claim.

## Red → green

- **Red before fix:** `test-sblong-red-nova.c` compiled against the exact old core
  (`64b00485…`) and exited **1** with **7 failures**: R15-1 receive ID, R15-2 missing stable
  slot API, R15-3 provisional start leak, R15-4 missing summary scheduler, R15-5 missing
  clock flag, R15-6 per-finish threshold, plus the 100-short stress. Log:
  `red-run-before-fix.log`.
- **Green after fix:** `test-sblong-green-nova.c` includes the exact corrected core
  (`0e3fae25…`), built with ASan/UBSan, exited **0** with **0 failures**
  (`green-run.log`). Cases include all six findings plus mixed starts/finishes at cap 1,
  R14-shaped seq200/taskgen4/tid253, frame budget, stale attach, pending→complete, and
  summary cap. These are synthetic host fixtures, not runtime boot/speed reproductions.

## Build, package, audit

- Real ARM64 TU compile using the existing `mach_msg.o.json` argv (only output changed):
  exit 0, object `48b1c8a7…`; it byte-matches the object used by the build.
- Incremental full build with the reviewed narrow bash dSYM shim preset on the first
  corrected build: exit 0; raw kernel
  `59f690df15b4e78364762490305860e005e06fd1a0f613dac013a63379048113` (73,949,184 B).
- Package in the accepted R14 96 MiB envelope, only
  `EFI/UniXNU/Payloads/kernel.macho` replaced; PAN passed; header/DTB/AVB/TOML/non-kernel
  EFI preserved.
- Corrected candidate:
  `artifacts-nova/boot-speed-round15-nova/probe-correction-nova/package/sblong-r15-corrected-final-nova/boot-warm-sblong-r15-candidate-nova.img`
  SHA `025b1c1c8a6b08fa0b50c2d4a70bf264fbbb8d4aef864e8ff252b1836f3dac20`;
  payload `717c10cc…6574`; manifest state **BUILT_AWAIT_NEW_USER_INSTALL_COMMAND**,
  `installation_allowed=false`, `do_not_flash=true`, no enabled installer.
- Audit PASS: only `mach_msg.c` differs from the 6,186-entry R14 freeze and from the initial
  R15 revision; policy `60b0dced…`/tests `a1276115…` exact;
  `BootWatchdogPolicy.o`, `BootLog.o`, `NativeRamBlock.o`, `NativeUfs.o`, `kern_exec.o`
  byte-identical to R14; corecrypto source exact/object recorded. Linked checks: finish hook
  target, post-unlock send backtrace, three capture conversions, drain try-lock, no finish
  conversion.

## BBS positive binding / supersession

`6001009` is packed as the Mach request-header `msgh_id` at **offset 20** by
`_BKSDisplayGetMainScreenInfo` (`0x18dcc7a30`): the 64-bit table slot `0x18dd209a0 =
0x5b9171` is stored to `sp+0x18`, so its high half lands at header offset 20 (`sp+0x1c`);
header base is `sp+8`, bits `0x1513`, call at `0x18dcc7ad0`. This is a **client-side BBS
template binding**, not a launchd-native opcode and not proof of runtime seq200 semantics or
wait cause. The earlier `launchd-request-binding-nova/REPORT`/`evidence` bounded-negative
wording is marked **SUPERSEDED** with backup; link/hash in `BBS-POSITIVE-BINDING-nova.*`.

## Limits

Diagnostic only; expected no direct time reduction. No root-cause fix, R15 Hello, install,
or speed claim. The service-side cause remains for the next capture, which will now carry
exact finish/result/receive ID, stable slot/PC identity, clock validity, bounded summaries,
and one sender backtrace. Sol independently checks the actual diff/artifacts.

## Key files

- Corrected source: `f4969ca5…f4047`
- Source diff from initial: `source-diff-from-initial-nova.patch`
- Red/green logs: `red-run-before-fix.log`, `green-run.log`
- TU object: `48b1c8a7…`
- Raw kernel: `59f690df…`
- Candidate image: `025b1c1c…`
- Package manifest: `b0c76a4a…`
- Audit/validation: `9af9043a…`, `d12d9cf4…`
- Initial rejected image: `6887a6d0…` (superseded)
- Plan v1.15 and changelog append follow this report.
