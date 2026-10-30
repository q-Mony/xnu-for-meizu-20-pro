# R17 local OSLog encoding regression fix (Nova)

**Status:** `BUILT_AWAIT_NEW_USER_INSTALL_COMMAND`
**Flags:** `installation_allowed=false`, `install_authorized=false`, `do_not_flash=true`, `hardware_tested=false`, `device_accessed=false`
**Scope:** local logging-regression fix only. Original ~36 s display wait and ASTC fault remain unresolved.

## Chronology disclosure (no false test-first claim)

The prior capped dsh run had already applied the `life(2048)` and `sbfault(1024)`
production formatting edits before this corrected actual-path RED was run. The
initial full bug-red test was missing from that run. This completion pass does
not claim test-first; it re-ran the RED against the **original R16 back-up
emitters** and the **verbatim production encoder** with the real production
location path (`loc_size=4`, `sid=0`, `OS_LOG_DEFAULT`), then validated the
already-present production fix.

## Real RED (reproduced twice)

Command: `python3 run-host-suite-nova.py` in `<nova-root>/artifacts-nova/boot-speed-round17-nova/log-encode-fix-nova/host`.

- Case `life-normal`, real encoder, original R16 life emitter, `loc_size=4`, `sid=0`, `logmem=0`, `hide=0`.
- Exit **86**, empty stdout, stderr:

```
ASSERT_FAIL file=log_encode.c line=120 expr=log_fits_cmd(ctx, cmd_sz) case=life-normal loc_size=4 sid=0 hide=0 logmem=0
```

The same result was reproduced twice for `life-normal` and twice for
`life-max` (4/4 primary RED runs). The exact runtime budget was read from the
real encoder frame under lldb:

| field | value |
|---|---|
| `ctx_content_off` | 244 |
| `ctx_content_sz` | 250 |
| `ctx_buffer_sz` | 256 |
| `sizeof(hdr)` | 2 |
| `cmd_sz` (`sizeof(cmd)+arg_size`) | 6 |
| `arg_size` | 4 |
| `hdr_cmd_cnt` | 30 |
| `log_fits_cmd` RHS (`244+2+6`) | **252 > 250** |

The failing scalar is the 4-byte `reply_end_seen` flag (the 5th flags scalar,
printed as `reply_end`; value=1) after the prior `reply` flag brought
`ctx_content_off` to 244. This is the real double-count:
`log_add_arg` checks `log_fits_cmd(ctx, arg_len)` (244+2+4 = 250, passes), then
`log_add_cmd` checks `log_fits_cmd(ctx, cmd_sz)` (244+2+6 = 252, asserts). No
budget simulator was used.

Evidence: `<nova-root>/artifacts-nova/boot-speed-round17-nova/log-encode-fix-nova/host/r17-red-green/red-life/budget-evidence-nova.json` and
`<nova-root>/artifacts-nova/boot-speed-round17-nova/log-encode-fix-nova/host/r17-red-green/red-life/lldb.txt`.

## Selection review: unrelated change removed

The prior run had also converted `meizu: selection` to `snprintf`. Using the
original R16 selection block with the real default path:

- `sid=0`, `loc_size=4`: exit 0, **no assert**, `encode_fail=1` (the 41 scalar
  commands overflow the 250-byte command area and return ENOMEM), console text
  complete at 328 bytes. This is pre-existing and not the R16 panic.
- `sid=1`, `loc_size=8`: exit 86 with an assert. This is **supplemental only**:
  `sid=1` is not `OS_LOG_DEFAULT`, so it does **not** prove the hardware path
  and is **not** claimed as the root cause.

Decision: the speculative selection formatting change was removed and the exact
R16 `printf` statement restored (SHA-256 `838f65a963261f96ba7082cafbf603a7eb28332e48d796713c207f3734b82afe`,
byte-identical to R16). Only the life and sbfault base formatters remain changed.

## What changed

| file | change |
|---|---|
| `osfmk/ipc/mach_msg.c` | `nova_life_print` builds one 2048-byte line with `snprintf`, checks `n`/`fmt_trunc`, then emits one `printf("%s", line)`. `nova: life v=16` schema unchanged. |
| `osfmk/arm64/sleh.c` | `meizu_sb_fault_emit` builds the base record in a 1024-byte line with `snprintf`, checks `n`/`fmt_trunc`, then emits one `printf("%s", base)`. Existing `sbfault-regs` single-line formatter unchanged (byte-identical R16). |

