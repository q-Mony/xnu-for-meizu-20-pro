# R14 Runtime-Log Analysis Review Result

New file, no previous version. Status: **R14_RUNTIME_LOG_ANALYSIS_CORRECTED_AND_BOUND**.

Model/config: `deepseek-official/deepseek-flash`, reasoningEffort `high`; dsh session `session-d9a564c8-4e60-4e4c-b3ac-a08399514bd1`. No delegation or fallback.

## Correction
- Added first Setup L1621: `capture_ticks=1916221829`, `capture_ns=99803220260`.
- R14 SpringBoard L730: `1029625136`, `53626309166`; therefore **SB→first Setup = 46.176911094s**.
- The previous report omitted first Setup and treated launchd→first-spawn **19.796054115s** as the largest interval. It is retained only as a nested quiet segment.
- All four R14 bootphase anchors are now explicit: root_device_resolve **16.262040208s**, root_mount 0.018290833s, systemos_prepare 0.155462916s, data_prepare 0.006026042s.
- All compared `capture_ns`/bootphase `ns` values validate as `floor(ticks*1e9/19200000)`.

## Same-domain before/after
| Interval | R14 | R7 | R8 |
|---|---:|---:|---:|
| SB→first Setup | 46.176911094 | 152.523992969 | 161.579416303 |
| launchd→SB | 21.094823698 | 21.108157239 | 21.102992395 |
| launchd→first Setup | 67.271734792 | 173.632150208 | 182.682408698 |
| lsd→first Setup | 42.077641250 | 146.035655573 | 157.792883542 |

R14 SB→Setup is 115.402505209s shorter than R8 (**-71.4215%**) and 106.347081875s shorter than R7 (**-69.7248%**). This is a single-run, nonidentical warm/capture comparison; it supports an observed internal segment reduction, not total Boot→Hello or a single cause.

## Startup boundaries
- Largest measured high-level startup interval ending at first Setup: **SB→Setup 46.177s**.
- `launchd exec→first spawn(fsck)` = **19.796054115s**, nested within that span.
- Post-Setup tail to last log = **67.930022865s**, classified as log retention/UI operation, not startup.
- R14 rootresolve 16.262s vs R8 7.369s and R7 7.386s; exact same bootphase anchors.
- Setup armed is not rendered Hello; user-confirmed Hello remains independent.
- No Boot-menu wall clock and no user duration, so **Boot→Hello total is unmeasured**.

## Stability/completeness
- No actual panic/crash/watchdog event lines in the captured R14 window; only DumpPanic L907 and ReportCrash L1390 execs plus policy ready L246. Absence is window-limited, not a blanket stability guarantee.
- Autolog ring dropped/NUL = 0/0; diagnostic `sbipc loss dropped=119317` is a separate counter.
- Warmload fields (start→end 10.2468425s, read 0.331257917s, hash 8.667086354s, import 9.286926406s) overlap; not summed and no read-dominance claim.

## Bound hashes
- REPORT `6c82cb83b22f9646846cf842517d3a09b7e524056670b5fe16c6040b62757e7b`
- timestamps `91c89132af26ad571406af963690bead7e0b598d034ee99107150cdcdd633ace`
- evidence `55246db59402f5d6dc9ae0fc8713d2e51f21be6ae56fee325d939145c4f770b0`
- raw R14 text `dfeb9005406a5f8bec9a003a2953258cee25b25fb444d17100de40101ef01f02`
- R7 text `01e6b73d77676d6436c3d898dc9671ce464bcb422d6d8ffb92ed0b735147420a`
- R8 text `dcf50ef3eaf2dc02157f62f5f04b752be9e5b3b29b40c881b5ab73ec3c9f81c9`
- this JSON `d84b48ea9daf00b47c8f02d111ade80156028ebd2aa8e4996962d5d4be10c90b`

No device access, no build, no source/package edit; historical R7/R8 logs and prior device statements untouched. Remaining targets are hypotheses only: SB→Setup ~46s, launchd pre-first-spawn ~20s, warmload hash ~8.7s.
