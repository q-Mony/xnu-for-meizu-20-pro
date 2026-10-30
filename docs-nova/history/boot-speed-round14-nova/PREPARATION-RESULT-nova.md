# R14 Preparation Result (Nova)

- Status: `PREPARED_AWAIT_SOL_INSTALLER_REVIEW`
- Round: `boot-speed-round14-nova`
- New file; no previous version.
- Created (UTC): `2026-10-30T06:23:42.452843+00:00`
- Model: `deepseek-official/deepseek-flash`, reasoningEffort `high`
- dsh session: `session-d9a564c8-4e60-4e4c-b3ac-a08399514bd1`
- Session evidence: persisted session events show provider `deepseek-official`, model `deepseek-flash`, reasoningEffort `high`; no delegation or fallback.
- Scope: prepare exact R14 installer and manifest, six mocked installer tests, one read-only Android B preflight. No production edit, build, partition write, reboot, slot change, install or start.

## Candidate binding

| Item | SHA-256 | Bytes |
|---|---|---|
| R14 boot image | `6ea4078227980d8c1f0a98982fa6dd37e5a91b251384674dca53699b0f264641` | 100663296 |
| R14 raw kernel | `ea94322cf5540e8778b27ea39a7b2971f568d2097a920ad0a508d0cbbc1a9246` | 73932800 |
| R14 payload kernel | `3d67f555f00f14ac99696b4e74ea9d396dcb8a660bc23eb9d67cd43139e7449d` | 73932800 |
| Sol final acceptance | `3b544cc5e98665bf815127c9d54575799d6f93e56731646e307f006086485ec3` | 4291 |
| R13 reference boot_a | `9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2` | 100663296 |

## Installer and diff

- Installer: `<nova-root>/artifacts-nova/boot-speed-round14-nova/device-run-nova.py` (`905c6b985e7cde1d17cc99292d97e8f51c2956caf161971a36462829a5db13dd`, 10273 bytes)
- R13 base: `<nova-root>/artifacts-nova/boot-speed-round13-nova/device-run-nova.py` (`62c9ffeb8872ec238eb93d84149042fd4e6758d0eccec3fe8d51f22e04faea1b`)
- Diff: `<nova-root>/artifacts-nova/boot-speed-round14-nova/installer-diff-nova.patch` (`44f501177cc9f90bed0129399d58937807e3fe6a62ac85a97bb12746a79d43a5`, 1655 bytes)
- Control flow unchanged; exactly five permitted substitutions: docstring, `LOG`, `STAGE_DIR`, `BASE_A`, `CANDIDATE`.
- Test source copied byte-for-byte: `1d56111521a5e2c8218a7fe1e0e7a9dd624c9f8ef4021a73d9b1f6a7da3bf1d1` equals R13 `1d56111521a5e2c8218a7fe1e0e7a9dd624c9f8ef4021a73d9b1f6a7da3bf1d1`.
- Preserved: identity tag, full A/B hashes, geometry, GPT/warm-export checks, inactive `boot_a` only, `conv=fsync`, full A/B readback, no extra Android boot.

