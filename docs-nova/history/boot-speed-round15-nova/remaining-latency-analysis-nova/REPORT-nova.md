# R15 remaining-latency analysis (post-R14)

Scope: local evidence only. No device access, no `adb`/`fastboot`, no mount, no build was
performed in this revision, and no production edit. Raw source is the frozen R14 text; R7/R8 are historical comparators only.
All times below are Mach/CNTVCT at 19.2 MHz, `ns = floor(ticks*1e9/19200000)` unless noted.
This is a trace replay, not a boot reproduction.

## Model / execution configuration

- Fixed provider `deepseek-official`, model `deepseek-flash`, reasoning effort `high`.
- Persisted launch overlay: `dsh-analysis-overlay-nova.yml` (lines 1-5) and disabled
  subagent/fork/workflow tools (lines 10-21). No delegation.
- R15's **actual persisted request header is now independently verified by Sol**:
  `remaining-latency-analysis-nova/SOL-EXECUTOR-CONFIG-nova.json` records the persisted
  session and one header with provider `deepseek-official`, model `deepseek-flash`,
  reasoning effort `high`. This covers the first request; the final follow-up request
  header will be checked by Sol. The launch overlay remains corroborating configuration,
  not the only proof.
- R14 acceptance explicitly overrides the two old report semantics: `publish_ticks/19.2MHz`
  is an absolute timestamp, and the 19.796 s interval is **before** SpringBoard exec.

## Replay and baseline assertions

`replay-evidence-nova.py` (SHA-256 in evidence JSON) reads the frozen log, splits physical
lines at each `nova-ts` prefix, binds SHA-256, and asserts 52 conditions: all pass.

| anchor | raw line | capture_ticks | capture_ns |
|---|---:|---:|---:|
| launchd exec | L256 | 624604521 | 32531485468 |
| SpringBoard exec | L730 | 1029625136 | 53626309166 |
| lsd exec | L908 | 1108331117 | 57725579010 |
| first Setup armed | L1621 | 1916221829 | 99803220260 |

- `SB->Setup = 46.176911094 s`; R8 `161.579416303 s`; R7 `152.523992969 s`.
- `launchd->SB = 21.094823698 s`; `launchd->Setup = 67.271734792 s`;
  `lsd->Setup = 42.077641250 s`.
- First `/sbin/fsck` spawn is on physical **L509**, where the preceding printf lacked a
  newline and two `nova-ts` prefixes share one physical line. The accepted
  `19.796054115 s` is exactly the delta from the launchd log-line tick to that physical
  line's **first** prefix. The appended `spawn request` segment is 1.372 ms later
  (`19.797426 s`), so the accepted baseline is reproducible and the 1.37 ms does not
  change any conclusion. In capture domain this interval is `19.796086875 s`: the launchd
  log line is 629 ticks (32.76 µs) after its exec capture, so the table below uses the
  capture-domain value for the exact join. A line-only parser that takes the second prefix
  would shift the number; that is the main parser trap here.

## No-double-count accounting

The two high-level intervals are consecutive, not nested:

| interval | ns | s | content |
|---|---:|---:|---|
| launchd -> first spawn (capture domain) | 19,796,086,875 | 19.796087 | **inside** launchd->SB; accepted log-line delta is 19.796054115 (+32.76 µs) |
| first spawn -> SpringBoard | 1,298,736,823 | 1.298737 | remainder of launchd->SB |
| **launchd -> SpringBoard** | **21,094,823,698** | **21.094824** | sum of the two rows above |
| **SpringBoard -> first Setup** | **46,176,911,094** | **46.176911** | separate, starts after SB exec |
| **launchd -> first Setup** | **67,271,734,792** | **67.271735** | 21.094824 + 46.176911 |

Do **not** add 19.796 s to 46.177 s, and do **not** add the 19.326 s cs4k union to either;
it is inside the 19.796 s. The 19.796 s is ~93.85% of launchd->SB.

## Findings, ranked

### F1. The pre-SB 19.796 s is real dyld/codesign read+hash work, not a launchd timer (confidence: high)

23 `nova: cs4k` lines from L282 to L494, all inside `F_ADDFILESIGS` ->
`ubc_cs_blob_add` -> `cs_native4k_prepare`, give:

| field | observed |
|---|---:|
| files/intervals | 23 |
| bytes read (sum) | 2,724,626,432 |
| 256 KiB reads (sum) | 10,403 |
| `total_ms` (sum) | 19,326 ms |
| `read_ms` (sum) | 10,900 ms |
| `hash_ms` (sum) | 8,403 ms |
| interval union | 19,326,000,000 ns (19.326 s) |
| overlap between calls | 0 ns |
| coverage of the 19.796 s window | **97.6255%** |
| uncovered residual | 470,054,114 ns (0.470 s) |

