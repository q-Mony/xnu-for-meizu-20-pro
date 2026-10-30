# R16 root-cause report (Nova, corrected)

**Scope.** Bounded, read-only analysis of the retained R15 log and the mounted iOS 20H392
dyld cache/backboardd. No production build, device, mount, or install. All artifacts here are
new; this report supersedes the rejected 16:50 version (snapshot kept under
`snapshot-nova/boot-speed-round16-nova/probe-implementation-nova/20261030T170000+0800/`).
Baseline: `autolog-nova.txt` SHA-256 `2a069c79…20ebb`, 6222 lines.

**This does not fix the crash or the startup delay.** The original runtime cause remains
unresolved. The work below fills diagnostic blind spots so a later run can distinguish
candidate causes instead of guessing.

## Verdict

1. **The crash seam is instruction-level.** The fault is `ldr q0,[x28]` at libate
   `__ZL9DecodeRowPvm+0x84` (unslid `0x1d3c3da4c`), inside a block loop. The address is
   **not** a single `base + stride*row`: the loop at `0x1d3c3dafc..0x1d3c3db0c` advances
   `x28 += 16` per block, so at fault
   `x28 = base + stride*row + 16*block_index` (modulo any pre-loop adjustment).
   `x0` is a local copy of the block-buffer argument, `x9` the stride, `x10` the base and
   `x1` the row **at the time they were read**, but those are caller-saved and may already
   have been clobbered by the preceding `BL` at call site `0x1d3c3da40` (static branch
   target `0x1d58ed450`; final callee symbol unverified and may be a stub island).
   Fault-time `x0/x1/x9/x10` must therefore not be published as original arguments.
   `x19` (caller local DecodeRow context) and `x28` (current cursor) are callee-saved, but
   the original base/stride/row should still be recovered from stack metadata.
2. **FAR is not necessarily x28.** The `ldr` reads 16 bytes, so it can straddle a page.
   The ESR reports the faulting address, which need only lie in `[x28, x28+16)`. A 16-byte
   aligned FAR therefore does **not** prove `x28` (or the base) is 16-byte aligned, and
   `FAR == x28` cannot be assumed. With no GPR captured, no base alignment can be inferred
   from FAR at all.
3. **The missing state is exact:** all `x0..x28`, the current `x28`, the `x19` context and a
   bounded window around the caller frame containing the saved callee registers/original
   block-buffer fields, plus the caller chain. `x19` is a caller local DecodeRow context, not
   the `at_block_buffer_t` itself; its fields were copied from the original argument.
   In particular `context+0x58` holds a copied caller region descriptor and is **not** the
   actual row index; fault-time caller-saved words must not be relabeled as the row.
4. **Producer identity is not proven either way.** `DecodeRow` has one direct reference in
   libate `__text`; `DecompressTexels` has no direct `bl` caller in this cache. That absence
   does **not** prove the producer is reached exclusively by virtual dispatch, nor that it is
   statically unrecoverable: indirect/framework calls and code outside this cache remain
   possible. The claim is scoped to "not resolvable from this cache's direct call graph".
5. **H4 stays OPEN.** The startup descriptor array is consumed inline by `main` and the
   display service is entry 6 of 10 with no direct wait between `main` and registration.
   That does not rule out indirect waits, framework waits, earlier-phase waits, or late
   registration. The 36.33 s display query is still unexplained by a retained timestamp.
6. **No kernel/VM cause is established or excluded.** The absence of mmap/mprotect/munmap
   lines in the retained TEXT window does not prove no VM events occurred (the log is
   filtered/truncated). The `fb_layout`/`negroni_surface_create_physical` helper check only
   does not support one framebuffer path; it does not exclude other mapping paths. ESR
   `0x92000007` says the L3 leaf was invalid for this access; it does not absolve any kernel
   mapping implementation.
7. **R8 independently corroborates the same instruction.** R8 autolog
   `dcf50ef3…81c9` L485 `check_np start_addr a=1a9a14000`, slide `0x29a14000`; L10604
   `pc=1fd651a4c` -> unslid `0x1d3c3da4c`, `far=1047cd000` (raw user-data address; the
   cache slide applies only to bound PC/LR here and must not be subtracted from FAR);
   L10607 crash/exit. Same unslid PC, different FAR from R15, so this is not an R15 artifact.
   R8 is preserved read-only.

## Crash evidence (corrected interpretation)

