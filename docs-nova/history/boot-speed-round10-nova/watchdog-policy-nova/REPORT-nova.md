# R10 strict warm watchdog-policy reuse

## Finding and repair

R9 reached owner Data preparation, then the existing watchdog hook returned errno 17. In the unchanged function, an existing directory is already accepted, but both disabled.plist and disabled.migrated are created with O_EXCL and rejected if present. The source comment deliberately refused reused Data. The actual R8 capture contains both files, so warm replay reaches a predictable policy-open EEXIST. The returned panic alone does not establish the exact recorded stage; the unchanged-function reproduction below establishes that path with the captured files.

Only `unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c` contains production changes. It adds a `nova-data-warm=1` branch after the unchanged Data-mount and volatile-store preflight. The non-warm path retains its previous exclusive-create behavior and errors. The warm branch retains a verified no-follow directory descriptor, uses openat for the two basenames and only handles EEXIST by strict read-only verification. All other errors and descriptor-close errors propagate. Existing objects are never overwritten, truncated, deleted, chmodded or chowned.

The exact R8 policy is 356 bytes and contains watchdogd=true, dhcp6d=true, bootpd=true and ftp-proxy-embedded=false. All four entries are preserved. Accepted policy bytes are limited to that exact independently extracted serialization and the original 222-byte cold serialization. There is no substring matching or permissive plist parser. The directory must have mode 0755 and the verified 501:20 or root 0:0 identity. A directory newly created by this invocation may additionally be root 0:20, accounting for inherited parent gid20; an arbitrary pre-existing 0:20 directory is not added to the whitelist.

Policy must be a regular single-link file owned by 0:0, mode 0644 and an exact allowed length. Marker must be a regular single-link empty file owned by uid0, gid0 or gid20, mode0644. These rules retain the actual R8 marker's gid20. Existing files are checked before open, against the opened descriptor, after exact content/EOF reads, and again through the retained directory path; device/inode, mode, uid/gid, link count and length must agree. Same-filesystem checks, no-follow flags and rejection of special objects precede reads. New missing files retain the cold policy semantics; only newly created inodes are normalized to root0:0 mode0644 and read back for verification.

## Regression evidence

- **RED:** The unchanged production source, actual extracted 356-byte file and R8 metadata produce `FAILED stage=open disabled.plist errno=17`. See `red-regression-nova.log`. No existing bytes are written.
- **GREEN:** The modified actual function accepts that same fixture and directory/policy/marker metadata, preserves all 356 bytes and marker gid20, and performs no existing-file write/chown/chmod/sync.
- **737 actual-function host cases** pass under ASan/UBSan. They retain the former cold opt-in/Data/store/write-error/short-write/stat checks; test every byte mutation of both accepted policies; cover directory and file ownership/mode/type/link/size/filesystem errors, short reads, unexpected EOF data, changed descriptor/path identity and every syscall/copy/close failure position on cold, existing-warm and fresh-warm paths. Every run checks all descriptors are closed and owner-mount environment scratch offsets are intact. Both root0:0 and inherited root0:20 newly created directory cases pass; unsupported new gid21 and existing0:20 fail.
- Real ARM64 iOS17 kernel compile recipe passes with **zero warnings**, `-Wall -Wextra -Werror`. This is a target object result, not full-link or device validation.

The two existing repository test files were updated with root authorization so old tests are not left uncompilable after adding read/openat/fstatat64. They contain only host testing changes. All three changed files were backed up under `snapshot-nova/boot-speed-round10-nova/watchdog-policy-nova/`. R9 loader source, R9 frozen outputs, XNU callers, signature chain and board identity were not changed. Parent owns full build, packaging, device test and central CHANGELOG.

Run from the Nova root:

```sh
xcrun clang -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror unixnu-meizu20pro-nova/package/tests/WatchdogHostTests.c -o artifacts-nova/boot-speed-round10-nova/watchdog-policy-nova/watchdog-host-tests-nova
artifacts-nova/boot-speed-round10-nova/watchdog-policy-nova/watchdog-host-tests-nova artifacts-nova/boot-speed-round10-nova/policy-fixture-nova/disabled.plist-nova.bin
```

Source hashes: `frozen-sources-nova.json`. Full machine-readable evidence: `validation-nova.json`. Target command/log/object, original-function red source, before-source manifest and implementation diff are retained beside this report. Independent integration review and runtime confirmation remain parent-owned; no speed improvement or successful Hello is claimed by this implementation result.
