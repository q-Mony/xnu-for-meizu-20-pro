# R10 watchdog repair build and payload audit — Nova

New report. Final static status: **PASS_WATCHDOG_STATIC_BINDING**. No device action was performed by this assignment.

## Candidate

| Artifact | SHA256 |
|---|---|
| Raw kernel | `f0ea25e88198c793777886581b29399a24abe4f866d2563cec9ff792471530d8` |
| Patched payload kernel | `ae3d54df5a2fd5df2f729bf3fb8d1067c2a1e0737925c99b51e098763f84b50a` |
| Final boot image | `995a2e784c5cea35280f71171629b16e6fb431a699ca9bec363224c60a120c2f` |

Raw: `build-20261029T213858113547Z-nova/kernel-raw-nova.macho`.

Payload: `../packaging-nova/watchdog-candidate-final-nova/kernel-payload-nova.macho`.

Boot: `../packaging-nova/watchdog-candidate-final-nova/boot-warm-watchdog-candidate-nova.img`.

## Build and preserved inputs

The isolated full build passed in 55.978 seconds; the raw kernel is 73,932,800 bytes. All 6,186 monitored input hashes remained identical before and after the build. Compared with R9, only the authorized policy source and two host-test files changed. The production policy hash is `8cab1ad1971a3d03b1c581102cb9fd61b7e381a73904d36aad6f77eb1a696bce`, matching the implementation freeze and independent review.

Formal `kern_exec.o`, `NativeRamBlock.o` and `NativeUfs.o` remain byte-identical to R9. The R10 policy object differs from R9 and exactly matches its formal archive member. The reconstructed compile recipe uses the frozen production source, ARM64 target and production guards, with no host-test macro. The actual final linker input still force-loads the policy archive. The executed builder and the inherited kern_exec compile recipe are recorded; the reconstructed command is not represented as a separate process trace.

Build evidence: `build-20261029T213858113547Z-nova/build-result-nova.json` and its source inventories/objects.

## Linked behavior

The unchanged PID 1 hook reaches the new `_meizu_boot_watchdog_policy`. Actual linked direct calls continue through the warm branch, file handling and exact-existing-file verifier; the verifier's read, openat and stat targets are resolved. Its direct calls contain no write/chown/chmod/sync/unlink/rename target. The six policy functions match their formal object bytes modulo individually recorded linker relocation words. The 356-byte expected warm policy, with its terminating NUL, also matches object, raw and payload.

All 33 previously audited warm-driver functions retain their object bindings. The original asynchronous workers, source/read path, media gate and diagnostic calls remain bound. This is static evidence; branch feasibility and file-validation behavior are covered separately by the implementation and independent host reviews.

## Package and checkpoint verification

Packaging starts from the exact R9 boot image (`b3359865b616882a78d166478b786acda51cbcf970d8e5a56330d85b85df0110`) and changes only `EFI/UniXNU/Payloads/kernel.macho`. The complete EFI file set was re-extracted and verified. Boot TOML including `nova-data-warm=1`, cold Data seed, trust cache and ADT are byte-identical to R9. Firmware/header/DTB/AVB bytes are unchanged apart from the expected Android v0 image checksum. The existing entry patch was applied once and PAN verification passed.

Forty-one selected functions were compared from raw to payload. All six new policy functions and all selected warm-driver functions match exactly with zero exclusions. `_load_init_program` has the sole four-byte inherited checkpoint entry difference:

- Its 94-byte checkpoint stub is byte-identical to the pinned R9 payload stub.
- The stub replays the displaced original instruction.
- The decoded return branch rejoins `_load_init_program + 4`.
- Its logging target remains `_native_blog_stage`.
- The actual policy call sites and the other 7,180 caller bytes are unchanged.

This is a specific verified inherited hook, not a general exclusion for changed instructions. The first final-audit rejection is retained at `link-audit-20261029T214119803357Z-nova/audit-nova.json`; it exposed the strict whole-caller comparison. The script version before the explicit hook check is preserved under `snapshot-nova/boot-speed-round10-nova/build-audit-script-nova/20261029T214330785587Z/`. No production source, kernel or boot image was modified to resolve that audit issue.

The final boot image's independently extracted payload matches the audited payload hash.

## Final evidence and scope limit

- Final linked audit: `link-audit-20261029T214331047021Z-nova/audit-nova.json`, SHA256 `b29375f9a34f3ab2d7550d3420398def8724fb4288143edb9cc36ba9b80d2d86`.
- Packaging verification: `../packaging-nova/watchdog-candidate-final-nova/evidence-nova/verification-nova.json`, SHA256 `49ab90b792720e8a0bd8ebdde3d916944dc161ebebd6ac117e28b3b8e3e8ac3f`.
- Independent review: `../integration-review-nova/integration-result-nova.json`, SHA256 `a5b04fc3d4b86b2cae521a349d2e7f0085033a3269e6543982fa52f3002d7164`, PASS with no blocking findings and 3,933 ASan/UBSan cases.
- Implementation validation: `../watchdog-policy-nova/validation-nova.json`, 737 ASan/UBSan cases and a target compile pass. Its original-function fixture returns EEXIST 17; the repaired function accepts the exact fixture without modifying existing files.

Static and host validation do not prove the repaired phone boot, reaching Hello, warm cache use or a startup speed improvement. The parent owns the device trial and its runtime result.
