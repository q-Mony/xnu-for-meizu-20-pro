# R13 install preparation and read-only device check

New file, no previous version. Status:
**PREPARED_AWAIT_SOL_INSTALLER_REVIEW_NO_DEVICE_MUTATION**.

## Authorization

User command: **刷入验证**. Sol's static acceptance:
`artifacts-nova/boot-speed-round13-nova/SOL-BUILD-ACCEPTANCE-nova.json`
SHA256 `75e34bb31287f04f7faae585c20d337f8b85454ab7eae7539e096bc03c0b91fe`.

This dispatch is preparation and read-only check only. No install/start phase
was invoked; no partition was written, no slot was switched, and no reboot
occurred. Installation remains gated on Sol's review of the concrete installer.

## Installer adaptation

- R12 source: `artifacts-nova/boot-speed-round12-nova/device-run-nova.py`
  SHA256 `8c71649d6075dfee31f428ac6a56282cd005d1e1561c94e5aa3ed448e61e8599`
- R13 installer: `artifacts-nova/boot-speed-round13-nova/device-run-nova.py`
  SHA256 `62c9ffeb8872ec238eb93d84149042fd4e6758d0eccec3fe8d51f22e04faea1b`
- Diff: `artifacts-nova/boot-speed-round13-nova/install-preparation-nova/installer-diff-nova.patch`
  SHA256 `2060a8184f5e3763d9edd5b885a43ad3f9db9250a87d1244146b5353222f4f48`

The diff is limited to the docstring, `LOG`/`STAGE_DIR` round12→round13, and:

| Constant | R13 value |
|---|---|
| `BASE_A` | `c8addd613a564fc2114d8920a5e2798bfeef8b66074577a4a0e3fced94368129` (current R12 A) |
| `BASE_B` | `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2` (unchanged) |
| `CANDIDATE` | `9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2` |

The R13 test script is a byte-for-byte copy of R12:
`artifacts-nova/boot-speed-round13-nova/test-device-install-nova.py`
SHA256 `1d56111521a5e2c8218a7fe1e0e7a9dd624c9f8ef4021a73d9b1f6a7da3bf1d1`.

The install path retains identity/hash/geometry checks, GPT and pinned warm
export preflight, staged size/hash, two full A/B prewrite checks, exactly one
inactive `boot_a` `dd ... conv=fsync`, and a full A/B postwrite readback. It
never writes `boot_b` and does not add an intermediate Android boot.

## Mocked-transport tests

`python3 artifacts-nova/boot-speed-round13-nova/test-device-install-nova.py`:
**6/6 PASS**, device_accessed false. Result:
`artifacts-nova/boot-speed-round13-nova/device-install-host-tests-nova.json`
SHA256 `a647be077a3fc82a2d388100d40b963255206eea5a8327dcadb368587f8a62d8`.

Cases: success/no extra Android boot, active slot refused, staged hash refused,
staged size refused, failed write never reboots, bad readback never reboots.

## Read-only device check

Command:
`python3 artifacts-nova/boot-speed-round13-nova/device-run-nova.py check`
→ exit 0, status `READONLY_DIRECT_WRITE_PRECONDITIONS_CHECKED`,
`device_mutated=false`, no serial printed (hashed device tag only).

| Item | Observed |
|---|---|
| Active slot | Android `_b` |
| `boot_a` | `c8addd613a564fc2114d8920a5e2798bfeef8b66074577a4a0e3fced94368129` |
| `boot_b` | `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2` |
| Boot geometry | 100,663,296 bytes per partition |
| `boot_a` read-only | false |
| `dd` explicit fsync | available |

Evidence:
`logs-nova/boot-speed-round13-nova/check-device-nova.json`
SHA256 `b45a82c806b835277d717aab7197be802e0c180236e6703477e50f52896d609e`.

This check covers identity, A/B hashes, geometry and direct-write capability.
GPT and warm-export preflight remain in the install path and were **not** run in
this read-only dispatch.

## Manifest

`artifacts-nova/boot-speed-round13-nova/delivery-manifest-nova.json`
SHA256 `c4e573a6c071bd1425ab9bccb339387f412a9852e2173a57d56c7a81886a7db8`.

- Status `AWAIT_SOL_INSTALLER_REVIEW`; `installation_allowed=false`,
  `requires_sol_installer_review=true`.
- Binds the exact R13 image/kernel/source hashes, R12 `BASE_A`, original `B`,
  unchanged R12 warm source, 29 source hashes and 34 verification artifacts.
- Source map starts from the R12 manifest with only Policy and HostTests hashes
  updated; all other 27 source digests matched the current tree.
- No install/start authorization is claimed by this manifest yet.

## Next gate

Sol reviews the concrete installer and manifest. A separate dispatch may then
mark the manifest `READY_FOR_AUTHORIZED_DEVICE_RUN` and execute install/start
under the user's **刷入验证** authorization.

This remains static/mock/read-only preparation. Hardware Hello and speed are not
tested; no partition, slot or reboot action occurred.