Per-call ranges: bytes 19,873,792-333,447,168; reads 76-1,272; `total_ms` 234-3,861;
`read_ms` 172-2,827; `hash_ms` 61-1,032. Read and hash are sequential phases inside one
call (`ubc_subr.c:5866-5929`), and calls are sequential (union == sum == 19.326 s). The
residual is one 208.9 ms gap after the last cs4k print before the spawn, 37.3 ms before the
first print, and sub-17 ms inter-call gaps; it is ADDFILESIGS signature read/mmap/setup and
scheduler gaps, not a timer.

- 12/23 cs4k intervals can be bracketed by the first 24 traced `addfilesigs` begin/result
  pairs; the other 11 occur after the success-trace cap at L442 (`kern_descrip.c:3909`) and
  therefore have no begin/result bracket. Their own end timestamp and `total_ms` still
  bound them; code order places every cs4k print before `addfilesigs result`
  (`kern_descrip.c:4036,4052`, `ubc_subr.c:5959`).
- This is **not** the warmload record. Warmload L181/L182 reports `read=0.331 s`,
  `hash=8.667 s`, `import=9.287 s`, `publish=31.908 s`; those overlap and `publish` is an
  absolute timestamp. Do not sum them or treat them as this interval.
- Existing hash acceleration is active: `native_sha256_digest`
  (`libkern/crypto/corecrypto_sha2.c:208`) selects the ARM64 SHA2 compression path when
  `ID_AA64ISAR0_EL1.SHA2` is 1/2 (`:192-193`) and batches 16 KiB per preemption-disabled
  interval. The trace therefore supports "real work", not "hash was unaccelerated".

### F2. The largest observed piece of SB->Setup is one launchd reply wait of at least 27.246 s (confidence: medium-high for the lower bound; medium for dominance)

Three snapshots of the same SpringBoard message, L1021/L1283/L1437:

| line | phase | pending | age_ms | send_id | endpoint |
|---|---:|---:|---:|---:|---|
| L1021 | 4 | 1 | 6,823 | 6001009 | launchd (rname=10103) |
| L1283 | 4 | 1 | 17,047 | 6001009 | same ep/epoch |
| L1437 | 4 | 1 | 27,246 | 6001009 | same ep/epoch |

- `age_ms` is **WAIT-phase age**, not send-to-completion: `sbipc` computes
  `now - r.when` (`mach_msg.c:1653`) and `r.when` is reset on every phase transition
  (`mach_msg.c:593`). Phase 4 is `MEIZU_IPC_WAIT` (`mach_msg.c:136-137`). The three
  snapshots are 10.224 s / 10.200 s apart, and their age deltas are 10.224 s / 10.199 s;
  implied WAIT start spread is only 0.600 ms. This is one continuously pending receive.
- At the last snapshot the wait is **27.246 s**, i.e. **at least 59.00%** of the 46.177 s
  SB->Setup. The exact send-to-completion time is unknown: there is no completion/result
  line for `seq=200`, and the receiver-map goes `matched` (L926, 58.065 s) -> `matched`
  (L1153, 68.281 s) -> `unavailable` (L1303, 75.429 s) -> `unavailable` (L1445,
  85.626 s). "Unavailable" is not a completion.
- The request ID `6001009` is **not decoded**. No local definition or disassembly binding
  was found; only the verified numeric value, the launchd endpoint, and `rname=10103`
  (a port name, not a service name) are used. Do not infer the operation from the number.
- lsd does not explain this wait. lsd exec is 4.099 s after SB; its trap counters reach
  6.628 s cumulative by 40.1 s of lsd age, with `sum_is_concurrent=1` and a maximum single
  bucket of 2.384 s. The repeated container-registration signal is absent: only 8
  `container` lines exist, none a repeated registration flood. WarmData is active
  (warmload L181: status=0, bytes=231,153,664, digests verified=1), but this single run
  cannot attribute the absence to warmData alone.
- The remaining 18.931 s of SB->Setup is **not attributed by this probe**; it is not
  proven idle or free, and the same reply may continue past the last snapshot.

### F3. Diagnostics and lsd CPU do not dominate; scheduling cost is not measurable from this trace (confidence: medium-high for the bound, low for scheduling)

- Autolog write path before Setup (L1546, t=94.129 s): 61 attempts/writes, 8,749,056 bytes,
  `copy_ns=1,681,875`, `write_ns=259,242,291` (259.242 ms), `max_write_ns=40,944,375`.
  That is 0.56% of SB->Setup for the measured ring write path. It does **not** measure all
  kernel `printf`/format cost.
- lsd task CPU: 11 samples. First at +33 ms: 16.361 ms. At 67.771 s: 1.720 s. Final at
  157.958 s: 1.760 s over ~100.23 s wall (1.76%). This bounds **lsd's own CPU**, but task
  CPU alone cannot prove idle or quantify scheduler delay; other tasks are not covered.
- Observed progress lines report `np=4`, not 8. Do not assume all 8 CPUs were active and do
  not convert task CPU into busy/idle claims.
