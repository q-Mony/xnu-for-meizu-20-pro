# R16 review-fix report (new file, no previous version)

Scope: diagnostic additions in `osfmk/arm64/sleh.c` and `osfmk/ipc/mach_msg.c`,
plus prior-task tests/docs. No other source, device, build tree, package, mount,
config, or network action. Diagnostic only; **root cause remains unresolved and
no speed/device acceptance is claimed.**

## Source changes after review

### sleh.c
- Admission/dedup/quota now run first, using immutable scalar keys only, under
  the existing atomic busy gate; the owner unlocks before any copyin and fills
  only its own reserved slot. Duplicate/full/busy paths do zero user copy.
- Guard `fp==0`, `sp==0`, alignment, canonical top, and the entire
  `fp-0x60..fp+0x100` window. Added total stack-read cap and checked frame reads.
- A partial user copy is scrubbed (`data`, `copied`, `len`) before returning
  `COPY_ERR`, so no partial words can be printed/reconstructed.
- x19/context+0x58 words are now labeled `sbfault-x19raw` / `actual_row=unknown`;
  `context+0x58` is not claimed to be the real row.

### mach_msg.c
- Receive invocation token is returned by `nova_life_receive_begin` and carried
  through `mach_msg_receive_results` into `nova_life_receive_end`; reply begin
  returns a token and reply end is bound to that fixed selected operation.
- Separate recv and reply server identities; stable finish keys
  (seq+taskgen+tid); receive/reply end cheap token checks before the lock.
- Metadata (cookie/epoch) is computed while the local pin is certainly owned,
  before admission/transfer; pin is released exactly once outside the lock.
- Finish adds a precomputed 2 s grace, capped by the 300 s admission deadline,
  so sender-syscall return racing server-send end is not read as "no reply".
- Per-stage lock-drop counters recorded; missing/expired observations print
  `missing=unknown` (never "no receiver/reply").
- Removed one-argument `os_atomic_load`; removed unlocked `nova_life.active`
  reads; counters are atomic throughout and snapshotted under lock.

### root-cause report
Verified prior corrections (straddling FAR, `+16` block advancement,
caller-saved-clobber, inline registration limitation, VM text absence). Added
the explicit `context+0x58 != actual row` caveat. R8 corroboration unchanged.

## Tests (host, ASan/UBSan)
- `extract-and-test-nova.py`: extracts the EXACT marked production helper blocks
  (stack core, new fault-reserve core, life core) and runs
  `test-r16-nova.c`. **GREEN**, exit 0. Covers zero/max FP with no copy, full
  window bounds, partial-copy EFAULT scrub, reserve dup/quota/invalid, token
  separation, nested receive, different reply thread, fixed reply-end token,
  finish keys, grace/deadline, reused port rejection.
- `run-red-nova.py`: runs against the **saved pre-correction extracted cores**
  left by the stopped prior run. **RED**, 8 defects reproduced (zero/max FP
  accepted, unwanted copies, partial bytes retained, reply identity overwrite,
  seq-only finish, no finish grace).
- `audit-integration-nova.py`: static hook token propagation and lock/ref
  boundary checks. **PASS** (25 checks). This is a source check only and does
  not prove runtime causality.

## Actual ARM64 TU compile
Replayed the recorded JSON clang argv with output redirected to `tu/`, `-g`
changed to `-g0`, no link/dSYM/full build.

| TU | exit | object sha256 |
|---|---|---|
| sleh | 0 | `3a97445852633f1c9ba68153b64d2963b06752bb2b47a0bde4c1f88e67f9caa3` |
| mach_msg | 0 | `8da50c22778e95af21d34b38ed9990dae5fd7feaf7bb9b32e8503e88406a17b8` |

## Source hashes (sha256)
- sleh: `a1220ca9b616bb8f2be650a2600c827181a5bb6782b9b31347b6ac751a51da74`
- mach_msg: `f8758befcf7d79318c0e777adb632423816bea9a9012b39d14b7912475afb2e9`

## Backups
- `snapshot-nova/boot-speed-round16-nova/probe-review-fix-nova/post-fix/`
- `snapshot-nova/boot-speed-round16-nova/probe-review-fix-nova/pre-fix-nearest/`
- `BACKUP-NOTE-nova.md` records that the exact pre-fix source left by the
  stopped prior run was not recoverable (no snapshot was created then; Git HEAD
  is stock XNU). The nearest implementation-start snapshot and the saved
  pre-correction extracted cores were used instead.

## Unresolved limits
- No device, boot, install, full build, or speed measurement. TU compile does
  not link or prove runtime behavior.
- Exact original row index remains unknown; x19/context+0x58 are hypothesis
  only. Root cause and H1/H2/H4 remain open.
- Static integration checks cannot establish ordering/causality.
