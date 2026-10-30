#!/usr/bin/env python3
"""Generate the minimal Meizu 20 Pro hardware Apple-format DT for the first boot.

The tree mirrors the proven sheng first-boot fixture (same required nodes and
properties the source XNU boot converged on) with real Kalama/SM8550 values
taken from this phone's exported board DT (meizu20pro-dt.dtb):

  - GICv3 distributor 0x17100000/0x10000, redistributor 0x17180000/0x200000,
    stride 0x40000 (intc@17100000)
  - PSCI 1.0 over SMC (firmware/psci)
  - CPU MPIDR affinities 0x000..0x700 (cpus/cpu@0..700)
  - CPU timebase 19200000 Hz (SM8xxx architectural counter)
  - UFS host controller 0x1d84000/0x3000, PHY 0x1d80000/0x2000
  - root compatible qcom,kalama-mtp/qcom,kalama/qcom,mtp

This is first-boot bring-up data: the platform has no matching platform
expert yet, so the kernel is expected to reach the video console and report
the genuine missing-driver panic on screen. No Apple device identity is
claimed and no property is invented beyond these pinned sources.
"""
from pathlib import Path
import argparse
import os
import re
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]

DRAM_BASE = 0x80000000
ARMIO_BASE = 0x17000000          # APSS peripheral block containing the GIC
GIC_DIST = 0x17100000
GIC_DIST_SIZE = 0x10000
GIC_REDIST = 0x17180000
GIC_REDIST_SIZE = 0x200000
TIMEBASE = 19200000
UFS_HC = 0x1d84000
UFS_HC_SIZE = 0x3000
UFS_PHY = 0x1d80000
UFS_PHY_SIZE = 0x2000


def node(name, properties=(), children=()):
    properties = [('name', name.encode() + b'\0'), *properties]
    result = struct.pack('<II', len(properties), len(children))
    for key, data in properties:
        result += struct.pack('<32sI', key.encode(), len(data))
        result += data + bytes(-len(data) % 4)
    return result + b''.join(children)


