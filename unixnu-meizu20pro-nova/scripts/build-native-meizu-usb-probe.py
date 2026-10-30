#!/usr/bin/env python3
"""Build only the opt-in m2391 GCC probe using an existing XNU compile record.

Normal board build: no arguments, paths relative to the assembled tree.
Staged validation: --tree ACTIVE_TREE --source STAGED_C --out ISOLATED_OUT.
Never invokes make, assembles a tree, or edits active XNU sources/objects.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--source', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    tree = args.tree.resolve()
    source = (args.source or tree / 'drivers/NativeMeizuUsbProbe/UsbReadiness.c').resolve()
    out = (args.out or tree / 'build/native-meizu-usb-probe').resolve()
    record_path = tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json'
    rec = json.loads(record_path.read_text().strip().rstrip(','))
    out.mkdir(parents=True, exist_ok=True)
    obj = out / 'UsbReadiness.o'
    original = rec['arguments']
    command = []
    source_replaced = False
    i = 0
    while i < len(original):
        value = original[i]
        if value == rec['file']:
            command.append(str(source))
            source_replaced = True
        elif value in ('-o', '-MF', '-MT', '-MJ', '-MQ'):
            if value == '-o':
                command += ['-o', str(obj)]
            i += 1
        elif value in ('-MD', '-MMD', '-MP'):
            pass
        elif value == '-Xclang' and i + 1 < len(original) and original[i + 1].startswith('-W'):
            i += 1
        elif not value.startswith('-W'):
            command.append(value)
        i += 1
    if not source_replaced or '-o' not in command or '-c' not in command:
        raise SystemExit('Unexpected startup compile record')
    command += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-mgeneral-regs-only', '-I', str(tree / '.cache/xnu-work/osfmk')]
    subprocess.run(command, cwd=rec['directory'], check=True, timeout=120)
    data = obj.read_bytes()
    magic, cpu, subtype, kind, count, _ = struct.unpack_from('<6I', data)
    if (magic, cpu, subtype, kind) != (0xfeedfacf, 0x100000c, 0, 1):
        raise SystemExit('Expected ARM64 Mach-O object')
    offset, deployment, startup_section = 32, None, False
    for _ in range(count):
        cmd, length = struct.unpack_from('<II', data, offset)
        if cmd == 0x32:
            deployment = struct.unpack_from('<II', data, offset + 8)
        if cmd == 0x19:
            sections = struct.unpack_from('<I', data, offset + 64)[0]
            for j in range(sections):
                sec = offset + 72 + j * 80
                name, segment = struct.unpack_from('<16s16s', data, sec)
                if name.rstrip(b'\0') == b'__init_entry_set' and segment.rstrip(b'\0') == b'__BOOTDATA':
                    startup_section = True
        offset += length
    if deployment != (2, 0x110000) or not startup_section:
        raise SystemExit(f'Unexpected deployment/startup hook: {deployment}, {startup_section}')
    archive = out / 'libNativeMeizuUsbProbe.a'
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', str(archive), str(obj)], check=True, timeout=30)
    undefined = subprocess.check_output(['xcrun', 'nm', '-u', str(obj)], text=True)
    expected = {'_PE_parse_boot_argn', '_io_map', '_kprintf', '_ml_io_unmap',
                '_PAGE_SHIFT_CONST'}
    actual = {line.split()[-1] for line in undefined.splitlines() if line.strip()}
    if actual != expected:
        raise SystemExit(f'Unexpected external dependencies: {actual}')
    report = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'record': str(record_path), 'command': command, 'directory': rec['directory'],
              'object_sha256': hashlib.sha256(data).hexdigest(),
              'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
              'undefined_symbols': sorted(actual), 'startup_section': startup_section,
              'platform': 'ARM64 iOS 17.0', 'full_kernel_build': False}
    (out / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'm2391 USB probe: {archive} ({archive.stat().st_size} bytes); ARM64/startup/dependency audits passed')


if __name__ == '__main__':
    main()
