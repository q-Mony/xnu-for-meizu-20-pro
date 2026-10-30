# R18 installer preparation — gate closed for Sol review

New file; no previous version.

- Status: `AWAIT_SOL_INSTALLER_REVIEW_AND_DISPATCH`
- Latest user command: `安装`; scope: exact R18 install/start only (not executed here)
- Target: `[redacted-device-serial]` only; unrelated devices ignored, enumerate-only
- Candidate: `<nova-root>/artifacts-nova/boot-speed-round18-nova/build-package-nova/package/boot-warm-r18-streamsha-candidate-nova/boot-warm-r18-streamsha-candidate-nova.img` (`be40edfedadbc161e3e86864c9d8803243c34426f78007f983213c07c879c21b`, 100663296 bytes)
- Config: `session-575b3e82-ffbd-4071-ac5a-772c07d1adea` / `deepseek-official` / `deepseek-flash` / `high`; no delegation/fallback

## Installer
- R18 root `device-run-nova.py`: `7c1d5687aa92d2ee1d59d3e9936a637066845774394131f7071a85c33286b0b6` (10588 bytes)
- R17 template: `f7c11491e384efcd51691983ec6fb7e9e6429d473a2c43d4821b74429c3264c7`
- Five anchored substitutions only; 13/13 functions byte-identical, `select()` byte-identical,
  reverse substitution restores exact R17 bytes; diff 5 removed / 5 added lines.
- Preserved: target `[redacted-device-serial]`, `-s`, B `7df205f1…`, export `7e6cc198…`.
- Proof: `<nova-root>/artifacts-nova/boot-speed-round18-nova/install-preparation-nova/installer-mechanical-proof-nova.json` (`dace069538a9b6d0278b8eedd144cf1d463c957389585bfc7a6663c7053197fc`)
- Diff: `<nova-root>/artifacts-nova/boot-speed-round18-nova/install-preparation-nova/installer-diff-vs-r17-nova.patch` (`01e6f14f5c05c7ea66a719d1a0bcfe17201ae4ccc5db0db06f4ffbff28f35b4a`)

## Tests (actual installer, mocked transport)
- Transport: 6/6 PASS, exit 0; `f6cce1b0a78c98176e37e10306d979b8391b9a6ce6c4c41ae59e472fa2e9b19c`
- Selector: 9/9 PASS, exit 0; `da9752ba1e8fc932a09c229964e491c17247db1a56ee87341cf164169a5d40b3`
- Total 15; no USB access in either test.

## Source
- Entire accepted source map copied: 6186/6186 current files rehashed; 0 mismatches.
- Freeze: `77f483d631ad403930c2631df0a324754d88893046caffbaa8d4db15cd9355fc`; exact copy verified.

## Fresh read-only check
- `READONLY_DIRECT_WRITE_PRECONDITIONS_CHECKED`; Android B; boot_a `8796d6c5824ce563fda8b4d0e1a429f235749254648cc160534e5807283338d6`; boot_b `7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2`
- 96 MiB geometry asserted; boot_a read-only capability false; dd fsync true.
- `device_mutated=false`, `actions=[]`; no install/start/staging; GPT/export not claimed here.
- Record: `<nova-root>/logs-nova/boot-speed-round18-nova/check-device-nova.json` (`4495bb4bee373fb22549c253a5302a71ddf665d4f1c33ea9ae9a5f87eaf212bc`)

## Gate / artifacts
- Manifest: `<nova-root>/artifacts-nova/boot-speed-round18-nova/delivery-manifest-nova.json` (`a5c51028f0a0195cd804a8ba7834de6532a323a7816536b6a630f3af998756f8`, 968081 bytes)
- Verification: `<nova-root>/artifacts-nova/boot-speed-round18-nova/install-preparation-nova/verification-result-nova.json` (`bff850feb3f0d54a0292eb68b9f99e815a9b68ebc6cddc7b5c6d0d43a5e56820`; 66 checks PASS)
- SOL final: `63f5e78acca1d0b9d2a49117299a5b0221fe1aba72fedb0a66586647219d747d`; build: `6bbe1579a727c85fdbfa3c523107fb14d189b18756d87df4d811120d9927f56f`
- Package validation: `ba221226f48a4c1672ed4a29a19962585cae2d1ca8caa2b5f9f5451ac8556e71`
- Warm acceptance: `eb6e4bb529d01c03489fd70ceac14122684fe9f788f828c5cc677bb129cad38a`
- `installation_allowed=false`; pending `sol_installer_review`, `separate_execution_dispatch`.
- No production/build/package/R17/global-config modifications; no device mutation.

## Limits
Mocked tests do not prove hardware write success. Static preparation does not prove Hello,
runtime behavior or speed. Check does not validate GPT or warm export; install/start will.
