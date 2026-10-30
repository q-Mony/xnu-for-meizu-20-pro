# m2391 GCC USB readiness probe

This is a board-specific, one-shot observation module, disabled by default.
`meizu-usb-probe=1` enables it at XNU `EARLY_BOOT` / `STARTUP_RANK_LAST`, after
kernel VM, logging and thread-call startup. Absent or non-1 numeric values return
before mapping, logging or reading hardware. It uses XNU's numeric bootarg parser.
Only link it into the Meizu 20 Pro board build; it has no runtime board discovery.

The module maps exactly the 4 KiB GCC page at `0x139000` with `VM_WIMG_IO`,
`VM_PROT_READ`, and an unmappable kernel-map allocation. It reads GDSCR at +4
and five CBCRs at +0x18, +0x88, +0x8c, +0x28 and +0x24, then unmaps before
printing the values. Each volatile 32-bit load has an ARM64 `dmb oshld` barrier.
There is no polling, sleep, worker, IRQ handler, or retry. Six reads is the
software bound; it is not a bus-fault recovery mechanism or a hard time bound.

GCC is the SoC clock-control register block used by the Kalama clock driver,
independent of the downstream USB controller clock enable state. The local
NativeUfs driver also observes GCC, but its controller reads and SMMU behavior
are not copied. DT and Linux source establish these register locations and
read semantics; they cannot prove current firmware access permissions or
that a bus read cannot fault/stall. `io_map` does not establish accessibility
and uses XNU `KMA_NOFAIL` allocation. The opt-in explicitly accepts this limited
GCC observation assumption. A map-failure check is defensive, not fault recovery.
No DWC3, PHY, EUD, TCSR, reset, regulator/SPMI, or SMMU register is read or written.
No separate controller override is offered because safe access has not been
established. CPU page-table allocation is the only mapping infrastructure effect.

Interpret `running-hint` as local enable=1 and CLK_OFF=0 at one instant. It
establishes neither frequency nor the entire parent-clock/interconnect path.
Voted AXI branches with HWCG enabled always return `hw-gated-unknown`, even
if CLK_OFF=0. All-ones CBCR reads are marked invalid. No enable is reported as
`no-local-enable`, not proof that other masters have stopped the clock.

The board's `qcom,support-cfg-gdscr` means GDSCR bit 31 alone is insufficient
for its full power handshake. The exact downstream regulator implementation
is absent from the pinned kernel tree. This phase deliberately reads only the
DT-specified GDSCR word, labels `pwr_on_bit` as raw, and reports
`power-ready=unknown`. It does not access adjacent CFG_GDSCR based on an
assumed implementation. PHY rails, reference clocks, repeater, VBUS/role,
SMMU SID 0x40 and endpoint state remain unknown. Final output always says
`USB-ready=unknown; DWC3/PHY skipped`. This provides no enumeration, DMA,
USB transport, or real-time USB logging.

Sources are commit-pinned at
https://github.com/m2391-dev/android_kernel_meizu_m2391/tree/dfc9fd58332e3f44402c7d05a260ff89bd6dc36a
and corroborated by the 45-node local-DTB comparison in
`usb-log-research/REPORT.md`. `UsbReadinessRegs.h` cites individual source paths
and line locations. The staged validation manifest records SHA-256 hashes.

Build integration consists of a `board.toml` archive-builder entry and a
force-load addition to `CONFIG_XNU_EXTRA_LD_ARCHIVES`, preserving the existing
watchdog archive. The startup registration then needs no XNU source patch,
IOKit personality, or device-tree edit. Neither the bootarg nor a firmware boot
configuration is changed by integration. See the staged `integration.patch`.
