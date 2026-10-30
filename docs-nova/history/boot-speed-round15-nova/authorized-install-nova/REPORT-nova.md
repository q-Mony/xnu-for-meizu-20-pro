# R15 exact install and start — reboot issued, awaiting user Hello

- Final status: `XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION`
- Candidate: `53470de0d676da3e41cb9c6a894b62b10686e0ef0480799535db0d1e331fe0ed`, 100663296 bytes
- Install phase: exit `0`, `INSTALLED_A_AND_FULL_READBACK_VERIFIED_IN_ANDROID_B`
- Start phase: exit `0`, bootloader B → slot A confirmed → Fastboot reboot issued
- No post-start USB commands. Hello is **not** confirmed; no boot-success or speed claim is made.

## Authorization and exact inputs

- User authorization: `安装验证`.
- Sol installer acceptance: `artifacts-nova/boot-speed-round15-nova/SOL-INSTALLER-ACCEPTANCE-nova.json`, SHA-256 `51efa6ad3d780dac83a43f1e849d54938d79229890ed7ef9f2d6e00832a39da4`, status `INSTALLER_ACCEPTED_GO_INSTALL_AND_START`, reviewed `2026-10-30T08:09:27.537507Z`.
- Installer: `7a478dae98af62d322ba1243072c426c8fdd8e3ccb3ad3f8ed25c169164efbbd`.
- Manifest before authorization: `6161a3a5eba16bad7deff049bee7890d308efbd69fc8ec1365e2f6e8dd29f999`.
- Manifest after authorization: `2f7cbe2e413351199532722f6e7c5b5361c047e4c78c8b245f20cc3eb54a60c6`, 26465 bytes.
- Authorization diff: `manifest-authorization-diff-nova.patch`, SHA-256 `7483f084ceb0566f8346b393429c11ce018a6e0f2a9e840871edc75861304646`.
- Sol acceptance, installer, candidate, host-test result and preflight hashes all matched before execution. All 29 mapped sources and 46 manifest artifact records re-verified.
- Authorization-only changes: `status` → `READY_FOR_AUTHORIZED_DEVICE_RUN`; `installation_allowed` true; `requires_sol_installer_review` / `requires_separate_execution_dispatch` false; `pending_gates` empty; acceptance path/hash/review time recorded. All image, source, artifact and warm hashes were preserved.
- Backup: `snapshot-nova/boot-speed-round15-nova/authorized-install-nova/before-authorization/delivery-manifest-nova.json`, SHA-256 `6161a3a5…`.

## Install phase

- Shell window: `2026-10-30T08:11:06Z` → `08:11:11Z`; exit `0`; stderr empty.
- Installer window: `2026-10-30T08:11:06.137872Z` → `08:11:11.151710Z`.
- Fresh Android B, full A/B, root, 96 MiB geometry, GPT and warm-export checks passed before staging.
- Staged image hash/size passed; A/B rechecked immediately before the one write.
- Single inactive `boot_a` write with `conv=fsync`: `08:11:10.277298Z` → `08:11:10.429610Z`.
- Full same-Android A/B readback: `boot_a` = `53470de0…`, `boot_b` = `7df205f1…` unchanged; `full_partition_readback_match: true`, `boot_b_unchanged: true`, `extra_android_reboots: 0`.
- Temporary remote image removed. No other partition touched.
- Phase record `afce7b161bacda2d6c93ee7e26f3e8212fc152f75b9fc90520280edf2dd9d1a7`; stdout identical; stderr empty; exit code `0`.

## Start phase

- Pre-start recheck: Android B, `boot_a` = `53470de0…`, `boot_b` = `7df205f1…`, GPT and warm export `7e6cc198…` passed.
- Shell window: `2026-10-30T08:11:19Z` → `08:11:33Z`; exit `0`; stderr empty.
- Installer window: `2026-10-30T08:11:19.403776Z` → `08:11:33.152264Z`.
- `adb reboot bootloader` completed `08:11:24.742528Z`.
- Fastboot confirmed slot B (`current-slot: b`) at `08:11:33.039126Z`.
- `set_active a` completed `08:11:33.104303Z`; confirmed `current-slot: a` at `08:11:33.130342Z`.
- Fastboot reboot into A issued `08:11:33.152234Z`; status `XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION`.
- One Android reboot to bootloader, then one Fastboot reboot into A. No extra Android boot; no other partition.
- Phase record `c1d1a6acb259c2dc0edd46b98a2c38e8c6467449b4aad9d741869d4f5e54b3a2`; stdout identical; stderr empty; exit code `0`.
- No USB command was run after the reboot was issued.

## Session and constraints

- Executor: `deepseek-official` / `deepseek-flash` / reasoningEffort `high`; no fallback/delegation.
- Session: `session-4539e35b-8296-4f27-80d3-7f1ecc2b24a3`; overlay `cb5addf9…`.
- No production/source/installer edits, rebuilds, package edits or test reruns.
- No raw serial or credential is printed or stored.
- Historical R14 artifacts and R15 package/Sol-local flags were not edited.

## Current state

The device was last observed as Android B with A=`53470de0…` and B=`7df205f1…`; slot A was then selected and the reboot was issued. Runtime state after reboot is unverified. The next required evidence is the user's Hello observation. No boot success, runtime behavior or speed improvement is claimed.
