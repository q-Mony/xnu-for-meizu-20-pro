#!/usr/bin/env python3
"""Build the Meizu boot log archive: the kernel-side frame buffer text console.

Compiled with the exact C flags of the kernel that the tree just built (the
compile record of startup.c), then archived for the force_load link. The module
draws checkpoints, kprintf output and the interrupt counter on the frame buffer
that boot_args describes; see drivers/NativeBootLog/BootLog.c.
"""
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-meizu-bootlog'
OUT.mkdir(parents=True, exist_ok=True)
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
    arguments += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                  '-I', str(ROOT / '.cache/xnu-work/osfmk')]
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
source = ROOT / 'drivers/NativeBootLog/BootLog.c'
obj = OUT / 'BootLog.o'
run(rewrite(c_record, source, obj, ['-mgeneral-regs-only']), cwd=c_record['directory'])
audit(obj)

archive = OUT / 'libNativeMeizuBootLog.a'
archive.unlink(missing_ok=True)
run(['xcrun', 'ar', 'rcs', archive, obj])
print(f'meizu boot log: {archive.relative_to(ROOT)} ({archive.stat().st_size} bytes)')
