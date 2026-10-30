# R16 PANIC log recovery and read-only analysis (Nova)

**Status:** COMPLETE — one authorized Fastboot A -> Android B return, retained log captured, panic text absent from the persisted window.
**New files only.** No shared CHANGELOG edit, no flash/install/start, no partition write, no command to any non-target device, no device command after capture.

## Authorization and scope

- User supplied Fastboot confirmation: `现在到fastboot了你可以你好好看下日志了`.
- Authorized action: exactly one return from target Fastboot A to Android B for read-only log recovery.
- Explicitly not authorized/not performed: new candidate install, flash, start, slot cycling, export/partition write, other-device command.
- Sole target: `[redacted-device-serial]` (device tag `8258937a53b0`); all other enumerated devices ignored and never addressed.
- Effective config: `deepseek-official` / `deepseek-flash` / `high`; no delegation or fallback.

## Preconditions verified before recovery

| Item | Expected | Actual |
|---|---|---|
| R16 installer SHA-256 | `26a2e29e...192951` | match |
| R16 candidate image SHA-256 | `51a02483...0bf51` | match |
| BASE_B boot_b SHA-256 | `7df205f1...45a2` | match |
| install record | `INSTALLED_A_AND_FULL_READBACK_VERIFIED_IN_ANDROID_B` | match, SHA `c76709d3...92cc4` |
| start record | `XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION` | match, SHA `8519f38a...53486` |
| Sol install acceptance | `INSTALL_AND_START_ACCEPTED_AWAIT_HELLO_CONFIRMATION` | match, SHA `22b7fd9e...f0305` |
| Fastboot current slot | A | A confirmed |

No Hello record was required for this PANIC round.

## Recovery phase (one action)

The new R16 return script is a byte-for-byte copy of the accepted R15 script except its docstring (`R15` -> `R16`); its AST body is identical. It imported the accepted R16 installer (hash asserted), whose `TARGET_SERIAL` is `[redacted-device-serial]`. Execution:

1. selected exact target in Fastboot and confirmed `current-slot: a`;
2. selected B and confirmed `current-slot: b`;
3. issued reboot to B.

Result: `RETURNING_ANDROID_B_FOR_READONLY_CAPTURE`. Return record SHA-256 `790b42bc...e8ace9`.

ADB wait: 14.196 s, 8 bounded polls (2 s interval, 90 s cap). Other ADB devices were ignored.

## Read-only post-boot checks (separate from recovery)

- current slot: `b`
- `boot_a`: `51a0248347f0e70644a28d30f932ce59245ada3920ade777ac5835578e30bf51` (R16)
- `boot_b`: `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2` (BASE_B)
- root available; boot geometry 100663296 bytes each
- GPT primary header/entries unchanged
- warm export SHA-256 `7e6cc19843c0ee28eb8f1891a7d53e074688743902ef9ccf01b20d1a48e57140`
- no partition write; `device_mutated: false`

Post-boot record SHA-256 `6660e76e...cc231b`. Device was left on Android B; no further device command was issued.

## Capture

- Reader copied with one minimal change: exact-target serial filter before the single-ID assertion, retaining the expected device-tag check and the full 32 MiB ring protocol/hash/geometry/read-only behavior.
- Output: `logs-nova/boot-speed-round16-nova/candidate-run-nova/`
- raw 32 MiB SHA-256 `33123594...528f5d`
- text 246202 B SHA-256 `fa47666f...90a0fbf`
- ring_total `246029`, dropped `0`, reserved `0`, NUL `0`, `MEIZULOG` header valid
- capture not truncated; metadata SHA-256 `70a305d7...9aaa9`

## Panic facts

User report: Boot -> Apple ~80 s, then panic:

```
panic=0xfffffff007050818 cpu0 caller=0xfffffff00896b808
log_encode.c:120 Assertion failed: log_fits_cmd(ctx, cmd_sz)
```

Retained-window result:

