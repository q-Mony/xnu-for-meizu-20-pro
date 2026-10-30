# Review result: R15 backboardd startup-wait report

New file; no previous version. Explanatory revision only: no disassembler rerun, no new
research, no build/device/production/config change. Another executor owns CHANGELOG/plan.

## Binding

- Revised report: `backboard-init-analysis-nova/REPORT-nova.md`
  - SHA-256 `bb7eb2ce107a4485d2d34cb559afaecf123eca68f484335e549bd1d0a99198db`
- Original report: `af7a04dbfa158eb4dd0aefc25ea0ac0b446b192c6726e0cb7c4ab3030a97c2a9`
- Original script: `8f147f4d7a7a152d4b8a5e89dffc6a4743ebc072e6d581ce4848b0dc1f0642d8`
- Revised script: `2cd2852c5e474847d86ae2096bc1669571351df2fd88a653ef01eada0a6cc1ba`
- Original parsed evidence: `19d78e83c846d0fb1c8f624322784a1040a8039f7325879b1eec810ae5edb0f4`
- Revised parsed evidence: `2be8886f39ab1a2e17b4145d9126349e9cdff52cf74cbda3c0890f795c165d6f`
- Input binary: backboardd SHA-256 `7aa6f8bf5ea2c3634fb8c8916a4e9c3c84508de78eccf57ca65c1ae4f676267c`,
  UUID `70b72340-737a-3973-8b47-4e9970f4ecb9`.
- Pre-review copies are in `backup-nova/` with hashes recorded in `review-result-nova.json`.

Only explanatory text changed in the script (`KEY_EVENTS` string for `0x10005d514`) and in the
JSON (`key_events.system_shell_checkin`). Machine-code results (sections, imports, xrefs,
wait calls, constants) are unchanged.

## Corrections applied

1. **FOREVER group waits.** `DISPATCH_TIME_FOREVER` is the caller deadline; it does not bound
   when a producer can leave the group. The three sites (`0x1000181f4`, `0x100018a3c`,
   `0x100018a74`) remain plausible candidates for a finite ~30.53 s delay pending runtime
   stack and producer evidence. Removed the claim that a ~30 s release is not this wait.
2. **Negative scan scope.** Zero 30e9/30e6/30e3/30.0 hits now applies only to the explicitly
   scanned forms in this executable. Computed deadlines, shared-framework wrappers,
   Objective-C/indirect callees and generic-call timeouts are not excluded.
3. **Named-port registration retained.** `0x10000a360` registers
   `com.apple.backboard.display.services`, stores the server at `0x100104b70`, and its helper
   `0x100023ee8` calls `_GSRegisterPurpleNamedPort` (`0x100023f08`),
   `_dispatch_mach_create_f` (`0x100023f78`), `_dispatch_mach_connect` (`0x100023f94`).
   Launchd hold/handoff timing and whether initialization reached this site remain runtime
   unknowns.
4. **60 s digitizer timer.** The timer at `0x100038de0..0x100038df4` (handler `0x100041808`)
   is 60 s / 5 s leeway. Asynchronous scheduling does not prove causal independence; it is
   not an identified 30 s inline delay. A missing digitizer or a timer-related release at
   ~30 s is not excluded.
5. **Verdict narrowed** to "No explicit 30 s timeout identified in the bounded static scan".
   No behavior fix is proposed; the three FOREVER-deadline waits remain candidates.

## Unchanged next event

Timestamp the `0x10000a390` display-server store and take one backboardd stack sample at
25-35 s. Do not duplicate the R15 sender/finish probe.