Static checks (`<nova-root>/artifacts-nova/boot-speed-round17-nova/log-encode-fix-nova/host/static-integration-nova.json`):

- exactly the life function changed in `mach_msg.c`; exactly the sbfault emit function changed in `sleh.c`;
- selection statement byte-identical R16;
- regs formatter byte-identical R16 (`5c21529ecab01119f63c47a66a357cfc7d1fcc8f17d76edf7d271d90fb1e5957`);
- no allocation in either formatter;
- no shared `log_encode.c` / assert / global-buffer change (`fd56862b...e827`);
- schema `nova: life v=16` and `pin_owned:%u` preserved.

## GREEN matrix and worst case

- 56 post-fix cases: all 7 cases × `loc_size` 4/8 × `sid` 0 × `logmem` 0/1 × `hide` 0/1.
- All exit 0, no assert/panic, no ASan/UBSan report, no `fmt_error`/`fmt_trunc`.
- `encode_fail=0` for life and sbfault; `logmem=1` gives `truncated=0` (full expansion),
  `logmem=0` gives bounded truncation (`life=1`, `sbfault=2`) with complete console text.
- Selection remains the pre-existing ENOMEM case (`encode_fail=1`) with complete console; no assert.
- Full console comparison: **50 identical, 0 mismatched, 6 pre-RED skipped**.
  The skipped cases are the six pre-fix loc4/sid0 assertions, where the pre-fix process
  produced no console. The retained R16 console fixtures (`pre-console/*.log`) are
  byte-identical to the corrected post outputs for life-normal, life-max, sbfault-normal,
  sbfault-max, sbfault-x19, sbfault-bt, and selection.
- Worst case: life-max line **1079 < 2048**;
  sbfault-max base **444 < 1024**;
  sbfault-max regs **716 < 1024**.
- ASan/UBSan: address+undefined, `-fno-sanitize-recover=all`; no reports.
  `leaks` on the plain post binary: **0 leaks** for life-max and sbfault-max.

### life-max "ends early around flags" investigation

The retained `pre-console/life-max.log` is **not truncated**: 1080 bytes, one
line through `drain:4294967295`, byte-identical to the corrected post output
(SHA-256 `c0277db07a57e54238b0bbd7c8d9b1c5cb23fbf6bb3440c12121f26ebba47b97`). The apparent early end is the
**encoder command boundary**: at `content_off=244` the next 4-byte flags scalar is
`reply_end`; with `loc_size=8/sid=0` the encoder returns ENOMEM there, and with
`loc_size=4/sid=0` it asserts at `log_encode.c:120`. Console text is independent
of OSLog capacity and is complete. Truncation is not blessed.

## Freeze, TU, build

- 6186-file freeze verified against `source-freeze-r16-nova.json`
  (`f578ed91caf078f93cb27826aed0ae2a3f9bf4b56dfd55acbd06fefef256a60a`); exactly the two permitted files differ, no other
  file differs, no missing file. R16 freeze unchanged.
- TU replay: only the `-o` argument changed (`[4]`),
  `-g` preserved; both objects exactly match the full-build objects.
- Full local build: exit 0 with the reviewed PATH shim
  (`7bf8733a9ae09c886d180b17d49919138c1bcff191063b2b92549c8f7fc3d169`); raw kernel
  `658c301b39597f2879ab1a0ac0730a93f5d4677592d490deac581dfa25273946` (73949184 bytes).
- Protected objects unchanged: BootWatchdogPolicy, BootLog, NativeRamBlock,
  NativeUfs, kern_exec, corecrypto_sha2.

## Package

- Candidate image: `<nova-root>/artifacts-nova/boot-speed-round17-nova/log-encode-completion-nova/package/boot-warm-r17-logfix-candidate-nova/boot-warm-r17-logfix-candidate-nova.img`
- Image SHA-256: `8796d6c5824ce563fda8b4d0e1a429f235749254648cc160534e5807283338d6` (96 MiB; FAT 90 MiB)
- Payload kernel SHA-256: `6bbafb142a5e21d7627b708ea5901ceca0d5b6e619ed386972d8a67d2c433357`
- Raw before patch SHA-256: `658c301b39597f2879ab1a0ac0730a93f5d4677592d490deac581dfa25273946`
- Only `UniXNU/Payloads/kernel.macho` changed; all non-kernel EFI files,
  TOML, DTB/AVB/firmware header and post-FAT bytes byte-identical to R14/R15.