- **No panic/assert/log_encode text is present.** `panic` word count in the window is 0; the last normal line is at tick 97.839 s. There is no kernel backtrace in the window.
- One fresh boot header; first timestamped line at 17.831 s. The window spans **80.007 s** of kernel log and ends abruptly after ordinary UI/backboardd lines. It may be the panic boot truncated before the panic text could be encoded, or a subsequent post-panic boot; the ring alone cannot distinguish. This is an inference, not a recovered panic line.
- No `life v=16`, `sbfault`, or `log_encode` marker was emitted, so the R16 build identity is inferred from the fresh post-install boot, not from a unique R16 marker; whether it is the exact panic boot is indeterminate.
- The 16 `sblong start` PCs are SpringBoard userspace addresses, **not** a kernel panic stack.

Frozen R16 binary mapping (`raw-kernel-before-patch-nova.macho` SHA `5f25a1a6...52de0`; payload SHA `c445b47b...bdfe6`):

- `0xfffffff007050818` is in `__TEXT,__cstring` at file offset `0x4c818` and resolves to the format string `%s:%d Assertion failed: %s`. It is a **panic message pointer, not a code PC**.
- `0xfffffff00896b808` is the return address after `bl _panic` in `_Assert` (`_Assert+0x20`), which aliases the start of `_panic_with_data`. Nearest-symbol-only tools can misattribute it.
- `_Assert` at `0xfffffff00896b7e8` loads the format pointer and calls `_panic` at `0xfffffff00896b804`.
- The assertion strings `log_encode.c` and `log_fits_cmd(ctx, cmd_sz)` are at `0xfffffff007125918` and `0xfffffff0071259aa`. Read-only source context (`log_encode.c` SHA `fd56862b...e827`, line 120, `log_add_cmd`) confirms the expression; R17 local edits were not inspected or verified.
- PC-slide caveat: attribution treats the reported values as linked/unslid addresses. Both exact matches support that, but no runtime kernel slide value was recovered from the window; a nonzero slide would shift attribution.

## Request 6001009 timing

Calibration in the same boot: `timer_hz=19200000`, `cntvct_before=1859929059`, `mach_ticks=1859929060`, `mach_ns=96871305208`.

- start `begin_ticks=1154685118` -> 60.139850 s
- finish `finish_ticks=1853483322` -> 96.535590 s
- completed duration `698798204` ticks -> **36.395740 s**
- `result=0`, `recv_id=6001109`, `finish_phase=6`, `clock_error=0`
- start backtrace `n=16`, `trunc=1`; finish `bt_state=0`, `n=0`, `err=0`, `trunc=0`

This is a completed SpringBoard `sblong` stall in this run. Its causal relation to the panic is **not established**.

## Limits

- Panic text and kernel stack were not retained; the reported panic line remains user-provided.
- No automatic Boot -> Hello claim; the ~80 s Apple observation is user-reported only.
- R16 build identity is inferred from the fresh post-install boot, not from a unique in-window marker.
- No R17 fix, mutable production verification, or performance improvement is claimed.
- No shared CHANGELOG write in this dispatch.

## Artifacts

- return script: `artifacts-nova/boot-speed-round16-nova/return-to-android-b-nova.py` (SHA `41978ea9...604b0`)
- reader: `artifacts-nova/boot-speed-round16-nova/runtime-log-nova/read-autolog-r16-nova.py` (SHA `65c9f6ff...5a0d9`)
- orchestrator: `artifacts-nova/boot-speed-round16-nova/runtime-log-nova/run-r16-recovery-nova.py` (SHA `b5146a03...fc4679`)
- recovery outputs: `artifacts-nova/boot-speed-round16-nova/runtime-log-nova/return-*`, `capture-*`
- records: `logs-nova/boot-speed-round16-nova/return-to-android-b-nova.json`, `post-boot-device-nova.json`
- capture: `logs-nova/boot-speed-round16-nova/candidate-run-nova/`
- evidence: `artifacts-nova/boot-speed-round16-nova/runtime-report-nova/evidence-nova.json`
