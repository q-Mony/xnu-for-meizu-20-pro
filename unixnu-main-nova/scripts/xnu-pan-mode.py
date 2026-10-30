#!/usr/bin/env python3
"""Preserve and invalidate every XNU product when the global PAN define changes."""
import argparse
from datetime import datetime, timezone
from pathlib import Path


def prepare(root, mode):
    if mode not in ('0', '1'):
        raise ValueError('XNU_NATIVE_PAN must explicitly be 0 or 1')
    root = Path(root)
    stamp = root / 'ios-obj/.native-pan-mode'
    identity = f'pan-v1:{mode}\n'
    if stamp.is_file() and stamp.read_text() == identity:
        return None
    # Unknown legacy objects are never assumed to match. Preserve all products,
    # including generated headers/assembly and compile records, as one generation.
    backup = root / 'pan-history' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    products = [root / name for name in ('ios-obj', 'ios-sym', 'ios-dst')]
    if any(p.exists() for p in products):
        backup.mkdir(parents=True)
        for path in products:
            if path.exists():
                path.rename(backup / path.name)
        print(f'PAN mode {mode}: preserved previous XNU products at {backup}', flush=True)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    temporary = stamp.with_suffix('.tmp')
    temporary.write_text(identity)
    temporary.replace(stamp)
    return backup


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--mode', required=True, choices=('0', '1'))
    args = parser.parse_args()
    prepare(args.root, args.mode)
