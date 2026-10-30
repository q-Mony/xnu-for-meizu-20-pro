# R15 sblong diagnostic: implementation, build, package, audit

Round: `boot-speed-round15-nova` / `sblong-probe-nova`
Status: **local diagnostic built and packaged; installation not authorized**.
No phone/device/adb/fastboot, mount/unmount, install, reboot, slot change, or global
tool/config repair was performed. No root-cause fix, Hello claim, or speed claim is made.

## Static lead addendum (`send_id=6001009`)

The BackBoardServices table hit is now classified: it is a **64-bit constant table**, not a
method/protocol dispatch table. The 4-byte hit at cache `.05` offset `0x13c9a4`
(VA `0x18dd209a4`) is the high half of the 64-bit slot at VA `0x18dd209a0`, value
`6001009` (`0x5b9171`). The consumer `ldr d0, [x8, #0x9a0]` at `0x18dcc7a94` is inside
`__BKSDisplayGetMainScreenInfo` (`0x18dcc7a30`); it stores the value into a local display
structure and does not branch or indirect-call on it. No launchd xref exists.

This proves the table is data consumed by a BackBoardServices display path, not a
launchd request map. It does **not** bind the SpringBoard→launchd seq200 semantics. The
same numeric ID observed toward launchd and backboardd remains compatible with a generic
transport constant, but no named launchd dependency is proven. Static evidence still says:
no fixed Mach timeout in R14 (`opt=0x8380000400304003`, `timeout=0`, no
`MACH_RCV_TIMEOUT` bit); 27.246 s is WAIT-phase age, not a completed interval.

See `launchd-request-binding-nova/STATIC-ADDENDUM-nova.md` and
`static-addendum-nova.json`.

## Diagnostic design

Production file changed: only
`unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c`
(R14 SHA `79a1e53b…a2741` → R15 SHA `8a7fbe46…c030`). The diff is **additive only**
(`source-diff-nova.patch`; no removed source lines).

- **Core rows** (`NOVA_SBLONG_CORE_BEGIN` at L187): 8 fixed boot-lifetime scalar rows; no
  retained ports/tasks/user pointers. Each row holds token, taskgen/tid/seq, begin/wait/
  finish ticks, duration, request/recv id, endpoint pid/gen/stop/kind, phase/finish phase,
  result, start/finish PC arrays, valid/first/finished/printed states.
- **Admission** (`nova_sblong_admit_locked`, L275): only `&meizu_ipc_latest`, pending,
  nonnested, send_seen, request `6001009`, endpoint pid1, nonzero endpoint generation,
  stop=end, kind=space, and in-window. No hardcoded seq200/pid25/generation52. The numeric
  filter is an observation filter, not semantic decoding.
- **Wait hook** (L2024): records the first WAIT transition; duration uses begin→finish,
  not phase-reset age.
- **Send hook** (L1999/2005): reserves the row under the existing `meizu_ipc_lock`, unlocks,
  takes at most one bounded current-thread user backtrace (16 frames) via
  `backtrace_user`/`BTUINFO_INIT` in `meizu_ipc_send` after copyin/live-kmsg snapshot and
  before `ipc_kmsg_send`, then re-locks and attaches only if token/taskgen/tid/seq still
  match. The first row is retained even if short.
- **Finish hook** (L2066/2074): at the first `pending -> false` transition, records exact
  finish ticks/result/phase before unlocking. Later short operations are dropped and counted;
  later long operations (≥1 s) are retained up to capacity. A finish backtrace is taken
  only for a retained later-long row lacking start PCs, after unlocking, and is labeled
  `stage=finish`; end ticks are recorded before that backtrace.
- **Drain** (`nova_sblong_drain`, L497, called from `meizu_sb_ipc_drain` L2218): copies
  rows under `try_lock`; on failed try-lock it returns without marking printed. It prints
  `nova: sblong v=15` summary/start/finish and bounded PC lines once, outside locks.
  Summary counters: rows, overflow, short_dropped, expired, mismatch, bt_skipped,
  unfinished, start_bt, finish_bt, next_token.
- **DONE→EXIT dedupe**: the first pending→false transition marks the row finished; the later
  EXIT cannot finish it again. Nested sentinel and all Mach return/options/deadline/wake/
  queue lines are untouched (audit: protected lines present once, no removed source lines).

No payload/body, user frame bytes, or strings are captured. No new lock or timer is added.

