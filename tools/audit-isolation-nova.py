#!/usr/bin/env python3
"""Read-only audit of active Nova trees; write only the named Nova report."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
COPIES = json.loads((ROOT / 'copy-manifest-nova.json').read_text())['copies']
TREES = [Path(c['target']) for c in COPIES]
SOURCES = {Path(c['target']): Path(c['source']) for c in COPIES}


def git(work, *args):
    return subprocess.run(['git', '-C', str(work), *args], capture_output=True, text=True,
                          env=dict(os.environ, GIT_OPTIONAL_LOCKS='0'))


def main():
    report = {'active_roots': list(map(str, TREES)), 'git_roots': [], 'git_errors': [],
              'external_tool_links': [], 'foreign_links': [], 'introduced_broken_links': [],
              'inherited_broken_links': [], 'git_metadata_escapes': [],
              'shared_source_inodes': [], 'excluded_other_session_outputs': []}
    for tree in TREES:
        for parent, dirs, files in os.walk(tree, followlinks=False):
            parent = Path(parent)
            if '.git' in dirs or '.git' in files:
                p = git(parent, 'rev-parse', '--absolute-git-dir', '--show-toplevel')
                if p.returncode:
                    report['git_errors'].append(str(parent))
                else:
                    values = p.stdout.splitlines()
                    report['git_roots'].append(str(parent))
                    for value in values:
                        if not Path(value).resolve().is_relative_to(ROOT):
                            report['git_metadata_escapes'].append({'work': str(parent), 'target': value})
            for name in dirs + files:
                path = parent / name
                original = SOURCES[tree] / path.relative_to(tree)
                if path.is_symlink():
                    resolved = path.resolve()
                    if not resolved.is_relative_to(ROOT):
                        category = 'external_tool_links' if str(resolved).startswith(('/opt/homebrew/', '/usr/local/')) else 'foreign_links'
                        report[category].append({'path': str(path), 'target': str(resolved)})
                    if not path.exists():
                        category = 'introduced_broken_links' if original.exists() else 'inherited_broken_links'
                        report[category].append(str(path))
                elif path.is_file() and original.is_file() and not original.is_symlink():
                    a, b = path.stat(), original.stat()
                    if (a.st_dev, a.st_ino) == (b.st_dev, b.st_ino):
                        report['shared_source_inodes'].append(str(path))
                if path.name in ('commondir', 'alternates') and path.is_file() and '.git' in path.parts:
                    for value in path.read_text().splitlines():
                        # alternates entries are based on the objects directory.
                        base = path.parent.parent if path.name == 'alternates' else path.parent
                        resolved = (base / value).resolve()
                        if not resolved.is_relative_to(ROOT):
                            report['git_metadata_escapes'].append({'work': str(path), 'target': str(resolved)})
    report['excluded_other_session_outputs'] = [str(t / 'out-sol') for t in TREES if not (t / 'out-sol').exists()]
    state = json.loads((TREES[0] / 'out/state.json').read_text())
    report['state_device_is_nova'] = Path(state['device']).resolve() == TREES[1]
    report['uefi_binaries_is_nova'] = (TREES[2] / 'Binaries/meizu20pro').resolve() == TREES[1] / '.cache/firmware/meizu20pro/uefi-binaries'
    report['branches'] = {str(p): git(p, 'branch', '--show-current').stdout.strip() for p in
                          (TREES[0], TREES[2], TREES[0] / 'out/meizu20pro/tree/.cache/xnu-work')}
    report['pass'] = all(not report[k] for k in ('git_errors', 'foreign_links', 'introduced_broken_links',
                                               'git_metadata_escapes', 'shared_source_inodes')) and report['state_device_is_nova'] and report['uefi_binaries_is_nova']
    report['limits'] = 'Isolation audit only. Inherited broken links and binary outputs are retained evidence, not proof of a working fresh build. No build or device action was performed.'
    (ROOT / 'isolation-audit-nova.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: len(v) if isinstance(v, list) else v for k,v in report.items() if k not in ('branches', 'limits')}, indent=2))
    raise SystemExit(0 if report['pass'] else 1)


if __name__ == '__main__':
    main()
