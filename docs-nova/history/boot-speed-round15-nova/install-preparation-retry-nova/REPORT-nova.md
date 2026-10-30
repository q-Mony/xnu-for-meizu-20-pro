# R15 installer preparation — read-only, awaiting Sol installer review and dispatch

- Status: `AWAIT_SOL_INSTALLER_REVIEW_AND_DISPATCH`
- `installation_allowed`: `false` in the new delivery manifest until Sol dispatch
- User authorization on record: latest explicit command `安装验证` authorizes the exact R15 install/start; no further user confirmation is required, but Sol review and a separate execution dispatch remain gates.
- This task performed preparation and read-only checks only. No partition write, reboot, slot change, install or start phase ran. The phone remained Android B.

## Identity and scope

- Executor: `deepseek-official` / `deepseek-flash` / reasoningEffort `high`; no fallback, no delegation (subagent tools disabled in the persisted overlay).
- Session: `session-4539e35b-8296-4f27-80d3-7f1ecc2b24a3`.
- Persisted overlay: `dsh-prepare-overlay-nova.yml`, SHA-256 `cb5addf9df9cfa97414337e81b18d86ae024d4e40e05efe3e812989b2b5c44e5`.
- The event stream does not contain per-request headers; it binds the session and the overlay fixes the configured model. No raw device serial or credential is printed or stored.
- Nova-only work. Historical R14 artifacts/logs and the R15 package/build/Sol acceptance flags were not edited.

## Exact candidate bindings

| Item | Value |
| --- | --- |
| Candidate path | `artifacts-nova/boot-speed-round15-nova/arm-lock-review-nova/package/sblong-r15-arm-lock-final-nova/boot-warm-sblong-r15-candidate-nova.img` |
| Candidate SHA-256 | `53470de0d676da3e41cb9c6a894b62b10686e0ef0480799535db0d1e331fe0ed` |
| Candidate bytes | `100663296` |
| Source `mach_msg.c` SHA-256 | `31328bebae5b320f2652fcf80e2cf95022dd2bb981bd11d5eaf1c1b0df985bd9` |
| Raw kernel SHA-256 | `e16b7a697b83276d86aed0764a4140a1d49f5c545aaa4d1b25efeeb5d4d7437d` |
| Payload kernel SHA-256 | `72b967c33cd010d6267e5a4b83a7086917b7903ab583ae78e80ed0f287552ef2` |
| Expected `boot_a` (R14) | `6ea4078227980d8c1f0a98982fa6dd37e5a91b251384674dca53699b0f264641` |
| Expected `boot_b` | `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2` |
| Pinned R8 warm export | `7e6cc19843c0ee28eb8f1891a7d53e074688743902ef9ccf01b20d1a48e57140` |
| TAG | `8258937a53b0` (unchanged) |

## Installer

New file, no previous version: `artifacts-nova/boot-speed-round15-nova/device-run-nova.py` (10273 bytes, SHA-256 `7a478dae98af62d322ba1243072c426c8fdd8e3ccb3ad3f8ed25c169164efbbd`).

Created by mechanical substitution of reviewed R14 `artifacts-nova/boot-speed-round14-nova/device-run-nova.py` (SHA-256 `905c6b985e7cde1d17cc99292d97e8f51c2956caf161971a36462829a5db13dd`). Exactly 5 lines changed; the region from `def utc():` to EOF is byte-identical:

1. docstring `round14` → `round15`
2. `LOG` `round14` → `round15`
3. `STAGE_DIR` `round14` → `round15`
4. `BASE_A` = R14 image `6ea40782…`
5. `CANDIDATE` = R15 image `53470de0…`

All functions, control flow and transport/error policy are unchanged. The install path still verifies the full A/B partitions immediately before the one inactive `boot_a` write from Android B, writes with `conv=fsync`, performs full same-Android A/B readback, and only then uses B bootloader → A startup with no extra Android reboot. `BASE_B`, TAG and R8 export are unchanged.

- Unified diff: `artifacts-nova/boot-speed-round15-nova/installer-diff-nova.patch`, 1691 bytes, SHA-256 `649069b12588d0f3e71ca6fb809ee9b4324b386362ffc9a639ac753d60bd04f9`.
- Machine-readable comparison: `installer-comparison-nova.json` (2220 bytes, SHA-256 `22a01fa39a45cefdc69663febc970a742c84832db08a2153cee05bf1fbeb2bd5`); `unexpected_changes: []`.

## Mock transport tests

`test-device-install-nova.py` copied byte-for-byte from R14 (4344 bytes, SHA-256 `1d56111521a5e2c8218a7fe1e0e7a9dd624c9f8ef4021a73d9b1f6a7da3bf1d1`) and run against the actual R15 installer. Six tests passed with no USB access:

- success with no extra Android boot
- active-slot refusal
- staged-hash refusal before write
- staged-size refusal before write
- failed write never reboots
- bad readback never reboots

Result: `status: PASS`, `tests: 6`, `source_sha256: 7a478dae…`, `device_accessed: false`. Root result SHA-256 `79b4c78464f5d8d4e9e8a81c26d81f51f3635e30ed0911dee7641af5351c868e`; archived copy and stdout log are in this directory. No unrelated test suites were rerun.

## Delivery manifest

New file, no previous version: `artifacts-nova/boot-speed-round15-nova/delivery-manifest-nova.json` (25963 bytes, SHA-256 `6161a3a5eba16bad7deff049bee7890d308efbd69fc8ec1365e2f6e8dd29f999`).

