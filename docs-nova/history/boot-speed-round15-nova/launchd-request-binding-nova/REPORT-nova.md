> **SUPERSEDED NOTE (R15 follow-up):** The client-side template binding is now positive:
> `6001009` is packed at Mach request header offset 20 by `_BKSDisplayGetMainScreenInfo`
> in BackBoardServices. See `BBS-POSITIVE-BINDING-nova.md` / `.json`. The earlier
> "no name remains" claim is superseded; the remaining launchd-local negative is only that
> no launchd-native opcode literal was found in the bounded search. This still does not prove
> runtime seq200 semantics or the cause of the wait.

# R15 follow-up: SpringBoard `send_id=6001009` launchd binding

Scope: static binding from the actual iOS 20H392 binaries on the pre-existing read-only
disk images. No mount/unmount, no launchd execution, no device access, no build, no
production edit. This does not treat a static trace as runtime execution.

## Direct answer

`6001009 == 0x5b9171` is verified. The actual launchd binary and the listed client images
do **not** contain this value as a launchd opcode, MIG dispatch ID, or send-stub immediate.
Therefore this analysis **cannot name the exact launchd request from the ID**. The evidence
does show that the same numeric ID is used toward more than one destination: R14 sees it
from SpringBoard to launchd (L1021/L1283/L1437) and from Setup to backboardd (L1742/L1743).
That makes a launchd-specific protocol name an unsupported inference, not a finding.

The static question is therefore answered negatively: **no actual-binary binding to a named
launchd routine was found**. The `~27 s` wait is a pending Mach receive, not a fixed
timeout identified by this ID.

## Target identities (actual files)

| target | bytes | SHA-256 | UUID |
|---|---:|---|---|
| `sbin/launchd` | 541600 | `12370e5e…8cac61c` | `5c15db07-b3bc-3df3-b62f-3e498b993b8e` |
| `SpringBoard.app/SpringBoard` | 112960 | `ea835980…125475` | `e1a4fe7d-29ff-3269-b3ab-50aed40c4495` |
| `usr/libexec/backboardd` | 1184848 | `7aa6f8bf…76267c` | `70b72340-737a-3973-8b47-4e9970f4ecb9` |
| `Setup.app/Setup` | 2107712 | `2841aff2…53a05` | `9b4d9b5a-97c8-38d0-a692-dc686fa7bb4b` |

Cache main: SHA-256 `76874e83…7a65aa`, UUID `8f970452-c9e3-3e88-b114-feb1864bc9e0`,
region `0x180000000`, maxSlide `0x55214000`.

Relevant cache images: `libxpc.dylib` UUID `dd50de06…c1d1d`; `SpringBoard.framework`
`22034aba…f7d2e`; `libsystem_kernel.dylib` `2e54c705…8f8d76`; `liblaunch.dylib`
`b0e15502…7d89a`; `BackBoardServices.framework` `a9d6b8ce…412d5`.

Launchd segment bindings include `__TEXT,__text` `20bc1231…5c68c`, `__TEXT,__const`
`5755a315…51169`, `__TEXT,__cstring` `53bc13f6…bf94c`, `__DATA_CONST,__const`
`2b702713…8229b`. All segment hashes and sizes are in `analysis-output-nova.json`.

**R14 shared-region anchor (not R7/R8):** raw L504
`meizu: check_np start_addr kr=0 a=1a7910000` gives runtime header `0x1a7910000`,
slide `0x27910000`, 16 KiB aligned and within maxSlide. This slide is stated for identity
only; **the static protocol map needs no runtime slide**.

## Static search and result

Method: exact 32/64-bit byte search plus capstone 5.0.7 `MOVZ`/`MOVK` immediate tracking in
`__text`; not a nearest-symbol or string guess. Searched the nine targets above (four
standalone Mach-Os and five cache images), not all 2710 images and not all 3 GiB.

- `launchd`: **no exact byte hit, no code immediate**.
- SpringBoard app, backboardd, Setup, libxpc, SpringBoard.framework, libsystem_kernel,
  liblaunch: no exact byte hit, no code immediate.
