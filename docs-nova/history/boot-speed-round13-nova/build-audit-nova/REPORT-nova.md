# R13 full kernel build, package and static audit report

New file, no previous version. Scope: freeze and full build of the two accepted
R13 source hashes, package only `kernel.macho` into the pinned R11 envelope, and
run the R13 link/object/raw/payload and post-link instruction audits. Sol owns
planning and acceptance; this execution was performed with
`deepseek-official/deepseek-flash`, `reasoningEffort=high`.

**Result: static build/package/link/instruction work PASS; hardware NOT tested;
no device was accessed; no installation is authorized or claimed.**

## 1. Authorization and source scope

Accepted source/local validation:
`artifacts-nova/boot-speed-round13-nova/owner-compat-nova/SOL-ACCEPTANCE-nova.json`
SHA256 `e0d83ea4735463f594206b6ff4042060e4975efcd238510e2dbdd6e8a88ad150`.
The accepted 798 ASan/UBSan function tests and ARM64 target object were not rerun.

The dispatch authorization was copied byte-for-byte to
`build-audit-nova/authorized-source-sha256-nova.json`
SHA256 `7a7013d0dac512c4edfd00203909d02a19a4b5690e221e980314c0a8214c104d`.
It contains exactly:

| Source | SHA256 |
|---|---|
| `unixnu-meizu20pro-nova/package/BootWatchdogPolicy.c` | `6e62ebfe69ec450070a48ff71d7248cefb13e3af924ba338e0fd12d1f0c32c5c` |
| `unixnu-meizu20pro-nova/package/tests/WatchdogHostTests.c` | `9dd5928a3775acd8d4b486b1c67feb367cfd652e361056cd2d8641d8a2e9b467` |

The exact pinned R11 baseline was copied unchanged:
`build-audit-nova/r11-source-baseline-nova.json`
SHA256 `5ef4ad828cb0c777f11407e0a37011888bbebdcb150dcf12d6af905d9f391472`,
6,186 files. The R11 final/build pins and R11/R12 outputs were preserved.

Input verification: `build-audit-nova/input-verification-nova.json`
SHA256 `acedb760deb8c88958f981a8b4e08f423949f1b046142a1d5fb6d6150369937a`.
It records `PASS_INPUT_SCOPE_AND_REVERSE_PATCH`: 6,186 monitored files, only the
two authorized files changed, and the checked kern-exec bridge reverse patch
check returned 0. Freeze record:
`build-audit-nova/r13-source-freeze-nova.json`
SHA256 `4a329474e3dbfe49e251d497e8436346090c099bf890f7a761b8c32b626e95ed`.

## 2. Full kernel build

Build result:
`artifacts-nova/boot-speed-round13-nova/build-audit-nova/build-20261030T050551303627Z-nova/build-result-nova.json`
SHA256 `26e40b85583e7beb6886fa2ea59ffa487fb57d96ba6d567f68077fc71371699f`,
status `BUILD_PASS_LINK_AUDIT_PENDING`, exit 0, source stable before/after.
Build log:
`logs-nova/boot-speed-round13-nova/build-20261030T050551303627Z-nova.log`
SHA256 `00cd352c7adbf13fff54b4f5a3e24067fd9392ef8f4fc5e7b27dc61de07ce915`.
Raw kernel:
`build-20261030T050551303627Z-nova/kernel-raw-nova.macho`
SHA256 `6b5c670cbe519701ec2d98c4185227aeff45379520de04939a2f73e38320e60a`,
73,932,800 bytes.

Build wrapper: `build-audit-nova/run-build-nova.py`
SHA256 `f775a812f2148f784f1f3e78682c0fe2e02614335c227f78c471a05136db8c6a`.
Frozen object checks passed against the pinned R11 build:

- `kern_exec.o` (and its `.o.json`), `BootLog.o`, `NativeRamBlock.o`,
  `NativeUfs.o`, `ufs-original-bindings.o` are byte-identical to R11;
- the BootLog/RamBlock/Ufs archives have exactly the R11 member lists and
  member bytes (archive container timestamps are ignored only for the container
  comparison);
- `kern_exec_exact_r11 = true`, `r11_frozen_objects_pass = true`.

Policy object: `d2ef4557cc2ca5c94744bd28296e466d093ac6ab0261357680b32a4f5204083d`.
Policy archive: `17e2307de0c48cfd48d217587191c3a621af4574d36cb25485caed69f0bcf94c`.

### Build-environment disclosure

