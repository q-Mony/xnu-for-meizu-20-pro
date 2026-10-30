#!/usr/bin/env python3
"""One-time local COW copy; never writes to the input trees."""
import datetime
import json
import os
from pathlib import Path
import stat
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path('/Users/ym/unixnu_ports')
PAIRS = [(SOURCE / name, ROOT / (name + '-nova')) for name in
         ('unixnu-main', 'unixnu-meizu20pro', 'uefi-sheng')]


def inventory(root, exclude=()):
    result = {}
    for parent, dirs, files in os.walk(root, followlinks=False):
        if Path(parent) == root:
            dirs[:] = [d for d in dirs if d not in exclude]
        for name in dirs + files:
            path = Path(parent) / name
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode):
                result[str(path.relative_to(root))] = ['link', os.readlink(path)]
            elif stat.S_ISREG(info.st_mode):
                result[str(path.relative_to(root))] = ['file', info.st_size, info.st_mtime_ns]
    return result


def main():
    for source, target in PAIRS:
        if not source.is_dir() or target.exists():
            raise SystemExit(f'Requires existing input and absent output: {source.name}')
    for name in ('logs-nova', 'snapshot-nova', 'artifacts-nova', 'inputs-nova', 'docs-nova'):
        (ROOT / name).mkdir(exist_ok=True)
    report = {'created_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'method': 'cp -cRpP (APFS clonefile, distinct inodes; never hardlinks)',
              'excluded': ['unixnu-main/out-sol'], 'copies': []}
    for source, target in PAIRS:
        print(f'Inventory and copy {source.name} -> {target.name}', flush=True)
        exclude = ('out-sol',) if source.name == 'unixnu-main' else ()
        before = inventory(source, exclude)
        if exclude:
            target.mkdir()
            for child in sorted(source.iterdir()):
                if child.name not in exclude:
                    subprocess.run(['/bin/cp', '-cRpP', str(child), str(target)], check=True)
        else:
            subprocess.run(['/bin/cp', '-cRpP', str(source), str(target)], check=True)
        after = inventory(source, exclude)
        copied = inventory(target)
        changed = [p for p in before.keys() | after.keys() if before.get(p) != after.get(p)]
        mismatches = [p for p in after.keys() | copied.keys() if after.get(p) != copied.get(p)]
        shared = []
        for rel, item in copied.items():
            if item[0] != 'file' or rel not in after:
                continue
            left, right = (source / rel).stat(), (target / rel).stat()
            if (left.st_dev, left.st_ino) == (right.st_dev, right.st_ino):
                shared.append(rel)
        entry = {'source': str(source), 'target': str(target), 'entries': len(copied),
                 'source_changed_during_copy': changed, 'copy_metadata_mismatches': mismatches,
                 'shared_regular_file_inodes': shared}
        report['copies'].append(entry)
        (ROOT / 'copy-manifest-nova.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({k: len(v) if isinstance(v, list) else v for k,v in entry.items()}), flush=True)
        if changed or mismatches or shared:
            raise SystemExit('Copy requires reconciliation; see copy-manifest-nova.json')
    for name in ('meizu20pro.dtb', 'meizu20pro-dt.dtb'):
        subprocess.run(['/bin/cp', '-cRpP', str(SOURCE / name), str(ROOT / 'inputs-nova' / name)], check=True)
    report['copy_complete'] = True
    (ROOT / 'copy-manifest-nova.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Copy complete. Path migration and isolation audit still required.', flush=True)


if __name__ == '__main__':
    main()
