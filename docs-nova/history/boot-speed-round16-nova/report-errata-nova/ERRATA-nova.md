# R16 report erratum (Nova, documentation-only)

Scope: two presentation corrections to `root-cause-nova/REPORT-nova.md`, plus the same
callsite clarity in `root-cause-nova/FIX-PROPOSAL-nova.md`. No research, code, test, build,
package, device, historical-result, acceptance-gate, or model/global-config change; runtime
evidence and limitations are unchanged.

1. R8 `far=0x1047cd000` is a raw user-data address, not a bound dyld-cache code address.
   The incorrect `-> unslid 0xdadb9000` arrow is removed. The cache slide applies only to
   bound PC/LR here and is not subtracted from FAR.
2. The instruction at unslid `0x1d3c3da40` is the previous `BL` call site, not its target.
   Static branch target is `0x1d58ed450`; the final callee symbol is unverified and may be a
   stub island. Earlier zero-helper wording is withdrawn; the caller-saved-register clobber
   warning and the LR-vs-DecodeRow-caller distinction remain.

Pre-edit backups: `snapshot-nova/boot-speed-round16-nova/report-errata-nova/pre-edit-20261030T172811+0800/`. Diff: `DIFF-nova.patch`. Hashes: `BEFORE-AFTER-SHA-nova.json`.