- Android v0 SHA1 recomputed; entry patch applied once (`entry + 0x78`); PAN passed;
  no new AVB signature.
- Markers present: `nova: life v=16`, `meizu: sbfault-regs`, `nova: sblong v=15`,
  `meizu: selection attempt=`.
- Independent validation: `<nova-root>/artifacts-nova/boot-speed-round17-nova/log-encode-completion-nova/package-validation-r17-nova.json`, all checks pass.

## R16 recovery and Sol acceptance (context)

R16's authorized recovery performed one Fastboot A -> Android B return, captured
the read-only 32 MiB ring (dropped 0), and found **no panic text or kernel
backtrace in the retained window**. The phone was left on **Android B**; no new
installation was performed. Sol accepted the recovery/capture with the panic-stack
limitation (`<nova-root>/artifacts-nova/boot-speed-round16-nova/SOL-RUNTIME-ACCEPTANCE-nova.json`, SHA-256
`0e6393c8cd3493180571da09093377214e87c557cd60f53dd01d625ac23af7f4`). The retained report is
`<nova-root>/artifacts-nova/boot-speed-round16-nova/runtime-report-nova/REPORT-nova.md` (SHA-256
`996a05e1fb77b8830cc9b22a40da974e54ec99c5c7be0f9f3ed7409f2c18e3c4`).

The user-reported `caller=0xfffffff00896b808` is the `_Assert` return address
after `bl _panic`, not a code PC; `panic=0xfffffff007050818` points at the
assert format string. No runtime slide or full hardware panic stack was
recovered, so the caller mapping remains cautious.

## Limits

- No device, adb, fastboot, mount, reboot, slot, installation, or install
  authorization in this task.
- Local red/green proves the OSLog encoding regression only; it does not prove
  a hardware boot result and is not a claim about ASTC or the ~36 s display wait.
- The ~80 s Boot-to-Apple observation is user-reported; 36.4 s is one completed
  request, not total Boot-to-Hello.
- `sid=1` selection assertion is supplemental only and not the hardware path.

## Bound hashes

| artifact | SHA-256 |
|---|---|
| `RESULT-nova.json` | `aa7915a9a16a30ba66d57b487cb46126619f4e72325d90d87c9fe2b9c3274d27` |
| source freeze | `1ebeb2bb4ece61019c7a4567ffe0455c1dd4174d23207d68957a08408f32b69b` |
| host suite result | `2e4aba8d43ec90fddf5b0a38d8ca2e5b759906b0b20e19ad45eedc5649748af2` |
| budget evidence | `1a4c5bbaadfadd040ce3a93cb279e06faf1a351b3d0e00f92c8e2f64f9776153` |
| static integration | `cbb7b6701ec784381605ac99fec43555a860e359f55e11ff364dd3cab28a4ce7` |
| TU result | `b6b0055b493f872d946da7b72150c8411d15eb9324729d60f7948bce369433f0` |
| build result | `31ee2eb5e6d1e12766affdbb73164fa7938ef010b6db1fa3ab832395bbf65e61` |
| package validation | `0dc60268c953a20e3f48ca704b8c78addefc95593ca4fa971ed99298135d4649` |
| package manifest | `275fa8644d118453cc23dc3ae1dfc22ecc0448b9ed47e3526995416bbe8fa7ab` |
| candidate image | `8796d6c5824ce563fda8b4d0e1a429f235749254648cc160534e5807283338d6` |
| payload kernel | `6bbafb142a5e21d7627b708ea5901ceca0d5b6e619ed386972d8a67d2c433357` |

All new files state no previous version. The pre-edit back-up is
`<nova-root>/snapshot-nova/boot-speed-round17-nova/log-encode-completion-nova` (SHA256SUMS `22e15fbfedda5424681a98da932927b15039709cacc7bbc067ff307509780421`).

---

`RESULT-nova.json` SHA-256: `aa7915a9a16a30ba66d57b487cb46126619f4e72325d90d87c9fe2b9c3274d27`
