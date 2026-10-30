# Independent CPU probe review — Nova round 6

2026-10-30. New review file; no previous version. Reviewer: `lsd_cpu_accounting`,
independent of the CPU probe and archive-builder authors. Read-only review of
production source, host tests, existing ARM64 object and build integration;
no production modification, full build or device operation.

**Verdict: no blocker for the candidate using the selected `.config`.**
The probe follows the one-shot read-only domain0 scope. One configuration portability
issue was found and reported; the subsequently added compile-time guard resolves it.

## Reviewed behavior

| Requirement | Evidence and conclusion |
| --- | --- |
| Disabled means zero hardware access | `CpuFreqReadiness.c:153` checks the complete bounded boot string before claim, counter/MPIDR reads, mapping or printing. `PE_boot_args()` returns the ARM64 1024-byte CommandLine array. The parser accepts exactly one whitespace-delimited `nova-cpufreq-probe=1`; duplicates, malformed numbers and missing NUL fail closed. The ARM64 object branches back before its first `mrs`/`io_map` on rejection. Software bootarg parsing still occurs on later autolog calls. |
| Fixed four-load order | `probe_read32` performs one volatile const 32-bit load then `dmb oshld`. The production call order is enable `+0x000`, state `+0x020`, DCVS `+0x0b0`, request `+0x320`, all within the one 4KiB page. Object offsets `0x3cc`, `0x438`, `0x440`, `0x448` independently show those four loads from the `io_map` result, each followed by `dmb oshld`. There is no load loop, LUT walk or access to another domain. |
| Enable loaded once, early stop | Only the first load obtains enable. `UINT32_MAX` and bit0-clear branch directly to matching unmap; read count and valid mask are 1. Neither branch reads state/DCVS/request. |
| One producer and correct publication | An acquire/release exchange claims the only producer. `published_snapshot` is assigned completely, then `snapshot_ready` is stored release; getter loads ready acquire before copying and never performs MMIO. The real object contains `ldaxr/stlxr` claim, `stlr` publication and `ldar` getter. Ready is never reset and the snapshot has no subsequent writer. |
| Mapping flags and cleanup | The actual XNU prototype accepts `(phys_addr, size, flags, prot, unmappable)`. The call supplies `0x17d91000`, `0x1000`, `VM_WIMG_IO`, `VM_PROT_READ`, `true`. The real object passes flags=7, prot=1 and unmappable=1. XNU `osfmk/arm/io_map.c` uses `kernel_map` plus `KMA_DATA` for unmappable late mappings and preserves the passed protection. `ml_io_unmap` removes the mapping and frees from `kernel_map`; the aligned one-page request matches its arguments. All successful-map software paths unmap exactly once before publication/printing; map failure does not unmap. |
| No STARTUP or frequency writes | The module exports only the explicit probe and cached getter; no startup registration or thread creation is present. Independent `otool -l` inspection confirms no `__init_entry_set` or `__mod_init_func`. The only mapped-address instructions are the four loads; stores target stack/local publication state, not MMIO. Undefined symbols are only PAGE_SHIFT_CONST, PE_boot_args, io_map, ml_get_interrupts_enabled, ml_io_unmap and printf. |
| Honest sample meaning | MPIDR affinity endpoints are recorded but do not pin execution. CNTFRQ is labeled `timer_hz`; request is labeled raw index and CPU frequency remains `unknown`. The module does not claim this one snapshot measures initial or whole-boot frequency. |

## Test review

The host harness compiles the actual production C and only substitutes platform
access/output functions. Its map/read/unmap shims check exact PA, size, memory flags,
read protection, removable allocation, allowed offset sequence, load count and
cleanup. Reentry is exercised during map, read and unmap. The publication test blocks
the producer inside unmap while another thread performs 10,000 unpublished getter
calls and attempts a redundant producer call. After completion, 1,000 repeated calls
must retain one sample, its original timestamps and one log line.

The existing result records 48 passing ASan/UBSan cases and two deliberate negative
controls that fail with the expected harness code/text. Cases cover disabled and
malformed bootargs, full-string NUL boundary, 4KiB/IRQ/affinity preconditions, mapping
failure, enable short circuits, raw zero values, reentry and publication. Source SHA
in that result matches both the current production source and the real-object build
report. No redundant host rebuild was performed during this read-only review.

The threaded test validates the exposed ready boundary; it is not a weak-memory
model checker or ThreadSanitizer run. Source-level acquire/release and the target
`ldar/stlr` disassembly provide the independent ordering evidence. Host tests cannot
establish actual MMIO accessibility or recover a bus abort/stalled load.

