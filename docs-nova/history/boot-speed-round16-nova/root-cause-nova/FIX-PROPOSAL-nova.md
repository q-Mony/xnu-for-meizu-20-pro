# R16 diagnostic proposal (Nova, corrected)

**Decision:** implement two narrowly bounded diagnostics, **not a behavior fix**. The
original crash and startup delay remain unresolved. The probes fill blind spots that
currently make any fix a guess. No wait/deadline/capability/fault-suppression change and no
libate/framebuffer patch.

## Why these seams

`__ZL9DecodeRowPvm+0x84` executes `ldr q0,[x28]` inside a block loop. At fault,
`x28 = base + stride*row + 16*block_index` (the loop at `0x1d3c3dafc..db0c` advances
`x28 += 16`). The 16-byte load can straddle a page, so FAR lies in `[x28, x28+16)` and
`FAR == x28` is not guaranteed. `x0/x1/x9/x10` are caller-saved and the preceding `BL` at
call site `0x1d3c3da40` (static branch target `0x1d58ed450`; final callee symbol unverified,
may be a stub island) may clobber them; they cannot be read as original arguments. `x19` is the
caller's local DecodeRow context (not the `at_block_buffer_t`), and its fields were copied
from the original argument. `x28` is the current cursor. The retained log has none of these
values, no frame chain, and no receiver-side timestamp for the 36.33 s request.

### A. Extend the existing SpringBoard userfault probe (`osfmk/arm64/sleh.c`)

Keep the existing 8 immutable/deduplicated/try-atomic slots, async callout, no formatting
under the recorder, and no retained task/proc pointers. Add to each slot:

- `x[29]`: all `x0..x28`; `x29/x30` remain covered by the recorded `fp/lr`.
- `x19` and its validity; `x28` current cursor; the fault PC/FAR/length provenance.
- A bounded caller-stack snapshot: validate `fp` as 16-byte aligned, canonical, `fp >= sp`,
  `fp - sp <= 64 KiB`; snapshot `[fp-0x60, fp+0x100)` (352 bytes ≤ 512) with guarded
  `copyin`; record window start/length/copied/error/truncation.
- A bounded FP/LR chain: ≤16 frames, each 16-byte read, reject noncanonical/unaligned/
  backward/out-of-range FP and frame cycles; record count/error/truncation.

The recorder must copy the user stack only in a safe thread context (interrupts enabled,
not interrupt context, preemption enabled). Otherwise it stores saved-state-only with an
explicit `ctx=unsafe` reason. That fallback still preserves GPRs/FAR but cannot recover
stack-resident original base/stride/row or the caller chain; document this limitation in
the emitted line.

The emit path prints **all** captured fields with valid/error metadata, never just a subset:
all 29 registers, PC/LR/SP/FP/FAR/ESR, the `x19` pointer, the selected frame words
(saved callee registers at `fp-0x50..fp-0x28`, frame record at `fp/fp+8`, and caller
context words at `fp+0x10..fp+0x90`), and the FP/LR chain. It never bulk-dumps a data
buffer or arbitrary text. A full 352-byte hex dump, if ever needed, is restricted to the
private Nova autolog and is not part of the default line.

Offline parsing is conditional: only when the faulting PC/FAR shape matches the `ldr q0`
site and `x19 == fp+0x10` in the observed frame may the window be read as the DecodeRow
context. Every other fault is reported as provenance-only.

### B. First-request lifecycle (`osfmk/ipc/mach_msg.c`)

Reuse the existing `meizu_ipc_lock` and the R15 first-selected sblong row (cap 1). Add one
fixed lifecycle record for the first `request_id == 6001009`:

- request send: sender seq/taskgen/tid, raw ticks, request id, pinned reply-port identity;
- receiver kernel-delivery begin: live `kmsg` in `mach_msg_receive_results` before copyout,
  matched by the pinned reply-port pointer, never by numeric id alone;
- receiver copyout end/error: result from the receive path;
- server reply-send begin/end/error: generic send of `6001109` matched by the same pinned
  port pointer; actual server pid/taskgen/tid and raw ticks;
- sender finish: sender seq/taskgen/tid and raw ticks.

Pin exactly one `ipc_port` reference while the original kmsg owns a valid right, before
`ipc_kmsg_send`; never retain or dereference a kmsg after send/free; never take a port lock
under the probe lock; release outside the lock, exactly once, on admission rejection, on
sender finish, or on a 300 s retirement if no reply. Print only at retirement, from scalar
state copied under the existing periodic drain; generic hooks do a cheap ID/null check and
`try_lock`, dropping rather than blocking. Logs contain no pointer values, only a salted
port cookie and identities. Counters record overflow/drop/invalid identity/missing events.

If exact correlation is impossible (reply right not valid, port identity not pinnable), the
record says `identity=missing` and does not guess. Kernel delivery/copyout is **not** proof
that the userspace handler started. The R15 sender observation is unchanged.

## Acceptance (host only; no device/build claim)

- RED before the change: retained log lacks the new `x0..x28`/stack/frame fields and the
  receiver/reply lifecycle line; a fixture without them cannot reconstruct
  `base+stride*row+16*block_index`.
- GREEN after the change: extracted production helper harnesses (not handwritten mirrors)
  cover all registers printed, caller-saved clobber, cross-page FAR (`FAR != x28`), frame
  cycle/overflow/EFAULT/unsafe context, slot quota/dedup, request/reply mismatch, reused
  port after release, rejected admission, send failure/timeout/missing receive, no duplicate
  release, late/stale events and ordering; ASan/UBSan clean.
- Parser schema accepts the new lines and rejects incomplete/contradictory ones.
- No real ASTC host reproducer exists here and no speed-fix claim is made.

## Explicitly not proposed

- No libate pointer-ignore/error-suppression patch.
- No shortened/forced display reply, dummy digitizer capability, or fault suppression.
- No `NegroniFramebuffer.cpp` change on current evidence.
- No production behavior change until Sol reviews the diff and the host evidence.

## Provenance

Corrected proposal (rejected previous version replaced; backup in
`snapshot-nova/boot-speed-round16-nova/probe-implementation-nova/20261030T170000+0800/`).
Inputs: R15 log `2a069c79…20ebb`; R8 log `dcf50ef3…81c9`; cache UUID `8f970452…bc9e0`;
libate UUID `d185fd86…3f1ba`; backboardd `7aa6f8bf…6267c`. No device, build, mount, or
shared CHANGELOG write in this proposal step.
