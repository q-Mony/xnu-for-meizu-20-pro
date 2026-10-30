# R15 arm-lock correction: authoritative local candidate

Status: **local diagnostic built and packaged; installation not authorized**. This is a
diagnostic probe, not a speed/root-cause fix. No device, adb/fastboot, mount/unmount,
install, reboot, slot change, or global config repair was performed.

## Defect and narrow fix

The f496 source initialized `nova_sblong_summary` and `nova_sblong_long_ticks` **before**
`meizu_sb_ipc_capture` acquired `meizu_ipc_lock`, while drain read them under the lock.
That was a C data race and could expose partially initialized scheduler state.

Corrected source:
`31328bebae5b320f2652fcf80e2cf95022dd2bb981bd11d5eaf1c1b0df985bd9`.

Changes:

- `meizu_sb_ipc_capture` now precomputes the 300 s window, 10 s interval and 1 s threshold
  **locally before** the lock (safe setup), then acquires `meizu_ipc_lock`.
- Under that lock it records `now`, sets `meizu_ipc_started`, and calls
  `nova_sblong_capture_arm_locked(now, window, interval, long)`.
- The locked helper checks the once guard, writes the threshold and scheduler fields, and
  `nova_sblong_summary_arm` publishes `.armed` **last**.
- No summary/threshold access, setter, old arm call or unlocked once-check remains outside
  the lock. Repeated capture returns without resetting rows, threshold, scheduler or quotas.
- Summary snapshots now include the current summary in `summaries` (periodic/final count).
- No new lock/atomic/conversion was added to send/finish. Existing six fixes remain.

Static integration check (`test-arm-lock-source-nova.py`, result
`arm-lock-source-result-nova.json`): exactly three conversions before lock; no summary/
threshold/setter access before lock; locked arm call between lock and unlock; threshold
before arm; `.armed` last; no old unlocked arm. This is **static source evidence**, not
runtime execution; host scheduler tests alone cannot prove lock placement.

## Red / green

- Rejected initial/red evidence remains: old core `64b00485…`, red log `ba515bbb…`,
  7 failures. The six original findings are still covered by the preserved green harness.
- New arm-lock green harness uses the exact extracted region (`51432214…`), ASan/UBSan,
  exit **0**, **0 failures** (`green-run.log` `a6cfe9af…`). Tests include all six findings,
  summary current-count, cap 1/stable slot, 100-short invisibility, clock validity and
  precomputed threshold. Synthetic only—not a boot reproduction.

## Build / package

- Real ARM64 TU compile with existing `mach_msg.o.json` argv (only output changed): exit 0;
  object `69ddaecc…` byte-matches build object.
- Full incremental build with the reviewed PATH dSYM shim preset on first corrected build:
  exit 0; raw kernel `e16b7a69…` (73,949,184 B).
- Package in R14 accepted 96 MiB envelope; only `EFI/UniXNU/Payloads/kernel.macho` replaced;
  PAN and non-kernel EFI/header/DTB/AVB/TOML checks pass.
- Authoritative candidate:
  `artifacts-nova/boot-speed-round15-nova/arm-lock-review-nova/package/sblong-r15-arm-lock-final-nova/boot-warm-sblong-r15-candidate-nova.img`
  SHA `53470de0d676da3e41cb9c6a894b62b10686e0ef0480799535db0d1e331fe0ed`.
  Payload `72b967c3…`; manifest `2fe7cd3f…` state
  **BUILT_AWAIT_NEW_USER_INSTALL_COMMAND**, `installation_allowed=false`,
  `do_not_flash=true`, no installer.
- Audit `439ff3aa…` PASS: only `mach_msg.c` differs from the 6,186-entry R14 freeze and
  from the prior R15 revisions; policy/tests exact; policy/BootLog/RamBlock/Ufs/kern_exec
  objects byte-identical; corecrypto source exact/object recorded; linked finish hook,
  post-unlock send backtrace, capture conversions, drain try-lock and no finish conversion
  checked. Source freeze `291a3cff…`; object comparison `5d43ce5d…`.

## Supersession

- Initial R15: source `8a7fbe46…`, image `6887a6d0…` — **REJECTED/SUPERSEDED**.
- Prior correction: source `f4969ca5…`, image `025b1c1c…` —
  **SUPERSEDED BY THIS ARM-LOCK CORRECTION**, sidecar `e72326d0…`.
- Backups: `snapshot-nova/boot-speed-round15-nova/sblong-probe-nova/rejected-initial-r15-nova/`
  and `snapshot-nova/boot-speed-round15-nova/probe-correction-nova/before-arm-lock-fix/`.

## Documentation qualification / BBS

`backboard-init-analysis-nova/REPORT-nova.md` §5 is qualified: one receiver sample pair
cannot exclude prior blocking, unsampled workers, or handler/reply dependency; “no direct
wait import” does not exclude wrappers; a FOREVER deadline can be signalled after 30 s;
bounded/no-inline-timeout evidence is not a runtime exclusion. Positive named-service and
FOREVER evidence preserved. Backup `bb7eb2ce…`; current report `d876ada8…`.

BBS positive binding remains: `6001009` is packed at Mach request header offset 20 by
`_BKSDisplayGetMainScreenInfo`; this is client-side BBS template evidence, not a
launchd-native opcode and not runtime causality. `BBS-POSITIVE-BINDING-nova.json`
`bae6659a…`; earlier bounded-negative report/evidence remain superseded.

## Limits

Host tests are synthetic and the lock integration check is static. The service-side cause is
not fixed or proven. The next capture is required for exact completion/caller evidence.
No R15 Hello, install, reboot or speed claim. Sol independently accepts the actual diff and
artifacts.
