# R9 pinned warm Data loader — implementation

Only the assigned two drivers and two new pure headers changed. Each pre-existing source was backed up before edits; intermediate UUID and lifecycle corrections have separate Nova snapshots. Central CHANGELOG is parent-owned. No full kernel build, image packaging or device operation was performed by this subtask.

## Implemented behavior

`nova-data-warm=1` starts a retained RamBlock worker after importing the original cold seed. The matching callback returns without waiting. Root UFS publishes one immutable read-only source descriptor only after manifest, signed window identities and publisher dispatch succeed. The worker reads the fixed export in at most 128 KiB chunks, validates the exact R8 capture header, packed layout, pinned header/payload/full logical SHA256 and cold origin, then replaces sparse storage before publishing Data. Every failed import discards only its private pages and preserves the cold seed; no half-applied disk is published.

The existing root publisher waits for exactly one Data UUID media with BSD identity and the current RamBlock registry ID before starting OS. The actual Data UUID is `691978E3-CC8E-4F14-9BF9-D55DAB2D1A4C`, confirmed against the active generated owner-data-volume.h and independent R8 filesystem evidence. SystemOS remains first. Worker/source/media ordering has no IOMedia dependency in the source decision. Root gate failure refuses OS rather than triggering init's immediate missing-Data lookup.

A publication lock separate from the I/O gate supports synchronous registerService block I/O and serializes stop; stop sets cancellation before acquiring it. Worker self retention is released at completion. Exact matching retains the current ancestor; registry IDs prevent using a different RamBlock. Warm boot disables capture/export writes and persistence attachment. This remains mode 0 volatile Data after import, with unchanged root signature verification and unchanged board identity. Active seed identity is the actual warm logical hash after success.

## Validation completed here

- Actual `WarmLoad.h` host harness: **386 synthetic cases** under ASan/UBSan, including all 3 read-failure positions, all 41 allocation-failure positions, all 152 budget and all 152 cancellation checkpoints, malformed header/layout/padding, cold/header/payload/logical hash rejection, overflow bounds, zero-extents input and complete sparse reconstruction. Every failed run releases all private allocations.
- Actual R8 256 MiB export file: **PASS** with pinned capture ID 4013558640 and all pinned hashes. 1765 reads including header, largest allocation **1 MiB** (page directory), peak live candidate allocations **232196480 bytes** (excludes the caller's 132 KiB stage/header and original cold seed). No 231 MB contiguous staging allocation.
- Real ARM64 compiler recipes: both drivers compile successfully. RamBlock has zero warnings; UFS has 12 conversion warnings on pre-existing unchanged statements. Objects and full compile logs are under this directory. Compilation is not a final link or device result.
- Independent actual wrapper/lifecycle/source/gate tests are owned by the integration-review agent and reported separately; this report does not substitute the importer harness for IOKit runtime proof.

Reproduce host checks from the Nova root:

```sh
xcrun clang++ -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1 -Werror -Wno-deprecated-declarations -I unixnu-main-nova/drivers/NativeRamBlock artifacts-nova/boot-speed-round9-nova/warm-load-implementation-nova/test-warm-load-nova.cpp -o artifacts-nova/boot-speed-round9-nova/warm-load-implementation-nova/test-warm-load-nova
artifacts-nova/boot-speed-round9-nova/warm-load-implementation-nova/test-warm-load-nova logs-nova/boot-speed-round8-nova/captured-data-window-nova/export-window-captured-nova.bin
```

## Runtime interpretation and limits

`native_warm_load_status=0` means complete byte restoration succeeded; durable `nova: warmload digests` contains actual payload and logical digests and verified=1. Other terminal statuses indicate cold fallback or cancellation; 100 means in progress. `warmsource state=1` and `root_gate=2` indicate source readiness and actual Data-media readiness respectively. The exported start/source/publish/end/first-read event ticks plus read/hash/import elapsed ticks distinguish loading overhead from later userland work. Mach ticks are wall-clock ticks, not CPU time; import time includes read and hash time, so do not add them together.

Budgets: source wait 10 seconds, read/import/hash 30 seconds, Data root gate 60 seconds. Checkpoints do not interrupt in-flight UFS lock/command, memory operations or synchronous IOKit work. These bounds may produce an observable cold fallback rather than a warm hit when the source is too late. Existing transport poison on timeout/fatal error is preserved: retaining cold bytes does **not** guarantee root UFS can still boot after transport failure. No reset, poison clearing, transport retry, generic persistence or Setup repair was added.

This is a pinned experimental replay of a block cut. It does not prove APFS/application consistency, warm cache reuse, faster Boot→Hello, or acceptable runtime overhead. The next parent-owned validation must bind these sources to the final linked kernel, package with only the authorized boot-argument change, and measure the next runtime with actual loader status/digests. APFS semantic failure after publication cannot hot-swap back to cold; disable warm on the next boot.

Machine-readable evidence: `validation-nova.json`, `target-results-nova.json`, `review-candidate-sources-nova.json`, `before-sources-nova.json`; implementation diff: `implementation-diff-nova.patch`; exact frozen contract: `CONTRACT-nova.md`.
