# R14 authorized install and start report

New file, no previous version. Status:
**INSTALLED_AND_START_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION**.

Model/config: `deepseek-official/deepseek-flash`, reasoningEffort `high`.
dsh session: `session-d9a564c8-4e60-4e4c-b3ac-a08399514bd1` (headless).
No delegation or fallback.

User command **刷入** is current. Sol accepted the exact installer at
`artifacts-nova/boot-speed-round14-nova/SOL-INSTALLER-ACCEPTANCE-nova.json`
SHA256 `b213e131f1eb31ffa30afc9697da1c1c3068ea0f964cd1a6d7739acd8d648705`.
Static final acceptance:
`build-audit-nova/SOL-FINAL-ACCEPTANCE-nova.json`
SHA256 `3b544cc5e98665bf815127c9d54575799d6f93e56731646e307f006086485ec3`.
The static file's historical `installation_allowed=false` text is superseded
only for this exact R14 by the current user command and Sol installer acceptance.

## Exact inputs checked before install

| Item | Value |
|---|---|
| Installer | `device-run-nova.py` `905c6b985e7cde1d17cc99292d97e8f51c2956caf161971a36462829a5db13dd` |
| Candidate | `6ea4078227980d8c1f0a98982fa6dd37e5a91b251384674dca53699b0f264641`, 100,663,296 bytes |
| Source digests | 29/29 current |
| Nested artifact records | 37/37 path+size+SHA current |
| R13 base A | `9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2` |
| Original B | `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2` |
| Install/start phase JSON absent | confirmed before execution |
| Mocked host tests | 6/6 PASS, `417b961d…e8c65` (not rerun) |
| Read-only check | PASS, `d4075f9c…04dd5` |
| Warm export | `7e6cc19843c0ee28eb8f1891a7d53e074688743902ef9ccf01b20d1a48e57140` |

Manifest before the gate edit:
`e006269f7ef1f0c13ec7c11ae6b4d0afa96e0d12448aa6b21cd9626f5a42674d`
(backed up under
`snapshot-nova/boot-speed-round14-nova/authorized-install-nova/`).
Authorized manifest after opening only the review/authorization gates:
`5e49bfb03ae660a3ef28a26aca0ddc8827860ca3d3ff24eb2a3c5763221b3a23`.
The gate-only diff is
`authorized-install-nova/manifest-authorization-diff-nova.patch`
SHA256 `9205de8dd3bbcfde7dd1e1f66d73faf2369ea400b2d32df1182062e3c05eb0b9`.
Image, source, nested artifact and warm-source hashes were preserved; the
installer was not edited.

The manifest's `installation` and `known_limits` blocks are pre-run preparation
snapshots and were left unchanged per the gate-only edit instruction. The
authoritative execution state is the raw phase JSON and `RESULT-nova.json`.

## Install

Command: `python3 artifacts-nova/boot-speed-round14-nova/device-run-nova.py install`
→ exit 0, status `INSTALLED_A_AND_FULL_READBACK_VERIFIED_IN_ANDROID_B`.
Started `2026-10-30T06:27:18.171038+00:00`; finished
`2026-10-30T06:27:23.308774+00:00`.

- Fresh Android B identity, root, geometry and full A/B hashes checked.
- GPT primary header/entries and pinned R8 warm export rechecked.
- Candidate staged, size/hash verified, A/B rechecked immediately before write.
- One inactive `boot_a` write with `conv=fsync`:
  `2026-10-30T06:27:22.426204+00:00` → `2026-10-30T06:27:22.588367+00:00`.
- Full A/B readback in the same Android session:
  - `boot_a = 6ea4078227980d8c1f0a98982fa6dd37e5a91b251384674dca53699b0f264641`
  - `boot_b = 7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2` (unchanged)
- `extra_android_reboots=0`; temporary image removed.

