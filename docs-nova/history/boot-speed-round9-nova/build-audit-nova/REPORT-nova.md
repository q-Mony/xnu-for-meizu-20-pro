# R9 warm-load build and link audit — Nova

New report; no previous version. This directory is owned by the build/audit subagent. Production source, packaging, central documentation and device operations remain owned by their assigned agents.

## Final result

Final status: `PASS_WARM_STATIC_BINDING`. Machine-readable result: `link-audit-20261029T211226175413Z-nova/audit-nova.json`.

Full `./unixnu-nova build kernel -j 8` passed in 44.894 seconds. Source hashes before and after were identical. The source scope contains exactly the two driver changes and two added headers authorized for R9:

- `NativeRamBlock.cpp`
- `NativeUfs.cpp`
- `WarmLoad.h`
- `WarmLoadContract.h`

The inventory has 6,186 entries versus 6,184 at R9 preparation. No unexpected changes were found. The scope covers tracked/unignored main and nested driver/kit inputs, actual XNU inputs, board source/configuration, the pinned generated Data header and wrapper; it is not an assertion about every file on the machine. The 22 named R8 source/configuration hashes are independently pinned to the successful R8 build. R8 raw kernel and formal driver objects/archives/recipes were copied before R9 build.

Build provenance: `build-20261029T210847465806Z-nova/build-result-nova.json`.

Independent raw artifact: `build-20261029T210847465806Z-nova/kernel-raw-nova.macho`.

- SHA256: `e22dc2076504cb5f59e84bbb51e0ed5a9b7e189fdb12d81431f1a88adcac83c5`
- Size: 73,928,704 bytes.
- R8 `kern_exec.o` remains byte-identical; evidence: `preserved-r8-object-check-nova.json`.

## Raw binary findings

`link-audit-20261029T211041117985Z-nova/audit-nova.json` reports `PASS_RAW_WARM_STATIC_BINDING`.

The two formal archives contain exactly the preserved formal objects. Their compile recipes point to the frozen Nova source and headers, target ARM64, use the required production build guards and enable no warm host-test macro. Actual XNU link input files include both driver archives. Thirty-three selected driver functions match their object bodies after excluding individually recorded relocation words; linked call targets and worker arguments are checked separately.

The linked start virtual-table entries, two `kernel_thread_start` worker addresses, RamBlock worker→load→source/read/SHA path, UFS start→startRoot→source decision, UFS publisher→Data media gate and durable stats chain are present at reachable direct call sites. The two asynchronous worker addresses are resolved from actual x0 ADRP/ADD setup, with only explicitly recognized x1/x2 setup instructions before the call. This does not infer execution from symbol names alone.

The listed importer/source boundaries have no direct calls to the forbidden export/store/log write, sync or open functions. This check does not traverse every generic transport instruction or virtual callback. Lifecycle ordering, configuration refusal and write gating remain subject to the separate implementation review and host tests.

The GPU export writer `_boot_log_export_put` is not defined. Existing capture and diagnostic symbols remain present.

## Final payload check

The final audit covers 34 selected functions. All retain their linked addresses and exact bytes from raw kernel to payload, with zero exclusions. The package verification binds the recorded raw hash to this payload; the final boot image's separately extracted kernel is identical to it. The final boot-image hash was also recomputed. Sources and selected build evidence remained unchanged through this final audit.

- Package: `../packaging-nova/warm-candidate-final-nova/`.
- Payload SHA256: `299401d5e77c7136c2a2f7ab7f812875a8efe6ad06d32fb4fe2c0651c3f59a96`.
- Boot-image SHA256: `b3359865b616882a78d166478b786acda51cbcf970d8e5a56330d85b85df0110`.

This closes the build/static-binding assignment. Independent implementation review and device authorization/state are separate gates owned by the parent agent.

## Audit tool notes and limits

Two retained incomplete audit attempts preceded the successful raw audit: one selected a C++ helper using an ambiguous substring; the other deliberately rejected an initially unsupported MOVZ x1 setup instruction. The script was corrected after inspecting the exact formal symbols and linked disassembly. These attempts did not change production source or binaries. Each existing script version was backed up under `snapshot-nova/boot-speed-round9-nova/build-audit-script-nova/` before editing.

No device was accessed. Static binding does not prove runtime branch feasibility, a successful warm import, APFS recovery behavior, reaching Hello, or a startup speed improvement. A later controlled on-device run is still required.