- `status`: `AWAIT_SOL_INSTALLER_REVIEW_AND_DISPATCH`
- `installation_allowed`: `false`
- `requires_sol_installer_review`: `true`; `requires_separate_execution_dispatch`: `true`
- `authorization.latest_user_text`: `安装验证`; exact R15 install/start recorded, only this candidate authorized, no further user confirmation required.
- `authorization.execution_status_value_for_dispatch`: `READY_FOR_AUTHORIZED_DEVICE_RUN`. At Sol dispatch, only the status gate should be opened; source/image/warm bindings must not change.
- `source_sha256`: 29 entries copied from the R14 manifest. Only `unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c` was updated (`79a1e53b…` → `31328beb…`), and it was already present in the R14 map. All 29 entries match the accepted R15 source freeze (`291a3cff…`, 6186 entries, only `mach_msg.c` changed) and the current Nova tree. Zero mismatches.
- `warm_source`: exact copy of the R14 `warm_source` object, including `raw_window_sha256` `7e6cc198…`.
- Evidence binds the Sol local acceptance, arm-lock source/build/object/payload/package/test/PAN, and this installer/test/check evidence with path, SHA-256 and byte size.
- `verify_delivery` emulated read-only against the actual manifest, sources, image and warm export: all checks would pass once the status gate is opened. Evidence `manifest-verification-nova.json` (968 bytes, SHA-256 `a8972fa08ee742f67d0a06b056880233429e993b06840d392f1476ef96ab0466`, `status: PASS`).

## Read-only device check

Ran the copied R15 installer's `check` phase only; exit 0.

- Device inventory at preparation: exactly one known ADB target, Android `_b`; no fastboot device.
- Current slot: `b`; `boot_a` = `6ea40782…` (R14 image), `boot_b` = `7df205f1…` (original).
- Both boot partitions: `100663296` bytes; `boot_a` reported read-only `0`; device `dd` supports `fsync`.
- `status`: `READONLY_DIRECT_WRITE_PRECONDITIONS_CHECKED`; `device_mutated: false`.
- Evidence: `logs-nova/boot-speed-round15-nova/check-device-nova.json` (794 bytes, SHA-256 `9afeb79fc7de86388726a188cca57e558adbdcc5abc518f43bafd8ee0d9fa84a`), archived here. No serial printed. No install/start phase log exists.

## File hashes for review

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `artifacts-nova/boot-speed-round15-nova/device-run-nova.py` | 10273 | `7a478dae98af62d322ba1243072c426c8fdd8e3ccb3ad3f8ed25c169164efbbd` |
| `artifacts-nova/boot-speed-round15-nova/delivery-manifest-nova.json` | 25963 | `6161a3a5eba16bad7deff049bee7890d308efbd69fc8ec1365e2f6e8dd29f999` |
| `artifacts-nova/boot-speed-round15-nova/installer-diff-nova.patch` | 1691 | `649069b12588d0f3e71ca6fb809ee9b4324b386362ffc9a639ac753d60bd04f9` |
| `artifacts-nova/boot-speed-round15-nova/test-device-install-nova.py` | 4344 | `1d56111521a5e2c8218a7fe1e0e7a9dd624c9f8ef4021a73d9b1f6a7da3bf1d1` |
| `artifacts-nova/boot-speed-round15-nova/device-install-host-tests-nova.json` | 265 | `79b4c78464f5d8d4e9e8a81c26d81f51f3635e30ed0911dee7641af5351c868e` |
| `logs-nova/boot-speed-round15-nova/check-device-nova.json` | 794 | `9afeb79fc7de86388726a188cca57e558adbdcc5abc518f43bafd8ee0d9fa84a` |
| `install-preparation-retry-nova/installer-comparison-nova.json` | 2220 | `22a01fa39a45cefdc69663febc970a742c84832db08a2153cee05bf1fbeb2bd5` |
| `install-preparation-retry-nova/manifest-verification-nova.json` | 968 | `a8972fa08ee742f67d0a06b056880233429e993b06840d392f1476ef96ab0466` |
| `install-preparation-retry-nova/host-tests-stdout-nova.log` | 684 | `0e19c09ad3271d5bd0bf3e790ff03aac6bded9bbd775ee614846973947da3570` |
| `sol-final-review-nova/SOL-LOCAL-ACCEPTANCE-nova.json` | 10742 | `ce724d5dcc5f58f0b57341d5fe1d17777c41eafa3d1757e9c76ea3dfa6b20bcf` |
| R15 candidate image | 100663296 | `53470de0d676da3e41cb9c6a894b62b10686e0ef0480799535db0d1e331fe0ed` |
| R15 source `mach_msg.c` | 127077 | `31328bebae5b320f2652fcf80e2cf95022dd2bb981bd11d5eaf1c1b0df985bd9` |
| R15 raw kernel | 73949184 | `e16b7a697b83276d86aed0764a4140a1d49f5c545aaa4d1b25efeeb5d4d7437d` |
| R15 payload kernel | 73949184 | `72b967c33cd010d6267e5a4b83a7086917b7903ab583ae78e80ed0f287552ef2` |

## Gate for Sol

1. Review the mechanical installer diff, the byte-identical test, the new manifest/source map, and the read-only check evidence.
2. If accepted, dispatch install/start without asking the user again. Open only the manifest status gate to `READY_FOR_AUTHORIZED_DEVICE_RUN`; do not alter candidate, source, warm or evidence hashes.
3. The existing installer then performs fresh full A/B verification, one inactive `boot_a` write with `fsync`, full same-Android A/B readback, and B bootloader → A startup, with no extra Android reboot.

No Hello, runtime behavior, or speed improvement is claimed. This report covers preparation and read-only checks only.
