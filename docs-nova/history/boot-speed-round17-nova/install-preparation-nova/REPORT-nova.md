# R17 install preparation — Nova

Status: **PREPARATION_COMPLETE_AWAIT_SOL_INSTALLER_REVIEW_AND_DISPATCH**
Gate: installation_allowed=false; Sol review + separate execution dispatch required.
Model: `deepseek-official` / `deepseek-flash` / reasoning `high`; no delegation, no fallback.
Device operations: read-only `check` only. No install/start/flash/reboot/slot/mount/partition/export write.

## Exact target and expected state
- Sole target: `[redacted-device-serial]` (TAG only in logs; exact-serial selector unchanged).
- Accepted R17 local acceptance: `5b708840e054b2ac6a93f4a2610a4a31c6d487ff7c9132def643b01030d8a6ea` (status `LOCAL_LOGGING_REGRESSION_FIX_ACCEPTED_AWAIT_NEW_USER_INSTALL_COMMAND`), all 12 bindings verified actual files.
- Candidate image: `8796d6c5824ce563fda8b4d0e1a429f235749254648cc160534e5807283338d6`, 100663296 bytes (96 MiB).
- Expected boot_a (R16): `51a0248347f0e70644a28d30f932ce59245ada3920ade777ac5835578e30bf51`.
- Expected boot_b: `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2`.
- Pinned R8 warm export: `7e6cc19843c0ee28eb8f1891a7d53e074688743902ef9ccf01b20d1a48e57140`.

## Installer (new R17 root file, five substitutions only)
- New installer: `f7c11491e384efcd51691983ec6fb7e9e6429d473a2c43d4821b74429c3264c7`, 10588 bytes.
- R16 source (immutable): `26a2e29e789555ce81a3eeb6efd6e1da3bd63911b78d4befbd4036ebda192951`.
- Full diff: `installer-diff-vs-r16-nova.patch` `eba8fe2834dcc536c5d2329894a4bc82568ce412bd75655e0dd09a275d6bd315`.
- Reverse substitution restores R16 bytes: **True**.
- Functions byte-identical: **13/13**, changed `[]`; `select()` and exact-target constant byte-identical.
- Only changed lines: docstring round, LOG, STAGE_DIR, BASE_A R15→R16, CANDIDATE R16→R17.

## Tests (actual installer code, mocked transport, no device access)
- Six transport tests: **PASS 6/6**, exit `0`, 0.122s, device_accessed=False.
  Result `fc77439a1e9494f7af19e4a37c9fbd8956bb1cf44ac4d2b99246b5ef91404f17`; test source is byte-identical to R16 `1d561115…`.
- Nine selector tests: **PASS 9/9**, exit `0`, 0.091s, device_accessed=False.
  Result `d547dcc94addf127472c6a01ada1673561c1ea9307e84f7425984b714bf09f90`; source `aedf3ff4a25c8af40afadad1e284a6cda893e1027d9bd26116e9f6c45f175099` with only docstring/import-label substitutions.

## Fresh read-only check (exact target)
`device-run-nova.py check` exit `0`, 0.809s:
- Android `_b`, root available; boot_a `51a0248347f0e70644a28d30f932ce59245ada3920ade777ac5835578e30bf51`, boot_b `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2`.
- Both boot partitions 96 MiB; boot_a read-only `False`; dd fsync `True`.
- `device_mutated=false`, no actions, no serial printed.
- **Not checked here:** GPT and warm export (they run only inside install()/start()); no claim otherwise.

## Manifest (gate closed)
- `artifacts-nova/boot-speed-round17-nova/delivery-manifest-nova.json` `45d735a6c61e046bb8d4b1900819be75e5ac112eeec4dbf365515193c96971b4`, 32156 bytes.
- Status `AWAIT_SOL_INSTALLER_REVIEW_AND_DISPATCH`; top-level and authorization `installation_allowed=false`; requires Sol installer review and a separate execution dispatch.
- 29 source hashes (only sleh `a05b515c…` and mach_msg `3fed44b5…` updated) validated against R17 freeze and current tree; `warm_source` is an exact R16 object copy.
- 43 named artifact bindings include every accepted R17 binding, the acceptance itself, pack verification/PAN, installer/tests/results/check evidence.

## Verification
`verification-result-nova.json` `4f2783268968bb7fe1e51a7cbfee5c9994adb63a164e6723e12bb0720796abb5`: 60/60 checks pass, 0 failed.

## Mutations
None. Partitions written: none. Slots changed: no. Reboots: 0. Install/start/export phases: not run. R16 installer/manifest and historical build/package/acceptance flags unchanged.

## Next gate
Sol installer review, then a separate execution dispatch. Stop here; no user input requested.
