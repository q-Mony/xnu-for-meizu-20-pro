# R15 backboardd startup wait analysis (read-only, static)

New file; no previous version. Target: actual iOS 20H392 binaries already mounted read-only.
No production edits, builds, device access, remount, or target execution.

## Verdict

**No explicit 30 s timeout was identified in the bounded static scan of backboardd.** In
the explicitly scanned instruction/constant forms (movz/movk/mov immediates, fmov
immediates, PC-relative ldr literal loads over all of `__TEXT,__text`; 8-byte aligned
scans of `__TEXT,__const` and `__DATA_CONST,__const`) no 30e9 ns, 30e6 us, 30e3 us, or
30.0 s value was found. This does not exclude a computed deadline, a value loaded
indirectly, a shared-framework wrapper, or an Objective-C/indirect callee.

The only `30`-related immediate found by that scan is a log-format length. A **60 s /
5 s-leeway main-screen-digitizer timer** does exist; its scheduling setup is asynchronous,
but it has not been shown to be causally independent of all startup dependencies, and it
is not an identified 30 s inline delay. The three `dispatch_group_wait` sites pass
`DISPATCH_TIME_FOREVER` as the caller deadline; that bounds the caller, not the producer,
so a producer signal/leave after ~30 s remains consistent with them.

Consequently no 30 s inline timeout was identified in backboardd; this does not establish
that the R14 30.52973265625 s gap is unrelated to a longer-running wait or to a dependency
release at ~30 s. Static analysis cannot turn that gap into runtime causality; the bounded
next step is a receiver-side timestamp/stack pair (section "Next event"), not a display or
timeout change.

## Target identity

| item | value |
|---|---|
| binary | `/Volumes/SydneyUpdate20H392.D22D221OS/usr/libexec/backboardd` |
| SHA-256 | `7aa6f8bf5ea2c3634fb8c8916a4e9c3c84508de78eccf57ca65c1ae4f676267c` |
| size | 1,184,848 bytes |
| UUID | `70b72340-737a-3973-8b47-4e9970f4ecb9` |
| arch | arm64 (`0x0100000c`), MH_EXECUTE |
| entry (`LC_MAIN`) | unslid VA `0x100009664` |
| disassembled | `__TEXT,__text` `0x100002090..0x1000908c4`, 0x8e834 bytes, 145,933 insns |
| function starts | 2,987 from `LC_FUNCTION_STARTS` (binary is stripped: 742 nlist entries, 0 local defined symbols) |
| chained fixups | `LC_DYLD_CHAINED_FIXUPS`; 453 `__stubs` resolved through `dyld_info -fixups` + `__got` |

