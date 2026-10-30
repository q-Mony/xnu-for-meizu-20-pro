# R14 marker-compat local validation

Scope: local diagnosis, minimal production fix, host regression and ARM64
single-translation-unit compile only. No device, install, slot, reboot,
persistent image, full kernel build or packaging action.

## Input

R13 runtime evidence (`logs-nova/boot-speed-round13-nova/candidate-run-nova/autolog-nova.txt` lines 242-247):

- existing directory: `040755 uid=99 gid=99 nlink=5 dev=16777221 ino=100`,
  `mkdir EEXIST=17 created_directory=0`;
- `disabled.migrated`: `0100644 uid=0 gid=99 size=0 nlink=1`;
- failure: `branch=2 op=12 stage=warm disabled.migrated errno=22`.

Marker `st_dev`/`st_ino` and the actual `disabled.plist` owners were not logged.
The replay therefore uses the observed parent dev/ino, sets the marker dev equal
to the parent only for the device-equality path, and uses a synthetic marker ino.

R13 accepted source hashes were verified before editing:

| file | R13 SHA-256 |
|---|---|
| `BootWatchdogPolicy.c` | `6e62ebfe69ec450070a48ff71d7248cefb13e3af924ba338e0fd12d1f0c32c5c` |
| `WatchdogHostTests.c` | `9dd5928a3775acd8d4b486b1c67feb367cfd652e361056cd2d8641d8a2e9b467` |
| `WatchdogHostShim.h` | `bb3691ed9d2d6bd0112b54825ef03a35c6fc503f855f26252803b8f005fdc9ab` (unchanged) |

## Red before production edit

`--r14-repro` invokes the actual `meizu_boot_watchdog_policy` through the existing
host shim. Built with the R13 host argv, output path replaced only.

- exit code `1`;
- actual: `errno=22 branch=2 op=12 stage=warm disabled.migrated`;
- log: `red-repro-nova.log`, hash frozen at
  `6e62ebfe69ec450070a48ff71d7248cefb13e3af924ba338e0fd12d1f0c32c5c`.

A supplementary replay with the final harness against a frozen pre-fix policy
copy also exits `1` with the same tuple (`red-replay-nova/run-nova.log`).

## Fix

Only `unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c` and
`unixnu-meizu20pro-nova/package/tests/WatchdogHostTests.c` changed.

The warm policy computes a local boolean only after the existing directory has
passed the R13 checks:

```c
allow_marker_gid99 = !created_directory && parent.st_uid == 99 && parent.st_gid == 99;
```

It is passed through `watchdog_warm_file` and used only on the `EEXIST`
verification of `disabled.migrated`. Newly created files are still normalized
`0:0` and verified with compatibility disabled. The metadata helper accepts
`gid99` only for a marker with `allow_marker_gid99` and keeps:

- regular file, uid `0`, mode `07777 == 0644`, empty size, `nlink == 1`;
- device equality with the checked parent;
- no-follow open, fd/path before/after identity, content and EOF checks;
- close/error order and no writes, chown, chmod or truncation of existing inodes.

No global compatibility state was added. Plist gid0, marker gid0/gid20,
directory tuples and the cold path are unchanged.

Diagnostics added without changing R11/R12/R13 formats:

- `nova: policy-marker-compat version=14 active=1 kind=disabled.migrated uid=0 gid=99`
  after the marker's full existing-file verification and close succeed;
- `nova: policy-metadata-reject version=14 kind=%s allow_marker_gid99=%d file_dev=%u parent_dev=%u file_ino=%llu parent_ino=%llu`
  before the metadata/device `EINVAL` return.

Both strings are present in host output and in the ARM64 target object
(`target-diagnostic-strings-nova.log`).

## Green and regression

- Reproducer: exit `0`, `result=0 branch=2 op=26 stage=warm disabled.migrated`.
- Full ASan/UBSan suite: exit `0`, **867** actual-function cases
  (798 R13 cases retained + 69 new), zero sanitizer findings.
- New coverage includes observed `99:99` parent + `0:99` marker success with
  zero existing mutations, two consecutive invocations, gid0/gid20 preservation,
  rejection for other directory tuples/created/cold contexts, plist gid99
  rejection, marker uid/size/mode/type/link/device failures, marker fd/path
  identity races, marker EOF extra byte, every marker syscall fault, and the
  newly created `0:20` directory path with compatibility disabled.
- Bad marker metadata is asserted not to reach the marker read open.

## Target

ARM64 single-TU compile used the R13 target argv with only the `-o` path
changed: exit `0`, empty log, object
`BootWatchdogPolicy-target-nova.o` SHA-256
`01c748fb82364216dda673ff1427ea4c051b5b55d5809ca5a06acfc1746c928d`.
This is not a full kernel build.

## Artifacts

`host-command-nova.json`, `target-command-nova.json`, `source-diff-nova.patch`,
`source-hashes-nova.json`, `backup-manifest-nova.json`,
`validation-nova.json`, red/green logs and exact exit-code files are in this
directory. All R14 evidence files here are new and have no previous version;
backups are copies of the existing R13 policy/tests under
`snapshot-nova/boot-speed-round14-nova/marker-compat-nova/`.

## Boundaries

This proves only the measured existing-marker gid99 predicate is fixed and
locally validated. It does not prove the original boot, Hello, runtime directory
ownership source, or speed improvement. APFS mapping and the source of uid/gid99
were not reinvestigated. The phone remains last verified on Android B and was
not accessed. Sol review is required before source hash freeze or any full build.