Raw record: `logs-nova/boot-speed-round14-nova/install-device-nova.json`,
archived as `authorized-install-nova/install-nova/phase-nova.json`,
SHA256 `f3d6ab8c4fd914fda13a2897afbbd4f6c9730b8696712e01cca9719a5b602b25`.
stdout is byte-identical; stderr empty; exit code file hash `9a271f2a…86aa`.

## Start

Command: `python3 artifacts-nova/boot-speed-round14-nova/device-run-nova.py start`
→ exit 0, status `XNU_REBOOT_ISSUED_AWAIT_HELLO_CONFIRMATION`.
Started `2026-10-30T06:27:30.720922+00:00`; finished
`2026-10-30T06:27:44.707822+00:00`.

- Fresh full A/B, GPT and warm-export checks passed.
- `reboot bootloader` at `2026-10-30T06:27:36.347936+00:00`;
  Fastboot confirmed `current-slot: b`.
- `set_active a` at `2026-10-30T06:27:44.665541+00:00`;
  confirmed `current-slot: a` at `2026-10-30T06:27:44.689341+00:00`.
- `fastboot reboot` issued at `2026-10-30T06:27:44.707797+00:00`.

Raw record: `logs-nova/boot-speed-round14-nova/start-device-nova.json`,
archived as `authorized-install-nova/start-nova/phase-nova.json`,
SHA256 `084b6196bb3e62c424173402b4558084734017c48db27ae3693ade236db84226`.
stdout is byte-identical; stderr empty; exit code file hash `9a271f2a…86aa`.

**The XNU reboot was issued, not observed as a successful hardware boot.**
Hello has not been confirmed; the user's screen report is awaited. No logs from
the new XNU were read and no post-boot device access was performed.

## Boundaries

- Only `boot_a` was written, once, with fsync; `boot_b` is unchanged.
- Slot was switched B → A; no extra Android boot was introduced.
- No raw serial was printed; only the existing device tag is recorded.
- No production source, build, package, installer, test or static audit artifact was changed.
- No runtime fix, Hello success or speed improvement is claimed.

## Evidence

| Artifact | SHA256 |
|---|---|
| `RESULT-nova.json` | `d291204be7adce68c9e229e8d0823828698de199d94263e01233ebb52a97be29` |
| `install-nova/phase-nova.json` | `f3d6ab8c4fd914fda13a2897afbbd4f6c9730b8696712e01cca9719a5b602b25` |
| `install-nova/stdout-nova.json` | `f3d6ab8c4fd914fda13a2897afbbd4f6c9730b8696712e01cca9719a5b602b25` |
| `install-nova/stderr-nova.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `install-nova/exit-code-nova.txt` | `9a271f2a916b0b6ee6cecb2426f0b3206ef074578be55d9bc94f6f3fe3ab86aa` |
| `start-nova/phase-nova.json` | `084b6196bb3e62c424173402b4558084734017c48db27ae3693ade236db84226` |
| `start-nova/stdout-nova.json` | `084b6196bb3e62c424173402b4558084734017c48db27ae3693ade236db84226` |
| `start-nova/stderr-nova.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `start-nova/exit-code-nova.txt` | `9a271f2a916b0b6ee6cecb2426f0b3206ef074578be55d9bc94f6f3fe3ab86aa` |
| `check-nova.json` | `d4075f9c7263c0271de2fc3996448c255e218b428e57cec910a2f84a2d604dd5` |
| `manifest-authorization-diff-nova.patch` | `9205de8dd3bbcfde7dd1e1f66d73faf2369ea400b2d32df1182062e3c05eb0b9` |
| `delivery-manifest-nova.json` (authorized) | `5e49bfb03ae660a3ef28a26aca0ddc8827860ca3d3ff24eb2a3c5763221b3a23` |
| `delivery-manifest-nova.json` (before edit backup) | `e006269f7ef1f0c13ec7c11ae6b4d0afa96e0d12448aa6b21cd9626f5a42674d` |

Next: user reports the screen/Hello state. No further device action is taken
until then.
