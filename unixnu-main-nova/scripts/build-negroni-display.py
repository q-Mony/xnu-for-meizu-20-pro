#!/usr/bin/env python3
"""Compile the Negroni built-in display against this target's XNU compile record."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = root / 'drivers/NegroniDisplay'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=root / 'build/negroni-display')
parser.add_argument('--verify', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
names = ('NegroniSurface', 'NegroniFramebuffer')
inputs = [source / (name + '.cpp') for name in names]
inputs.extend(source / name for name in ('NegroniSurface.h', 'FramebufferBounds.h', 'GopHandoffBounds.h'))
digests = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
archive = out / 'libNegroniDisplay.a'
report = out / 'build-report.json'
if args.verify:
    recorded = json.loads(report.read_text())
    if recorded['source_sha256'] != digests or recorded['archive_sha256'] != hashlib.sha256(archive.read_bytes()).hexdigest():
        raise SystemExit('Negroni source or archive changed; rebuild before linking')
    print('verified', archive)
    raise SystemExit(0)
record_path = root / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/compile_commands.json'
records = json.loads(record_path.read_text())
record, = (r for r in records if r['file'].endswith('/IOService.cpp'))
out.mkdir(parents=True, exist_ok=True)
objects = []
for name in names:
    target = out / (name + '.o')
    command = list(record['arguments'])
    command[command.index(record['file'])] = str(source / (name + '.cpp'))
    command[command.index('-o') + 1] = str(target)
    command.append('-DNEGRONI=1')
    result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out / (name + '.log')).write_text(result.stdout)
    if result.returncode:
        raise SystemExit(f'{name} compile failed:\n{result.stdout[-4000:]}')
    objects.append(target)
if archive.exists():
    archive.unlink()
subprocess.run(['/usr/bin/ar', '-rcs', str(archive), *(str(p) for p in objects)], check=True)
report.write_text(json.dumps({'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
                              'source_sha256': digests}, indent=2) + '\n')
print('built', archive)
