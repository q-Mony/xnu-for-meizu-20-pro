# Native SHA-256 adapter validation — Nova

The candidate uses a local copy of the existing SHA-256 descriptor. Only its
compression callback changes. Each batch contains at most 256 blocks (16 KiB),
reads the current CPU SHA2 feature while preemption is disabled, and uses the
hardware entry point only for feature encodings 1 or 2. Scalar fallback runs
after restoring preemption. Zero-block calls do not probe features or touch
hardware. The original global provider and assembly entry point are unchanged.

The separate assembly wrapper includes the existing SHA-256 implementation
under a new symbol and locally forces BUILDKERNEL=1. The resulting routine saves
and restores q0–q7 and q16–q24 (17 vectors, 272 bytes).

## Passing evidence

`run3-nova/results-nova.json` contains source hashes and results:

- 5,480 full digest cases with actual corecrypto scalar source, descriptors,
  init/update/final and real ARM64 SHA instructions. Known vectors include empty,
  abc, the standard 56-byte example and one million `a` bytes. Tests also cover
  random lengths, offsets 0–31, block/padding/batch boundaries, incremental
  updates, repeated finalization and the public API fallback.
- 144 feature/chunk cases: every feature field 0–15, and block counts
  0/1/255/256/257/511/512/513/1025. A changing-feature case verifies that the
  adapter probes again on each batch. Instrumented preemption checks pairing,
  nested state restoration, hardware execution inside the disabled interval,
  and scalar execution outside it.
- All q0–q31 preserved by the actual hardware routine for block counts
  0/1/2/256; the zero case uses null state/data to verify early return.
- ASan and UBSan pass. The host harness compile log is empty.
- The adapter C and new assembly were separately compiled with the real kernel
  compile database arguments, redirecting only their output files. Host and
  production assembly disassembly is identical. The C compile has only the
  pre-existing missing `EXPORT_HDRS/libsa` include-directory warning.
- `production-fallback-variants-nova.json` records successful production compiles
  with no SHA2 ISA macro, no source provider, and no CRYPTO_SHA2. Each still
  defines `native_sha256_digest` without an undefined hardware entry point.

Run from the Nova workspace with a fresh output directory:

```sh
python3 artifacts-nova/boot-optimization-nova/sha256-nova/test-sha256-nova.py \
  --output artifacts-nova/boot-optimization-nova/sha256-nova/<fresh-run>-nova
```

This script reads the production adapter directly. Only the privileged register
read and preemption functions are replaced by host instrumentation; it does not
substitute a rewritten digest algorithm. The production scalar sources are
compiled directly from the pinned corecrypto feed. Generated header links point
only into that Nova feed. All commands are retained in each run directory.

## Limits

The Mac runs the real SHA assembly but cannot execute this kernel's privileged
feature-read/preemption path. Those behaviors are checked by instrumentation
and production-object compilation, not by execution at EL1. Real boot,
scheduling, I/O and handset speed remain device-validation tasks.

An unsanitized host benchmark over 4,096 × 16 KiB measured scalar 0.327255 s and
hardware assembly 0.030114 s (10.87×). This measures host compressor work only;
it is neither a phone performance result nor a boot-time prediction.

`run1-nova` and `run2-nova` are retained failed harness iterations: the first had
an otool-format assertion mismatch; the second exhausted the harness trace
array during the million-byte incremental test. Both are harness issues fixed
before the passing third run; neither required changing production behavior.
