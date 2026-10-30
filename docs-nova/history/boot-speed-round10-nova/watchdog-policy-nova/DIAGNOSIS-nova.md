# R10 watchdog policy failure — read-only diagnosis

## Proven code path

`kern_exec.c:9750–9753` calls `meizu_boot_watchdog_policy` only after successful Data prepare and panics on any nonzero return. The panic reported by the user is error 17. The exact tree's `bsd/sys/errno.h:109` defines 17 as EEXIST.

The active source is `unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c` (the generated tree kit path resolves to this same file):

- Lines 55–57: mkdir already accepts EEXIST, so an existing directory alone does not produce this panic.
- Lines 61–65: policy creation deliberately uses O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW and returns any open error. The comment explicitly refuses a reused seed carrying an existing launchd policy.
- Lines 97–104: marker creation independently uses the same exclusive-create rule. An existing marker is another EEXIST failure after a newly created policy succeeded.
- The caller passed data_error=0; the preflight's data_error passthrough is therefore not the observed EEXIST source.

R8 autolog line 231 records successful initial policy creation. R8 saved complete file inventory independently records `db/com.apple.xpc.launchd/disabled.plist` at 356 bytes and `disabled.migrated` at 0 bytes. Warm replay carries those files into R9. The original compiled policy string is only 222 bytes, so comparing only the original bytes would still reject the actual captured policy. The runtime failure stage must be confirmed from recovered R9 logs if preserved; the panic alone cannot distinguish policy from marker open.

## Deterministic actual-function reproduction

The unchanged current production source was copied to `repro-source-nova/BootWatchdogPolicy.c`; only host syscall shims were adapted to model EEXIST. The source SHA is recorded in `source-provenance-nova.json`. ASan/UBSan run output in `current-function-repro-nova.log` proves:

1. Fresh directory/policy/marker → success.
2. Existing directory only → success.
3. Existing policy and marker → exact `FAILED stage=open disabled.plist errno=17`; no content inspection or file overwrite occurs.
4. Existing marker only → exact `FAILED stage=create disabled.migrated errno=17` after writing the new policy.

This is a red-capable compatibility reproduction at the actual policy function. It does not claim a second APFS-device reproduction or a recovered panic log.

## Minimal repair boundary, pending actual file bytes and metadata

Keep the policy enabled, the Data-mount success and mode-0 checks, and exclusive creation for absent files. Handle only an actual EEXIST with strict read-only reuse validation; preserve every other syscall error. Validate the directory as a real owned directory, reject symlinks and wrong types, and validate existing policy/marker on retained descriptors. Existing files must be root-owned regular files, with a single link and specifically allowed permissions, exact bounded content and complete EOF; marker must be empty. Never truncate, rewrite, chmod or chown a mismatching existing object. A successful close remains required; partial reads or copy faults fail.

The actual 356-byte fixture must first establish whether launchd only changed serialization or changed policy semantics. Do not use substring matching, accept an arbitrary plist merely because it contains watchdogd, or implement an unbounded permissive XML parser. For the pinned R8 experiment, a narrow exact-byte whitelist of proven semantically correct forms is smaller and more auditable than a new generic plist parser. If the captured file contains extra disabled services, unexpected values, ownership or mode changes, report and decide explicitly rather than silently accepting them.

Candidate rejection coverage: policy false/missing key/additional key/trailing data, malformed XML or binary form outside reviewed whitelist, symlink in path, directory/FIFO/device as file, non-root owner/group, writable or executable/special modes, hard links, nonempty marker, changed identity/metadata between checks, short read/EOF mismatch, and all fstat/read/copy/close errors. Cold/default behavior must remain valid; ordinary persistent Data stays refused.

Root has assigned only BootWatchdogPolicy.c for production edits and is extracting the real read-only R8 fixture. No production source has been changed at this diagnosis stage. Expanded host shim/tests will remain in this R10 artifact directory, not the production tests directory. R9 frozen artifacts and the R8 captured source remain untouched.
