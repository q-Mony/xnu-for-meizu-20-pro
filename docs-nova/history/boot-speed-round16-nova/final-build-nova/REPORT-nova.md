# R16 final local formatter correction + build/package (new file, no previous version)

Diagnostic only. This is not an original-root-cause fix, not a speed claim, and
no device/install/reboot/slot/mount/config/network action was taken. The device
was not accessed and remains Android B.

## Source change (only one formatter block)

Pre-edit assertion passed before any edit:
- sleh.c `a1220ca9b616bb8f2be650a2600c827181a5bb6782b9b31347b6ac751a51da74`
- mach_msg.c `f8758befcf7d79318c0e777adb632423816bea9a9012b39d14b7912475afb2e9`

`osfmk/arm64/sleh.c` now formats `sbfault-regs` in a `char line[1024]` on the
existing async emission worker: one bounded `snprintf` header, 29 checked
bounded register fields, a `regs_trunc` tail, and exactly one
`printf("%s", line)`. No allocation; fault/copy/lifecycle logic is byte-identical
outside this precise block (reconstruction audit vs. the pre-edit backup passed).
`mach_msg.c` is unchanged. Post-edit sleh.c
`17db32f399e7e527b7811ad88e606fe61507d28c13355b378ef6b031fd23d7af`.

Backup: `snapshot-nova/boot-speed-round16-nova/final-build-nova/before-formatter/`
(with `SHA256SUMS.txt`).

## Host tests (observation logic only; not ASTC/boot proof)

- Existing production extracted-core harness `probe-review-fix-nova/extract-and-test-nova.py`:
  **PASS**, exit 0.
- New `test-formatter-nova.py` extracts the actual `NOVA_SB_FMT_CORE` helper:
  **PASS**, max 29 `ffffffffffffffff` fields, normal values, `regs_trunc=0/1`,
  no overflow, small-buffer truncation; ASan/UBSan clean.
- New `audit-formatter-nova.py`: **PASS**, outside-formatter byte identity plus
  single-emit/1024-buffer/no-allocation checks.
- New `parser-schema-nova.py`: **PASS**, full actual output including
  `regs_trunc`, `sbfault-x19raw`, and actual v16 lifecycle keys/tokens/lock_drops.
  The historical parser/results were not modified.

## Actual ARM64 TU and full build

TU replays changed only `-o`; `-g` was preserved. Both objects exactly match the
full-build objects:

| TU | exit | object sha256 |
|---|---|---|
| sleh | 0 | `f22235307889a7f382dace60920106418290b58e8c7ba2cc2f5d22971697d2b9` |
| mach_msg | 0 | `5bd73b91fb39f8a6266667a57afb5957c638625c52d25744f4ddaa4852154019` |

Source inventory: **6186/6186 hashes verified**, exactly `osfmk/arm64/sleh.c` and
`osfmk/ipc/mach_msg.c` differ from the R15 baseline. R16 freeze
`source-freeze-r16-nova.json`
`f578ed91caf078f93cb27826aed0ae2a3f9bf4b56dfd55acbd06fefef256a60a`.

Full `./unixnu-nova build kernel -j8` with reviewed PATH shim
`7bf8733a9ae09c886d180b17d49919138c1bcff191063b2b92549c8f7fc3d169`:
**exit 0**, log `8a16754bef3f27185dd78325c6fc287542c4f6ad79659da8152df314fbb6df8c`.
Raw kernel `5f25a1a62127a69981d91c609985ad3a48b153c0c85be4839646f0139c952de0`
(73,949,184 bytes). Protected BootWatchdogPolicy/BootLog/NativeRamBlock/NativeUfs/
kern_exec/corecrypto_sha2 objects were unchanged (prebuild hashes preserved).

## Package (read-only helper copied/adapted)

Accepted R14 envelope `6ea40782…` was used and independently checked against the
accepted R15 image `53470de0…`: non-kernel EFI files and all non-SHA1 envelope
bytes are equal. Only `EFI/UniXNU/Payloads/kernel.macho` changed. Entry patch was
applied to a copied payload; the pre-patch raw copy is preserved. PAN, DTB, AVB,
TOML and Android v0 SHA1 checks passed.

- Image: `package/boot-warm-r16-diagnostic-candidate-nova/boot-warm-r16-diagnostic-candidate-nova.img`
  `51a0248347f0e70644a28d30f932ce59245ada3920ade777ac5835578e30bf51` (96 MiB)
- Payload `c445b47b7667eaa8130e15a4c1f9f6e5cf52a24910b87a6c94c9e652695bdfe6`;
  raw before patch `5f25a1a6…`; manifest `902a04e7…`
- Markers present: `nova: life v=16`, `meizu: sbfault-regs`, preserved
  `nova: sblong v=15`.
- Status `BUILT_AWAIT_NEW_USER_INSTALL_COMMAND`; `installation_allowed=false`,
  `install_authorized=false`, `do_not_flash=true`, `hardware_tested=false`,
  `diagnostic_only=true`; no installer.

## Backups / config / claims

Backups: `snapshot-nova/boot-speed-round16-nova/final-build-nova/before-formatter/`,
`final-build-nova/prebuild/` (sleh.o, mach_msg.o, kernel.macho, protected-object
hashes), and the CHANGELOG backup created immediately before its single append.
Model config: fixed `deepseek-official/deepseek-flash`, reasoning `high`; no
delegation or fallback.

No original-root-cause fix is claimed, no speed improvement is measured or
claimed, and no device action occurred.