```diff
--- a/artifacts-nova/boot-speed-round13-nova/device-run-nova.py
+++ b/artifacts-nova/boot-speed-round14-nova/device-run-nova.py
@@ -1,5 +1,5 @@
 #!/usr/bin/env python3
-"""Nova round13 read-only warm Data device phases. New file; fixed candidate and expected A/B hashes.
+"""Nova round14 read-only warm Data device phases. New file; fixed candidate and expected A/B hashes.
 
 Each invocation freshly validates identity/state. Install writes inactive boot_a from
 Android B and verifies both entire boot partitions without another Android boot.
@@ -17,18 +17,18 @@
 
 NOVA = Path(__file__).resolve().parents[2]
 HERE = Path(__file__).resolve().parent
-LOG = NOVA / 'logs-nova/boot-speed-round13-nova'
+LOG = NOVA / 'logs-nova/boot-speed-round14-nova'
 ADB = '[local-home]/...'
 FASTBOOT = '[local-home]/...'
 TAG = '8258937a53b0'
-BASE_A = 'c8addd613a564fc2114d8920a5e2798bfeef8b66074577a4a0e3fced94368129'
+BASE_A = '9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2'
 BASE_B = '7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2'
-CANDIDATE = '9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2'
+CANDIDATE = '6ea4078227980d8c1f0a98982fa6dd37e5a91b251384674dca53699b0f264641'
 R8_LOG = NOVA / 'logs-nova/boot-speed-round8-nova'
 EXPORT_SHA = '7e6cc19843c0ee28eb8f1891a7d53e074688743902ef9ccf01b20d1a48e57140'
 serial = ''
 record = {'new_file_no_previous_version': True, 'device_tag': TAG, 'actions': []}
-STAGE_DIR = '/data/local/tmp/nova-boot-speed-round13-nova'
+STAGE_DIR = '/data/local/tmp/nova-boot-speed-round14-nova'
 
 
 def utc():
```

## Six mocked installer tests

- Command: `python3 artifacts-nova/boot-speed-round14-nova/test-device-install-nova.py`
- Exit: `0`; result `PASS`; tests `6`; device accessed `false`.
- Evidence: `<nova-root>/artifacts-nova/boot-speed-round14-nova/device-install-host-tests-nova.json` (`417b961d5236f67f262f6e553b9389aa687ce9969ed7f102e2262051a66e8c65`, 265 bytes).
- Limit: exercises actual installer control flow with mocked transport; does not prove hardware write success.

## Read-only Android B preflight

- Command: `python3 artifacts-nova/boot-speed-round14-nova/device-run-nova.py check`
- Exit: `0`; status `READONLY_DIRECT_WRITE_PRECONDITIONS_CHECKED`; phase `check`; device mutated `false`.
- Current slot: `b`; boot_a `9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2`; boot_b `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2`; geometry `100663296` bytes.
- Direct-write preconditions: target `boot_a`, read-only `false`, `dd` fsync `true`.
- Serial was not printed or stored; identity matched the expected tag.
- Evidence: `<nova-root>/logs-nova/boot-speed-round14-nova/check-device-nova.json` (`d4075f9c7263c0271de2fc3996448c255e218b428e57cec910a2f84a2d604dd5`, 794 bytes).
- No install/start phase record exists; no partition write, slot change or reboot occurred.

## Root delivery manifest

- Path: `<nova-root>/artifacts-nova/boot-speed-round14-nova/delivery-manifest-nova.json`
- SHA-256: `e006269f7ef1f0c13ec7c11ae6b4d0afa96e0d12448aa6b21cd9626f5a42674d`; bytes `21753`
- Status `AWAIT_SOL_INSTALLER_REVIEW`; `installation_allowed=false`; `requires_sol_installer_review=true`.
- Binds 37 R14 artifacts and 29 source entries; the 29-entry map comes from the R13 root manifest with only `BootWatchdogPolicy.c` and `WatchdogHostTests.c` updated; all other 27 digests re-verified.
- Warm source is identical to R13.
- No R13 check/test/authorization was copied as current; no mutable changelog artifact gate or circular hash.

## No mutation

- Partitions written: none; slot changed: false; reboots: 0; install/start: not run.
- Production source edited: false; build performed: false; R13 files modified: false.
- R13 immutability: installer `62c9ffeb8872ec238eb93d84149042fd4e6758d0eccec3fe8d51f22e04faea1b`, test `1d56111521a5e2c8218a7fe1e0e7a9dd624c9f8ef4021a73d9b1f6a7da3bf1d1`, root manifest `20c853b1222494b55f6c34d1e6ce97704321db618f1ed01b320f1553e038098c`.

## Sol gates

1. Sol reviews this exact installer, diff and root manifest.
2. A separate execution dispatch performs any install/start.

Latest user text `刷入` is recorded as authorization for the separate execution dispatch only; this preparation task kept installation disabled.
