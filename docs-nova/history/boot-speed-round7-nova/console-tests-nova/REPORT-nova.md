# R7 ring-only timestamp implementation — Nova

2026-10-30. New report, no previous version. Six owned production files changed; each original was backed up before modification. No kern_exec edit, build, flash, reboot, or slot change in this subtask. Parent integrates the central changelog.

The existing 8 MiB text ring now receives a fixed 56-byte ASCII prefix:

```text
[nova-ts:v1 c=cntvct t=0123456789abcdef f=01234567 k=P] 
```

There is one trailing space and no added newline. `t` is 16 lowercase hexadecimal CNTVCT digits; `f` is 8 lowercase hexadecimal CNTFRQ digits. `P` marks the first input byte of a printf/IOLog printbuf invocation, `L` the first input byte after an internal newline, and `R` the start of a manually emitted message. Empty invocations emit no marker. The timestamp is read at the actual first-byte callback, not during state initialization. Multi-call fragments can contain inline `P` markers.

The printbuf uses private flags bit 0x4 to remember input newline state; it does not change the struct layout, screen buffer, or console_write text. This works when the screen buffer truncates a long line and with write_on_newline disabled. First-byte prefixes and that original byte share one atomic ring reservation. Bare messages retain their original character writes and receive a separate prefix reservation immediately before the first byte. All known bare sites are covered: kern_cs cs_kill, kern_exit abwp, sleh brk64, and the one-time NativeBootLog boot-video message.

The prefix writer uses ISB, CNTVCT/CNTFRQ reads, manual hexadecimal conversion, a bounded local array, and the existing atomic reserve primitive. It does not call printf, allocate, acquire a lock, access cpu_data, or perform storage I/O. It is enabled only for ARM64 DEVELOPMENT, with an explicit host-only counter seam for testing. Other builds preserve plain ring text and disable the phase helper output.

Separate safe-stage APIs `nova_boot_phase_begin(phase)` and `nova_boot_phase_end(phase,id,result)` emit Mach ticks plus converted ns and a unique atomic id. These are not called by the prefix/putc/exception paths. Begin records its time before printf, so that printing cost belongs to the phase interval. Such intervals measure elapsed wall time, not exclusive CPU. Callers are owned by the parent/build agent and must use the returned id.

Validation used the exact production functions extracted from serial_general.c and the actual printbuf struct definition. Source hashes and extraction boundaries are recorded in source-binding-nova.json. Host tests substitute register reads, screen output, and phase printf only.

- ASan/UBSan enabled run: 19 checks passed. Cases cover empty output, multiline/blank lines, fragmented invocations, write_on_newline false, four long-line/flush combinations, repeated clear, bare R, untagged putc, zero/max counter fields, invalid bare kind, nested timestamp emission, ring wrap, 8 concurrent writers/16,000 complete prefixes, and phase id/result/timing semantics.
- Disabled build: 2 checks passed; original screen/ring text preserved and phase helpers inactive. Compiled with warnings as errors.
- Negative control removes L marker triggering from the extracted copy; the multiline body/marker assertion fails as expected. Production files were not changed by the negative control.
- Scoped git diff whitespace validation passed. Test results are stored in results-nova.json. The actual kernel build/link remains an integration requirement; host tests do not replace it.

This retains the original ring's concurrency limits. Reserving a prefix contiguously does not make the whole message atomic, nor does it establish a committed snapshot: another writer can interleave later payload bytes, and a concurrent snapshot can see stores in progress or a wrapped/truncated prefix. CNTVCT readings can be out of ring order if a writer is preempted between reading the counter and reserving bytes. The offline reader must validate complete markers and must not invent times for malformed/unmarked text or identify the preceding function as the cause of every adjacent log gap. This change covers the existing mirrored printf/IOLog ring paths; unrelated kprintf/console paths are not newly intercepted.

The MEIZULOG disk header, persisted chronological text, ring capacity, staging dimensions, and existing drop accounting remain unchanged. More text may cause earlier ring wrap; capture metadata must be checked on the next real run. Boot-menu/Apple/Hello visual anchors are outside this change.
