# Meizu 20 Pro (SM8550 / kalama) — UniXNU bring-up status

2026-10-23. Everything below was observed on the phone, not inferred.

## Where the boot stops now

The kernel completes `arm_init` / `arm_vm_init`, `kernel_bootstrap`, the whole VM
and kmem bootstrap, kext loading, and IOKit (Skywalk self-tests pass,
`IOSurfaceRoot` starts). It then panics because the payload has no root
filesystem:

```
panic(cpu 0 caller 0xfffffff007a120a9): "Failed to mount root device" @IOKitBSDInit.cpp:984
```

That is the end of kernel initialization; the next phase needs a root device.

## Acceptance criteria

1. Entry checkpoints and exception reporting — **met on hardware**.
   The panel console shows numbered checkpoints from the MMU enable onward and
   an early reporter prints `ESR/ELR/FAR/SCTLR/TCR/X0/X2/LR`. It caught two real
   faults: the BCM2837 panic-log external abort (`ELR=PE_create_console+0x48`)
   and the kext-summary protection fault.
2. 4 KiB page tables, memory, platform init — **met**. Checkpoints observed:
   `MMU ON`, `SCTLR`, `arm_init`, `arm_vm_init`, `vm_set_page_size`,
   `arm_vm_prot_init`, `set_mmu_ttb_alternate`, `pmap_bootstrap`,
   `machine_startup`, `kernel_bootstrap`, `vm_page_bootstrap`, `pmap_startup`,
   `zone_bootstrap`, `vm_object_bootstrap`, `vm_map_init`, `kmem_init`, then
   `vm_commpage_init` binds the console and kprintf flows.
3. Single-core GICv3 + architectural timer — **met on hardware**. The ArmCpu
   driver (already linked with `CONFIG_XNU_ARMCPU=y`) owns the GIC: it enables
   SGI 0..15 and PPI 27 as group 1 (IRQ) and counts the timer in
   `armcpu_timer_interrupts`. The status line shows `G=1` (armcpu_gic_ready),
   `T=63` and `IRQ=64` in the same boot, `F=0x124F800` (19.2 MHz) with CNTVCT
   advancing. FIQ stays 0 because this fork uses the group-1 IRQ path, not the
   Apple FIQ path.

## Things that had to be fixed (all verified on the phone)

- AVB: the `boot_b` footer must carry the stock boot partition's own chained
  vbmeta, taken from stock `boot_b[46,891,008 .. +2496]`. Using a blob from the
  `vbmeta_b` partition gives `fastboot-reason:avb` and the bootloader never runs
  the kernel. Layout: `[boot.img][stock boot vbmeta 2496][zeros][footer(1,0,len,len,2496)]`.
- The frame buffer is outside the Darwin window and XNU never maps it. The
  entry patcher adds it to the bootstrap page tables (2 MiB Device blocks, or a
  1 GiB block when the window starts above 4 GiB) and `native_blog_start()`
  draws from the MMU enable onward; `_arm_vm_init` drops that map, so
  `native_blog_keep_identity()` restores the bootstrap TTBR0 at `_pmap_bootstrap`.
- The early fault reporter must give VBAR back once the kernel can handle its
  own exceptions: `native_blog_restore_vectors()` runs at `_kernel_bootstrap`.
  Before that, a recoverable protection fault (kext summaries are made read-only
  and re-protected) looked like a fatal fault.
- `PLATFORM_PANIC_LOG_PADDR` from the BCM2837 header (0x3c0fc000) is compiled
  into `check_for_panic_log()`; reading it is an external abort on this board.
  Fixed with `CONFIG_XNU_DT_PANIC_REGION=y`.