- Only cache hit: `BackBoardServices.framework` `__TEXT,__const`, VA `0x18dd209a4`
  (cache `.05` file offset `0x13c9a4`), inside a table of consecutive small values
  (`0x5b916d`, `0x5b916e`, `0x5b916f`, `0x5b9170`, `0x5b9171`, `0x5b9177`, …). This is a
  data table entry, **not** a launchd dispatch/opcode and not a code xref target found in
  the bounded check. It does not name a launchd request.

The launchd imports `_xpc_pipe_handle_mig` (libSystem/libxpc), but the local `libxpc`
inspection shows `__xpc_pipe_pack_message` defaults to `0x20000000` and has explicit
connection IDs `0x20000000`/`0x30000000`/`0x40000000`; it does not supply `0x5b9171`.
The bootstrap client path reaches `__xpc_bootstrap_routine` with routine arguments
`0xce`/`0xcf`; those are not `0x5b9171` either. This does not prove the message is XPC, but
it rules out the obvious launchd/libxpc constants in the searched images.

## Static vs runtime limits

| question | result | basis |
|---|---|---|
| Is `6001009` a launchd opcode? | **No proof; not found** | no exact/immediate hit in actual launchd |
| Is it a MIG/subsystem table entry? | **No proof; not found** | no literal/table match in launchd; no dispatch xref |
| Is it launchd-specific? | **No; same ID also to backboardd** | R14 L1021/1283/1437 vs L1742/1743 |
| Is the 27 s a fixed receive timeout? | **No fixed Mach timeout** | `opt=0x8380000400304003`, `timeout=0`, no `MACH_RCV_TIMEOUT` bit in the R14 record |
| Is a service-side timeout excluded? | **No** | static launchd service path is not bound; only the Mach layer is bounded |
| Is lsd the dependency? | **No evidence** | endpoint is launchd pid1/gen4; lsd is a separate exec |
| Is the exact sender call chain known? | **No** | no payload/backtrace for seq200; sender is SpringBoard, caller unknown |

Runtime limit: the 6.823/17.047/27.246 s values are WAIT-phase ages of the same pending
operation, not a completed request duration. The absence of a completion line is missing
evidence, not proof that the wait lasted only 27.246 s.

## REA status

REA MCP native tools are not present in this session, and no `rea` CLI is installed
(`command -v rea` empty). The optional `npx` package was not installed or used; no client
configuration, registration, or live runtime was changed. Offline work used the existing
capstone 5.0.7 and `otool`. This is a bounded static-analysis limitation, not a claim about
REA availability in other sessions.

## Recommended NEXT action (fewer cycles)

Do **not** implement a launchd behavior patch or guess a service name. The next capture
should be one build that records the missing cause, not another age snapshot:

1. **Distinct retained completion slot** for the selected SpringBoard→launchd wait
   (`send_id=6001009`, endpoint pid1/gen4): `seq`, `taskgen`, `begin_ticks`,
   `wait_start`, `finish`, final `result`, `send_id`, endpoint pid/gen. One slot per
   generation, with explicit `lost`/overwritten accounting. Do not use the currently lossy
   `sbipc` ring as the only completion store.
2. **One bounded sender backtrace** at the first send of this ID from SpringBoard (or one
   receiver identity if the send path is not safe). Cap the frame count and print once;
   preserve the final return and taskgen. This is the only way to name the caller without
   guessing from the numeric ID.
3. **No payload capture** until a verified request layout exists. The current evidence does
   not establish a safe dictionary/body layout; do not copy unknown bytes.
4. Bound the probe by before/after timestamps, exact `taskgen`, and one final
   `result`; no hot-path log broadening and no snapshot-derived completion claim.

The current age-only proposal is insufficient: it would turn 27.246 s into a total, but
would still leave the caller/dependency unknown. The combined probe above should identify
the SpringBoard call site and exact wait outcome in one device cycle.

## Artifacts

- `analyze-launchd-nova.py` and `analysis-output-nova.json` (new, no previous version).
- Prior `remaining-latency-analysis-nova` deliverables were revised with backups under
  `snapshot-nova/boot-speed-round15-nova/remaining-latency-analysis-nova/before-launchd-binding-revision/`.
- No production source, build, previous report, or raw log was modified.
