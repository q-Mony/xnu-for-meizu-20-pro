# BBS evidence correction review

New files; no previous version. Documents-only correction from a bounded read-only cache replay.
No device, build, source, mount, install, global-config, CHANGELOG, plan or backboard-REPORT action.

## Reproduced bytes and arithmetic

- Cache main: `76874e8375252ee643011ff8063f0cce12355f05cf5f9897a34e5679667a65aa`, UUID `8f970452c9e33e88b114feb1864bc9e0`
- Image: `/System/Library/PrivateFrameworks/BackBoardServices.framework/BackBoardServices`, UUID `a9d6b8ce7863381fb1ab1055a11412d5`
- `Cache.read(0x18dd209a0,8)` = `0000000071915b00` (`/Volumes/SydneyUpdate20H392.D22D221SystemCryptex/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64.05` at `0x13c9a0`)
- u32 pair = `(0, 6001009)`; u64 = `0x005b917100000000`; **not** `0x5b9171`
- `0x18dcc7a90 adrp x8,0x18dd20000`; `0x18dcc7a94 ldr d0,[x8,#0x9a0]`; `0x18dcc7a98 str d0,[sp,#0x18]`
- low half `0` -> `sp+0x18` (base+0x10); high half `6001009` -> `sp+0x1c` (base+0x14); request base `sp+8` => **msgh_id offset 20**
- `0x18dcc7ab8 mov w1,#0x4003`; `0x18dcc7abc movk w1,#0x30,lsl #16` => **`0x00304003`**, not `0x30004003`
- `0x18dcc7ac0 mov w2,#0x18`; `0x18dcc7ac4 mov w3,#0x44`; `0x18dcc7ad0 bl 0x1948ed304`

All replay assertions are true (12 checks).

## Corrections and hashes

| file | before | after |
|---|---|---|
| `BBS-POSITIVE-BINDING-nova.md` | `6d561cd7...` | `57a2e0fd39c88cdf18cf9dc65860ee4d2002c7a4640d766a016c527efac791e2` |
| `BBS-POSITIVE-BINDING-nova.json` | `bae6659a...` | `49aeeb383ed793f44aaf8a3f082f9687dcbf140e2a4580596bc24290617ceb92` |
| `launchd-request-binding-nova/evidence-nova.json` | `0d2e0b30...` | `b29df2c21e3194afb62440b8d1e2dd0d567ce647df781e732aa3331bc54b7a5f` |
| `launchd-request-binding-nova/REPORT-nova.md` | `04967113...` | unchanged (`04967113c355276b4dbc020fba2ee2172c07010c62ff5affd2d79b18bd0406e2`); no same numeric mistake found |

Corrected facts: the slot is the full 64-bit value `0x005b917100000000` (bytes
`0000000071915b00`), not scalar `0x5b9171`; the call immediate is `0x00304003`, not
`0x30004003`. The JSON-to-evidence hash pointer and markdown self-hash were updated.

Historical hashes: BBS superseded report `8e64ddbd...`; backboard report current
`bb7eb2ce...` (independently reviewed), pre-review `af7a04db...` retained as history only.
The current backboard report path is linked from the BBS record; the obsolete hash is not
presented as current.

## Retained conclusion and limits

`_BKSDisplayGetMainScreenInfo` remains a positive **BackBoardServices client-template**
binding at `0x18dcc7a30`. It is not a launchd-native opcode. Runtime seq200 semantics and the
wait cause still require the sender stack; no speed fix or new candidate is proposed.

Replay: `bbs-evidence-review-nova/replay-bbs-nova.py` (`9da15f30f9591fd072a38087771106ed748f65718732a3b68426e40ae5ff67f0`),
`bbs-evidence-review-nova/replay-result-nova.json` (`3c3bb2a507af63018116dba1aa8839fc9bd3eef86b4b4aebc2395dc3b42c28f5`).
