#!/usr/bin/env python3
"""Bundle the unchanged original 16.7.16 StaticTrustCache and Cryptex1,SystemTrustCache modules.

The two IPSW members are IM4P envelopes around the binary trust cache (version 2, UUID, count,
24-byte entries). DarwinBoot and the kernel binary-search the bundle, so the modules are kept
byte for byte and the entry hashes inside each module must already be ascending.
"""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
def find_cache():
    for base in (ROOT, *ROOT.parents):
        for relative in ('out/ipsw/20H392', 'ipsw/20H392', 'unixnu-main/out/ipsw/20H392'):
            candidate = base / relative
            if candidate.is_dir():
                return candidate
    return ROOT.parent / 'ipsw/20H392'


CACHE = find_cache()
OUT = ROOT / 'build/original-static-system-trust'
COMPONENTS = ('StaticTrustCache', 'Cryptex1,SystemTrustCache')
MODULES = 2


def der_element(data, at):
    if at + 2 > len(data):
        raise SystemExit('truncated DER')
    tag, size = data[at], data[at + 1]
    at += 2
    if size & 0x80:
        count = size & 0x7f
        if not 0 < count <= 4 or at + count > len(data):
            raise SystemExit('invalid DER length')
        size = int.from_bytes(data[at:at + count], 'big')
        at += count
    if at + size > len(data):
        raise SystemExit('DER element exceeds input')
    return tag, data[at:at + size], at + size


def im4p_payload(path):
    data = path.read_bytes()
    tag, sequence, _ = der_element(data, 0)
    if tag != 0x30:
        raise SystemExit(f'{path.name}: not a DER sequence')
    tags, at = [], 0
    while at < len(sequence):
        tag, value, at = der_element(sequence, at)
        tags.append((tag, value))
    names = [value for tag, value in tags if tag == 0x16]
    payload = [value for tag, value in tags if tag == 0x04]
    if len(names) < 3 or names[0] != b'IM4P' or len(payload) != 1:
        raise SystemExit(f'{path.name}: unexpected IM4P layout')
    return payload[0]


def trust_module(path, component):
    sidecar = Path(str(path) + '.json')
    metadata = json.loads(sidecar.read_text()) if sidecar.is_file() else {}
    if metadata.get('component') not in (None, component):
        raise SystemExit(f'{path.name}: sidecar names {metadata["component"]}, expected {component}')
    module = im4p_payload(path)
    if len(module) < 24 or struct.unpack_from('<I', module)[0] != 2:
        raise SystemExit(f'{component}: not a version 2 trust cache')
    uuid = module[4:20]
    count = struct.unpack_from('<I', module, 20)[0]
    if len(module) != 24 + 24 * count:
        raise SystemExit(f'{component}: entry count does not match the module size')
    for index in range(count):
        at = 24 + index * 24
        if not 1 <= module[at + 20] <= 4:
            raise SystemExit(f'{component}: entry {index} has an unsupported type')
        if index and module[at - 24:at - 4] >= module[at:at + 20]:
            raise SystemExit(f'{component}: entries are not in ascending order')
    return module, dict(component=component, entries=count, uuid=uuid.hex(),
                        module_sha256=hashlib.sha256(module).hexdigest())


def main():
    sources = {}
    for sidecar in sorted(CACHE.glob('*.trustcache.json')):
        sources[json.loads(sidecar.read_text())['component']] = Path(str(sidecar)[:-5])
    missing = [name for name in COMPONENTS if name not in sources]
    if missing:
        raise SystemExit(f'missing fetched components {missing}; run "unixnu ipsw fetch {" ".join(COMPONENTS)}"')
    modules, report = [], []
    for name in COMPONENTS:
        module, facts = trust_module(sources[name], name)
        modules.append(module)
        report.append(facts)
    cursor, starts = 4 + 4 * MODULES, []
    for module in modules:
        starts.append(cursor)
        cursor += len(module)
    bundle = struct.pack('<%dI' % (1 + MODULES), MODULES, *starts) + b''.join(modules)
    # The loader's structural checks, run here so a bad bundle never reaches the boot image.
    count = struct.unpack_from('<I', bundle)[0]
    if count != MODULES:
        raise SystemExit('bundle count mismatch')
    for index in range(count):
        start = struct.unpack_from('<I', bundle, 4 + 4 * index)[0]
        end = struct.unpack_from('<I', bundle, 8 + 4 * index)[0] if index + 1 < count else len(bundle)
        if start & 3 or end - start < 24 or struct.unpack_from('<I', bundle, start)[0] != 2:
            raise SystemExit('bundle module header rejected')
        entries = struct.unpack_from('<I', bundle, start + 20)[0]
        if end - start != 24 + entries * 24:
            raise SystemExit('bundle module size rejected')
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'original-static-system.bundle').write_bytes(bundle)
    (OUT / 'bundle.json').write_text(json.dumps(dict(
        modules=report, bundle_sha256=hashlib.sha256(bundle).hexdigest(), bundle_bytes=len(bundle)), indent=2) + '\n')
    print(f'PASS: {MODULES} original trust modules, bundle {len(bundle)} bytes, '
          f'sha256 {hashlib.sha256(bundle).hexdigest()[:16]}')


if __name__ == '__main__':
    main()