## Builder and dependency review

- `board.toml:57` registers the new archive builder; the selected `.config:16`
  supplies `{tree}/build/native-meizu-cpufreq-probe/libNativeMeizuCpuFreqProbe.a`.
  Existing `scripts/unixnu/kernel.py` expands `{tree}`, translates CONFIG_XNU keys
  to XNU environment values and runs board archive builders before the final XNU pass.
- `build-xnu-4k-baseline.sh:640` checks that archive exists, force-loads it and adds it
  to `KERNEL_ARCHIVE_DEPENDENCIES`. That is passed as `XNU_EXTERNAL_KERNEL_ARCHIVES`;
  the actual XNU `makedefs/MakeInc.kernel:191` lists it as an explicit link-target
  prerequisite. Rebuilding the archive therefore participates in incremental relink.
- The staged archive builder and staged build-xnu script currently have the same
  SHA256 as their source versions. The staged driver points inside the Nova device
  tree, not to Sol/Astra. The autolog call invokes the explicit function in the existing
  scalar-summary sequence; there is no new startup hook or periodic MMIO producer.
- The new builder uses a Nova-local compile record and a fixed Xcode compiler,
  passes argument arrays without shell expansion, strips old output/dependency flags,
  writes only its selected Nova output directory, rejects symlink outputs and clears
  include/compiler environment injection variables. It checks input hash stability,
  ARM64/iOS17 object format, absent startup sections, exact expected undefined symbols
  and exactly two exports before archiving. The reviewed isolated object matches its
  recorded SHA256. The audit of the final linked kernel remains the parent task.

## Configuration finding and resolution

`configs/meizu20pro_defconfig` enables `XNU_OWNER_CACHE_MOUNT` but does not include
`CONFIG_XNU_MEIZU_CPU_FREQ_PROBE_ARCHIVE`. The initially reviewed `kern_exec.c`
referenced the probe under that owner-cache path unconditionally; resetting to the
defconfig would have left an unresolved probe symbol. The selected candidate `.config`
was unaffected because it includes the archive.

After reporting this, the parent added `-DXNU_MEIZU_CPU_FREQ_PROBE=1` only in the
archive-selection block, and the accounting agent guarded the autolog extern and call
with that macro. The defconfig remains unchanged and runtime bootarg opt-in is still
separate. Actual declaration/call guard extracts compile, link and run with macro
undefined/0/1 in the final accounting test run. Since this reviewer made the two
kern_exec guards while fixing the separate BSD task API compile issue, their final
independent review is assigned to `cpu_probe_impl`; no self-review independence is
claimed for that delta. No CPU probe implementation or archive builder was changed
by this reviewer.

## Boundaries and recorded inputs

The real XNU `io_map` late path uses `KMA_NOFAIL`, so a synthetic map-return-zero test
only validates that branch, not recovery from allocation failure or hardware abort.
The four-read count bounds successful software operations, not bus completion time.
Interrupt/MPIDR checks are observations and do not prevent intervening scheduling.
These limits are explicitly documented in source and scope. This is diagnostic
evidence collection and provides no demonstrated boot-speed improvement by itself.

| Input | SHA256 |
| --- | --- |
| CPU source | `820e379fa22902d43cb822889d1f10d38bf9842bf7b36be13328a1ca42f0f13a` |
| CPU header | `19c5dd230a43aa77fe011a9a54f888efe5ba892e17c6ae3f492b3d0073d55fb7` |
| New archive builder | `56b06d429c9d8bb515ca9fa4b29ab24235ed5c9f62a35d67550a762f0d853e66` |
| board.toml | `5fbe89b03a184dc15b380d6d5cc4e3bafd350aa9bbe543f018b0f552df85fc9b` |
| Selected .config | `379539b826e5e2fcdd1b9282b7a840bb5955ef1ad3122afbf8a0c0b3c0e57310` |
| build-xnu script, after parent adds archive-selection macro | `213998f238e48a9cf826ace3225807b35bbabf7d8e4c76a37ae899d940cc112a` |
| Inspected ARM64 object | `eab2226506a7b3578a0468d88ba1268c0ec726703722acb0ea5fe841eb7701dc` |

Object and build report: `packaging-nova/isolated-probe-build-exact-deps-nova/`.
Host test evidence: `cpufreq-tests-nova/test-results-nova.json`.
