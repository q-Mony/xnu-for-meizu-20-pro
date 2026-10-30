#!/usr/bin/env python3
"""Build the opt-in, guarded original-PMGR integration instrument.

The provider is a plain IOPMGR subclass. The original ApplePMGRNub factory
(from the target kernelcache) stores the provider pointer and dispatches to
its vtable, so the provider needs no original ApplePMGR storage or metaclass:
slots 312/313 (the iOS 16.7.16 AON read/write) carry our implementations and
every other PMGR slot stops with a refusal panic.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/sheng-platform-power'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'kits/NativePrelink'))
from native_prelink import macho


def run(command, **kwargs):
    return subprocess.check_output([str(v) for v in command], timeout=90, **kwargs)


def require(value, message):
    if not value:
        raise SystemExit(message)


# Target ApplePMGR vtable geometry (iOS 16.7.16, iPhone X cache).
AON_READ_SLOT = 312
AON_WRITE_SLOT = 313
SLOTS = AON_WRITE_SLOT + 1
# Original ApplePMGRNub factory and its AON dispatch, prelinked (slide -16 MiB).
# Entry, not the third instruction: 0x46832D4 starts with the prologue
# stp x22,x21,[sp,#-0x30]! and its epilogue pops that frame. Entering at
# 0x46832DC skips the allocation and the return raises SP by 0x30, which
# silently corrupts the caller's saved registers and frame.
NUB_CREATE = 0xFFFFFFF0046832D4
NUB_READ = 0xFFFFFFF004685734
NUB_WRITE = 0xFFFFFFF004683DBC

header = '''// Generated slot layout for the target ApplePMGR vtable (iOS 16.7.16).
#pragma once
#include <IOKit/IOPMGR.h>
class ShengPmgrBase : public IOPMGR {
  OSDeclareAbstractStructors(ShengPmgrBase)
public:
  // The original nub reads ApplePMGR state out of its provider; keep the
  // exact target storage and zero it (OSObject's allocation is zeroed).
  unsigned char originalPrivate[0x36968 - sizeof(IOPMGR)];
'''
guards = ''
for i in range(177, SLOTS):
    if i == AON_READ_SLOT:
        header += '  virtual uint32_t readAon() = 0;\n'
        guards += '  uint32_t readAon() override { return 0xffffffffu; }\n'
    elif i == AON_WRITE_SLOT:
        header += '  virtual void writeAon(uint64_t) = 0;\n'
        guards += '  void writeAon(uint64_t) override { }\n'
    else:
        header += f'  virtual void slot{i}() = 0;\n'
        guards += f'  void slot{i}() override {{ unavailable({i}); }}\n'
header += '};\n'
(OUT / 'OriginalPMGRABI.h').write_text(header)
(OUT / 'GuardedPMGRSlots.inc').write_text(guards)
source = ROOT / 'kits/meizu20pro/ShengPlatformPower.cpp'
obj = OUT / 'ShengPlatformPower.o'
record = json.loads((ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
command, i = [], 0
arguments = record['arguments']
while i < len(arguments):
    arg = arguments[i]
    if arg == record['file']:
        command.append(source)
    elif arg == '-o':
        command += ['-o', obj]
        i += 1
    elif arg == '-Xclang' and arguments[i + 1].startswith('-W'):
        i += 1
    elif not arg.startswith('-W'):
        command.append(arg)
    i += 1
command += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-c99-designator',
            '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
            '-Wno-unnecessary-virtual-specifier', '-I', OUT, '-I', ROOT / '.cache/xnu-work/osfmk']
run(command, cwd=record['directory'])
raw = obj.read_bytes()
info = macho(raw)
require(info['kind'] == 1, 'Require actual ARM64 subtype-0 Mach-O object')
build_version = [struct.unpack_from('<2I', raw, at + 8) for cmd, at, _ in info['commands'] if cmd == 0x32]
require(build_version == [(2, 0x110000)], 'Require iOS17 build target')
sym_at = next(at for cmd, at, _ in info['commands'] if cmd == 2)
at, count, strings, _ = struct.unpack_from('<4I', raw, sym_at + 8)
symbols = []
for i in range(count):
    ix, kind, _, _, address = struct.unpack_from('<IBBHQ', raw, at + i * 16)
    symbols.append((raw[strings + ix:raw.index(b'\0', strings + ix)].decode(), address, kind))
vtable = next(va for name, va, _ in symbols if name == '__ZTV18ShengPlatformPower')
relocations = {}
for segment in info['segments']:
    for section in segment['sections']:
        if section['va'] <= vtable < section['va'] + section['size']:
            at, count = struct.unpack_from('<2I', raw, section['command_offset'] + 56)
            for i in range(count):
                offset, bits = struct.unpack_from('<2I', raw, at + i * 8)
                if bits >> 27 & 1 and bits >> 28 == 0 and bits >> 25 & 3 == 3:
                    relocations[section['va'] + offset] = symbols[bits & 0xffffff][0]
entries = [relocations.get(vtable + 16 + i * 8) for i in range(SLOTS)]
require(all(entries) and vtable + 16 + SLOTS * 8 not in relocations,
        f'Exact {SLOTS}-entry provider table')
for slot in range(177, SLOTS):
    require('18ShengPlatformPower' in entries[slot], f'Provider implementation required at slot {slot}')
for slot in (AON_READ_SLOT, AON_WRITE_SLOT):
    require('readAon' in entries[slot] or 'writeAon' in entries[slot],
            f'AON implementation at slot {slot}')
bindings = {'_sheng_original_pmgr_nub_create': NUB_CREATE,
            '_sheng_original_pmgr_nub_read': NUB_READ,
            '_sheng_original_pmgr_nub_write': NUB_WRITE}
asm = OUT / 'original-bindings.S'
asm.write_text(''.join(f'.globl {name}\n.set {name}, {va:#x}\n' for name, va in bindings.items()))
sdk = run(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
run(['xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-isysroot', sdk, '-c', asm, '-o', OUT / 'original-bindings.o'])
late = OUT / 'ShengPlatformPower.late.o'
run(['llvm-objcopy', '--remove-section=__DATA,__mod_init_func',
     '--globalize-symbol=__GLOBAL__sub_I_ShengPlatformPower.cpp', obj, late])


def text_section(blob):
    section = next(s for seg in macho(blob)['segments'] for s in seg['sections'] if s['name'] == '__text')
    return blob[section['fileoff']:section['fileoff'] + section['size']]


require(text_section(raw) == text_section(late.read_bytes()), 'Late registration cannot change instructions')
archive = OUT / 'libShengPlatformPower.a'
archive.unlink(missing_ok=True)
run(['xcrun', 'ar', 'rcs', archive, late, OUT / 'original-bindings.o'])
report = {'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
          'bindings': {name: hex(va) for name, va in bindings.items()},
          'virtual_entries': SLOTS, 'aon_slots': [AON_READ_SLOT, AON_WRITE_SLOT],
          'mode': 'IOPMGR provider; original nub factory; unimplemented slots stop',
          'runtime_qualified': False, 'original_executable_modified': False}
(OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: 314-slot IOPMGR provider; original nub bindings; unimplemented slots stop')
