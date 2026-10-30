# R18 Exact Install/Start Result — Nova

New file; no previous version.

**Status:** `XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION`  
Not Hello, not a speed claim, not a black-screen fix claim.

## Authorization
- Latest user command: `安装`.
- Sol acceptance: `artifacts-nova/boot-speed-round18-nova/SOL-INSTALLER-ACCEPTANCE-nova.json` SHA-256 `b7ab0a0937422ba100b4e63bcf26f82cd15b512086f1bf549978e6406d272b36`, status `INSTALLER_ACCEPTED_GO_INSTALL_AND_START`, reviewed `2026-10-30T19:58:14.088052+08:00`.
- Installer SHA-256 `7c1d5687aa92d2ee1d59d3e9936a637066845774394131f7071a85c33286b0b6`.
- Manifest before `a5c51028f0a0195cd804a8ba7834de6532a323a7816536b6a630f3af998756f8`; after `f86b4011e2e961a14ae0d819bc7d9c0746147dcdac3ea78dcc2b663de98a8987`. Only status/authorization gates plus exact command/acceptance/scope metadata changed; image/source/artifact/warm fields preserved.
- Backup/diff: `snapshot-nova/boot-speed-round18-nova/authorized-install-nova/before-authorization/20261030T115950Z` (full diff SHA-256 `0033ac144ff269708ff0b855bde2d85c6c2a1f6d7a6aa000447930d03e3bdcad`).
- Pre-install: 6186 source refs and 35 artifact refs rehashed, 0 mismatches; no prior install/start phase records.

## Install — once, exit 0
- Status: `INSTALLED_A_AND_FULL_READBACK_VERIFIED_IN_ANDROID_B`; `2026-10-30T11:59:55.864179+00:00` → `2026-10-30T12:00:01.279449+00:00`.
- Android B; one inactive `boot_a` write `2026-10-30T12:00:00.410945+00:00` → `2026-10-30T12:00:00.566411+00:00`.
- Same-boot full readback: `boot_a=be40edfedadbc161e3e86864c9d8803243c34426f78007f983213c07c879c21b`, `boot_b=7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2`.
- `boot_b` unchanged; extra Android boots `0`; temporary staging image removed; stderr empty.

## Start — once, exit 0
- Status: `XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION`; `2026-10-30T12:00:08.456484+00:00` → `2026-10-30T12:00:22.485103+00:00`.
- Rechecked A/B/GPT/export; entered bootloader; confirmed B; selected A; confirmed A; reboot issued `2026-10-30T12:00:22.485074+00:00`.
- Zero USB commands after reboot; no Android return; no Hello/log read from the device.

## GPT/export facts
- GPT primary header `0283fddc…4408` and entries `6b65baf4…8cc7` (4096 B each) rechecked by installer.
- `unixnu` 68,719,476,736 B; export window offset 68,417,486,848, 268,435,456 B, SHA-256 `7e6cc19843c0ee28eb8f1891a7d53e074688743902ef9ccf01b20d1a48e57140`.
- Install and start both record `export_backup_and_gpt_rechecked=true`.

## Config/session
- `deepseek-official` / `deepseek-flash` / `high`, no delegation/fallback.
- Execution session `session-49427f8b-addb-4326-9e78-8a62511442bf`; Sol acceptance review session `session-575b3e82-ffbd-4071-ac5a-772c07d1adea`.
- Target only `[redacted-device-serial]`; global read-only enumeration only, all targeted commands used exact `-s`.

## Evidence
- Install phase `logs-nova/boot-speed-round18-nova/install-device-nova.json` `99f6f0a11196618b48d0c576033df973a24d0cfafb597467af706aaf5bf79fc0`; stdout/stderr/exit/timing in `logs-nova/boot-speed-round18-nova/authorized-install-nova/20261030T115955Z`.
- Start phase `logs-nova/boot-speed-round18-nova/start-device-nova.json` `b0f7d88735522d4ef6cf056dc62168dc936cabb65e461d4898d642e30a6d7703`; stdout/stderr/exit/timing in `logs-nova/boot-speed-round18-nova/authorized-install-nova/20261030T120008Z`.
- Full machine result: `artifacts-nova/boot-speed-round18-nova/authorized-install-nova/RESULT-nova.json`.
