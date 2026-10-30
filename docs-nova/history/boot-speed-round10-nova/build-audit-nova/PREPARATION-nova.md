# R10 build, package and link audit preparation — Nova

New report. Preparation only: no R10 source freeze, build, package, or device action has occurred. The parent owns central scope/device scripts and CHANGELOG; the implementation owner alone edits the production watchdog policy.

## Input and scope

The only permitted production difference from R9 is `unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c`. The parent additionally authorized maintenance of exactly two existing host-test files: `unixnu-meizu20pro-nova/package/tests/WatchdogHostShim.h` and `unixnu-meizu20pro-nova/package/tests/WatchdogHostTests.c`. The same 6,186-entry R9 source inventory is pinned by SHA256 `dec1479353f831f5c1348acf123c809146557b37c9a9a4285f9af9e64484ab6a`. Both R9 warm drivers and their new headers, all diagnostic inputs, the kern_exec bridge and build configuration remain frozen. Additional fault-test artifacts belong to the implementation artifact directory.

Eleven R9 source/build inputs were independently copied into `snapshot-nova/boot-speed-round10-nova/r9-build-baseline-nova/`. `r9-baseline-nova.json` records their hashes and private copies. R9 frozen artifacts are read-only references.

## Actual R9 compile/link path

1. `board.toml` selects `build-meizu-watchdog-policy.py` as a board archive builder.
2. `tree/kits/meizu20pro/BootWatchdogPolicy.c` resolves through its parent kit path to the original Nova board package source; there is no separate generated policy source to edit.
3. The generated tree builder is byte-identical to the pinned board builder. It verifies the existing kern_exec patch using a reverse `git apply --check` before compiling. The hook is already installed, so no kern_exec source/object edit is expected.
4. The builder derives its ARM64 command from `kern_exec.o.json`, substitutes the policy input/output, strips inherited dependency-output and warning arguments, and adds `-mgeneral-regs-only -Wall -Wextra -Werror -Wno-unused-parameter`.
5. The output is `build/meizu-watchdog-policy/BootWatchdogPolicy.o`, archived as `libMeizuWatchdogPolicy.a`. Actual R9 link inputs force-load that archive.
6. R9 `_load_init_program` directly calls `_meizu_boot_watchdog_policy` after the Data preparation hook. The linked function is at `0xfffffff00895a574`, 924 bytes. Object and raw bytes match modulo listed relocation words; raw/payload function bytes match exactly. Evidence is `r9-watchdog-baseline-evidence-nova/audit-nova.json`.

R9 baseline policy source SHA256: `53c6366ee4bed4b2f5a1980adae73c3e8777a40db0875cb6288dc9b16dd81235`.

R9 baseline policy object SHA256: `93e978dd2ece39ef3aea45ac408de71e503c381c32768b20aec0845e29c9a41d`.

## Prepared tools

- `run-build-nova.py`: default/input-only checks scope without building. Once the implementation owner and parent confirm the repaired source is frozen, `--freeze-sources` records it, then `--build` runs the isolated full kernel build. It saves before/after source inventories, reconstructed policy recipe, formal policy/warm objects and archives, kern_exec object/recipe, independent raw kernel and build log. It must reject any out-of-scope change or unstable source.
- `audit-linked-watchdog-nova.py`: audits the new policy object/archive and real caller, requires kern_exec.o and both R9 warm driver objects to remain identical to R9, rechecks retained warm worker/call bindings and later compares all selected functions against the packaged payload and final extracted kernel.
- `../packaging-nova/pack-watchdog-candidate-nova.py`: requires a successful R10 build-result path. It uses the exact R9 boot envelope and changes only `kernel.macho` within EFI. Boot TOML, `nova-data-warm=1`, cold Data seed, trust cache, ADT and every other payload file must remain byte-identical. Firmware/header/DTB/AVB bytes are preserved except the required Android v0 image checksum. It patches the entry once, verifies PAN and extracts the final image to recheck contents.

R9 package baseline SHA256: `b3359865b616882a78d166478b786acda51cbcf970d8e5a56330d85b85df0110`.

Build and packaging `--check-inputs-only` checks passed during preparation. The allowlist now contains the production policy plus exactly the two authorized host-test files. No R10 repair/build success is claimed by these preparation checks. The repaired policy's branch semantics and fault behavior will require the independent implementation review/tests; runtime success and startup improvement require the later device trial owned by the parent.
