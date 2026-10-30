# STATIC-ADDENDUM: BackBoardServices `0x5b9171` table hit

Scope: read-only cache analysis. No runtime launchd execution, device, mount, or edit.

## Finding

The literal at `dyld_shared_cache_arm64.05` file offset `0x13c9a4`
(`BackBoardServices` VA `0x18dd207a0`..`0x18dd20c40`, `__TEXT,__const`) is not an
Objective-C method list or a launchd protocol dispatch table. It is a **64-bit constant
table**: the 4 bytes at VA `0x18dd209a4` are the high half of the 64-bit slot at
`0x18dd209a0`, whose value is `6001009` (`0x5b9171`). Neighbouring slots are also 64-bit
constants (`0x5b9168`, `0x5b9169`, `0x5b916c`, `0x5b916d`, `0x5b916e`, `0x5b916f`,
`0x5b9170`, `0x5b9171`, `0x5b9177`, ...), then later pointer/string material.

## Consumer and table semantics

- `0x18dcc7a94`: `ldr d0, [x8, #0x9a0]` in
  `__BKSDisplayGetMainScreenInfo` (`0x18dcc7a30`), loading the 64-bit slot `6001009`.
  The function stores `d0` into a local struct (`str d0, [sp, #0x18]`) and calls display
  APIs; it does **not** branch or indirect-call on the value.
- Xrefs into the table range use `ldr d0/q0` loads at many offsets; they consume values,
  not method names or function pointers. No xref to this cell was found in launchd.
- Therefore this is a **data-constant consumer in BackBoardServices**, not a method/protocol
  table and not a launchd request map. The R14 same-ID observation toward both launchd and
  backboardd remains compatible with a destination-agnostic/generic transport constant,
  but no launchd operation name is established.

## Limit

This addendum proves the table type and the BBS consumer; it does not prove that SpringBoard's
seq200 launchd message carries the same semantic operation. That still requires the sender
backtrace/completion probe. No behavior patch follows from this table.

Raw evidence: `static-addendum-nova.json`; parser/search script `analyze-launchd-nova.py`
and `analysis-output-nova.json`.
