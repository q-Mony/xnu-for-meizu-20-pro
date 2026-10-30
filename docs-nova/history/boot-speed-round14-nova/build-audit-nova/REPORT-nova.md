# R14 build, package and static audit

Status: `BUILT_PACKAGED_LINK_AND_INSTRUCTION_AUDITS_PASS_INSTALLATION_DISABLED`

Scope: Nova only. Full R14 kernel build, kernel-only package and static
linked/control-flow audits. No USB/adb/fastboot, install, reboot, slot change,
persistent image edit, installer or device access.

## Authorization and source scope

- Sol acceptance: `artifacts-nova/boot-speed-round14-nova/marker-compat-nova/SOL-ACCEPTANCE-nova.json`
  SHA-256 `26c0b229de67b59757a97df012fb47d259031a69efc57a19ed76c504f989852a`.
- Authorized exact sources:
  - `unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c`
    `60b0dced3d3a44bfabd490d623b0eccfe435a1a2dc87a120efb4deef08d4e646`
  - `unixnu-meizu20pro-nova/package/tests/WatchdogHostTests.c`
    `a1276115b8da111edc6ff1a18438ede63ab78441388d86814166ca3f3f9d56e0`
- Authorization file SHA-256 `e5b4d1503defc6cf50ba2187d5f2b32118aaf10469a5decfb64547b4b9acb62d`.
- Pinned R11 inventory: 6,186 files, only the two authorized files differ.
- Source freeze: `r14-source-freeze-nova.json` SHA-256
  `c122a21fa77a372211d03a8f1f70d8d8991bc7ece35328a489119db3c2d34923`.
- Reverse bridge check passed; input verification:
  `input-verification-nova.json`.

## Build

- Wrapper: `run-build-nova.py` SHA-256
  `aa95787dc133cd5ff5c0fa8f50ccc0f036fa1be33c085787ef5e18e6d4a8ad06`.
- Command: `./unixnu-nova build kernel -j 8`, exit `0`.
- Result: `build-20261030T055320628543Z-nova/build-result-nova.json`
  SHA-256 `b443583d7a27ffa7c563a761a8a044ddb172714238b6cb931a1e36dd096bd959`.
- Log: `logs-nova/boot-speed-round14-nova/build-20261030T055320628543Z-nova.log`.
- Raw kernel: `kernel-raw-nova.macho`,
  SHA-256 `ea94322cf5540e8778b27ea39a7b2971f568d2097a920ad0a508d0cbbc1a9246`,
  73,932,800 bytes.
- `r11_frozen_objects_pass=true`; `kern_exec_exact_r11=true`.
  BootLog/RamBlock/Ufs objects and archive members equal R11.
- Reviewed narrow dSYM shim was pre-bound on PATH:
  `build-helpers-nova/bash`, SHA-256
  `7bf8733a9ae09c886d180b17d49919138c1bcff191063b2b92549c8f7fc3d169`.
  It removes only the dsymutil stderr process-substitution wrapper; sources and
  link flags are unchanged. The build passed on the first attempt.
- `/bin/ps` is unavailable in this sandbox; the reviewed `pgrep` fallback was
  used for competing-process detection.

## Package

- Packer: `packaging-nova/pack-watchdog-r14-candidate-nova.py` SHA-256
  `5db170c1bbd2d9c602a5690052b0f5aeee00a95e0769bc6ce0e7619f9990e381`.
- Verification: `packaging-nova/watchdog-r14-candidate-final-nova/evidence-nova/verification-nova.json`,
  SHA-256 `036d3dee4a7fc3f506a155b4f1ef88ba110d54fe4e672cc9c2f16920b3810ed6`,
  status `PASS`.
- Image: `boot-warm-watchdog-r14-candidate-nova.img`,
  SHA-256 `6ea4078227980d8c1f0a98982fa6dd37e5a91b251384674dca53699b0f264641`,
  **100,663,296 bytes (96 MiB)**.