## Tests (synthetic, not boot execution)

`test-sblong-nova.c` includes the **exact extracted** core region (`sblong-core-nova.inc`),
not a parallel reimplementation. Built with clang `-fsanitize=address,undefined
-DNOVA_SBLONG_HOST`; run exit 0:

`PASS sblong-core: first-short retained, later-long retained, short dropped, cap/overflow,
DONE/EXIT dedupe, filters, stale attach no-mutation, backward clocks, error path,
pending-then-complete`

Covered: first short retained; phase/wait start; R14-shaped seq200/taskgen4/tid253;
later short dropped; later long retained; cap exhaustion/no eviction; overflow; DONE/EXIT
dedupe; mismatched token/taskgen/nested/process/id/endpoint/window; zero/backward clocks;
capture error/no-PC; pending drained then complete; completion before first drain; frame
budget; stale attach cannot mutate a row; unfinished count.

These are synthetic host fixtures. They are **not** a runtime boot reproduction and do not
prove the probe fires on the real SpringBoard request.

## Target TU, build, package

- Real ARM64 TU compiled with the existing `mach_msg.o.json` argv (output redirected):
  exit 0, object SHA `ba2a4962…53ba9a`, no probe-specific warning. Compile log/argv/compiler
  hashes in `tu/` and `validation-nova.json`.
- Full local build: `./unixnu-nova build kernel -j8`, with the reviewed narrow PATH dSYM shim
  (`7bf8733a…`, prepended only for the build process), exit 0. Raw kernel
  `46013035…cf6723c` (73,932,800 bytes), object/source hashes bound in
  `build-result-nova.json`. The first attempt failed only at the sandboxed dSYM `/dev/fd`
  process substitution; the reviewed shim was then used. No device operation occurred.
- Package: R14 accepted 96 MiB envelope `6ea40782…641` as baseline; only
  `EFI/UniXNU/Payloads/kernel.macho` replaced. Entry patch/PAN used the same R14-pinned
  patcher/tool. Candidate:
  `artifacts-nova/boot-speed-round15-nova/sblong-probe-nova/package/sblong-r15-candidate-final-nova/boot-warm-sblong-r15-candidate-nova.img`
  SHA-256 `6887a6d0a4a841e75a230d7e5c8394627894c70efd0ec87a0d216baed071d19d`,
  100,663,296 bytes. Payload kernel SHA `97d11916…02411e`; PAN verified.
- Manifest state **`BUILT_AWAIT_NEW_USER_INSTALL_COMMAND`**,
  `installation_allowed=false`, `do_not_flash=true`, `install_authorized=false`.
  No installer file was created.

## Audit

- Source freeze (R14 6,186 entries): **only mach_msg.c changed**; no missing files.
- R14 policy `60b0dced…` and tests `a1276115…` exact.
- Byte-identical objects vs R14 build: `BootWatchdogPolicy.o`, `BootLog.o`,
  `NativeRamBlock.o`, `NativeUfs.o`, `kern_exec.o`. The R14 build did not retain a
  corecrypto object; `corecrypto_sha2.c` is source-exact against the freeze and the current
  object hash is recorded.
- Diff has no removed source lines; protected `ipc_kmsg_send`, DONE/EXIT finish, and
  pending assignment lines each appear once. No return/deadline/queue/wake edit.
- Linked raw kernel contains 13 `_nova_sblong*` symbols and the version-15 format strings.

## Limits / next

The probe predicts **no direct boot reduction**. It is designed to make one next capture
return exact request duration, final result/stage, endpoint/taskgen, and a bounded caller
stack, instead of another age-only snapshot. The service-side cause remains unobservable
until that capture. Installation/reboot/slot change requires a **new explicit user command**;
this report stops before installation.

## File identities

- Production source: `osfmk/ipc/mach_msg.c` `8a7fbe46…c030`
- Source diff: `source-diff-nova.patch` `03b5c56e…af82c`
- Host test/core: `51da0727…5def9582` / `64b00485…5242770`
- TU object: `ba2a4962…53ba9a`
- Raw kernel: `46013035…cf6723c`
- Candidate image: `6887a6d0…071d19d`
- Package manifest: `1291a6a8…9f57c`
- Audit: `ffefaf9c…fc6467e`
- Validation: `2b972949…ec3c8e4`
- Source freeze: `59253c21…9a495808` (6,186 entries; only mach_msg.c changed)
