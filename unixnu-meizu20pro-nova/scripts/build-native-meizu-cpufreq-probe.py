#!/usr/bin/env python3
"""Build the opt-in autolog CPU probe with the existing ARM64 XNU recipe.

New file; no previous version. --tree/--source/--out support isolated object
validation without make or changes to active XNU objects. No STARTUP entry.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def nova_path(path, nova, label, exists=True):
    result = path.resolve(strict=exists)
    require(result.is_relative_to(nova), f'{label} must resolve inside Nova: {result}')
    return result


def audit_object(data):
    require(len(data) >= 32, 'Truncated Mach-O object')
    magic, cpu, subtype, kind, count, command_bytes = struct.unpack_from('<6I', data)
    require((magic, cpu, subtype, kind) == (0xfeedfacf, 0x100000c, 0, 1), 'Expected ARM64 Mach-O object')
    require(32 + command_bytes <= len(data), 'Truncated Mach-O commands')
    offset, deployment, startup = 32, [], []
    for _ in range(count):
        cmd, length = struct.unpack_from('<II', data, offset)
        require(length >= 8 and offset + length <= 32 + command_bytes, 'Invalid Mach-O command')
        if cmd == 0x32:
            require(length >= 24, 'Truncated build-version command')
            deployment.append(struct.unpack_from('<II', data, offset + 8))
        if cmd == 0x19:
            require(length >= 72, 'Truncated segment command')
            sections = struct.unpack_from('<I', data, offset + 64)[0]
            require(72 + sections * 80 <= length, 'Truncated section table')
            for index in range(sections):
                name, segment = struct.unpack_from('<16s16s', data, offset + 72 + index * 80)
                name, segment = name.rstrip(b'\0'), segment.rstrip(b'\0')
                if name in (b'__init_entry_set', b'__mod_init_func'):
                    startup.append((segment.decode(), name.decode()))
        offset += length
    require(offset == 32 + command_bytes, 'Command count/size mismatch')
    require(deployment == [(2, 0x110000)] and not startup,
            f'Unexpected deployment or automatic startup entry: {deployment}, {startup}')
    return {'platform': 'ARM64 iOS 17.0', 'startup_section': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--source', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    nova_roots = [path for path in Path(__file__).resolve().parents
                  if path.name == 'nova' and (path / 'AGENTS.md').is_file()]
    require(len(nova_roots) == 1, 'Cannot identify unique Nova workspace')
    nova = nova_roots[0]
    tree = nova_path(args.tree, nova, 'Tree')
    source = nova_path(args.source or tree / 'drivers/NativeMeizuCpuFreqProbe/CpuFreqReadiness.c', nova, 'Source')
    header = nova_path(source.with_suffix('.h'), nova, 'Header')
    out = nova_path(args.out or tree / 'build/native-meizu-cpufreq-probe', nova, 'Output', exists=False)
    record_path = nova_path(tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json', nova, 'Compile record')
    rec = json.loads(record_path.read_text().strip().rstrip(','))
    cwd = nova_path(Path(rec['directory']), nova, 'Compile directory')
    nova_path(Path(rec['file']), nova, 'Recipe source')
    original = rec['arguments']
    require(isinstance(original, list) and all(isinstance(arg, str) for arg in original), 'Malformed compiler arguments')
    compiler = Path(original[0]).resolve(strict=True)
    require(str(compiler).startswith('/Applications/Xcode.app/Contents/Developer/Toolchains/'), 'Unexpected compiler')
    # No shell expansion is used. Audit all inherited absolute path arguments;
    # the Xcode compiler is the only external path in the existing recipe.
    for value in original[1:]:
        for candidate in (value, value.partition('=')[2]):
            if candidate.startswith('/'):
                nova_path(Path(candidate), nova, 'Recipe path', exists=False)
    out.mkdir(parents=True, exist_ok=True)
    obj, archive = out / 'CpuFreqReadiness.o', out / 'libNativeMeizuCpuFreqProbe.a'
    for output in (obj, archive, out / 'build-report.json'):
        require(not output.is_symlink(), 'Refusing a symlink output')
    command, source_replaced, index = [], 0, 0
    while index < len(original):
        value = original[index]
        if value == rec['file']:
            command.append(str(source)); source_replaced += 1
        elif value in ('-o', '-MF', '-MT', '-MJ', '-MQ'):
            require(index + 1 < len(original), 'Missing compile flag argument')
            if value == '-o':
                command += ['-o', str(obj)]
            index += 1
        elif value in ('-MD', '-MMD', '-MP'):
            pass
        elif value == '-Xclang' and index + 1 < len(original) and original[index + 1].startswith('-W'):
            index += 1
        elif not value.startswith('-W'):
            command.append(value)
        index += 1
    require(source_replaced == 1 and command.count('-o') == 1 and '-c' in command, 'Unexpected startup compile record')
    command += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-mgeneral-regs-only',
                '-I', str(tree / '.cache/xnu-work/osfmk')]
    before = {str(path): digest(path) for path in (source, header, record_path)}
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    for name in ('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'OBJC_INCLUDE_PATH',
                 'COMPILER_PATH', 'LIBRARY_PATH', 'SDKROOT', 'DEVELOPER_DIR'):
        env.pop(name, None)
    subprocess.run(command, cwd=cwd, env=env, check=True, timeout=120)
    data = obj.read_bytes()
    report = audit_object(data)
    undefined = subprocess.check_output(['/usr/bin/xcrun', 'nm', '-u', str(obj)], text=True, env=env)
    actual = {line.split()[-1] for line in undefined.splitlines() if line.strip()}
    required = {'_PE_boot_args', '_io_map', '_ml_io_unmap', '_ml_get_interrupts_enabled', '_printf', '_PAGE_SHIFT_CONST'}
    require(actual == required, f'Unexpected external dependencies: {actual}')
    defined = subprocess.check_output(['/usr/bin/xcrun', 'nm', '-gU', str(obj)], text=True, env=env)
    exported = {line.split()[-1] for line in defined.splitlines() if line.strip()}
    require(exported == {'_nova_cpufreq_probe_dump_once', '_nova_cpufreq_get_snapshot'}, f'Unexpected exports: {exported}')
    archive.unlink(missing_ok=True)
    subprocess.run(['/usr/bin/xcrun', 'ar', 'rcs', str(archive), str(obj)], cwd=out, env=env, check=True, timeout=30)
    require(before == {str(path): digest(path) for path in (source, header, record_path)}, 'Build inputs changed during object compilation')
    report.update({'source': str(source), 'source_sha256': before[str(source)],
                   'header': str(header), 'header_sha256': before[str(header)],
                   'record': str(record_path), 'record_sha256': before[str(record_path)],
                   'command': command, 'directory': str(cwd), 'compiler': str(compiler),
                   'object_sha256': hashlib.sha256(data).hexdigest(), 'archive_sha256': digest(archive),
                   'undefined_symbols': sorted(actual), 'exported_symbols': sorted(exported),
                   'full_kernel_build': False, 'device_accessed': False,
                   'entry_policy': 'Explicit autolog invocation only; no automatic startup section'})
    (out / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'm2391 CPU probe: {archive} ({archive.stat().st_size} bytes); ARM64/no-startup/dependency audits passed')


if __name__ == '__main__':
    main()
