#!/usr/bin/env python3
"""Build the Meizu touch archive: GENI I2C engine, Goodix Berlin protocol and the board probe.

The sources are compiled with the exact C and C++ flags of the kernel that the tree just built (the
compile records of startup.c and IOPlatformExpert.cpp), then archived for the force_load link.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-meizu-touch'
OUT.mkdir(parents=True, exist_ok=True)
PACKAGE = ROOT / 'kits/meizu20pro' if (ROOT / 'kits/meizu20pro').is_dir() else ROOT / 'package'
RECORDS = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'


def run(cmd, cwd=None):
    return subprocess.check_output([str(x) for x in cmd], timeout=180, cwd=cwd)


def record(path):
    return json.loads(path.read_text().strip().rstrip(','))


def rewrite(rec, source, obj, extra):
    arguments = []
    values = rec['arguments']
    i = 0
    while i < len(values):
        value = values[i]
        if value == rec['file']:
            arguments.append(str(source))
        elif value in ('-o', '-MF', '-MT', '-MJ'):
            if value == '-o':
                arguments += ['-o', str(obj)]
            i += 1
        elif value == '-Xclang' and i + 1 < len(values) and values[i + 1].startswith('-W'):
            i += 1
        elif not value.startswith('-W'):
            arguments.append(value)
        i += 1
    arguments += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-c99-designator',
                  '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
                  '-Wno-unnecessary-virtual-specifier', '-I', str(ROOT / '.cache/xnu-work/osfmk'),
                  '-I', str(ROOT / 'drivers/NativeGeniI2c'), '-I', str(ROOT / 'drivers/NativeGoodixBerlin'),
                  '-I', str(ROOT / 'drivers/NativeRpmh'),
                  '-I', str(PACKAGE)]
    arguments += [str(x) for x in extra]
    return arguments


def audit(path):
    data = path.read_bytes()
    magic, cpu, subtype, kind, count, _ = struct.unpack_from('<6I', data)
    if (magic, cpu, subtype, kind) != (0xfeedfacf, 0x100000c, 0, 1):
        raise SystemExit(f'{path.name}: not an ARM64 Mach-O object')
    offset, deployment = 32, None
    for _ in range(count):
        command, length = struct.unpack_from('<II', data, offset)
        if command == 0x32:
            deployment = struct.unpack_from('<II', data, offset + 8)
        offset += length
    if deployment != (2, 0x110000):
        raise SystemExit(f'{path.name}: unexpected platform deployment {deployment}')


c_record = record(RECORDS / 'osfmk/DEVELOPMENT/startup.o.json')
cxx_record = record(RECORDS / 'iokit/DEVELOPMENT/IOPlatformExpert.cpo.json')
objects = []
for source, flags in ((ROOT / 'drivers/NativeGeniI2c/GeniI2c.c', ['-mgeneral-regs-only']),
                      (ROOT / 'drivers/NativeGoodixBerlin/GoodixBerlin.c', ['-mgeneral-regs-only']),
                      (ROOT / 'drivers/NativeRpmh/Rpmh.c', ['-mgeneral-regs-only'])):
    obj = OUT / (source.stem + '.o')
    run(rewrite(c_record, source, obj, flags), cwd=c_record['directory'])
    audit(obj)
    objects.append(obj)
probe = PACKAGE / 'MeizuTouchProbe.cpp'
probe_obj = OUT / 'MeizuTouchProbe.o'
run(rewrite(cxx_record, probe, probe_obj, []), cwd=cxx_record['directory'])
audit(probe_obj)
objects.append(probe_obj)

archive = OUT / 'libNativeMeizuTouch.a'
archive.unlink(missing_ok=True)
run(['xcrun', 'ar', 'rcs', archive, *objects])
print(f'meizu touch: {archive.relative_to(ROOT)} ({archive.stat().st_size} bytes)')