- AMFI requires an `ApplePMGRNub`. Ported the Sheng board package
  (`package/ShengPlatformPower.cpp`, `scripts/build-sheng-platform-power.py`,
  `scripts/audit-amfi-pmgr-contract.py`) and enabled `CONFIG_XNU_SHENG_PMGR=y`
  plus the `sheng-pmgr=2` boot argument. The audit passes against this board's
  prelinked kernel (same kext layout as Sheng's, same kernelcache hashes).
- The console renders XNU's `iso_font` least-significant-bit first.

## Rebuild and flash

```
cd ~/unixnu_ports/unixnu-main
./unixnu build firmware --uefi ~/unixnu_ports/uefi-sheng --edk2 ~/unixnu_ports/uefi-sheng/Mu_Basecore
./unixnu build payload --uefi ~/unixnu_ports/uefi-sheng --unique-chip-id 57a096775e3c1f8c
./unixnu bootimg --uefi ~/unixnu_ports/uefi-sheng \
  --fd ~/unixnu_ports/uefi-sheng/Build/meizu20proPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd \
  --dtb ~/unixnu_ports/meizu20pro.dtb --ramdisk out/meizu20pro/payload/payload.img
# slot A: [boot.img][stock boot_a[46891008..+2496]][zeros][footer(1,0,len,len,2496)]
fastboot flash boot_a out/meizu20pro/boot/boot_a_flash.img
```

Keep the stock `vbmeta_b` and everything else untouched. Rollback is
`fastboot flash boot_b ~/.qoder/backups/meizu20pro/20261023-000614/boot_b.img`.

## Timer interrupt finding (2026-10-23)

The timebase runs (`F=0x124F800` = 19.2 MHz, `CNTVCT`/`CNTPCT` advance) but the
kernel receives no timer interrupt: FIQ stays 0 and IRQ only shows a few
unrelated interrupts. The virtual timer's PPI 27 is enabled only by
`pexpert/arm/pe_fiq.c:pe_init_fiq()`, whose only caller is the hwtrace code, and
that function needs a `/arm-io/gic` DT node. Our ADT names the controller
`/arm-io/intc` and has no `device_type = "timer"` node, so neither the FIQ path
nor `pe_arm_map_interrupt_controller()` completes.

Attempt (2026-10-23): the full `pe_fiq.c` sequence was run from the board
package at `bsd_init`. It destabilised the system (several reboots and a panic;
the panel showed a noise frame), most likely because the distributor/group
registers are locked by the secure firmware. Reverted to the previous image.

A safer next attempt: touch only the per-CPU redistributor (`GICR_ISENABLER0`
bit 27, and `GICR_IGROUPR0` if it is writable), read every register back and
`kprintf` the values, and enable one group at a time instead of rewriting
`GICD_CTLR`/`ICC_IGRPEN0` outright.

## Dual boot plan (2026-10-23)

Slots are encoded by Qualcomm in the GPT partition attributes, byte 54 of each
entry: bit 2 = SLOT_ACTIVE, bit 6 = BOOT_SUCCESSFUL, bit 7 = UNBOOTABLE. Today
`boot_b` = 0x77 (active, bootable), `boot_a` = 0xf2 (inactive, marked
UNBOOTABLE). `misc` sector 0 is empty; the QTI HAL (`bootctl`) reads/writes the
GPT instead.

Plan: UniXNU goes to `boot_a` (footered with the stock `boot_a` vbmeta blob,
sha256 aa24828c...), Android stays on `boot_b` with its KernelSU root in
`init_boot_b`. Android -> UniXNU: root runs `bootctl set-active-boot-slot 0`
(plus clearing the UNBOOTABLE bit on slot a). UniXNU -> Android: the menu's
"Android (slot b)" entry rewrites the GPT attribute byte for the A/B list in
both primary and backup tables, updates the CRCs and resets.

The switch tool is `meizu20proPkg/Tools/BootAndroid` (`BootAndroid.c`), built
into the firmware volume (see below). It also leaves the inactive slot's
success bit alone, clears its stale UNBOOTABLE bit, and refuses to write unless
the primary and backup tables agree on entry count/size and the write length
is block-aligned (96 x 128 B at 4 KiB sectors on this board).

### Firmware images are executable, fixed-media images are not

`ENABLE_SECUREBOOT=1` links `DxeImageVerificationLib` into SecurityStubDxe.
Its handler gives `ALWAYS_EXECUTE` to `IMAGE_FROM_FV` and applies
`PcdFixedMediaImageVerificationPolicy` (deny) to fixed media, so `LoadImage`
of `Tools/BootAndroid.efi` from the FAT payload returned `EFI_ACCESS_DENIED`
(the menu showed "returned: access denied"). The fix keeps Secure Boot as
built: the app is packed into FVMAIN (`meizu20pro.fdf`, file GUID
5D3B7A11-9C3E-4F0B-8B6A-2E7C1D4A9F31) and the entry uses UniXNU's
`fv:<GUID>` path, which builds the FV file device path at menu time.

The platform seed in the flashed payload is `57a096775e3c1f8c` (README), not
the `8c1f3c5e7796a057` in the older command block here: extracting
`/chosen/unique-chip-id` from the ADT inside the flashed boot image is the
authoritative check.


Registered TWRP device tree for this exact device:
`Xpsoted/android_device_meizu_m2391-TWRP` (recovery partition is 100 MiB and
ramdisk-only, matching this phone). It would allow the documented
`scripts/dual_sys` flow, but no prebuilt image exists.

### Menu switch: firmware volume vs. no disk driver (2026-10-23)

The `fv:` entry now loads (the Secure Boot denial is gone), but the tool
returned `EFI_NOT_FOUND`: the first-console firmware (`MEIZU_VENDOR_HW = 0`)
loads no UFS driver, so the only BlockIo device is the payload RAM disk and
there is no GPT to rewrite. With `MEIZU_VENDOR_HW = 1` the stock chain cannot
come up here:

- `EnvDxe` is prebuilt; its own DebugLib embeds `PcdDebugPropertyMask = 0x2F`,
  so its `ASSERT_EFI_ERROR (Not Found)` dead-loops. Overriding the platform PCD
  only affects modules built from this tree.
- Packing the stock `uefiplat.cfg` (GUID DDE58710...) into the FV, publishing
  the handover DT as `gFdtTableGuid`, and matching the stock dispatch order did
  not change the result: the vendor DXEs expect the config HOBs the stock
  Meizu PEI builds from that file, which the Mu SEC/PEI does not.
- Stock FV order (from the uefi partition dump): EnvDxe #2, ChipInfo #27,
  PlatformInfo #30, UFSDxe #43, CmdDb #44, PwrUtils #45, Rpmh #46, Npa #47,
  ULog #48, Vcs #49, Clock #50, ICB #52, Smem #53; DDRInfoDxe lives in a
  separate FV. So ordering was not the cause.

Current state: slot switching is PC/Android driven. UniXNU -> Android:
`fastboot set_active b`; Android -> UniXNU: root `bootctl set-active-boot-slot 0`.
The menu entry is disabled in `board.toml`; the flashed image
`~/unixnu_ports/meizu20pro-flash/boot_a_unixnu_8fe7304b.img`
(sha256 8fe7304b..., FD 7c5c39a4...) still carries it and reports `Not Found`.

The no-PC route that does not depend on the vendor UEFI stack is to switch the
GPT from XNU: boot the kernel with a dedicated argument, read/modify/write the
A/B attributes through NativeUfs, then PSCI-reset. That shares the UFS bring-up
needed for the root filesystem.

## Open items

- Root filesystem: needs the storage route (NativeUfs + SystemOS/Data images).
  Not started; any path there touches partitioning and needs its own decision.
- Confirm `CNTFRQ_EL0` = 19,200,000 and the timer PPI (27) once the status line
  shows the counters.
- Touch/HID drivers are linked in but not yet reached in a running user space.
