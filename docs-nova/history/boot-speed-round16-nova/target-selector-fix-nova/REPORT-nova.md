# R16 exact-target selector fix — Nova result

**Status:** `SELECTOR_FIX_AND_EXACT_TARGET_CHECK_PASSED_AWAIT_SOL_REVIEW`
**Model:** `deepseek-official` / `deepseek-flash` / `reasoning_effort=high`; no delegation, no fallback.
**Target:** exact serial `[redacted-device-serial]` only. All other devices are excluded from development/validation and were never addressed.

## 1. Selector fix

- Installer: `artifacts-nova/boot-speed-round16-nova/device-run-nova.py`
- Before: `69d6c154e83e37bee68e774563cd7ebcd764b57c1bc868a74ccfec5b43edf9cd` (10273 B)
- After: `26a2e29e789555ce81a3eeb6efd6e1da3bd63911b78d4befbd4036ebda192951` (10588 B)
- Added `TARGET_SERIAL = '[redacted-device-serial]'` and an exact-project-target comment.
- `select()` now filters enumeration rows by exact serial first. It requires the target row to be unique, have `len >= 2`, match the expected mode, and pass the existing TAG hash before assigning `serial`.
- Unrelated rows are ignored entirely; no command is scoped to them. `wait`/timeout behavior and every byte outside the constant and `select()` block are unchanged.
- Serial-only diff `cc454f9e4d15870c1c7c1314535ca6b855305f9c98c3ad83e051bc3a2e3501c3` (1752 B); R15→after diff `94e53aa39a1823f580e30c90cc601677e5666d70453cbdd71e35095ec02e153a` (2843 B).
- Outside-selector proof `1ec1b0d5e105e84f82932dfbe005527a00317a21ba8e74f52ff3ddf4bff2b81b` (2423 B): prefix equal after removing the added constant; suffix from `def adb(` byte-identical.

## 2. Tests

| Suite | Result | Count | Device |
|---|---|---|---|
| Exact-target selector unittest (actual installer `select()`, mocked enumeration) | PASS | 9 | not accessed |
| Unchanged R15 six transport tests re-run against corrected installer | PASS | 6 | not accessed |

- Selector test source `0719e17cb8ef66af360a16936c6dda34655e7406dbeffa1bd2c66aac2cc2b0c9`; result `f8d568f5e7cd137d813601a31db2de30ea7e76bbcec14321accdbb9f1c163edc`.
- Cases: target alone; unrelated first/last; absent target; duplicate target; wrong mode; unauthorized only; unrelated only; fastboot target with unrelated.
- Every mocked call was `(tool, 'devices')`; no targeted command contained the unrelated placeholder.
- Transport test source is byte-identical to R15 (`1d56111521a5e2c8218a7fe1e0e7a9dd624c9f8ef4021a73d9b1f6a7da3bf1d1`), 6/6 PASS, new result `fa4588536dc0aa5ad02492f90efb5bfa2f011a7b6e5096aae6cbf6ecc1848fdd`.

## 3. Read-only check (exact target only)

`device-run-nova.py check` exited 0.

- Android B; `boot_a = 53470de0d676da3e41cb9c6a894b62b10686e0ef0480799535db0d1e331fe0ed`.
- `boot_b = 7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2`.
- Both boot partitions asserted `100663296` B (96 MiB); `boot_a` read-only = false; `dd` fsync available.
- `device_mutated = false`; no reboot, slot change, partition write, install, start or export write.
- Record `logs-nova/boot-speed-round16-nova/check-device-nova.json` `c1faca9606da61dd0885b109216679b5afb22933191685d6bb3559f161391303` (794 B).
- GPT and warm-export checks live only in `install()`/`start()`; they were not run and are not claimed.
- The prior failed check `23ef1dbe7dca9fc07b0ac09d436165f6097ef9a3c137f48de6d9b09b911e2e5e` was preserved under `snapshot-nova/.../target-selector-fix-nova/preserved/historical-failed-check-20261030T093925Z-nova/`, not deleted and not reported as current status.

## 4. Manifest

- `artifacts-nova/boot-speed-round16-nova/delivery-manifest-nova.json` `86e12ee60b3824ec9091ca05d0feb1fa3262606e4e32ded602f1d5d3fb20f4b0` (29035 B).
- Gate remains `AWAIT_SOL_INSTALLER_REVIEW_AND_DISPATCH`; `installation_allowed=false`; installation not performed.
- 83 bound path/hash/byte objects verified, all match (`binding-verification-nova.json` `5400a927a9e3eaae8fb779f6931f6d96c08a02ed5d68cb46affdd22f883079a0`).
- Candidate/source/build/payload/warm hashes unchanged: candidate `51a0248347f0e70644a28d30f932ce59245ada3920ade777ac5835578e30bf51` (100663296 B), 29 source entries, raw warm export `7e6cc19843c0ee28eb8f1891a7d53e074688743902ef9ccf01b20d1a48e57140`.
- Historical failed check and prior test result are explicitly bound as history.

## 5. AGENTS / changelog

- `Nova AGENTS.md` appended with the exact-serial rule: `08dfba09b80e1f99ade2bac9eb6f8abc497821c4f453b535c4e4ca7e6299ef57`.
- Parent `[local-home]/...` is outside the Nova workspace. The append was denied by the workspace-write sandbox and no approval channel was available. The exact append is preserved as `parent-AGENTS-device-rule-nova.patch` (`79d7d0d2a02a1ab7c368f0e5162f1cdf697b4748a8e13665404b2a90d06ecccf`, 921 B) for application by Sol/user. This is the only incomplete requested item.
- `CHANGELOG-nova.md` backed up to `snapshot-nova/.../target-selector-fix-nova/before-changelog-edit/CHANGELOG-nova.md` (`f79ae7403e2b87247db62e6917c99b5298c02c51a109f5c8b81b6bc765891d11`) and appended exactly once; final `0e833ad704434f0c294191b8612d74f46b449328aecb214981bc7182bb35344e` (101194 B).

## 6. Boundaries

No production source/build/package changes. No install, start, reboot, slot change or partition write in this dispatch. No command was scoped to any serial other than `[redacted-device-serial]`. Stopped for Sol review; no user question.