def empty_nvram_proxy():
    """A fresh volatile CHRP bank for upstream IODTNVRAM, with no controller."""
    def header(name, size):
        raw = bytearray(struct.pack('<BBH12s', 0x70, 0, size // 16, name))
        checksum = sum(raw)
        while checksum > 255:
            checksum = (checksum & 255) + (checksum >> 8)
        raw[1] = checksum
        return raw

    image = bytearray(8192)
    image[:16] = header(b'nvram', 32)
    image[32:48] = header(b'common', 16 + 2048)
    free_at = 32 + 16 + 2048
    image[free_at:free_at + 16] = header(b'free', len(image) - free_at)
    struct.pack_into('<I', image, 16, zlib.adler32(image[20:]))
    return [('nvram-bank-size', struct.pack('<I', len(image))),
            ('nvram-bank-count', struct.pack('<I', 1)),
            ('nvram-current-bank', struct.pack('<I', 0)),
            ('nvram-proxy-data', bytes(image))]


def read_wifi_mac(path):
    """Read a private, colon-separated factory address without exposing its contents."""
    try:
        # Bound the read: six octets plus an optional LF or CRLF, nothing else.
        with path.open('rb') as stream:
            raw = stream.read(20)
    except OSError:
        raise ValueError('--wifi-mac-file: cannot read input file') from None
    if not re.fullmatch(rb'[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}(?:\r?\n)?', raw):
        raise ValueError('--wifi-mac-file: expected six colon-separated hex octets')
    value = bytes.fromhex(raw.decode('ascii').strip().replace(':', ''))
    if value == bytes(6) or value == b'\xff' * 6 or value[0] & 1:
        raise ValueError('--wifi-mac-file: expected a nonzero unicast address')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'build/meizu20pro/boot/kernel.macho.adt')
    parser.add_argument('--dram-size', type=lambda value: int(value, 0), required=True,
                        help='Size of the low DRAM bank handed to XNU, e.g. 0x80000000')
    parser.add_argument('--owned-memory-size', type=lambda value: int(value, 0), default=None,
                        help='Explicit DarwinBoot contiguous window request, 256 MiB..8 GiB in 2 MiB steps')
    parser.add_argument('--root-match-volume-uuid', default=None,
                        help='/chosen/root-matching: select the root by the UUID of an IOMedia (the standard source mechanism)')
    parser.add_argument('--unique-chip-id', type=lambda value: int(value, 16), default=None,
                        help='/chosen/unique-chip-id (8 bytes) and --chip-id (4 bytes) seed XNU IOPlatformUUID; software identity seeds, not Apple silicon IDs')
    parser.add_argument('--chip-id', type=lambda value: int(value, 16), default=None)
    parser.add_argument('--wifi-mac-file', type=Path, metavar='FILE',
                        help='Private factory Wi-Fi address file: six colon-separated hex octets, optional final newline; '
                             'adds /chosen/mac-address-wifi0 as six bytes (omitted by default)')
    identity = parser.add_mutually_exclusive_group()
    identity.add_argument('--ipad-identity', action='store_true',
                        help='/product and /buttons describe an iPad-class device without a Home button (MobileGestalt reads these from the tree); '
                             'values follow the unchanged iPad7,1 tree, the button set is the phone\'s')
    identity.add_argument('--iphone-identity', action='store_true',
                        help='/product and /buttons describe this iPhone X (iPhone10,3/D22) target; values are read from the '
                             'iOS 16.7.16 DeviceTree.d22ap (artwork 2436 @3x, phone idiom, fallback iPhone9,2)')
    parser.add_argument('--cpu-regs', default='0',
                        help='Comma-separated CPU MPIDR affinity values (the /cpus reg of each core); the first is the running boot CPU. '
                             'SM8550 numbers its cores 0x000..0x700 in Affinity1 (kalama.dtsi). More than one adds /psci')
    parser.add_argument('--psci-method', choices=['smc', 'hvc'], default='smc',
                        help='/psci method used by the secondary-CPU start (the board DT declares smc)')
    parser.add_argument('--packed-data-ramdisk', action='store_true', help='Hand off validated DBRDISK1 bytes to the native sparse block driver, without a dense memdev alias')
    parser.add_argument('--data-ramdisk', action='store_true',
                        help='/chosen/memory-map RAMDisk placeholder; the loader fills in the real range for a scratch Data seed')
    parser.add_argument('--persistent-data', action='store_true',
                        help='The Data disk keeps its writes across boots: no ephemeral-storage, so first-boot data migration runs')
    parser.add_argument('--trust-cache', action='store_true', help='Zeroed TrustCache sidecar handoff placeholder')
    parser.add_argument('--gpu-addon', action='store_true', help='Zeroed GpuAddon sidecar handoff placeholder (the loader fills in kernel.macho.gpuaddon)')
    parser.add_argument('--text-fixup', action='append', default=[],
                        help='/chosen/text-fixups entry <file name>,<file offset>,<original>,<replacement> for the kernel\'s signed-text fixups')
    parser.add_argument('--display-width', type=int, default=None,
                        help='Expected GOP width; default 1080 from Meizu SimpleFbDxe firmware PCD')
    parser.add_argument('--display-height', type=int, default=None,
                        help='Expected GOP height; default 2400 from Meizu SimpleFbDxe firmware PCD')
    args = parser.parse_args()
    wifi_mac = None
    if args.wifi_mac_file is not None:
        try:
            wifi_mac = read_wifi_mac(args.wifi_mac_file)
        except ValueError as error:
            parser.error(str(error))
    if (args.display_width is None) != (args.display_height is None):
        parser.error('--display-width and --display-height must be supplied together')
    width = 1080 if args.display_width is None else args.display_width
    height = 2400 if args.display_height is None else args.display_height
    if not (0 < width < 1 << 32 and 0 < height < 1 << 32):
        parser.error('display dimensions must be nonzero uint32 values')
    apple = args.ipad_identity or args.iphone_identity
    if (args.unique_chip_id is None) != (args.chip_id is None):
        parser.error('--unique-chip-id and --chip-id must be supplied together')
    if args.unique_chip_id is not None and not (0 < args.unique_chip_id < 1 << 64 and 0 < args.chip_id < 1 << 32):
        parser.error('--unique-chip-id must fit 8 bytes and --chip-id 4 bytes, both nonzero')
    if args.owned_memory_size is not None and (
            not 0x10000000 <= args.owned_memory_size <= 0x200000000
            or args.owned_memory_size % 0x200000):
        parser.error('Owned memory must be 256 MiB to 8 GiB, aligned to 2 MiB')

    try:
        cpu_regs = [int(value, 0) for value in args.cpu_regs.split(',')]
    except ValueError:
        parser.error('--cpu-regs must be integers')
    if (not cpu_regs or cpu_regs[0] != 0 or len(set(cpu_regs)) != len(cpu_regs) or len(cpu_regs) > 8
            or any(reg < 0 or reg > 0xffff or reg & 0xff for reg in cpu_regs)):
        parser.error('--cpu-regs: up to eight distinct Affinity1-numbered cores, boot CPU 0 first')
    cpus = [node('cpu%d' % index, [('state', b'running\0' if index == 0 else b'waiting\0'),
                                   ('reg', struct.pack('<I', reg)),
                                   ('device_type', b'cpu\0'), ('compatible', b'arm,cortex-a715\0'),
                                   ('timebase-frequency', struct.pack('<I', TIMEBASE))])
            for index, reg in enumerate(cpu_regs)]
    tree = node('device-tree', [
        # hw.product/hw.machine/hw.target come from the root 'model'/'target-type' (IODTPlatformExpert);
        # libMobileGestalt prefix-matches hw.product against iPhone/iPod/iPad, so the iPad identity needs both here.
        ('model', b'iPhone10,3\0' if args.iphone_identity else (b'iPad7,1\0' if args.ipad_identity else b'meizu20pro-first-boot\0')),
        *([('target-type', b'D22\0' if args.iphone_identity else b'J120\0')] if apple else []),
        # Root compatible strings of the compiled board DT (qcom,kalama-mtp / qcom,kalama / qcom,mtp).
        ('compatible', b'qcom,kalama-mtp\0qcom,kalama\0qcom,mtp\0'),
        ('#address-cells', struct.pack('<I', 2)),
        ('#size-cells', struct.pack('<I', 2)),
    ], [
        node('chosen', [('random-seed', os.urandom(64)),
                        *([('mac-address-wifi0', wifi_mac)] if wifi_mac is not None else []),
                        # Without these XNU never publishes IOPlatformUUID and gethostuuid() blocks forever
                        # for callers (configd) that pass a zero timeout.
                        *([('unique-chip-id', struct.pack('<Q', args.unique_chip_id)), ('chip-id', struct.pack('<I', args.chip_id))]
                          if args.unique_chip_id is not None else []),
                        # This boots the normal target OS. Volatile Data is
                        # described separately by ephemeral-storage below.
                        ('osenvironment', b'main\0'),
                        # Security status as on a dev-fused, undemoted board (tuple 0,0,1,1), as read by MobileGestalt: mobileactivationd's
                        # DeviceType then takes its native "Hactivation" branch (Activated without an activation record; no SEP or
                        # baseband). Values established on hardware by github.com/dipa-studio/ios-sweet (tools/mkadt.py).
                        *([('certificate-production-status', struct.pack('<I', 0)), ('effective-production-status-ap', struct.pack('<I', 0)),
                           ('certificate-security-mode', struct.pack('<I', 1)), ('effective-security-mode-sep', struct.pack('<I', 1))]
                          if apple else []),
                        # This property describes the actual volatile Data RAM disk. A persistent one is not ephemeral: with the property
                        # datamigrator skips every migration (os_variant_uses_ephemeral_storage), so the staged system apps never install.
                        *([('ephemeral-storage', struct.pack('<I', 1))]
                          if (args.data_ramdisk or args.packed_data_ramdisk) and not args.persistent_data else []),
                        *( [('darwinboot-memory-size', struct.pack('<Q', args.owned_memory_size))]
                           if args.owned_memory_size else [] ),
                        ('dram-base', struct.pack('<Q', DRAM_BASE)),
                        ('dram-size', struct.pack('<Q', args.dram_size)),
                        # FrontBoard's mode validation needs a nonzero point scale.
                        # The iPhone X identity uses a 3x display; 1080x2400 is 360x800 points.
                        *([('display-scale', struct.pack('<I', 3))] if args.iphone_identity else []),
                        # The loader replaces this exact 56-byte placeholder with a checked
                        # GOP mode and final-map reservation in the copied tree at boot.
                        ('negroni-gop-handoff', bytes(56)),
                        # Image4 hash algorithm. The target iPad's DT specifies sha2-384 in /chosen; this
                        # selects the algorithm only and asserts nothing about Apple silicon or fuses.
                        ('crypto-hash-method', b'sha2-384\0'),
                        # The target iPhone tree carries the Image4 boot policy in /chosen: a zeroed
                        # nonce and manifest hash, and "no mix-n-match prevention". Without the status
                        # property the 16.7.16 AppleImage4 panics in its mix-n-match policy lookup.
                        *([('mix-n-match-prevention-status', struct.pack('<I', 0)),
                           ('nonce', bytes(8)),
                           ('boot-manifest-hash', bytes(48))] if apple else []),
                        *( [('root-matching', ('<dict><key>IOProviderClass</key><string>IOMedia</string>'
                                               '<key>IOPropertyMatch</key><dict><key>UUID</key><string>' +
                                               args.root_match_volume_uuid.upper() + '</string></dict></dict>\0').encode())]
                           if args.root_match_volume_uuid else [] ),
                        # Strict policy for the original AMFI: platform code only. This keeps AMFI
                        # enforcing; it does not relax it.
                        ('amfi-only-platform-code', struct.pack('<I', 1)),
                        *([('text-fixups', b''.join(f.encode() + b'\0' for f in args.text_fixup))] if args.text_fixup else []),
                        *empty_nvram_proxy()],
             [node('memory-map', ([("PackedRAMDisk" if args.packed_data_ramdisk else "RAMDisk", bytes(16))] if (args.data_ramdisk or args.packed_data_ramdisk) else []) +
                                 ([('TrustCache', bytes(16))] if args.trust_cache else []) +
                                 ([('GpuAddon', bytes(16))] if args.gpu_addon else [])),
              # Read by the original AppleImage4 at start-up. The 16.7.16
              # Image4 requires the boot nonce hash (BNCH) here; a dev-fused,
              # secure-boot-level-0 board has no nonce, so it is all zeroes.
              node('manifest-properties', [('BNCH', bytes(48))])]),
        node('cpus', children=cpus),
        *([node('psci', [('compatible', b'arm,psci-1.0\0'), ('method', args.psci_method.encode() + b'\0')])] if len(cpus) > 1 else []),
        # Suspend/resume is not implemented for this board. Use the original
        # root-domain policy input rather than allowing the CPU to park forever.
        # kern.maxvnodes: this kernel is built with CONFIG_VNODES=1024 (xsmall), so desiredvnodes clips to 1024 and the desktop's
        # ~1950 live vnodes made vnode jetsam kill idle daemons about once a second (int-102 log). XNU reads the value from /defaults
        # before it clips; this is its own formula, memory/64K + 1024, for the iPad7,1's 4 GiB.
        node('defaults', [('sleep-disabled', struct.pack('<I', 1)), ('kern.maxvnodes', struct.pack('<I', 66560))]),
        node('options', []),
        # The original AppleImage4 panics without a product node. Name and model are the
        # board DT's own strings; the Meizu 20 Pro has power + two volume buttons and no Home button.
        *( [node('buttons', [('compatible', b'buttons\0'), ('device_type', b'buttons\0'),
                             ('button-names', b'hold\0voldown\0volup\0'),
                             ('home-button-type', struct.pack('<I', 2))])] if apple else [] ),
        node('product', [('product-name', b'iPhone X\0' if args.iphone_identity else (b'iPad Pro (12.9-inch) (2nd generation)\0' if args.ipad_identity else b'Meizu 20 Pro\0')),
                         ('model', b'meizu,meizu20pro\0'),
                         *( [('product-description', b'iPhone X\0' if args.iphone_identity else b'iPad Pro (12.9-inch) (2nd generation)\0'),
                             ('artwork-device-idiom', b'phone\0' if args.iphone_identity else b'pad\0'),
                             ('artwork-device-subtype', struct.pack('<I', 2436 if args.iphone_identity else 2732)),
                             ('artwork-scale-factor', struct.pack('<I', 3 if args.iphone_identity else 2)),
                             ('artwork-display-gamut', b'P3\0'),
                             ('compatible-device-fallback', b'iPhone9,2\0' if args.iphone_identity else b'iPad6,7\0'),
                             ('thin-bezel', struct.pack('<I', 1))] if apple else [] )]),
        node('arm-io', [('device_type', b'arm-io\0'),
                        ('#address-cells', struct.pack('<I', 2)),
                        ('#size-cells', struct.pack('<I', 2)),
                        ('ranges', struct.pack('<QQQ', 0, ARMIO_BASE, 0x08000000))],
             [node('intc', [('compatible', b'arm,gic-v3\0'),
                            ('reg', struct.pack('<4Q',
                                                GIC_DIST - ARMIO_BASE, GIC_DIST_SIZE,
                                                GIC_REDIST - ARMIO_BASE, GIC_REDIST_SIZE)),
                            ('redistributor-stride', struct.pack('<Q', 0x40000))]),
              # UFS (0x1d84000) lies below the arm-io window, so a child reg offset would be
              # negative. The node exists only for class matching; NativeUfs takes its
              # physical windows from the board DT constants in GccUfs.h.
              node('ufshc', [('compatible', b'qcom,sm8550-ufshc\0')]),
              # The panel is supplied by the active UEFI GOP. No fixed physical
              # reg: DisplayDxe may choose a different base, stride or mode.
              node('disp0', [('device_type', b'display-subsystem\0'),
                             ('compatible', b'negroni,display\0'),
                             ('canvas-width', struct.pack('<I', width)),
                             ('canvas-height', struct.pack('<I', height))])]),
    ])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(tree)
    print(f'meizu20pro ADT: {len(tree)} bytes, dram {args.dram_size:#x}, timebase {TIMEBASE}')


if __name__ == '__main__':
    main()
