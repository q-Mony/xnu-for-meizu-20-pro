# R13 authorized install and start report

New file, no previous version. Status:
**INSTALLED_AND_START_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION**.

User command **刷入验证** is current. Sol accepted the exact installer at
`artifacts-nova/boot-speed-round13-nova/SOL-INSTALLER-ACCEPTANCE-nova.json`
SHA256 `c5a6173d1708c3cd51b1c62b5e5a7336b50cbbb344bfc6a4acf9b1adc9de6295`;
Sol's static build acceptance is `75e34bb3…` (`SOL-BUILD-ACCEPTANCE-nova.json`).

## Exact inputs checked before install

| Item | Value |
|---|---|
| Installer | `device-run-nova.py` `62c9ffeb8872ec238eb93d84149042fd4e6758d0eccec3fe8d51f22e04faea1b` |
| Candidate | `9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2`, 100,663,296 bytes |
| Source digests | 29/29 current |
| Verification artifacts | 34/34 current |
| R12 base A | `c8addd613a564fc2114d8920a5e2798bfeef8b66074577a4a0e3fced94368129` |
| Original B | `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2` |
| Install/start phase JSON absent | confirmed before execution |
| Mocked host tests | 6/6 PASS, `a647be07…` |
| Read-only check | PASS, `b45a82c8…` |

Manifest before the gate edit:
`c4e573a6c071bd1425ab9bccb339387f412a9852e2173a57d56c7a81886a7db8`
(backed up under
`snapshot-nova/boot-speed-round13-nova/before-install-authorization-nova/`).
Authorized manifest after opening only the review/authorization gates:
`20c853b1222494b55f6c34d1e6ce97704321db618f1ed01b320f1553e038098c`.
Image, source, artifact and warm-source hashes were preserved.

The manifest's `installation` block is the pre-run preparation snapshot and was
left unchanged per the dispatch's gate-only edit instruction; the authoritative
execution state is the raw phase JSON and `RESULT-nova.json`.

## Install

Command: `python3 artifacts-nova/boot-speed-round13-nova/device-run-nova.py install`
→ exit 0, status `INSTALLED_A_AND_FULL_READBACK_VERIFIED_IN_ANDROID_B`.

- Android B confirmed; whole A/B hashes and geometry checked.
- GPT primary header/entries and pinned R8 warm export rechecked.
- Candidate staged, size/hash verified, A/B rechecked immediately before write.
- One inactive `boot_a` write with `conv=fsync`.
- Full A/B readback in the same Android session:
  - `boot_a = 9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2`
  - `boot_b = 7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2` (unchanged)
- `extra_android_reboots=0`; temporary image removed.

Raw record:
`authorized-install-nova/install-device-nova.json`
SHA256 `34b34b03ed4f03410b3fecec83896bb5ddeda75b2aa9d78472245494818f301b`.
Exit code 0. stdout is byte-identical to the raw record; stderr empty.

## Start

Command: `python3 artifacts-nova/boot-speed-round13-nova/device-run-nova.py start`
→ exit 0, status `XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION`.

- Fresh A/B and GPT/warm-export checks before reboot.
- `adb reboot bootloader`; Fastboot confirmed `current-slot: b`.
- `set_active a`; confirmed `current-slot: a`.
- `fastboot reboot` issued to the R13 candidate.

Raw record:
`authorized-install-nova/start-device-nova.json`
SHA256 `a0c69ecc207c104d569141893a0a5f8f2b33072d32f15b6be4f1784a252970a8`.
Exit code 0. stdout is byte-identical to the raw record; stderr empty.

**The XNU reboot was issued, not observed as a successful hardware boot.**
Hello has not been confirmed; the screen report is awaited. No logs from the new
XNU were read and no post-boot device access was performed.

## Boundaries

- Only `boot_a` was written; `boot_b` is unchanged.
- No extra Android boot was introduced.
- No runtime fix, Hello success or speed improvement is claimed.
- No source, build, package or audit artifact was changed by this installation.
- Raw serial was not printed; only the existing device tag is recorded.

## Evidence

| Artifact | SHA256 |
|---|---|
| `RESULT-nova.json` | `4f6caf67c924afcfd19f6aba59431b869271e17e52035cf3814ad1896a5a6895` |
| `install-device-nova.json` | `34b34b03ed4f03410b3fecec83896bb5ddeda75b2aa9d78472245494818f301b` |
| `start-device-nova.json` | `a0c69ecc207c104d569141893a0a5f8f2b33072d32f15b6be4f1784a252970a8` |
| `install-stderr-nova.txt` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `start-stderr-nova.txt` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

Next: user reports the screen/Hello state. No further device action is taken
until then.