| item | value |
|---|---|
| fault | `pc=0x215a71a4c lr=0x215a71a44 far=0x127900000 esr=0x92000007` |
| slide / unslid PC | `0x41e34000` / `0x1d3c3da4c` |
| image | `/usr/lib/libate.dylib`, UUID `d185fd86…3f1ba`, `__text` SHA-256 `03b819b2…e62b` |
| function | `__ZL9DecodeRowPvm` @ `0x1d3c3d9c8`, offset `0x84` |
| instruction | bytes `8003c03d` = `ldr q0,[x28]` (16-byte read) |
| loop advance | `x28 += 16` at `0x1d3c3dafc..0x1d3c3db0c` |
| fault formula | `x28 = base + stride*row + 16*block_index`; FAR in `[x28,x28+16)` |
| ESR | EC `0x24` (lower-EL data abort), IL `1`, DFSC `0x7` (translation fault L3), WnR `0` (read) |
| caller | `ASTCEncoder::DecompressTexels` @ `0x1d3c3d358`, call at `0x1d3c3d54c` |
| block-buffer contract | `[arg+0x00]`=base, `[arg+0x08]`=stride; row/block index from the loop, not from fault-time `x1` |
| VM window | no mmap/mprotect/munmap line in retained TEXT before L5941; this is not proof of absence |

`LR` is only the return from the preceding `BL` at call site `0x1d3c3da40` (static branch
target `0x1d58ed450`; final callee symbol unverified), not the DecodeRow caller.
The immediate caller identity must come from the saved frame chain, not `LR`.

## Trace oracle (baseline FAIL, unchanged)

`python3 replay-oracle-nova.py` exits **1**: target `6001009` L898→L1612 =
`697618183/19200000 = 36.334280365 s`, `result=0`; pid25 fault L5941, `rv=11` L5942,
respawn pid233 L5974. `--require-probe` exits **3** because the new capture fields are
absent in the retained log. **TRACE ORACLE ONLY** — it parses the trace and does not execute
libate/backboardd or verify a hardware fix.

## Ranked falsifiable hypotheses (H4 corrected)

| # | hypothesis | prediction / test | result |
|---|---|---|---|
| H1 | caller gave an unmapped block-buffer base; stride×row+16·block small | captured original base/stride/row; `base` unmapped | **open** — needs new GPR+stack capture |
| H2 | base mapped but the loop overruns the mapping | cursor near mapping end; FAR in `[x28,x28+16)` | **open** — needs new capture |
| H3 | 4K/16K granule or length-accounting mismatch unmaps a valid tail | fault near mapping end with a declared mapping | **not supported** by bounded source check; not excluded |
| H4 | display registration order, indirect/framework/earlier-phase wait, or late registration delays the reply | a blocking/indirect call before the display path or a late producer timestamp | **OPEN** — inline order alone does not refute |
| H5 | 36 s gated on the main-digitizer group / launchd handoff | timestamp of `0x100018a90` or `0x10000a390` late; wait-site stack | **open** — no receiver event |

## Decision

No production behavior change is justified. The smallest useful next step is diagnostic:
capture all GPRs, the `x19` context and a bounded caller-frame window at the existing
`sbfault` hook, and timestamp the first R15 sblong request's receiver/reply stages. Those
two probes are specified in `FIX-PROPOSAL-nova.md`. Do not patch libate to ignore the
pointer, shorten waits, synthesize a digitizer, or suppress faults.

## Erratum (R16 report correction)

This note corrects two presentation errors only; the runtime evidence and limitations above
are unchanged, and no new binary analysis was performed.

- R8 `FAR=0x1047cd000` is retained as a raw user-data address. It is not a bound dyld-cache
  code address; the cache slide applies only to the bound PC/LR here and is not subtracted
  from FAR.
- `0x1d3c3da40` is the previous `BL` call site, not its target. Its static branch target is
  `0x1d58ed450`, but the final callee symbol is unverified and may be a stub island; earlier
  callee-identity wording is withdrawn.

## Provenance

Corrected report (previous rejected version replaced; backup in the snapshot path above).
Inputs: R15 log `2a069c79…20ebb`; R8 log `dcf50ef3…81c9`; cache UUID `8f970452…bc9e0`;
libate UUID `d185fd86…3f1ba`; backboardd `7aa6f8bf…6267c`. No device access, no build, no
shared CHANGELOG write in this analysis step.
