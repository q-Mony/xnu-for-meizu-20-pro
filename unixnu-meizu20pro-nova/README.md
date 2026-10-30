# UniXNU-meizu20pro

Device repository skeleton for the Meizu 20 Pro (SM8550, kalama) for [UniXNU](https://unixnu.com).
It follows the layout and schema of [UniXNU-sheng](../unixnu-sheng), the only qualified SM8550 port.

**Status: kernel builds and prelinks, and a first-console boot image builds; nothing has been booted on the
phone.** `./unixnu build kernel` compiles XNU, links the Meizu touch and HID archives, and prelinks
`out/meizu20pro/kernel/kernel.macho` against the iPadOS 17.7.11 kernelcache. The firmware step builds the
Meizu UEFI FD from `uefi-sheng/Platforms/Meizu/meizu20proPkg`, and `build payload` / `bootimg` turn it into
`payload.img` and `boot.img`. The firmware runs no vendor hardware drivers yet, so it boots with whatever
display state the previous stage left; the XNU-side UFS and USB work is unvalidated.

## What this repository builds

- `drivers/NativeGeniI2c` + `drivers/NativeGoodixBerlin` + `package/MeizuTouchProbe.cpp`
  (`scripts/build-native-meizu-touch.py`): the GENI I2C engine, the Goodix Berlin D protocol and the staged
  board probe. `meizu-touch=4` votes the two RPMh supplies, then the QUP clocks, muxes the pins, releases
  reset, identifies the controller and polls the event block. It publishes the finger set through a seqlock.
- `drivers/NativeRpmh` (`Rpmh.c`, host test in `Test/rpmh.c`): command DB lookup and the active-TCS register
  sequence for the apps RSC (DRV 2 at `0x17a20000`, `qcom,tcs-offset = <0xd00>`), with the RSC v2.7 and v3.0
  layouts selected from the DRV_ID register. `MeizuTouchProbe.cpp` uses it to vote `ldob9` (2960 mV) and
  `ldob12` (1800 mV) before the controller leaves reset: neither rail is always-on, and Android's driver is
  their only consumer.
- `drivers/NativeHidInput` + `package/MeizuHidProbe.cpp` (`scripts/build-native-meizu-hid.py`): the owned
  IOHIDDevice that turns that finger set into a Built-In, Digitizer/Touch Screen HID service for backboardd.
  The builder fails unless this board's own prelinked image still carries the original IOHIDFamily text and the
  exact 207-slot IOHIDDevice ABI, so a shifted link cannot silently publish a wrong device.
- `uefi-sheng/Platforms/Meizu/meizu20proPkg` (firmware step, `--uefi` checkout): `meizu20pro.dsc` /
  `meizu20pro.fdf` plus four board drivers — boot memory and RAM disk from the handed-over DT, the stock
  DisplayDxe dependency shim, GOP hand-off for the frame-buffer log, and the SPMI power/volume keys as a
  console. `MEIZU_VENDOR_HW = 0` for the first console.
- `scripts/make-meizu-adt.py` + the payload/bootimg steps: the Apple-format DT for XNU (GIC, PSCI, eight
  cores, UFS host, kalama root) and the FAT payload volume (`MEIZUESP`, 192 MiB) that carries the prelinked
  kernel and its ADT.


## What is measured (sources in parentheses)

| Item | Value |
| --- | --- |
| SoC | SM8550 / kalama (`ro.board.platform`) |
| RAM | 11501072 kB (`/proc/meminfo`) |
| Machine | `Meizu M2391 (kalama MTP)`, `meizu,board-id = <0x2 0xf>` (DT) |
| userdata | `/dev/block/sda22`, 4096-byte LBAs, first `0x238f68`, last `0x3a8bffa` |
| Panel | `qcom,mdss_dsi_meizu_amoled_samsung_cmd`, Samsung AMOLED, **command mode + DSC**, 1440x3200@120, 6.81 in |
| DSI | ctrl0 `0xae94000`, ctrl1 `0xae96000` |
| Panel rails | PM8550 (vddi/avdd/vci), PM8550B (vddr) |
| Touch | Goodix Berlin (`goodix,brl-d`), I2C bus 5 (`i2c@a90000`) address `0x5d`, IRQ GPIO 25, reset GPIO 24, `goodix_firmware.bin` / `goodix_cfg_group.bin`, coordinate space 1440x3200 |
| Touch path | Direct I2C on `a90000`: the node carries `vm_mode` / `pvm`, `panel-vm` and VM GPIO 44-47, but `goodix,qts_en` is empty, so the vendor driver talks to the controller itself (`dmesg` shows no QTS RPC) |
| Touch supplies | `pm_humu_l9` (`ldob9`, cmd-db `0x41600`) at 2960 mV to `5-005d-avdd`, `pm_humu_l12` (`ldob12`, cmd-db `0x40a00`) at 1800 mV to `5-005d-iovdd`; neither rail is always-on, and no other consumer holds them |

The full device tree is exported at `../meizu20pro-dt.dtb` (`/sys/firmware/fdt`).

## Missing before a first boot

1. Hardware validation of the UEFI display: the `MEIZU_VENDOR_HW = 1` + `USE_CUSTOM_DISPLAY_DRIVER = 1`
   variant now packages the phone's own DisplayDxe and panel XMLs and builds, but it has never driven the
   panel. The panel init depends on DALSYS, clocks and the PMIC rails in UEFI, so the first-console variant
   (`0/0`) stays the default until this boots. The package lives in the Mu-Silicium checkout
   (`uefi-sheng/Platforms/Meizu/meizu20proPkg`), not in `uefi-meizu20pro`, which is still an unmodified clone
   and is not what `--uefi` points at.
2. Hardware validation of the touch stack: the RPMh rail vote, the GENI I2C probe, the Goodix identification,
   the coordinate orientation (flipX/flipY) and the event loop have never run on the phone. The controller is
   Goodix Berlin D (GT9916), I2C 5 address 0x5d; reference driver source and firmware are in
   `../goodix_ts_berlin` and `../meizu20pro-vendor`. Firmware download is not expected on a cold boot (the
   controller boots from its own flash). The supply rails are the one part Android used to provide: its
   `goodix` driver is the only consumer of `ldob9`/`ldob12`, so XNU must vote them itself — `drivers/NativeRpmh`
   does that, but the RSC major version this chip reports is still unknown, so the driver refuses a version it
   does not decode rather than guessing.
   (A UEFI-side probe would need a Qualcomm I2C DXE, which the Mu-Silicium tree does not have.)
3. Trust caches, the Data seed and the target identity: `scripts/make-meizu-adt.py` and the payload builder
   exist (the ADT is generated from this phone's board DT), but `--trust-cache`, `--owner-trust-cache` and
   `--data-ramdisk` are still empty, and `[ipsw]` currently points at sheng's iPadOS 17.7.11 image.
4. The platform seed (`--unique-chip-id`) is deliberately not stored in any repository; pass it on every
   payload build. The value already baked into `payload.img` is `0x57a096775e3c1f8c`.
5. A macOS host with full Xcode (not only Command Line Tools), LLVM/lld, cmake, ninja, mtools, dtc.

A fresh output directory needs one `./unixnu build prelink` before the HID archive builder has a reference
image to qualify against; the bootstrap kernel pass leaves the kernel it needs.


## Build

```sh
./unixnu defconfig meizu20pro_bringup_defconfig -D ../unixnu-meizu20pro
./unixnu fetch
./unixnu ipsw fetch
./unixnu build kernel
./unixnu build firmware --uefi ~/unixnu_ports/uefi-sheng
./unixnu build payload --uefi ~/unixnu_ports/uefi-sheng --unique-chip-id 57a096775e3c1f8c
./unixnu bootimg --uefi ~/unixnu_ports/uefi-sheng --dtb ../meizu20pro-dt.dtb
```

`--uefi` is not remembered between invocations, and the payload step compiles the panic screen against
`$UNIXNU_UEFI/Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib`; without it the step fails on
`Font.h`. The firmware FD path is
`~/unixnu_ports/uefi-sheng/Build/meizu20proPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd`.

The firmware has three variants, chosen by `[firmware.defines]` in `board.toml` (the DSC only defaults
these, so a `-D` would also win; the CLI passes board.toml):

| `MEIZU_VENDOR_HW` | `USE_CUSTOM_DISPLAY_DRIVER` | Modules | Image |
| --- | --- | --- | --- |
| `"0"` | `"0"` | Mu only, SimpleFbDxe; keeps the previous stage's display | `boot.img` |
| `"1"` | `"0"` | plus this phone's own UEFI drivers (SMEM, clocks, RPMh, cmd-db, SPMI, TLMM, I2C, TZ, UFS, USB) | `boot-vendorhw.img` |
| `"1"` | `"1"` | plus this phone's own DisplayDxe with its panel XMLs | `boot-display.img` |

The display variant is the phone's untouched `DisplayDxe` from its uefi partition. `Include/RAW.inc`
packages the three `Panel_meizu_*.xml` files it knows under their original FFS GUIDs, and DisplayDxe
selects one from the device tree bootargs: this phone passes
`msm_drm.dsi_display0=qcom,mdss_dsi_meizu_amoled_samsung_cmd`, which maps to
`Panel_meizu_amoled_samsung_dsc_cmd.xml` (1080×2400 active timing of the 1440×3200 panel). Seven of the
nine DEPEX protocol GUIDs come from PmicDxe/TLMMDxe/I2CDxe/ClockDxe/RpmhDxe/UFSDxe etc.;
`MeizuDisplayDepexDxe` installs empty protocols for the remaining two.

The Meizu ADT generator now emits `/arm-io/disp0` (`negroni,display`,
`display-subsystem`) and a 56-byte zero `/chosen/negroni-gop-handoff` slot for
DarwinBoot to fill in the copied tree. Its default 1080×2400 canvas reflects
the **SimpleFbDxe firmware PCD**, not the panel's physical 1440×3200 pixels.
For a verified custom GOP mode, invoke the generator with both
`--display-width` and `--display-height`; the payload helper currently does not
forward those options from `board.toml`. The loader must compare the final GOP
mode to this canvas and reject mismatches, then validate the final UEFI memory
map before filling the handoff. No fixed base, stride, GOP size or reservation
is asserted by the ADT.

The firmware memory table declares `Display Reserved` as
`0xB8000000..0xBAB00000` (0x2B00000 bytes, `EfiReservedMemoryType` in the
source), separate from the adjacent kernel reservation
`0xA8000000..0xB8000000`. With an illustrative 16 KiB mapping page, tightly
packed 32-bit 1080×2400 occupies 0x9E4000 mapped bytes and 1440×3200
occupies 0x1194000, each fitting inside the declared range **if** the active
GOP begins at 0xB8000000. SimpleFbDxe reports the range base and calculates
`FrameBufferSize` from its PCD: at 1080×2400 this is **0x9E3400**, short
of the rounded 16 KiB mapping by 0xC00. The present strict validator requires
the rounded mapping to fit inside GOP `FrameBufferSize`, so the default
SimpleFbDxe mode fails that test, even though the memory reservation covers it.
The vendor DisplayDxe may change the base,
dimensions, stride, format or size. The final memory map and GOP values have
not been captured on this phone. This static envelope does not establish that
the strict handoff bounds pass on hardware; the loader must fail closed when
the reservation descriptor or rounded mapping coverage is missing.

To build a variant, flip the values, run the firmware step, save the FD, and pass it to `bootimg`:

```sh
./unixnu build firmware --uefi ~/unixnu_ports/uefi-sheng
cp ~/unixnu_ports/uefi-sheng/Build/meizu20proPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd \
   out/meizu20pro/boot/SILICIUM_UEFI.vendorhw.fd
./unixnu bootimg --uefi ~/unixnu_ports/uefi-sheng --fd out/meizu20pro/boot/SILICIUM_UEFI.vendorhw.fd \
   --dtb ../meizu20pro-dt.dtb -o out/meizu20pro/boot/boot-vendorhw.img
```

Firmware builds are reproducible: the step pins `SOURCE_DATE_EPOCH` and `PYTHONHASHSEED`, because EDK II
picks each module's stack cookie with Python's salted `hash()`. The first-console FD is
`be519162…1c02`, the vendor one `810dc283…3217`, the display one `20beaff7…7a71d`.

`meizu20pro_bringup_defconfig` builds and prelinks today; `meizu20pro_defconfig` pulls in the profile that
needs live qualification evidence from a booted system and does not link yet.

Nothing in this repository has been flashed or booted on the phone.
