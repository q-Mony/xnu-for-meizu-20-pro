# NEXT-CANDIDATE-nova — bounded completion accounting for the 27 s launchd reply

Status: **proposal only**. No source edit, build, install, or device access was performed.
This is for Sol acceptance before any implementation.

## Decision

No boot-time **behavior** optimization is proven safe by the R14 trace. The 19.326 s
pre-SB cost is real kernel read+hash work and already uses ARM64 SHA2
(`libkern/crypto/corecrypto_sha2.c:192-193,208`); larger reads and naive hash changes are
not supported by the evidence, and bypassing or persisting the 4K validation table would
weaken the code-signing contract. The highest-value bounded next step is therefore
**instrumentation of the largest unexplained interval**: the single SpringBoard->launchd
receive wait that is already observed at 27.246 s (59.00% of the remaining 46.177 s
SB->Setup). It makes that interval exactly measurable without changing boot behavior.

## Candidate (one)

**Record the total operation age and one-shot completion of the exact pending
SpringBoard->launchd send, in the existing `sbipc` accounting.**

- Production file: `osfmk/ipc/mach_msg.c` only.
  - `meizu_ipc_print` (`:1654`): add `op_age_ms = (now - r.begin_ticks)` next to the
    existing `age_ms = (now - r.when)`. `begin_ticks` is already stored at `:626`; this is
    a read-only scalar add to an existing print.
  - `meizu_ipc_finish` (`:1620-1636`): retain `begin_ticks`, final `result`, and endpoint
    scalars in a **distinct fixed retained slot/drain** for one selected generation. The
    existing `sbipc` ring is lossy (`dropped=119317`, L5283); enqueuing a completion there
    would not by itself guarantee observation. Either use a dedicated non-lossy slot with
    explicit overwrite/loss accounting, or print the completion from the existing bounded
    drain only after proving that slot cannot be overwritten before drain. Do not claim
    exact completion from snapshots.
  - Reuse the `op_age` computation already present in the worker snapshot (`:1734-1739`);
    do not add a second clock source.
- Selection must be by the existing generation/endpoint/window fields, not by a hardcoded
  business name. The numeric request ID `6001009` may be logged but must not be decoded by
  guess; no local definition exists.
- Bound: at most one extra line per existing snapshot, one completion line per selected
  operation, fixed slots, no allocation, no new lock, no user-memory access.

## Why the existing probes fail

1. `sbipc latest` prints `age_ms = now - r.when`, and `r.when` is reset at every phase
   transition (`mach_msg.c:593,1653`). The observed 6.823/17.047/27.246 s is **WAIT-phase
   age**, not send-to-completion. The exact total is unavailable.
2. The probe is periodic snapshots of the "latest" record; it has no completion event for
   `seq=200`. The receiver-map lines are endpoint snapshots, not completion.
3. `result` is stored but not emitted for this record, and ring losses are large
   (`sbipc loss dropped=119317`, L5283), so a lost completion cannot be distinguished from
   a still-pending reply.
4. `rname=10103` is a port name, not the launchd service name; the request ID has no local
   definition. The current trace cannot tell which service/operation owns the 27 s.

## Predicted measurable benefit

- **No boot-time reduction is claimed** (expected delta ~0; one bounded line per snapshot
  and one per completion).
- On the next capture it gives the exact total pending duration and final `result`, turning
  the current lower bound (27.246 s) into an exact interval. If completion is near 27.3 s,
  the next optimization target is the remaining ~18.9 s after that reply; if it is much
  longer, the launchd reply remains the target and the 19.3 s hash work is not the place to
  spend risk.
- It also distinguishes `MACH_RCV_TIMED_OUT`/interruption from a normal reply, which the
  current aggregate `result` cannot do.

## Correctness constraints

- Do not change any Mach return code, deadline, timeout, queue order, scheduling decision,
  or wakeup.
- Do not add a lock or a blocking allocation on the send/receive path; print only from the
  existing bounded drain.
- Do not dereference a port or user pointer in the new path without the existing reference
  and validity rules.
- Cap all new output; no broad log spam, no per-message print.
- Keep the existing `begin_ticks`, phase, endpoint, generation and window semantics intact.
- No hardcoded request-ID semantics, no forced success, no security bypass, no validation
  table reuse without cryptographic justification.

## Fail-before / pass-after seam

This is an offline analysis seam, not a fabricated runtime repro. The parser test is run on
a frozen capture; it does not execute boot.

| stage | assertion | current R14 |
|---|---|---|
| fail-before | for the pending record, a line must contain `op_age_ms`, `complete=1`, `result=...`; `op_age_ms >= phase_age_ms`; `op_age_ms >= 27246` | **fails**: no `op_age_ms`, no completion line |
| pass-after | same assertion on the next capture; exactly one completion per selected seq; no return/deadline field changed in source diff | must pass before the capture is accepted |

The existing replay already proves the reproducible lower bound:
`seq200.last_phase_age_ns = 27,246,000,000`, `59.00%` of `SB->Setup`. The new seam only
adds the total-age/completion fields; it must not weaken the existing 52 baseline asserts.

## Build path (not executed)

Target tree: `unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work`, existing XNU build
target for the meizu20pro board. Local build is already user-authorized; **only install and
device operations** require a new explicit user command. This proposal still authorizes no
implementation, build, install, or device action; Sol accepts the design first.

## Alternatives considered and rejected for now

- **Larger `read_size` in `cs_native4k_prepare`**: the trace does not prove the 256 KiB
  read is setup-bound; "larger reads alone are not assumed faster."
- **Naive hash acceleration**: already active on ARM64 SHA2; no profile shows the remaining
  8.403 s is removable by a local, correctness-preserving change.
- **Skip/defer the 4K digest table or persist it**: would move vnode I/O under VM locks or
  rely on an unauthenticated table; conflicts with the existing protection handoff and
  audit constraints.
- **Change the launchd reply/timeout**: request semantics are undecoded; changing a timeout
  or forcing success would mask the wait, not fix it.
