#!/usr/bin/env python3
"""Compile the generic platform expert against the actual source-kernel ABI."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('component', choices=['ArmVirtPlatform', 'ArmDTPlatform', 'ArmCpu', 'NativeAES', 'NativeVirtioBlock', 'NativeRamBlock', 'NativeMobileDisplay', 'NativeVirtioConsole', 'NativeLegacyDisplay', 'NativeGOPPresenter', 'NativeOfflineGPU', 'NativeOfflineAccelerator', 'NativeIOGPUProviderProbe', 'NativeUfs', 'NativeHidInput', 'NativeKeyStoreEvents', 'NativeAdreno'])
parser.add_argument('--negative-aes-kat', action='store_true', help='Isolated own-source wrong-answer refusal control; never changes original inputs')
parser.add_argument('--verified-root', action='store_true', help='Authenticate all actual disk reads against an independently signed immutable policy')
parser.add_argument('--verified-root-bundle', action='store_true', help='Select exactly one capacity-matching policy per device, then independently authenticate it')
parser.add_argument('--negative-verified-block', action='store_true', help='Negative own-buffer control: corrupt only the completed DMA snapshot')
args = parser.parse_args()
if args.verified_root_bundle and not args.verified_root:
    parser.error('A policy bundle requires verified block mode')
if args.verified_root_bundle and args.negative_verified_block:
    parser.error('Select either bundle policy qualification or the isolated DMA snapshot fault')
if (args.verified_root or args.negative_verified_block) and args.component != 'NativeVirtioBlock':
    parser.error('Verified block mode requires the concrete read-only block transport')
if args.negative_verified_block and not args.verified_root:
    parser.error('The isolated input fault requires verified block mode')
component = args.component
recipe = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
record = json.loads(recipe.read_text().strip().rstrip(','))
source = ROOT / 'drivers' / ('NativeMobileDisplay' if component in ('NativeLegacyDisplay','NativeGOPPresenter') else 'NativeMetal' if component in ('NativeOfflineGPU','NativeOfflineAccelerator','NativeIOGPUProviderProbe') else component) / {'NativeAdreno':'NativeAdrenoService.cpp'}.get(component, component + '.cpp')
out = ROOT / 'build' / {'ArmVirtPlatform': 'armvirt-platform', 'ArmDTPlatform': 'armdt-platform', 'ArmCpu': 'armcpu', 'NativeAES': 'native-aes',
                        'NativeVirtioBlock': 'native-virtio-block', 'NativeRamBlock': 'native-ram-block',
                        'NativeMobileDisplay': 'native-mobile-display', 'NativeVirtioConsole': 'native-virtio-console',
                        'NativeLegacyDisplay': 'native-legacy-display', 'NativeGOPPresenter': 'native-gop-presenter', 'NativeOfflineGPU':'native-offline-gpu','NativeOfflineAccelerator':'native-offline-accelerator','NativeIOGPUProviderProbe':'native-original-provider', 'NativeUfs':'native-ufs', 'NativeHidInput':'native-hid-input', 'NativeKeyStoreEvents':'native-keystore-events', 'NativeAdreno':'native-adreno'}[component]
if args.verified_root:
    out = ROOT / ('build/native-virtio-block-verified-fault' if args.negative_verified_block else 'build/native-virtio-block-verified')
    if args.verified_root_bundle:
        out = ROOT / 'build/native-virtio-block-verified-bundle'
out.mkdir(parents=True, exist_ok=True)
if args.negative_aes_kat:
    if component != 'NativeAES':
        parser.error('The own-source KAT control requires NativeAES')
    out = ROOT / 'build/native-aes-kat-negative'
    out.mkdir(parents=True, exist_ok=True)
    text = source.read_text()
    marker = '0x76,0x49,0xab,0xac'
    if text.count(marker) != 1:
        raise SystemExit('The exact own-source NIST answer was not uniquely identified')
    (out / 'OriginalAESABI.h').write_bytes(source.with_name('OriginalAESABI.h').read_bytes())
    source = out / 'NativeAES.cpp'
    source.write_text(text.replace(marker, '0x77,0x49,0xab,0xac'))
destination = out / (component + '.o')
arguments = record['arguments']
command = []
index = 0
while index < len(arguments):
    value = arguments[index]
    if value == record['file']:
        command.append(str(source))
    elif value == '-o':
        command.extend(['-o', str(destination)])
        index += 1
    elif value == '-Xclang' and arguments[index + 1].startswith('-W'):
        index += 1
    elif not value.startswith('-W'):
        command.append(value)
    index += 1
command.extend(['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-Wno-c99-designator', '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
                '-Wno-unnecessary-virtual-specifier', '-I', str(ROOT / '.cache/xnu-work/osfmk')])
if component in ('NativeVirtioBlock', 'NativeRamBlock', 'NativeUfs'):
    command += ['-I', str(ROOT / 'build/storage-abi/include')]
    if args.verified_root:
        command += ['-I', str(ROOT / 'kits/VerifiedRoot/include'), '-DNATIVE_VERIFIED_BLOCK=1']
    if args.negative_verified_block:
        command += ['-DNATIVE_VERIFIED_BLOCK_INJECT_FAULT=1']
    if args.verified_root_bundle:
        command += ['-DNATIVE_VERIFIED_BLOCK_MULTI_POLICY=1']
if component == 'NativeUfs':
    command += ['-I', str(ROOT / 'kits/VerifiedRoot/include'), '-I', str(ROOT / 'drivers/NativeVirtioBlock'),
                '-DNATIVE_VERIFIED_BLOCK=1', '-DNATIVE_VERIFIED_BLOCK_MULTI_POLICY=1', '-mgeneral-regs-only']
if component=='NativeOfflineGPU':
    command += ['-I',str(ROOT/'drivers/NativeVirtioConsole')]
if component in ('NativeLegacyDisplay','NativeHidInput'):
    command += ['-I', str(out)]
if component == 'NativeAdreno':
    command += ['-I', str(ROOT / 'kits/ArmSmmu4K'), '-I', str(ROOT / 'kits/AdrenoPM4'),
                '-I', str(ROOT / 'kits/sheng'), '-I', str(ROOT / 'kits/AdrenoFirmware'),
                '-I', str(ROOT / 'build/sheng-gmu')]
(out / 'compile-command.json').write_text(json.dumps(command, indent=2) + '\n')
result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
(out / 'compile.log').write_text(result.stdout)
if result.returncode:
    raise SystemExit(result.stdout[-3500:])
data = destination.read_bytes()
magic, cpu, subtype, kind, count, size = struct.unpack_from('<6I', data)
if (magic, cpu, subtype, kind) != (0xfeedfacf, 0x100000c, 0, 1):
    raise SystemExit('Unexpected platform object architecture')
offset = 32
deployment = None
for _ in range(count):
    cmd, length = struct.unpack_from('<II', data, offset)
    if cmd == 0x32:
        deployment = struct.unpack_from('<II', data, offset + 8)
    offset += length
if deployment != (2, 0x110000):
    raise SystemExit(f'Unexpected platform deployment {deployment}')
extra = []
if component == 'NativeUfs':
    # NativeUfs binds IOBlockStorageDevice for every block transport linked into this kernel.
    # NativeRamBlock imports the same symbols and must not define a second set. from the prelinked IOStorageFamily, which
    # keeps no symbol table. Bind each imported symbol to the prelinked image's fixed address:
    # the class vtable sits in __PLK_DATA_CONST (two leading words, so the vptr is +16), C2
    # calls IOService's C2 and stores that vptr, and D2 is the four-byte tail branch to
    # ~IOService in the public source.
    sys.path.insert(0, str(ROOT / 'kits/NativePrelink'))
    from native_prelink import macho
    VTABLE = 0xfffffff005c73f50
    META = 0xfffffff006a091d0
    slots = {
        '__ZN20IOBlockStorageDevice4initEP12OSDictionary': 0xfffffff0045d9e30,
        '__ZNK20IOBlockStorageDevice11getPropertyEPK8OSSymbol': 0xfffffff0045d9e88,
        '__ZN20IOBlockStorageDevice13setPropertiesEP8OSObject': 0xfffffff0045d9f38,
        '__ZN20IOBlockStorageDevice11requestIdleEv': 0xfffffff0045da050,
        '__ZN20IOBlockStorageDevice7doUnmapEP26IOBlockStorageDeviceExtentjj': 0xfffffff0045da05c,
        '__ZN20IOBlockStorageDevice13doSetPriorityEP26IOBlockStorageDeviceExtentjh': 0xfffffff0045da068,
        '__ZN20IOBlockStorageDevice20doGetProvisionStatusEyyPjP35IOBlockStorageProvisionDeviceExtenty': 0xfffffff0045da08c,
        '__ZN20IOBlockStorageDeviceC2EPK11OSMetaClass': 0xfffffff0045d9d98,
        '__ZTV20IOBlockStorageDevice': VTABLE,
        # XNU declares gMetaClass as the MetaClass object itself (class::MetaClass
        # class::gMetaClass), and subclasses pass &super::gMetaClass as superClassLink.
        '__ZN20IOBlockStorageDevice10gMetaClassE': META,
    }
    kernel = ROOT.parent / 'kernel/kernel.macho'
    if kernel.is_file():
        image = kernel.read_bytes()
        layout = macho(image)
        def word(address):
            for segment in layout['segments']:
                if segment['va'] <= address and address + 8 <= segment['va'] + segment['filesize']:
                    return struct.unpack_from('<Q', image, segment['fileoff'] + address - segment['va'])[0]
            raise SystemExit(f'prelink address {address:#x} is not mapped')
        # slot 0/1 are the ABI's leading words; the first virtual (D1 trap) is +16 and
        # getMetaClass is slot 7 of the vptr (vtable + 72). D1/D0 trap on the abstract class.
        for address, value in {VTABLE: 0, VTABLE + 8: 0,
                               VTABLE + 16: 0xfffffff0045d9dbc,
                               VTABLE + 72: 0xfffffff0045d9dc4}.items():
            if word(address) != value:
                raise SystemExit(f'prelink layout changed at {address:#x}; re-derive the storage bindings')
    raw = destination.read_bytes()
    obj = macho(raw)
    sym_at = next(at for cmd, at, _ in obj['commands'] if cmd == 2)
    at, count, strings, _ = struct.unpack_from('<4I', raw, sym_at + 8)
    imported = set()
    for index in range(count):
        ix, kind, _, _, _ = struct.unpack_from('<IBBHQ', raw, at + index * 16)
        # Undefined external symbols only: N_EXT set, N_TYPE == N_UNDF.
        if not kind & 1 or kind & 0x0e:
            continue
        name = raw[strings + ix:raw.index(b'\0', strings + ix)].decode()
        if '20IOBlockStorageDevice' in name:
            imported.add(name)
    thunked = {'__ZN20IOBlockStorageDeviceD2Ev'}
    unknown = sorted(imported - set(slots) - thunked)
    if unknown:
        raise SystemExit(component + ' imports unbound storage symbols: ' + ', '.join(unknown))
    undefined = set()
    for index in range(count):
        ix, kind, _, _, _ = struct.unpack_from('<IBBHQ', raw, at + index * 16)
        if kind & 1 and not kind & 0x0e:
            undefined.add(raw[strings + ix:raw.index(b'\0', strings + ix)].decode())
    lines = ['# Native kernel addresses move with every relink, so the base destructor is a',
             '# tail thunk to the kernel symbol instead of a bound constant.',
             '.text', '.balign 4',
             '.globl __ZN20IOBlockStorageDeviceD2Ev', '__ZN20IOBlockStorageDeviceD2Ev:',
             'b __ZN9IOServiceD2Ev']
    if '_boot_progress_poll' in undefined:
        # ld64 rejects the undefined weak reference to the boot-progress layer, which this
        # board does not build.
        lines += ['.globl _boot_progress_poll', '_boot_progress_poll:', 'ret']
    if '_native_ram_block_gpu_addon' in undefined and component == 'NativeUfs':
        # The GPU addon path is compiled into kern_exec; this board has no addon.
        lines += ['.globl _native_ram_block_gpu_addon', '_native_ram_block_gpu_addon:',
                  'mov w0, #0', 'ret']
    for name in sorted(imported - thunked):
        lines += [f'.globl {name}', f'.set {name}, {slots[name]:#x}']
    bindings = out / ('ufs-original-bindings.S' if component == 'NativeUfs' else 'ram-block-bindings.S')
    bindings.write_text('\n'.join(lines) + '\n')
    bindings_object = out / ('ufs-original-bindings.o' if component == 'NativeUfs' else 'ram-block-bindings.o')
    subprocess.run(['xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-c', bindings,
                    '-o', bindings_object], check=True)
    extra.append(bindings_object)
    (out / 'ufs-bindings.json').write_text(json.dumps(
        dict(vtable=f'{VTABLE:#x}', metaclass=f'{META:#x}', base_dtor='thunk to __ZN9IOServiceD2Ev',
             bound=sorted(imported)), indent=2) + '\n')
archive = out / ('lib' + component + '.a')
archive.unlink(missing_ok=True)
subprocess.run(['xcrun', 'ar', 'rcs', archive, destination, *extra], check=True)
print(f'{component}: -Werror build and actual ARM64/iOS 17 object audit passed')
