# R8 NVDCAP1 capture protocol

2026-10-30, Nova. New document, no previous version. This is an experimental capture format, with no restore implementation.

All integers are unsigned little endian. The export window is exactly 65,536 × 4,096 bytes (256 MiB). The header occupies block 0; the encoded payload begins at block 1. The payload is the existing `DBRDISK1` format, not a new compression format. Its SHA-256 covers exactly `encoded_bytes`, excluding trailing block padding.

| Header byte offset | Width | Meaning |
|---|---|---|
| 0 | 8 | `NVDCAP1\0` |
| 8 | 4 | version = 1 |
| 12 | 4 | header_bytes = 4096 |
| 16 | 4 | state = 1, committed |
| 20 | 4 | flags = 1, BLOCK_CUT_NOT_CLEAN |
| 24 | 8 | logical_bytes, nonzero and 4 KiB aligned, at most 512 MiB |
| 32 | 8 | encoded_bytes, at least 32 and at most 268,431,360 |
| 40 | 8 | extent_count, at most 65,536 |
| 48 | 8 | payload_offset = 4096 |
| 56 | 8 | capture_id, nonzero |
| 64 | 32 | original cold seed's full logical SHA-256 |
| 96 | 32 | complete encoded payload SHA-256 |
| 128 | 32 | header SHA-256, calculated over all 4096 bytes with this field zeroed |
| 160 | 3936 | all zero |

Payload starts with `DBRDISK1`, u32 version 1, u32 page size 4096, u64 logical length, u64 extent count (32 bytes total). Each 16-byte extent is `(logical byte offset, byte length)`. Extents are in ascending order, nonoverlapping, nonempty, page aligned, and describe consecutive nonzero pages. The table is followed by all corresponding page data in table order. Null or fully zero source pages are omitted. Final padding through the next 4096-byte boundary is zero. Bytes beyond this padded record are not part of the record and are not cleared.

Producer order is: plan while Data I/O is frozen → write zero header → sync → stream complete payload → sync → write committed header → sync. Only the final successful sync returns status 0. All temporary buffers are allocated before taking the Data gate; the encoder allocates no memory and uses at most a 128 KiB stage plus a 4 KiB header. Every append is bounded by the planned encoded length; the existing UFS export wrapper independently enforces the export window.

On a payload, commit-header or later sync failure, the producer makes one best-effort zero-header write and sync. Initial invalidation failure stops before any payload write. A medium that persistently refuses I/O can retain an old header, or a new header after an unsuccessful final sync. Such results set `header_uncertain=1`; no protocol can promise physical invalidation when subsequent writes fail. Refusals before invalidation also leave old media untouched. A consumer must match the exact capture ID with this run's successful completion record and independently verify every header field, header hash, complete payload hash, extent bounds and padding. A valid-looking header alone is insufficient; a success log alone is also insufficient.

| Status | Value | Meaning |
|---|---|---|
| Success | 0 | payload and commit sync completed |
| Disabled | 1 | experiment not explicitly enabled, wrong partition, or zero capture ID at C entry |
| NotReady | 2 | no selected unique target or required mounted/online state missing |
| Unsupported | 3 | unsupported Data geometry/mode/source, invalid low-level ID/seed |
| WindowRefused | 4 | invalid/unavailable export window or GPT check failed |
| NoBuffer | 5 | temporary allocation failed, or invalid caller buffers |
| TooLarge | 6 | encoded payload does not fit |
| InvalidateWrite | 7 | first zero-header write failed |
| InvalidateSync | 8 | first zero-header sync failed |
| PayloadWrite | 9 | payload write failed |
| PayloadSync | 10 | payload sync failed |
| HeaderWrite | 11 | commit header write failed |
| HeaderSync | 12 | final commit sync failed |
| AlreadyAttempted | 13 | a prior enabled attempt already consumed the one-shot |
| SourceChanged | 14 | source shape/size differed from the frozen plan |

`SourceChanged` checks shape and byte count; it is not a substitute for the caller's freeze and is not a general concurrent-content-change detector. Globals start with status 100 (not called). Disabled calls do not consume the one-shot; repeated calls return 13 without overwriting the first attempt's diagnostic state. Times in the driver's summary are Mach absolute ticks, not nanoseconds or CPU time.

The Data gate yields a complete block-device cut, not a clean APFS/application snapshot. It includes completed writes and can include pages partially modified by an earlier failed request. Dirty filesystem/application buffers not submitted to the block device are outside the cut. `flags=1` must never be relabelled as clean solely because hashes, read-only fsck, or mounting succeed. This round does not restore or alter persistence mode.
