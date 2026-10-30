#!/usr/bin/env python3
"""Qualify the owned touch digitizer against this board's own prelinked IOHIDDevice, then archive it.

The reference image is the last successful prelink (out/<device>/kernel/kernel.macho and its plan), so every
instruction byte and ABI check below runs against the exact image this board boots. The Apple component itself
must still hash to the original IOHIDFamily text this port was qualified against, and the owned class must
override exactly the qualified virtual slots.

produce the reference once per build root with ``unixnu build prelink`` (the bootstrap kernel pass leaves the
kernel image it needs), then run this through ``unixnu build kernel``.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-meizu-hid'
OUT.mkdir(parents=True, exist_ok=True)
HEADER_DIR = ROOT / 'build/native-hid-input'
HEADER_DIR.mkdir(parents=True, exist_ok=True)
PACKAGE = ROOT / 'kits/meizu20pro' if (ROOT / 'kits/meizu20pro').is_dir() else ROOT / 'package'
RECORDS = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
REFERENCE = ROOT.parent / 'kernel/kernel.macho'
sys.path.insert(0, str(ROOT / 'kits/NativePrelink'))
from native_prelink import macho, backed_offset, exports  # noqa: E402

# The original IOHIDFamily text of the target kernelcache; every Meizu prelink must carry it unchanged.
HID_TEXT_SHA = 'bb044497e1fe4153f33094011868beb000378a744109c890af9a8d63fa7d3145'


def run(cmd, **kw):
    return subprocess.check_output([str(x) for x in cmd], timeout=180, **kw)


def require(value, why):
    if not value:
        raise SystemExit(why)


if not REFERENCE.is_file() or not REFERENCE.with_suffix('.json').is_file():
    raise SystemExit(f'{REFERENCE} missing: run "unixnu build prelink" once to produce the reference image '
                     '(the bootstrap kernel pass leaves the kernel image it needs)')

b = REFERENCE.read_bytes()
plan = json.loads(REFERENCE.with_suffix('.json').read_text())
m = macho(b)
require(hashlib.sha256(b).hexdigest() == plan['candidate_sha256'], 'Reference image matches its own prelink plan')
require(plan['original_executable_sha256']['com.apple.iokit.IOHIDFamily'] == HID_TEXT_SHA,
        'Reference prelink carries the qualified original IOHIDFamily text')


def read(a, n):
    at = backed_offset(m['segments'], a, n, len(b))
    return b[at:at + n]


names = {n: a for a, ns in exports(b, m).items() for n in ns}
e = next(e for e in plan['extents'] if e['module'] == 'com.apple.iokit.IOHIDFamily' and e['segment'] == '__TEXT_EXEC')
require(hashlib.sha256(read(e['va'], e['size'])).hexdigest() == HID_TEXT_SHA, 'Complete original HID instructions unchanged')
vptr = 0xfffffff005cbe2d0
methods = struct.unpack('<207Q', read(vptr, 207 * 8))
base = struct.unpack('<168Q', read(names['__ZTV9IOService'] + 16, 168 * 8))
changed = [i for i in range(168) if methods[i] != base[i]]
require(changed == [0, 1, 7, 12, 14, 21, 23, 76, 81, 82, 86, 87, 91, 92, 93, 101, 132, 140],
        'Exact original HID native-base overrides')
require([i for i, a in enumerate(methods) if a == names['___cxa_pure_virtual']] == [180] and all(methods)
        and read(vptr + 207 * 8, 24) == bytes(24), 'Exact original 207-slot abstract device')
# 20H392: the protected constructor is at 0x4726b9c; it calls the real
# IOService C2 through the kext's import slot and stores the instance vtable.
require(read(0xfffffff004726b9c, 0x20) == bytes.fromhex('fd7bbfa9fd030091c2d60094c8ac009008410b91080000f9fd7bc1a8c0035fd6'),
        'Original protected constructor and exact vptr store')
require(struct.unpack('<Q', read(0xfffffff005cbae98, 8))[0] == names['__ZN9IOServiceC2EPK11OSMetaClass'],
        'Actual native protected constructor binding')
require(struct.unpack('<Q', read(0xfffffff005cbaea0, 8))[0] == names['__ZN9IOServiceD2Ev'],
        'Actual native protected destructor binding')
# 20H392: start/handleReport slots are the 0x4726c34 / 0x472a730 entries and
# the subclass calls them through the same vtable bindings as before.
require(methods[21] == 0xfffffff004726c34, 'Original start entry')
require(methods[181] == 0xfffffff00472a730, 'Original handleReport entry')
require(read(0xfffffff004726c34, 8) == bytes.fromhex('f44fbea9fd7b01a9'), 'Original start prologue')
require(read(0xfffffff00472a730, 8) == bytes.fromhex('ff0301d1f65701a9'), 'Original handleReport prologue')
header = '''// Generated from the qualified original image; opaque private storage only.
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
class IOHIDDevice : public IOService {
 OSDeclareAbstractStructors(IOHIDDevice)
 unsigned char originalPrivate[0xd0-sizeof(IOService)];
public:
 using IOService::setProperty;
 IOReturn Dispatch(IORPC) override;
 void free() override;
 bool init(OSDictionary *properties=nullptr) override;
 bool setProperty(const OSSymbol *,OSObject *) override;
 bool willTerminate(IOService *,IOOptionBits) override;
 IOReturn configureReport(IOReportChannelList *,IOReportConfigureAction,void *,void *) override;
 IOReturn updateReport(IOReportChannelList *,IOReportUpdateAction,void *,void *) override;
 bool start(IOService *) override;
 void stop(IOService *) override;
 bool handleOpen(IOService *,IOOptionBits,void *) override;
 void handleClose(IOService *,IOOptionBits) override;
 bool handleIsOpen(const IOService *) const override;
 bool matchPropertyTable(OSDictionary *,SInt32 *) override;
 IOReturn message(UInt32,IOService *,void *) override;
 IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **) override;
'''
for i in range(168, 207):
    if i == 171:
        header += ' virtual OSString *newTransportString() const;\n'
    elif i == 173:
        header += ' virtual OSString *newProductString() const;\n'
    elif i == 180:
        header += ' virtual IOReturn newReportDescriptor(IOMemoryDescriptor **) const=0;\n'
    elif i == 181:
        header += ' virtual IOReturn handleReport(IOMemoryDescriptor *,unsigned,IOOptionBits);\n'
    else:
        header += f' virtual __attribute__((deprecated("Unqualified prototype: never call from source"))) void opaque{i}();\n'
header += '};\nstatic_assert(sizeof(IOHIDDevice)==0xd0,"Original device instance size");\n'
(HEADER_DIR / 'OriginalHidDeviceABI.h').write_text(header)

print(run(['python3', ROOT / 'scripts/build-arm-driver.py', 'NativeHidInput'], text=True), end='')
obj = HEADER_DIR / 'NativeHidInput.o'
raw = obj.read_bytes()
om = macho(raw)
st = next(p for c, p, _ in om['commands'] if c == 2)
sy, count, strings, _ = struct.unpack_from('<4I', raw, st + 8)
symbols = []
for i in range(count):
    ix, kind, section, _, va = struct.unpack_from('<IBBHQ', raw, sy + i * 16)
    symbols.append((raw[strings + ix:raw.index(b'\0', strings + ix)].decode(), va, kind))
vt = next(va for name, va, _ in symbols if name == '__ZTV14NativeHidInput')
relocations = {}
for segment in om['segments']:
    for sec in segment['sections']:
        if sec['va'] <= vt < sec['va'] + sec['size']:
            ro, rc = struct.unpack_from('<2I', raw, sec['command_offset'] + 56)
            for i in range(rc):
                offset, bits = struct.unpack_from('<2I', raw, ro + i * 8)
                if bits >> 27 & 1 and bits >> 28 == 0 and bits >> 25 & 3 == 3:
                    relocations[sec['va'] + offset] = symbols[bits & 0xffffff][0]
compiled = [relocations.get(vt + 16 + i * 8) for i in range(207)]
require(all(compiled) and vt + 16 + 207 * 8 not in relocations, 'Compiled exact 207-slot vtable')
bindings = {'__ZN11IOHIDDeviceC2EPK11OSMetaClass': 0xfffffff004726b9c, '__ZN11IOHIDDeviceD2Ev': 0xfffffff004726bbc,
            '__ZN11IOHIDDevice10gMetaClassE': 0xfffffff006a128e0, '__ZTV11IOHIDDevice': vptr - 16}
owned = []
for i, name in enumerate(compiled):
    if name.startswith(('__ZN14NativeHidInput', '__ZNK14NativeHidInput')):
        owned.append(i)
    elif '11IOHIDDevice' in name:
        bindings[name] = methods[i]
    else:
        require(i < 168 and names.get(name) == methods[i], 'Actual inherited native method ' + str(i))
require(owned == [0, 1, 7, 14, 86, 87, 111, 171, 173, 180], 'Only qualified HID methods overridden')
# This kernel compiler resolves qualified super calls through the original vtable.
undefined = {n for n, _, k in symbols if k == 1 and '11IOHIDDevice' in n}
require(undefined == set(bindings), 'All and only actual original HID imports bound')
asm = OUT / 'original-bindings.S'
asm.write_text(''.join(f'.globl {n}\n.set {n}, {a:#x}\n' for n, a in sorted(bindings.items())))
sdk = run(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
run(['xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-isysroot', sdk, '-c', asm, '-o', OUT / 'hid-original-bindings.o'])
llvm = Path(run(['brew', '--prefix', 'llvm'], text=True).strip()) / 'bin'
late = OUT / 'NativeHidInput.late.o'
run([llvm / 'llvm-objcopy', '--remove-section=__DATA,__mod_init_func',
     '--globalize-symbol=__GLOBAL__sub_I_NativeHidInput.cpp', obj, late])


def code(data, image):
    s = next(s for g in image['segments'] for s in g['sections'] if s['name'] == '__text')
    return data[s['fileoff']:s['fileoff'] + s['size']]


require(code(raw, om) == code(late.read_bytes(), macho(late.read_bytes())),
        'Owned code unchanged by deferred metaclass registration')

# The probe is ordinary kernel C++: the same recipe the platform experts use.
recipe = json.loads((RECORDS / 'iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
source = PACKAGE / 'MeizuHidProbe.cpp'
probe = OUT / 'MeizuHidProbe.o'
command, index, arguments = [], 0, recipe['arguments']
while index < len(arguments):
    value = arguments[index]
    if value == recipe['file']:
        command.append(str(source))
    elif value == '-o':
        command.extend(['-o', str(probe)])
        index += 1
    elif value == '-Xclang' and index + 1 < len(arguments) and arguments[index + 1].startswith('-W'):
        index += 1
    elif not value.startswith('-W'):
        command.append(value)
    index += 1
if __import__('os').environ.get('XNU_MEIZU_STORAGE_METADATA') == '1':
    command += ['-DHAVE_MEIZU_STORAGE_METADATA=1']
command += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-c99-designator',
            '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
            '-Wno-unnecessary-virtual-specifier', '-I', str(ROOT / '.cache/xnu-work/osfmk'), '-I', str(PACKAGE)]
result = subprocess.run([str(x) for x in command], cwd=recipe['directory'], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=True)
(OUT / 'MeizuHidProbe.log').write_text(result.stdout)
require(result.returncode == 0, result.stdout[-3500:])
probe_raw = probe.read_bytes()
magic, cpu, subtype, kind = struct.unpack_from('<4I', probe_raw)
require((magic, cpu, subtype, kind) == (0xfeedfacf, 0x100000c, 0, 1), 'Probe object is an ARM64 Mach-O object')

archive = OUT / 'libNativeMeizuHid.a'
archive.unlink(missing_ok=True)
run(['xcrun', 'ar', 'rcs', archive, late, OUT / 'hid-original-bindings.o', probe])
(OUT / 'abi-audit.json').write_text(json.dumps({
    'reference_image_sha256': plan['candidate_sha256'],
    'original_text_sha256': plan['original_executable_sha256']['com.apple.iokit.IOHIDFamily'],
    'original_slots': 207,
    'original_instance_size': 208,
    'owned_slots': owned,
    'bindings': {k: hex(v) for k, v in bindings.items()},
    'source_sha256': hashlib.sha256((ROOT / 'drivers/NativeHidInput/NativeHidInput.cpp').read_bytes()).hexdigest(),
    'probe_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
}, indent=2) + '\n')
print(f'meizu hid: {archive.relative_to(ROOT)} ({archive.stat().st_size} bytes)')
print('PASS: original HID instruction identity, base imports, call boundaries and exact owned vtable')
