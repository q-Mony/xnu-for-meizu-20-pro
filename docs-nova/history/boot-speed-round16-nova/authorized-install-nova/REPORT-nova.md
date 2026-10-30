# Nova R16 exact install/start report

## Final status

`XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION`

A reboot into slot A was issued. Hello, boot success, and speed improvement are **not** confirmed or claimed. No USB command was issued after the successful start.

- Target: `[redacted-device-serial]` only; other enumerated devices ignored and never addressed.
- Config: `deepseek-official` / `deepseek-flash` / `high`, no delegation/fallback.
- Session: `session-65bd69e6-d6c1-4d09-b3db-7fe9bef8f813` (headless profile).
- Result: `RESULT-nova.json` `9c2c0897e2547b9a129f861f79fce4a52f758ca4b4cb6d5196e851f37732180a`.

## Authorization and pre-run verification

- Sol installer acceptance: `SOL-INSTALLER-ACCEPTANCE-nova.json` `33e4c1cf151fae0aa7e1ead82feeb9a8b98088370629b12ea5e1ce9b76e26b96`, status `INSTALLER_ACCEPTED_GO_INSTALL_AND_START`, user command `安装`.
- Manifest before: `86e12ee60b3824ec9091ca05d0feb1fa3262606e4e32ded602f1d5d3fb20f4b0` (matches acceptance), backed up to `snapshot-nova/boot-speed-round16-nova/authorized-install-nova/before-authorization/`.
- Manifest after authorization-only edit: `a4f4177a0dd6958a8761584ad7521c91bcc4facd32b19d8bd0acdab16b5b076b`, status `READY_FOR_AUTHORIZED_DEVICE_RUN`, `installation_allowed=true`, review/dispatch `false`, `pending_gates=[]`.
- Authorization diff: `manifest-authorization-diff-nova.patch` `1c3194f38234c58cd43ba34d633e9c67df62b4524a91bda9e9d930636fc17acf`; exactly the 10 allowed status/authorization gate fields changed.
- All 29 source entries and all 37 artifact objects matched; image/source/artifact/warm hashes were preserved verbatim.
- Actual pre-state on the exact target: Android `_b`, root available, both boot partitions 100663296 bytes, `boot_a=53470de0…`, `boot_b=7df205f1…`.
- Preflight record: `prep-verification-nova.json` `b6559caf0eddef84874b15a6bb545d78e2d94420b14676913078b1b9637a20fc`.

## Install

- Command: `python3 artifacts-nova/boot-speed-round16-nova/device-run-nova.py install`
- Exit: `0`
- Status: `INSTALLED_A_AND_FULL_READBACK_VERIFIED_IN_ANDROID_B`
- Phase window: `2026-10-30T09:49:51.765892+00:00` – `2026-10-30T09:49:56.859226+00:00`
- Single inactive `boot_a` raw write + fsync: `2026-10-30T09:49:55.958877+00:00` – `2026-10-30T09:49:56.119638+00:00`
- Full same-Android A/B readback: `boot_a=51a02483…` (candidate), `boot_b=7df205f1…` (unchanged).
- GPT and warm export rechecked; export `7e6cc198…`; extra Android reboots `0`; temporary staging image removed.
- Stdout/phase JSON: `c76709d3d36a34bc17cc98d07da07da49470fbed165021032b678cae1ef92cc4` (identical). Stderr empty (`e3b0c442…`). Exit file `9a271f2a…`.
- Archived phase: `install-phase-nova.json`.

## Start

- Command: `python3 artifacts-nova/boot-speed-round16-nova/device-run-nova.py start`
- Exit: `0`
- Status: `XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION`
- Phase window: `2026-10-30T09:50:06.802505+00:00` – `2026-10-30T09:50:20.825560+00:00`
- Rechecked A/B and warm export; confirmed fastboot slot `b`, selected `a`, confirmed slot `a`, then issued reboot at `2026-10-30T09:50:20.825528+00:00`.
- Stdout/phase JSON: `8519f38a908bcf0df28c6f00609e8a9222ba47d2c351de8b49456ef0dce53486` (identical). Stderr empty (`e3b0c442…`). Exit file `9a271f2a…`.
- Archived phase: `start-phase-nova.json`.

## Claims and limits

- Not claimed: Hello, boot success, speed improvement, or manual boot timing.
- No post-start USB commands, no wait for Hello, no return to Android.
- The delivery manifest's `installation` block remains its pre-run snapshot because only authorization/status gates were changed; the phase JSONs and `RESULT-nova.json` are authoritative for execution.
- No production/source/installer edit, build, unrelated test, or write outside inactive `boot_a` plus the installer's own temporary staging file.