Section hashes (SHA-256 of the section's file bytes):

| section | VA | size | SHA-256 |
|---|---|---|---|
| `__TEXT,__text` | `0x100002090` | `0x8e834` | `53f3ea1f39992ecb3d4a2c8f152f676658406c1e0921a8ab117e3fa03ce64267` |
| `__TEXT,__stubs` | `0x1000908c4` | `0x153c` | `eaeadd97d6745b8ea3617ec2ee1f664acc8715965594d4404d96cde37f25770f` |
| `__TEXT,__const` | `0x1000a7300` | `0x580` | `25895492638d0d0e02d8cc416726dc09f3844dcaaa89fa820ec73e8af01c2dbf` |
| `__TEXT,__cstring` | `0x1000bfae2` | `0x729c` | `9521a44ea3d7a2fe1fae09dea1f9b280122e80b5e2bb0ec608c6956e63b084ca` |
| `__TEXT,__oslogstring` | `0x1000c6d7e` | `0x920a` | `d2c194345567432868e75d910bcac2287c0ae3e015874899b2f9aaa362180340` |
| `__DATA_CONST,__got` | `0x1000d8000` | `0x11b0` | `4811e054f99591c662d1921f7746dce9379fee040b5bf5191fcbdc3c8cb6194e` |
| `__DATA_CONST,__const` | `0x1000d91b0` | `0x4a48` | `f6909bcc4bc70cad9c23ab3c4d601a5640c3ffb486a2747496e9f512afceeb9d` |

## 1. The 30 s claim: negative, with the bounded scan shown

Scan basis: every AArch64 instruction in `__text` was decoded once (Capstone 5.0.7), tracking
`movz`/`movk`/`mov` immediate composition, `fmov` FP immediates, and PC-relative `ldr`
literal loads; plus an 8-byte aligned scan of `__TEXT,__const` and `__DATA_CONST,__const`.

- Hits for 30e9 / 30e6 / 30e3 / 30.0 s in these scanned forms: **zero**. The scan does
  not cover computed deadlines, values reached only through Objective-C/indirect dispatch,
  or timeouts inside shared-framework wrappers.
- Only two `30`-related hits in the scanned forms:
  - `0x1000231bc  mov w5, #0x1e` — **not a time value**. `w5` is the `__os_log_impl`
    format-string length consumed at `0x100023234`; the same function
    (`0x100023060`) is a display/orientation logging path. It reaches no timer or wait API.
  - `0x100038de0  mov x8, #0x404e000000000000` (double **60.0**), `0x100038de4 fmov d0,x8`,
    `0x100038de8 fmov d1, #5.0` — the digitizer sentinel below, **60 s**, not 30 s.
- No `_dispatch_semaphore_wait`, `_sleep`, `_usleep`, or
  `_IOServiceWaitForMatchingService` import exists at all (`dyld_info -fixups`/`__got`
  resolution). This does not exclude a timeout implemented inside a shared-framework
  wrapper, reached through Objective-C/indirect dispatch, or computed at runtime.
  `_mach_msg` is imported, so a blocking receive is structurally possible, but no
  backboardd call site in the scanned forms passes a literal 30 s deadline.
- Only 13 wait/timer-related imports exist: `_dispatch_after`, `_dispatch_group_wait`,
  `_dispatch_group_create/enter/leave`, `_dispatch_source_set_timer`, `_dispatch_activate`,
  `_IOServiceMatching`, `_IOServiceGetMatchingService(s)`, `_xpc_connection_activate`,
  `_wd_endpoint_activate`, and one HID timer-mode string helper.
- `IOService` matching is query-only: `_IOServiceGetMatchingService` at `0x1000462f8` /
  `0x100046318`, `_IOServiceGetMatchingServices` at `0x100045a68` / `0x1000467e8`.
  None is a wait-for-matching API in the direct import list.

## 2. Named waits that actually exist (and why none is a 30 s timeout)

| address | evidence | blocking? | timeout |
|---|---|---|---|
| `0x1000181d8` / call `0x1000181f4` | loads dispatch group at `[self+0x20]+0x38`, `mov x1,#-1`, `_dispatch_group_wait` | **yes** | `DISPATCH_TIME_FOREVER` (`-1`) |
| `0x100018a20` / call `0x100018a3c` | same group pattern, `mov x1,#-1` | **yes** | FOREVER |
| `0x100018a58` / call `0x100018a74` | same group pattern, `mov x1,#-1` | **yes** | FOREVER |
| `0x100038d54..0x100038df4` | `_BSContinuousMachTimeNow` stored at `[obj+0x50]`; `d0=60.0`, `d1=5.0`; selector `0x10009bf20` = `scheduleWithFireInterval:leewayInterval:queue:handler:`; handler `0x100041808` | **no** (async timer) | 60 s fire / 5 s leeway |
| `0x100041808` (handler) | logs `0x1000418c4` `*** still waiting for a main screen digitizer -- %gs elapsed ***`; elapsed = `-[obj+0x50]` | runs on its own queue | n/a |

The group is the object reached as `[self+0x20]` ivar `+0x38`; the only direct reference to
`0x1000181d8` in `__text` is from `0x100017d08` (function `0x100017c60`), i.e. it is reached
through a block/ObjC call, not a simple startup `bl`. The `-1` is only the caller's
deadline; it does not imply the group cannot be left after ~30 s. A producer that signals
or leaves the group at ~30 s is fully consistent with these sites, so they remain plausible
candidates pending runtime stack and producer timing.

The main-screen-digitizer sentinel is a 60 s timer with a 5 s leeway on the object's queue;
its handler logs elapsed time and clears/cancels state. Its scheduling setup is asynchronous,
but this analysis has not shown it to be causally independent of all startup dependencies.
It is not an identified 30 s inline delay. If the missing provider is the main digitizer, the
first sentinel log would be at ~60 s rather than ~30 s, but a timer-related dependency
release at ~30 s is not excluded.

Named dependency behind that timer (from strings/selectors, not from a symbol table):
**main-screen digitizer IOHID service** (`WaitForMainDigitizer`,
`_queue_startMainDisplayDigitizerMatchingTime`, `*** acquired main display digitizer (after %gs)`,
`*** lost main display digitizer ***`). These strings/selectors place it in an IOHID/digitizer path; they do not establish whether a missing provider caused the delay.

`0x10005d514` (`checkInAndWaitForDataMigration:completionBlock:` logging) is **not**
backboardd's own launchd check-in: it locks a system-shell object and logs through
`_BKLogSystemShell`; it belongs to the SpringBoard/system-shell check-in protocol, so it does
not establish the launchd handoff timing either.

## 3. Display service path (the receiver side of 6001009)

`0x10000a360` is the display MiG server start:

```
0x10000a368 adr x0, "BKDisplayServices MiG Server"      ; 0x1000c1759
0x10000a370 adr x1, "com.apple.backboard.display.services" ; 0x1000c1776
0x10000a378 adr x2, 0x1000dd750                          ; dispatch/routine table
0x10000a380 bl  0x100023ee8                              ; create/activate service
0x10000a388 adrp x8, 0x100104000
0x10000a38c ldr  x1, [x8, #0xb70]                        ; old server
0x10000a390 str  x0, [x8, #0xb70]                        ; publish new server object
```

Inside `0x100023ee8` this is a real named-port **server** registration, not a client:
`0x100023f08 bl _GSRegisterPurpleNamedPort` (registers the service name with
launchd/GraphicsServices), `0x100023f78 bl _dispatch_mach_create_f`, and
`0x100023f94 bl _dispatch_mach_connect` (Mach receive channel/listener). This is the
endpoint associated with that service name; whether launchd held it and when handoff
occurred remain runtime unknowns. No wait or timeout is present in this helper; if this
path ran, the receive side was created.

The start address is referenced as a function pointer from `__DATA_CONST,__const`
`0x1000d97a0` (raw chained value `0x1000000000a360`), i.e. it is dispatched from a startup
table rather than a direct `bl` in `main`. That is the exact point whose timestamp would tell
us whether the display service was installed before or after the 6001009 request.

Important limit: this is static ordering only. The function itself has no identified 30 s
wait and no blocking call. Whether initialization reached this point remains a runtime
unknown. If it runs late, a candidate cause is an earlier startup-table item or wait, or the
launchd/service handoff — none of which can be timestamped from the binary.

## 4. Bounded unresolved paths

1. **When `0x10000a360` executes.** No direct `bl`; only a table pointer at `0x1000d97a0`.
   The table iterator/phase order was not decompiled here.
2. **Whether the main thread is in a group wait at the time of the gap.** The three
   `DISPATCH_TIME_FOREVER` sites are real and carry a FOREVER caller deadline; a producer may
   still leave after ~30 s. Runtime PC/LR would
   confirm, but only one of them is in a function reached from `0x100017d08`.
3. **Whether the main-screen digitizer ever appears.** Static evidence proves a 60 s
   diagnostic timer, not whether the board publishes the IOHID service.
4. **launchd handoff.** The claim that launchd holds the display service until backboardd
   checks in is neither proven nor refuted by the backboardd image; the 6001009 message
   could queue there. This analysis does not establish it.

## 5. Next minimally useful receiver-side event (qualified)

R15's pending candidate already records sender/finish, so do **not** duplicate it. A useful
receiver-side pair would be:

- **Named ready event:** timestamp the store at `0x10000a390` (publish of the display MiG
  server object into global `0x100104b70`) after the `0x10000a380` call. This is positive
  static evidence that the BKDisplayServices server port is installed; it is not proof that
  the client handoff completed.
- **One bounded stack sample of backboardd at 25–35 s after exec** (or on first 6001009
  wait): capture PC/LR of every thread. A sample at `0x1000181f4`, `0x100018a3c`, or
  `0x100018a74` is at one of the FOREVER-deadline group waits; that alone does not prove the
  producer timing or the eventual signal.

**Qualification:** one pair of samples cannot by itself discriminate all three remaining
causes. If the service store has happened and every *sampled* thread is in service/runloop,
that is consistent with a launchd/client handoff delay, but it does **not** exclude prior
blocking, unsampled workers, or a handler/reply dependency. Correlated receive/reply
timestamps and repeated samples are required before any of those is selected.

Two weaker inferences must not be overread:

- “No direct wait import” does not exclude wrapper layers that call a wait internally.
- A `FOREVER` deadline can still be signalled after 30 s; a bounded/no-inline-timeout
  observation is not a runtime exclusion of that path.

## 6. Fix decision

**No fix is proposed.** No concrete source/capability mismatch is supported by this evidence:
no 30 s inline block was identified in either the source path examined or this binary scan, and the
digitizer dependency is an IOHID provider question that is not proven missing. Do not change
`NegroniDisplay/NegroniFramebuffer.cpp` timing, do not shorten or force responses, and do not
add a dummy digitizer/capability.

If the receiver-side stack later proves the forever group wait is gated on a specific absent
IOHID service, the correct class of fix is to supply that real board-level provider (board
driver / device-tree publishing), not to alter backboardd or the display driver. That is a
future decision, not established here.

## Reproduce

```sh
cd artifacts-nova/boot-speed-round15-nova/backboard-init-analysis-nova
python3 analyze-backboard-startup-nova.py > startup-static-nova.log 2>&1
```

Read-only inputs: the mounted backboardd above, `/usr/bin/dyld_info` (chained-fixup bind
targets), Capstone 5.0.7. No cache image or R14 slide was needed for this question; addresses
above are unslid. No old pycache was used or left behind.

## Deliverables and provenance

| file | SHA-256 | note |
|---|---|---|
| `analyze-backboard-startup-nova.py` | `8f147f4d7a7a152d4b8a5e89dffc6a4743ebc072e6d581ce4848b0dc1f0642d8` | new, no previous version |
| `startup-static-nova.json` | `19d78e83c846d0fb1c8f624322784a1040a8039f7325879b1eec810ae5edb0f4` | full parsed evidence (sections, imports, xrefs, calls, constants, key events) |
| `startup-static-nova.log` | `54ffb6aa33f1216abd55e71e1f6f4a14d9ac831cb31e9d182bc5ac08c5f8f298` | console transcript |
| `exports-nova.txt` | `208` bytes | `dyld_info -exports`: only 5 exports, no name for the analyzed addresses |

Caveats: all statements above are static observations/inferences from a stripped binary; no
runtime execution was performed. Function labels are behavioral (strings, selectors, call
structure), not local symbol names. Section hashes identify the exact bytes analyzed. The
30.52973265625 s R14 value is a gap between logged operations only; it is not used here as a
measurement of backboardd blocking.
