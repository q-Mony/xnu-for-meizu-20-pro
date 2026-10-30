#!/usr/bin/env python3
"""Build the Meizu-only notification endpoint; verify without mutating XNU with --verify-only."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tree', type=Path, default=ROOT)
parser.add_argument('--out', type=Path)
parser.add_argument('--verify-only', action='store_true')
args = parser.parse_args()
tree = args.tree.resolve()
xnu = tree / '.cache/xnu-work'
kit = tree / 'kits/meizu20pro' if (tree / 'kits/meizu20pro').is_dir() else ROOT / 'package'
bridge = kit / 'meizu-aks-notify-xnu.patch'
source = kit / 'MeizuKeyStoreNotification.cpp'
records = tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'

# The source is always compiled; an actual build changes XNU only when the
# defconfig switch is 1. Do not silently accept a typo in the switch.
mode = os.environ.get('XNU_MEIZU_AKS_NOTIFY', '0')
if mode not in ('0', '1'):
    raise SystemExit('XNU_MEIZU_AKS_NOTIFY must be 0 or 1')
if not args.verify_only:
    config = tree.parent / '.config'
    if not config.is_file():
        config = ROOT / 'configs/meizu20pro_defconfig'
    text = config.read_text()
    enabled = text.splitlines().count('CONFIG_XNU_MEIZU_AKS_NOTIFY=y') == 1
    if (enabled != (mode == '1')):
        raise SystemExit('XNU_MEIZU_AKS_NOTIFY and selected Meizu config switch disagree')
    flags = next((line for line in text.splitlines() if line.startswith('CONFIG_XNU_EXTRA_CFLAGS=')), '')
    links = next((line for line in text.splitlines() if line.startswith('CONFIG_XNU_EXTRA_LD_ARCHIVES=')), '')
    if mode == '1' and ('-DXNU_MEIZU_AKS_NOTIFY=1' not in flags or
                        '-I{tree}/kits/meizu20pro' not in flags or
                        '-Wl,-force_load,{tree}/build/meizu-aks-notify/libMeizuKeyStoreNotification.a' not in links):
        raise SystemExit('Meizu AKS opt-in requires CFLAGS and archive link wiring')
    if mode == '0':
        print('PASS: Meizu AKS notification endpoint disabled; no archive or XNU edit')
        raise SystemExit(0)
    if tree.name != 'tree' or tree.parent.name != 'meizu20pro':
        raise SystemExit('Meizu AKS notification build requires the Meizu board tree')
    if 'CONFIG_XNU_OWNER_DATA_MOUNT=y' not in text.splitlines() or 'CONFIG_XNU_NATIVE_RAM_BLOCK=y' not in text.splitlines():
        raise SystemExit('Meizu AKS notification requires the owner RAM Data mount')


def check_patch(*flags):
    return subprocess.run(['git', '-C', str(xnu), 'apply', *flags, '--check', str(bridge)],
                          capture_output=True, text=True)


# A default board invocation exits above before touching XNU or archives.
with tempfile.TemporaryDirectory(prefix='meizu-aks-notify-') as temporary:
    isolated = Path(temporary)
    if args.verify_only:
        for part in ('bsd/kern/kern_exec.c', 'iokit/Kernel/IOUserClient.cpp'):
            target = isolated / part
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(xnu / part, target)
        # The checked patch includes the board header through the board kit.
        header = isolated / 'kits/meizu20pro/MeizuAksNotificationPolicy.h'
        header.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(kit / header.name, header)
        subprocess.run(['git', 'apply', str(bridge)], cwd=isolated, check=True)
        sources = [source, isolated / 'bsd/kern/kern_exec.c',
                   isolated / 'iokit/Kernel/IOUserClient.cpp']
    else:
        if mode == '1':
            if check_patch('--reverse').returncode:
                check = check_patch()
                if check.returncode:
                    raise SystemExit(f'Meizu AKS hook context differs: {check.stderr.strip()}')
                subprocess.run(['git', '-C', str(xnu), 'apply', str(bridge)], check=True)
                for path in ('bsd/DEVELOPMENT/kern_exec.o', 'iokit/DEVELOPMENT/IOUserClient.cpo'):
                    (records / path).unlink(missing_ok=True)
        sources = [source]

    built = []
    for index, (path, record_name) in enumerate(zip(sources, (
            'iokit/DEVELOPMENT/IOPlatformExpert.cpo.json',
            'bsd/DEVELOPMENT/kern_exec.o.json',
            'iokit/DEVELOPMENT/IOUserClient.cpo.json'))):
        record = json.loads((records / record_name).read_text().strip().rstrip(','))
        obj = isolated / f'candidate-{index}.o'
        command = []
        values = record['arguments']
        i = 0
        while i < len(values):
            value = values[i]
            if value == record['file']:
                command.append(str(path))
            elif value in ('-o', '-MF', '-MT', '-MJ'):
                if value == '-o':
                    command.extend(['-o', str(obj)])
                i += 1
            elif value == '-Xclang' and i + 1 < len(values) and values[i + 1].startswith('-W'):
                i += 1
            elif not value.startswith('-W'):
                command.append(value)
            i += 1
        # In verification, compile both branches of the new hooks. The
        # production build uses the board's XNU_MEIZU_AKS_NOTIFY defconfig.
        command += ['-DXNU_MEIZU_AKS_NOTIFY=1', '-I', str(kit if index == 2 else (xnu / 'bsd/kern') if index == 1 else path.parent), '-I', str((xnu / 'iokit/Kernel') if index == 2 else path.parent), '-Werror', '-Wno-unused-parameter',
                    '-Wno-unnecessary-virtual-specifier', '-Wno-c99-designator',
                    '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
                    '-Wno-gnu-folding-constant', '-Wno-invalid-offsetof',
                    '-Wno-deprecated-anon-enum-enum-conversion']
        result = subprocess.run(command, cwd=record['directory'], capture_output=True,
                                text=True, timeout=180)
        if result.returncode:
            errors = '\n'.join(line for line in result.stderr.splitlines() if ': error:' in line or 'error generated' in line)
            raise SystemExit(f'{path.name} compile failed:\n{errors or result.stderr[-3000:]}\n{result.stderr[-3200:]}')
        raw = obj.read_bytes()
        if struct.unpack_from('<4I', raw) != (0xfeedfacf, 0x100000c, 0, 1):
            raise SystemExit(f'{path.name}: expected ARM64 Mach-O relocatable')
        built.append(obj)
        print(f'PASS: ARM64 compile {path.name}')

    if not args.verify_only and mode == '1':
        # EXTRA_CFLAGS must be seen by both modified source files even when
        # their old object mtimes are newer than the selected config.
        for path in ('bsd/DEVELOPMENT/kern_exec.o', 'iokit/DEVELOPMENT/IOUserClient.cpo'):
            (records / path).unlink(missing_ok=True)

    # Exercise the archive transform in both modes. Verify-only discards the
    # result, while a selected build installs the archive in its board tree.
    if args.verify_only or mode == '1':
        output = ((args.out or tree / 'build/meizu-aks-notify').resolve()
                  if not args.verify_only else isolated / 'archive-check')
        output.mkdir(parents=True, exist_ok=True)
        obj = output / 'MeizuKeyStoreNotification.o'
        # Keep the metaclass dormant in a kernel archive; publish it only at
        # the gated post-Data hook after preModLoad/postModLoad.
        objcopy = Path(subprocess.check_output(['brew', '--prefix', 'llvm'], text=True).strip()) / 'bin/llvm-objcopy'
        subprocess.run([str(objcopy), '--remove-section=__DATA,__mod_init_func',
                        '--globalize-symbol=__GLOBAL__sub_I_MeizuKeyStoreNotification.cpp',
                        str(built[0]), str(obj)], check=True)
        symbols = subprocess.check_output(['xcrun', 'nm', '-m', str(obj)], text=True)
        if ('external __GLOBAL__sub_I_MeizuKeyStoreNotification.cpp' not in symbols or
                '__ZN14AppleKeyStore' in symbols):
            raise SystemExit('Expected deferred Meizu metaclass only')
        sections = subprocess.check_output(['xcrun', 'otool', '-l', str(obj)], text=True)
        if '__mod_init_func' in sections:
            raise SystemExit('Metaclass would initialize before the volatile Data gate')
        archive = output / 'libMeizuKeyStoreNotification.a'
        archive.unlink(missing_ok=True)
        subprocess.run(['xcrun', 'ar', 'rcs', str(archive), str(obj)], check=True)
        if not args.verify_only:
            print(f'PASS: {archive}')
        else:
            print('PASS: deferred Meizu metaclass archive verified')
