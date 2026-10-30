#!/usr/bin/env python3
"""Build the opt-in storage metadata probe.

The probe reads IOBlockStorageDevice's metaclass and vtable out of the running kernel, which
is the only source of those addresses: the target kernelcache keeps no symbols for kext
classes. Gated by the boot argument meizu-storage-metadata=1; it panics with the dump.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-meizu-storage-metadata'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'kits/NativePrelink'))
from native_prelink import macho


def run(command, **kwargs):
    result = subprocess.run([str(v) for v in command], timeout=180, capture_output=True, text=True, **kwargs)
    if result.returncode:
        raise SystemExit(result.stdout[-4000:] + result.stderr[-4000:])
    return result.stdout


source = ROOT / 'kits/meizu20pro/StorageMetadataProbe.cpp'
obj = OUT / 'StorageMetadataProbe.o'
record = json.loads((ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
command, index = [], 0
arguments = record['arguments']
while index < len(arguments):
    argument = arguments[index]
    if argument == record['file']:
        command.append(source)
    elif argument == '-o':
        command += ['-o', obj]
        index += 1
    elif argument == '-Xclang' and arguments[index + 1].startswith('-W'):
        index += 1
    elif not argument.startswith('-W'):
        command.append(argument)
    index += 1
command += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unnecessary-virtual-specifier',
            '-Wno-c99-designator', '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
            '-I', ROOT / '.cache/xnu-work', '-I', ROOT / '.cache/xnu-work/osfmk']
run(command, cwd=record['directory'])
raw = obj.read_bytes()
info = macho(raw)
if info['kind'] != 1:
    raise SystemExit('Require an actual ARM64 subtype-0 Mach-O object')
deployment = [struct.unpack_from('<2I', raw, at + 8) for cmd, at, _ in info['commands'] if cmd == 0x32]
if deployment != [(2, 0x110000)]:
    raise SystemExit('Require iOS17 build target')
archive = OUT / 'libMeizuStorageMetadata.a'
archive.unlink(missing_ok=True)
run(['xcrun', 'ar', 'rcs', archive, obj])
report = {'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
          'gate': 'meizu-storage-metadata=1', 'runtime_qualified': False}
(OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: storage metadata probe archive built')