The first authorized attempt
(`build-20261030T050350905142Z-nova`, result SHA256
`bebd7e6b1c041a3d39a95ff3601a2fab32c586eaf347aa469c49dd19958bb744`)
reached link and then failed because this sandbox denies opening `/dev/fd` for
the pinned dSYM recipe's `2> >(grep ...)` process substitution. A narrow shim,
`build-audit-nova/build-helpers-nova/bash`
SHA256 `7bf8733a9ae09c886d180b17d49919138c1bcff191063b2b92549c8f7fc3d169`,
removes only that stderr process-substitution wrapper for the dSYM command;
`dsymutil` still runs with identical input, flags and output. No source, compile
or link recipe was changed. Separately, `/bin/ps` is EPERM in this sandbox, so
the wrapper checks the same competing-build signatures with `pgrep`
(`process_inspection = pgrep_fallback_ps_eperm`). Both adaptations are recorded
in `final-result-nova.json`.

## 3. Package

Package verification:
`artifacts-nova/boot-speed-round13-nova/packaging-nova/watchdog-r13-candidate-final-nova/evidence-nova/verification-nova.json`
SHA256 `3278f4b131b34a2f89f271bceeb26c004da6a4ce6d9105c6d9e1d3bb4da38ad4`,
status `PASS`.

- Boot image:
  `watchdog-r13-candidate-final-nova/boot-warm-watchdog-r13-candidate-nova.img`
  SHA256 `9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2`,
  exactly 100,663,296 bytes (96 MiB), FAT payload 94,371,840 bytes.
- Kernel payload:
  `kernel-payload-nova.macho`
  SHA256 `8c24bc84d5f030633276547cf396fcb5d5fec21a0f5d02a5c7a7b1020fc75cfa`.
- Changed EFI file: only `UniXNU/Payloads/kernel.macho`.
- All non-kernel EFI files, `unixnu.toml`/bootargs, firmware/header/DTB and AVB
  regions are byte-identical to the pinned R11 envelope. The 96 MiB boot
  envelope contains no Data partition and no Data bytes were written; the
  packer's inherited 512 MiB logical-Data bookkeeping is retained. Warm Data
  exactness on a device is not verified by this static task.
- One entry patch applied; PAN check passed; no new AVB signature was generated.
- Packer:
  `packaging-nova/pack-watchdog-r13-candidate-nova.py`
  SHA256 `5ec2a7651083a673ec76bf439497521749d16453248131348825ce2047e58a1b`.

## 4. Link / object / raw / payload audit

Report:
`build-audit-nova/link-audit-20261030T050801108226Z-nova/audit-nova.json`
SHA256 `eb33778ef462438047a3d40a4194b227692754cd8ee9b7742ac0175433af15af`,
status `PASS_WATCHDOG_STATIC_BINDING`.

- 3 diagnostic caller objects and 7 Policy text functions bind to raw modulo
  individually listed relocations; the changed function also binds to payload.
- 33 preserved warm-path functions and 43 selected raw/payload functions pass.
- The exact inherited R11 `load_init_program` checkpoint hook is revalidated:
  94-byte stub, displaced instruction replay, `_native_blog_stage` target,
  rejoin to caller+4, and unchanged policy call sites.
- Only `_watchdog_warm_policy` changed in the Policy object; the other six text
  bodies (`_meizu_boot_watchdog_policy`, `_meizu_watchdog_policy_diagnostic`,
  `_watchdog_stat`, `_watchdog_stat_at`, `_watchdog_verify_existing`,
  `_watchdog_warm_file`) are byte-identical to R12. No file policy was relaxed.
- R13 source/literal contract: exact predicate
  `!created_directory && parent.st_uid == 99 && parent.st_gid == 99` appears
  twice, no metadata-normalization source text, and the unique marker
  `nova: policy-owner-compat version=13 anonymous_existing=1 uid=99 gid=99` is
  present once in raw.

## 5. Post-link instruction audit

Report:
`build-audit-nova/postlink-instructions-20261030T051011578113Z-nova/audit-nova.json`
SHA256 `ce8a52323472d23c9af4cc3c3b052103dda79e0bbf919e1e60b769e6735411e8`,
status `PASS_R13_FINAL_INSTRUCTION_AUDIT`.

108 instruction checks, 18 function-relative branch assertions, 7 literal
address bindings, including the 36 inherited R11 caller checks. The reviewed
R13 warm-policy body is 664 bytes at link address `0xfffffff00895acf0`.
The new assertions cover, from the actually inspected object/raw/payload
disassembly:

- exact type+mode `S_IFDIR|0755` (`cmp 0x41ed`) with bad mode/type branching to
  the EINVAL close path;
- the original 501:20, 0:0 and this-boot-created 0:20 acceptance paths;
- exact uid 99, then `mkdir_errno != 0` (created-directory rejection), then gid
  99, i.e. the 99:99 tuple is existing-only;
- bad-tuple EINVAL default and the shared close/restore-diagnostic/return path;
- marker ADRP/ADD x0 binding to the unique literal, `BL _printf`, and
  fallthrough to the accepted warm-file stage, not the error path;
- unchanged file/cold/open assertions (byte-identical bodies, same
  function-relative offsets).

Manual control-flow rationale:
`postlink-instructions-20261030T051011578113Z-nova/manual-control-flow-nova.md`
SHA256 `e30d86e8b8ef19ecd2a3cc1ff0c8d900e442edfeb1b2f08af0d74c96033678f3`.
Disassemblies in that directory:
`warm-policy-object-nova.disassembly.txt`,
`warm-policy-raw-nova.disassembly.txt`,
`warm-policy-payload-nova.disassembly.txt`,
`verify-existing-raw-nova.disassembly.txt`,
`meizu-boot-watchdog-policy-raw-nova.disassembly.txt`,
`warm-file-raw-nova.disassembly.txt`.

## 6. Static validation versus hardware

| Class | Result |
|---|---|
| Source scope, exact two hashes, reverse patch check | PASS (static) |
| Full kernel build and frozen R11 object/member checks | PASS (static) |
| Package envelope, kernel-only EFI replacement, PAN, image | PASS (static) |
| Object → raw → payload link binding | PASS (static) |
| Post-link instruction control-flow review | PASS (static) |
| Hardware boot / Hello / runtime owner tuple | NOT TESTED |
| Speed improvement | NOT MEASURED |
| Device access (USB/ADB/Fastboot), flash, slot change, reboot | NONE |

This report creates no device authorization and does not claim installation.
A new explicit user install command is required.

## 7. Limitations

- Static evidence does not prove hardware boot, Hello, runtime success, or
  speed. The APFS origin of the measured 99:99 tuple remains unproven.
- R13 only accepts the exact existing 99:99 directory tuple after exact
  040755 type/mode; existing file content/identity checks are unchanged.
- The inherited prototype AVB metadata is preserved; no new authenticated AVB
  signature was generated.
- The dSYM-only shell shim and `pgrep` process-inspection fallback are build
  environment adaptations, disclosed above; the compiled kernel inputs and
  recipes are unchanged.
- Audit revisions were made only from inspected object/raw instructions after
  transient assertion failures. The final scripts, the BLOCKED link-audit
  attempt, the failed build result/log, and snapshots under
  `snapshot-nova/boot-speed-round13-nova/build-audit-revisions-nova/` are
  preserved. Pre-revision copies of the brand-new scripts were not all captured
  before the first edits; this is disclosed rather than hidden.

## 8. Deliverables

| Artifact | Path | SHA256 |
|---|---|---|
| Final result | `artifacts-nova/boot-speed-round13-nova/build-audit-nova/final-result-nova.json` | `e0c593022bae0359312bd160b9da732fb70764a8ff221aac0573c1d8a518ff0e` |
| Build result | `build-audit-nova/build-20261030T050551303627Z-nova/build-result-nova.json` | `26e40b85583e7beb6886fa2ea59ffa487fb57d96ba6d567f68077fc71371699f` |
| Package verification | `packaging-nova/watchdog-r13-candidate-final-nova/evidence-nova/verification-nova.json` | `3278f4b131b34a2f89f271bceeb26c004da6a4ce6d9105c6d9e1d3bb4da38ad4` |
| Link audit | `build-audit-nova/link-audit-20261030T050801108226Z-nova/audit-nova.json` | `eb33778ef462438047a3d40a4194b227692754cd8ee9b7742ac0175433af15af` |
| Instruction audit | `build-audit-nova/postlink-instructions-20261030T051011578113Z-nova/audit-nova.json` | `ce8a52323472d23c9af4cc3c3b052103dda79e0bbf919e1e60b769e6735411e8` |
| Boot image | `packaging-nova/watchdog-r13-candidate-final-nova/boot-warm-watchdog-r13-candidate-nova.img` | `9e24298c84c52f4447fd735cd4be0775d8633564bb01b9a59648cf064a21e8f2` |

No production source file was modified by this task. Old R11/R12 outputs were
not replaced.
