#!/usr/bin/env python3
"""Relocate copied files only; preserve originals under snapshot-nova."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / 'copy-manifest-nova.json').read_text())
PAIRS = [(Path(c['source']), Path(c['target'])) for c in MANIFEST['copies']]
MAP = {str(a): str(b) for a, b in PAIRS}
TOOL_SOURCE = '/Users/ym/unixnu_ports/tools/UEFITool/UEFIExtract/build/uefiextract'
TOOL_TARGET = ROOT / 'inputs-nova/tools-nova/uefiextract'
MAP[TOOL_SOURCE] = str(TOOL_TARGET)
PATTERN = re.compile('(?:' + '|'.join(re.escape(p) for p in sorted(MAP, key=len, reverse=True)) + r''')(?=$|[/\s"'\\}\]),;:])''')
BACKUP = ROOT / 'snapshot-nova/before-path-migration-nova'
REPORT = {'links': [], 'text_files': [], 'quarantined_caches': [], 'remaining_external_links': [], 'missing_links': []}


def remap(text):
    return PATTERN.sub(lambda m: MAP[m.group()], text)


def files_and_links(base):
    for parent, dirs, names in os.walk(base, followlinks=False):
        for name in dirs + names:
            path = Path(parent) / name
            if path.is_symlink() or path.is_file():
                yield path


def backup(path):
    target = BACKUP / path.relative_to(ROOT)
    if target.exists() or target.is_symlink():
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['/bin/cp', '-cRpP', str(path), str(target)], check=True)


def rewrite(path, new):
    old = path.read_bytes()
    data = new.encode()
    if old == data:
        return
    backup(path)
    path.write_bytes(data)
    REPORT['text_files'].append({'path': str(path.relative_to(ROOT)),
                                'before_sha256': hashlib.sha256(old).hexdigest(),
                                'after_sha256': hashlib.sha256(data).hexdigest()})


def main():
    if not MANIFEST.get('copy_complete') or BACKUP.exists():
        raise SystemExit('Requires completed copy and no previous migration.')
    BACKUP.mkdir(parents=True)
    TOOL_TARGET.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['/bin/cp', '-cRpP', TOOL_SOURCE, str(TOOL_TARGET)], check=True)
    for source, target in PAIRS:
        print(f'Relocating {target.name}', flush=True)
        for path in files_and_links(target):
            if path.is_symlink():
                old = os.readlink(path)
                original = source / path.relative_to(target)
                absolute = old if os.path.isabs(old) else os.path.normpath(original.parent / old)
                mapped = remap(absolute)
                if mapped != absolute:
                    new = os.path.relpath(mapped, path.parent)
                    if old != new:
                        backup(path)
                        path.unlink()
                        path.symlink_to(new)
                        REPORT['links'].append({'path': str(path.relative_to(ROOT)), 'before': old, 'after': new})
                continue
            parts = path.relative_to(target).parts
            if '.git' in parts:
                # Keep immutable history and object bytes unchanged.
                if path.name not in ('config', 'gitdir', 'commondir', 'alternates'):
                    continue
            if path.suffix in ('.log', '.map', '.md', '.rst') or '.bak' in path.name:
                continue
            if path.stat().st_size > 32 * 1024 * 1024:
                continue
            data = path.read_bytes()
            if b'\0' in data or not any(p.encode() in data for p in MAP):
                continue
            try:
                text = data.decode('utf-8')
            except UnicodeDecodeError:
                continue
            rewrite(path, remap(text))
    # Directory-derived defaults need suffixes too; do not rename board identity.
    board = ROOT / 'unixnu-meizu20pro-nova'
    assembled = ROOT / 'unixnu-main-nova/out/meizu20pro/tree'
    for base in (board, assembled):
        path = base / 'scripts/build-uefi-meizu.sh'
        if path.exists():
            rewrite(path, path.read_text().replace('$ROOT/../unixnu-main}', '$ROOT/../unixnu-main-nova}')
                    .replace('$ROOT/../uefi-sheng}', '$ROOT/../uefi-sheng-nova}'))
        path = base / 'scripts/import-meizu-uefi.py'
        if path.exists():
            rewrite(path, path.read_text().replace("ROOT.parent / 'uefi-sheng'", repr(str(ROOT / 'uefi-sheng-nova'))))
        path = base / 'scripts/test-meizu-touch-session.py'
        if path.exists():
            rewrite(path, path.read_text().replace("Path.home() / '.hermes/cache/scratch'", 'Path(' + repr(str(ROOT / 'scratch-nova')) + ')'))
    # Serialized caches may contain unrelocatable absolute paths. Preserve them
    # outside the build trees so future compilers regenerate them locally.
    for _, target in PAIRS:
        candidates = []
        for parent, dirs, names in os.walk(target, followlinks=False):
            dirs[:] = [d for d in dirs if d != '.git']
            for name in list(dirs):
                if name in ('__pycache__', 'ModuleCache', 'ModuleCache.noindex'):
                    path = Path(parent) / name
                    if not path.is_symlink():
                        candidates.append(path)
                    dirs.remove(name)
            for name in names:
                if name in ('build.db', '.cache', 'AutoGenCache', 'GlobalVarCache') or name.endswith(('.pickle', '.pkl', '.pcm', '.pch')):
                    path = Path(parent) / name
                    if not path.is_symlink() and path.is_file():
                        candidates.append(path)
        for path in candidates:
            dest = ROOT / 'snapshot-nova/inherited-caches-nova' / path.relative_to(ROOT)
            dest.parent.mkdir(parents=True, exist_ok=True)
            path.rename(dest)
            REPORT['quarantined_caches'].append(str(path.relative_to(ROOT)))
    for _, target in PAIRS:
        for path in files_and_links(target):
            if not path.is_symlink():
                continue
            resolved = path.resolve()
            if not resolved.is_relative_to(ROOT):
                REPORT['remaining_external_links'].append({'path': str(path.relative_to(ROOT)), 'target': str(resolved)})
            if not path.exists():
                REPORT['missing_links'].append(str(path.relative_to(ROOT)))
    (ROOT / 'path-migration-nova.json').write_text(json.dumps(REPORT, indent=2) + '\n')
    print(json.dumps({key: len(value) for key, value in REPORT.items()}), flush=True)
    if any(not row['target'].startswith(('/opt/homebrew/', '/usr/local/')) for row in REPORT['remaining_external_links']):
        raise SystemExit('Unqualified external links remain; see report.')


if __name__ == '__main__':
    main()