- lsd IPC: 363 completed / 16 errors at the last pre-Setup dump, cumulative 6.628 s with
  `sum_is_concurrent=1`; the 2.384 s max is not a wall-time budget. The 27.246 s wait is a
  SpringBoard->launchd wait, not an lsd bucket.

### F4. Setup's `hasConnection` wait is not established or excluded by R14 (confidence: low / unresolved)

- First Setup arm is L1621 at 99.803220 s. The kernel ulock trace shows **13/13**
  `setup-worker-lock-entry`/`result` pairs with `result=0`; the first wait is 15.631 ms and
  the maximum is 42.436 ms. These are **ulock wait/return** observations: the traced Setup
  workers returned, not hung, in this window. They are not `semaphore_wait_trap` events;
  R8's `hasConnection:` path used that different primitive, and absence of selected
  semaphore events does not identify callback behavior.
- The first `setup-budd-ipc` message is 25.637 ms after arm; at +588.411 ms, seq=28 has
  `id=0 result=10004003`. Local `osfmk/mach/message.h:1368` defines `0x10004003` as
  `MACH_RCV_TIMED_OUT`, so this is a timed-out receive, **not a crash or watchdog**.
- The Setup IPC trace closes at L4200, 20.006 s after arm (`setup-budd-ipc-close`:
  admitted/printed 2048, dropped 322), tail at L4947. No `hasConnection` symbol or
  userspace stack mapping appears in R14 (`hasConnection_symbol_found=false`). The 13
  ulock returns cannot be attributed to `-[ProximitySetupController needsToResume]`.
- There is **no actual panic/crash/watchdog event** in the captured window. L54/L55 are
  `gPanicBase` init errors, L246 is `watchdog policy: ready`, and L907/L1390 are
  `DumpPanic`/`ReportCrash` process execs, not crash events. Absence proves only this
  window.
- Setup armed is not rendered Hello. The log ends at L5298, 167.733243 s; there is no
  Hello first-frame anchor, so Setup->first useful UI and the full Boot->Hello remain
  unmeasured. Persistence of the R8 `hasConnection` wait after the traced Setup window is
  **missing evidence**, not a negative result.

## Source bindings (current production tree)

| file:line | role |
|---|---|
| `bsd/kern/ubc_subr.c:5835` | `cs_native4k_prepare` entry |
| `bsd/kern/ubc_subr.c:5878` | `read_size = MIN(limit, 256 KiB)` |
| `bsd/kern/ubc_subr.c:5897,5929` | read and hash tick accounting |
| `bsd/kern/ubc_subr.c:5959` | `nova: cs4k` print (limit >= 16 MiB, max 64) |
| `bsd/kern/ubc_subr.c:5009-5028` | `ubc_cs_blob_add` native-4K cache/protection handoff |
| `bsd/kern/kern_descrip.c:4036,4052` | `F_ADDFILESIGS` begin/result prints |
| `bsd/kern/kern_descrip.c:3909` | success-trace cap at 24 reports |
| `libkern/crypto/corecrypto_sha2.c:192-193,208` | ARM64 SHA2 selection / `native_sha256_digest` |
| `osfmk/ipc/mach_msg.c:593,1653` | `when` reset and `age_ms` phase-age computation |
| `osfmk/ipc/mach_msg.c:1620-1636` | completion clears `pending`, records `result` |
| `osfmk/ipc/mach_msg.c:1191` | `setup-budd-ipc` print |
| `osfmk/mach/message.h:1368` | `0x10004003 = MACH_RCV_TIMED_OUT` |
| `bsd/kern/kern_exec.c:3727-3752` | Setup arm capture / `setup-arm` print |
| `bsd/kern/sys_ulock.c:392,494,538` | `setupbt`, worker-lock entry/result prints |

## Missing evidence / unobserved gaps

1. No completion/result line for SpringBoard `seq=200`; exact send-to-completion is unknown.
2. No local binding for `send_id=6001009`; operation semantics intentionally undecoded.
3. No Hello first-frame anchor; no Boot menu wall clock.
4. No R14 userspace symbol/stack binding for `hasConnection`; kernel ulock returns are not
   proof of callback completion.
5. lsd's own CPU is known (1.760 s over ~100.23 s wall); sleep/wake state and whole-machine
   scheduling remain unmeasured, and task CPU alone does not prove idle/busy system state.
6. The first-request model header is Sol-verified; the final follow-up header remains a
   Sol check. The missing item for this investigation is the seq=200 request payload, not
   the model header.
7. No cold comparator with identical cs4k and sbipc instrumentation, so causal attribution
   of the R14 improvement to warmData or the gid fix is not possible.

## Handoff

All production files, previous reports, raw logs, R14 image/build records, and prior
CHANGELOG content are unchanged. New files in this directory have no previous versions.
Candidate implementation is not started; this report is for Sol acceptance.
