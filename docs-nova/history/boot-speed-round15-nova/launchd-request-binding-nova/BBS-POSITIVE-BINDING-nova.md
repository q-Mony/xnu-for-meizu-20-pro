# BBS positive binding (supersedes the bounded-negative wording)

Status: **positive static binding**. This does **not** make `6001009` a launchd-native
opcode and does not prove why the runtime wait occurred.

## Proven chain

| item | value |
|---|---|
| cache main | `dyld_shared_cache_arm64`, SHA-256 `76874e83…7a65aa`, UUID `8f970452…4bc9e0` |
| image | `BackBoardServices.framework`, UUID `a9d6b8ce…412d5` |
| constant table | `__TEXT,__const` VA `0x18dd207a0`..`0x18dd20c40` |
| 64-bit slot | VA `0x18dd209a0` = bytes `00 00 00 00 71 91 5b 00` = u64 `0x005b917100000000`; low u32 `0`, high u32 `6001009` (`0x5b9171`) |
| consumer | `__BKSDisplayGetMainScreenInfo` at `0x18dcc7a30` |
| load | `0x18dcc7a94  ldr d0, [x8, #0x9a0]` |
| pack | `0x18dcc7a98  str d0, [sp, #0x18]` |
| request header | starts at `sp+8`; `msgh_id` at header offset **20** = `sp+0x1c` |
| header fields | `sp+8: msgh_bits=0x1513`; `sp+0x10/0x14: remote/local ports`; `sp+0x18: voucher=0`; `sp+0x1c: msgh_id=6001009` |
| call | `0x18dcc7ad0` passes `x0=sp+8`, `w1=0x00304003`, `w2=0x18`, `w3=0x44` |

The 64-bit table value is `0x005b917100000000` (bytes `00 00 00 00 71 91 5b 00`), not the
scalar `0x5b9171`. It is stored at `sp+0x18`, so its low 32-bit half `0` lands at `sp+0x18`
(header voucher field, base+0x10) and its high 32-bit half `6001009` lands exactly at the
Mach request-header `msgh_id` offset 20 (`sp+0x1c`, base+0x14). This is the positive
template→function binding: **the `_BKSDisplayGetMainScreenInfo` request template uses
`6001009` as its Mach message ID**.

The call immediate is `w1=0x00304003` (`mov w1,#0x4003`; `movk w1,#0x30,lsl #16`), not
`0x30004003`. These two corrections replace the earlier shorthand; the static function
binding itself is unchanged.

## What is not proven

- It is not a launchd-native request/routine. The builder is in BackBoardServices; the
  destination/service handoff is separate.
- It does not prove that SpringBoard's seq200 observation is semantically this BKS display
  query, nor that the runtime wait was caused by this request. The runtime header/endpoint
  and sender stack remain the evidence needed for that.
- The earlier `launchd-request-binding-nova/REPORT-nova.md` and `evidence-nova.json`
  bounded-negative conclusion ("no name remains") is **SUPERSEDED** for the client-side
  template binding. It remains valid only for the narrow claim that no launchd-local opcode
  literal was found in the bounded launchd search.

## Links

- Current positive record: `BBS-POSITIVE-BINDING-nova.json`
- Prior independent backboardd analysis (read-only; owned/reviewed by another executor):
  `artifacts-nova/boot-speed-round15-nova/backboard-init-analysis-nova/REPORT-nova.md`.
  Current independently reviewed SHA-256 `bb7eb2ce…a99198db`. The pre-review hash
  `af7a04db…0a97c2a9` is retained as history only, not as the current report.
- Prior negative version backup:
  `snapshot-nova/boot-speed-round15-nova/launchd-request-binding-nova/before-positive-binding-nova/`
  (`REPORT` `8e64ddbd…`, `evidence` `993f65d8…`).