- Payload kernel SHA-256
  `3d67f555f00f14ac99696b4e74ea9d396dcb8a660bc23eb9d67cd43139e7449d`.
- R11 envelope preserved except `UniXNU/Payloads/kernel.macho`; exactly one
  entry patch applied and PAN verified.
- Firmware/header/DTB/AVB bytes, non-kernel EFI, TOML/bootargs and 512 MiB warm
  data inputs are byte-identical to the pinned R11 envelope.
- Packer now explicitly requires the build's `r11_frozen_objects_pass` and
  `kern_exec_exact_r11` in addition to the actual build checks.
- Installation is explicitly disabled: `installation_allowed=false`,
  `install_authorized=false`, `do_not_flash=true`, no device manifest.

## Linked audit

- Report: `link-audit-20261030T055706094634Z-nova/audit-nova.json`,
  status `PASS_WATCHDOG_STATIC_BINDING`.
- Exactly three Policy functions changed from R13:
  `_watchdog_warm_policy`, `_watchdog_warm_file`, `_watchdog_verify_existing`.
- The other four Policy bodies are byte-identical to R13; the cold entry is
  unchanged.
- 33 preserved warm functions and 43 selected raw/payload functions are bound
  through object/archive -> raw -> payload modulo individually recorded
  relocation words.
- Policy archive/object equality, actual force-load inputs, worker/vtable/
  `load_init` 94-byte hook, R11 caller evidence and the no-existing-file-mutation
  call check all pass.
- Source contract binds the approved R14 source, including three computations of
  the exact `!created && 99:99` predicate, EEXIST gating, new-create
  compatibility=false, plist gid0 and gated marker gid99.

## Instruction audit

- Report: `postlink-instructions-20261030T060144723544Z-nova/audit-nova.json`,
  status `PASS_R14_FINAL_INSTRUCTION_AUDIT`.
- 218 exact instruction-word checks, 45 branch assertions with condition codes
  and targets, 13 literal bindings, and all 36 pinned R11 caller checks retained.
- Reviewed R14 offsets/words cover:
  - exact directory `040755`, original 501:20 / 0:0 / created 0:20 tuples, and
    existing-only 99:99 context computed after validation;
  - `warm_file` EEXIST + `is_marker && allow` gate, create/read nofollow flags,
    and new-create verification receiving compatibility=false;
  - regular/uid0/0644/nlink1/size0 metadata, gid99 only behind the allow gate,
    gid0/gid20 and plist gid0 unchanged;
  - device mismatch rejection before the read open; fd/path identity and EOF
    checks remain reachable on accepted gid99;
  - metadata-reject argument registers/stack slots and literal, plus the success
    printf after verification and close success only;
  - close/error precedence.
- All changed-function object/raw/payload disassemblies and the manual rationale
  are in `postlink-instructions-20261030T060144723544Z-nova/`.

## Tooling retries

Build and package passed on the first attempt. Two audit-tool expectations were
corrected before the passing audits; neither changed source, build, package or
predicate scope:

- the first linked-audit run expected the rendered marker `uid=0` instead of the
  object's `uid=%u` format literal; the corrected run passed
  (`link-audit-20261030T055646404208Z-nova/audit-nova.json` records the blocked
  first attempt);
- the instruction audit's first two runs corrected one internal tail-target
  expectation and one size-reject target before the passing report. No source or
  binary was rebuilt.

## Preserved references

R11 final `790ad42c…`, R11 build `d23c1817…`, R12 build `0e95e131…`,
R13 final `e0c59302…`, R13 build `26e40b85…`, R13 Policy object
`d2ef4557…`. No R11/R12/R13 output was modified.

## Limits

Static evidence does not prove hardware boot, Hello, runtime directory ownership
or speed improvement. The success log can precede a later directory-close
failure and does not imply final boot success. APFS origin of 99:99 and the
marker dev/ino remain unproven. The phone remains last verified on Android B and
was untouched. No installer was created or executed; installation requires a
new explicit user command and Sol review.
