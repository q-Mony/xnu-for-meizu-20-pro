# R9 warm load implementation contract — Nova

New report; no previous file. Production changes are limited to NativeRamBlock.cpp, NativeUfs.cpp, new WarmLoad.h and WarmLoadContract.h. Each old production file is frozen in snapshot-nova/boot-speed-round9-nova/warm-load-implementation-nova/. Parent owns central CHANGELOG updates.

## Experiment and exact input

Default off. `nova-data-warm=1` is the only opt-in. Capture, persistent Data, reset arguments are refused for warm import; capture/export writes are disabled whenever warm is requested. Persistence remains mode 0 for this experiment. Root source additionally requires `native-ufs-root=1`, no probe, exact `unixnu` partition. Trust/owner signature validation and UUID are unchanged.

Only the independently validated R8 export is accepted: capture ID 4013558640, 536870912 logical bytes, 231147936 encoded bytes, 152 extents. Cold logical SHA256 373880d1f64695c6ba71db05354b019cbfe11d253d32427b8d493bcc085981b3; payload SHA256 a8ba0692f680ac463463e7640575c1fa75bde86be6c620ba0af00fabcd8547d9; header SHA256 685238b9377d64f172c1f89ae3c98ecba09d41a928faa8394c8fec91fffaa90d; warm logical SHA256 372549a9fcb74ba062fbd871a370199c7d32657cfa4e436040c120b6dbfcbd43. Fixed Data UUID 691978E3-CC8E-4F14-9BF9-D55DAB2D1A4C, inherited from active owner Data build. This remains a block cut, not a claim of clean/application-consistent snapshot.

## Ordering and lifetime

RamBlock start imports and hashes the original cold seed first. For warm opt-in it starts an independently retained kernel worker and returns; no raw-source or IOMedia wait occurs in matching/start. The source transitions pending→ready/rejected, release/acquire, only after root manifest, signed owner windows and root publisher dispatch have succeeded. Source does not depend on IOMedia, root mount, log open or writable store.

Worker waits for source, reads into its private candidate, validates all bytes, then swaps once before online/ready/registerService. All unsuccessful imports free candidate pages and keep the original cold object/hash. The worker retains its service until completion. Publication has a dedicated recursive lock, independent of block I/O gate; stop first cancels then serializes with publication, preventing late publication. The one experimental target is identified by unique registry entry ID; UUID search retains matching media/ancestor via registry iterators and requires the exact current RamBlock ID, online state and one unique UUID match. No unretained global object dereference.

NativeUfs existing publisher gates only OS publication until Data UUID media and BSD identity exist under that target. SystemOS remains ordered first. Missing/ambiguous/cancelled media or gate timeout stops OS publication; it does not proceed to predictable init ENOENT. No hot switching after publication.

## Budgets and bounds

Source wait: 10000 ms, poll 10 ms. Streaming read/import/hash budget: 30000 ms from load start. OS Data-media gate: 60000 ms, poll 20 ms. These are checked software budgets, not hard real-time limits: an in-flight host command, IOLock acquisition, allocation/free/hash chunk, synchronous registerService or registry traversal can exceed the next check. Existing hardware READ10 command timeout and host poison are unchanged. No retries/reset/poison clearing.

Single fixed export: LUN0, last 8192 log blocks, preceding 65536 export blocks, all blocks 4096 bytes. Source open verifies no unsigned wrap, READ10 range, partition bounds, reserve≥73728 and image+1024 block guard. Handle is immutable and read validates it and relative bounds every call. No log/store open, writes or sync in load.

Stage≤128 KiB; at most 32 blocks/read. Extent count≤65536, logical pages≤131072 (512 MiB), pointer table≤1 MiB; exact R8 extent count reduces metadata to 2432 bytes. All exported header and packed metadata bounds and reserved/padding bytes are validated. Header hash (self field zeroed), payload hash, full logical hash and cold origin must match pinned values. No 231 MB contiguous buffer is allocated. Peak includes cold seed plus candidate nonzero pages, pointer table and stage; all error paths release private allocations. Active seed hash becomes warm hash on success and remains cold hash on fallback; no content is mislabeled.

## Diagnostics and validation boundary

C API: native_ufs_warm_source_try_open, native_ufs_warm_source_read, native_ram_block_warm_media_ready, native_ram_block_warm_log_stats. Export diagnostic status and phase/cost ticks (start/source/read/hash/end/first-read), byte counts, source state and root gate state. Existing UFS stats hook prints a later durable summary; early IOLog alone is not assumed persistent. Timings use Mach absolute time and elapsed sums, not CPU time.

Host tests will exercise the actual pure importer with malformed input, bounds, cancellation/deadline, reads, allocation, digest failures and real R8 fixture. Independent reviewer covers actual driver-wrapper lifecycle and gate path. Target-only compilation uses Nova build recipes and writes objects under this artifact directory; full build/link/package/device actions remain parent-owned. Success here proves byte restoration and compiled ordering only; APFS recovery, actual warm cache use, Boot→Hello improvement and possible UFS poison on device remain unproven until the next authorized runtime trial.
