#!/usr/bin/env python3
"""Install the checked board hook and compile the Meizu watchdog policy.

The board archive builder applies only its checked kern_exec hunk. --verify-only
checks the hunk and compiles in an isolated directory without editing XNU.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path)
parser.add_argument('--tree', type=Path, default=ROOT)
parser.add_argument('--verify-only', action='store_true', help='Check hook and compile without writing XNU sources')
args = parser.parse_args()
tree = args.tree.resolve()
out = (args.out or tree / 'build/meizu-watchdog-policy').resolve()
record_path = tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/kern_exec.o.json'
record = json.loads(record_path.read_text().strip().rstrip(','))
out.mkdir(parents=True, exist_ok=True)
source = tree / 'kits/meizu20pro/BootWatchdogPolicy.c'
obj = out / 'BootWatchdogPolicy.o'
# The bridge belongs to the board kit, not the shared package.
bridge = tree / 'kits/meizu20pro/meizu-watchdog-kern-exec.patch'
xnu = tree / '.cache/xnu-work'
def patch_check(*flags):
    return subprocess.run(['git', '-C', str(xnu), 'apply', *flags, '--check', str(bridge)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
if patch_check('--reverse').returncode:
    checked = patch_check()
    if checked.returncode:
        raise SystemExit(f'Meizu kern_exec hook context differs: {checked.stderr.strip()}')
    if not args.verify_only:
        subprocess.run(['git', '-C', str(xnu), 'apply', str(bridge)], check=True)
        # A patched source must produce a new kern_exec object at the next
        # real kernel build even if the existing object has a newer mtime.
        (tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/kern_exec.o').unlink(missing_ok=True)
command = []
values = record['arguments']
i = 0
while i < len(values):
    value = values[i]
    if value == record['file']:
        command.append(str(source))
    elif value in ('-o', '-MF', '-MT', '-MJ'):
        if value == '-o':
            command.extend(['-o', str(obj)])
        i += 1
    elif value == '-Xclang' and i + 1 < len(values) and values[i + 1].startswith('-W'):
        i += 1
    elif not value.startswith('-W'):
        command.append(value)
    i += 1
command += ['-mgeneral-regs-only', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']
subprocess.run(command, cwd=record['directory'], check=True, timeout=120)
raw = obj.read_bytes()
magic, cpu, subtype, kind, count, _ = struct.unpack_from('<6I', raw)
if (magic, cpu, subtype, kind) != (0xfeedfacf, 0x100000c, 0, 1):
    raise SystemExit('Expected ARM64 Mach-O object')
deployment = None
at = 32
for _ in range(count):
    cmd, length = struct.unpack_from('<II', raw, at)
    if cmd == 0x32:
        deployment = struct.unpack_from('<II', raw, at + 8)
    at += length
if deployment != (2, 0x110000):
    raise SystemExit(f'Unexpected platform/deployment: {deployment}')
archive = out / 'libMeizuWatchdogPolicy.a'
subprocess.run(['xcrun', 'ar', 'rcs', str(archive), str(obj)], check=True, timeout=30)
print(f'PASS: ARM64 iOS17 policy object and archive: {archive}')
