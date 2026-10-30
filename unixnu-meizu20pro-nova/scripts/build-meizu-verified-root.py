#!/usr/bin/env python3
"""Sign this board's OS and SystemOS windows with a local owner key and build the verified-root archive.

NativeUfs links with -DNATIVE_VERIFIED_BLOCK=1 and publishes a window only when its reads match
a signed policy, so both windows of the flashed userdata image need their own manifest. 16 KiB
integrity units keep both tables inside the 96 MiB boot partition.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT.parent / 'userdata/userdata-root.img'
# Each window carries its own APFS volume identity; NativeUfs refuses duplicate identities.
UUID = {
    'OS': 'DADD93F8-DBBF-4B17-A1DF-0F11E64172F7',        # SydneyUpdate20H392.D22D221OS
    'SystemOS': '96096B28-B6DC-4BC7-9D3A-2632A0FE53F1',  # SydneyUpdate20H392.D22D221SystemCryptex
}
BLOCK = 16384
OUT = ROOT / 'build/verified-root-os-xnu'
KEYS = ROOT / 'build/verified-root-host/keys'
KEY, PUBLIC = KEYS / 'owner.pem', KEYS / 'owner-public.pem'
WINDOWS = ('OS', 'SystemOS')


def run(command, **kwargs):
    return subprocess.run([str(v) for v in command], check=True, **kwargs)


class Window:
    """Sequential view of exactly one window inside the flashed image."""

    def __init__(self, handle, remaining):
        self.handle = handle
        self.remaining = remaining

    def read(self, count):
        if self.remaining <= 0:
            return b''
        data = self.handle.read(min(count, self.remaining))
        self.remaining -= len(data)
        return data


def windows(image):
    with image.open('rb') as handle:
        header = handle.read(4096)
    if header[:8] != b'SHNGROOT' or struct.unpack_from('<I', header, 8)[0] != 1:
        raise SystemExit('userdata image has no SHNGROOT manifest')
    count = struct.unpack_from('<I', header, 12)[0]
    found = {}
    for index in range(count):
        at = 24 + 128 * index
        name = header[at:at + 32].split(b'\0')[0].decode()
        found[name] = struct.unpack_from('<QQ', header, at + 32)
    for name in WINDOWS:
        if name not in found:
            raise SystemExit(f'userdata image has no {name} window')
    return found


def sign(name, offset, size, policy):
    policy.mkdir(parents=True, exist_ok=True)
    record = policy / 'verification.json'
    stamp = {'window': name, 'image_bytes': size, 'block_bytes': BLOCK, 'volume_uuid': UUID[name],
             'key_sha256': hashlib.sha256(KEY.read_bytes()).hexdigest()}
    digest = hashlib.sha256()
    with IMAGE.open('rb') as handle:
        handle.seek(offset)
        remaining = size
        while remaining:
            chunk = handle.read(min(1 << 20, remaining))
            if not chunk:
                raise SystemExit(f'short {name} window')
            digest.update(chunk)
            remaining -= len(chunk)
    stamp['window_sha256'] = digest.hexdigest()
    cached = json.loads(record.read_text()) if record.is_file() else {}
    fresh = all(cached.get(key) == value for key, value in stamp.items()) and (policy / 'manifest.bin').is_file()
    if not fresh:
        with IMAGE.open('rb') as handle:
            handle.seek(offset)
            module.make(Window(handle, size), size, KEY, PUBLIC, UUID[name], 1, policy)
        block_count = (size + BLOCK - 1) // BLOCK
        last = size - (block_count - 1) * BLOCK
        cached = dict(stamp, blocks_verified=block_count,
                      sample_indices=[0, 1, block_count // 2, block_count - 1],
                      sample_lengths=[BLOCK, BLOCK, BLOCK, last], generation=1,
                      manifest_sha256=hashlib.sha256((policy / 'manifest.bin').read_bytes()).hexdigest())
        record.write_text(json.dumps(cached, indent=2) + '\n')
    metadata = json.loads(record.read_text())
    manifest = policy / 'manifest.bin'
    if hashlib.sha256(manifest.read_bytes()).hexdigest() != metadata['manifest_sha256']:
        raise SystemExit(f'{name} manifest differs from the recorded policy')
    run(['openssl', 'dgst', '-sha256', '-verify', PUBLIC, '-signature', policy / 'signature.bin',
         policy / 'header.bin'], capture_output=True)
    return metadata


if not IMAGE.is_file():
    raise SystemExit(f'missing {IMAGE}')

found = windows(IMAGE)
KEYS.mkdir(parents=True, exist_ok=True)
if not KEY.is_file():
    run(['openssl', 'genrsa', '-out', KEY, 3072], capture_output=True)
    run(['openssl', 'rsa', '-in', KEY, '-pubout', '-out', PUBLIC], capture_output=True)

spec = importlib.util.spec_from_file_location('manifest', ROOT / 'kits/VerifiedRoot/tools/manifest.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

policies = {}
for name in WINDOWS:
    offset, size = found[name]
    policies[name] = sign(name, offset, size, ROOT / ('build/verified-root-' + name.lower()))

os_offset, _ = found['OS']
os_metadata = policies['OS']
samples = bytearray()
with IMAGE.open('rb') as handle:
    for index, length in zip(os_metadata['sample_indices'], os_metadata['sample_lengths']):
        handle.seek(os_offset + index * BLOCK)
        data = handle.read(length)
        if len(data) != length:
            raise SystemExit('short sample unit')
        samples += data

OUT.mkdir(parents=True, exist_ok=True)
manifest_file = {name: ROOT / ('build/verified-root-' + name.lower()) / 'manifest.bin' for name in WINDOWS}
anchor = ROOT / 'build/verified-root-os/trusted-key.bin'
samples_file = OUT / 'samples.bin'
samples_file.write_bytes(samples)
lines = ['.section __DATA_CONST,__const', '.balign 8',
         '.globl _verified_root_manifest, _verified_root_os_manifest',
         '_verified_root_os_manifest:', '_verified_root_manifest:',
         f'.incbin "{manifest_file["OS"]}"',
         '.globl _verified_root_manifest_bytes', '_verified_root_manifest_bytes:',
         '.quad . - _verified_root_manifest', '.balign 8',
         '.globl _verified_root_systemos_manifest', '_verified_root_systemos_manifest:',
         f'.incbin "{manifest_file["SystemOS"]}"', '.balign 8',
         '.globl _verified_root_key', '_verified_root_key:', f'.incbin "{anchor}"', '.balign 8',
         '.globl _verified_root_sample_indices', '_verified_root_sample_indices:',
         '.quad ' + ','.join(map(str, os_metadata['sample_indices'])),
         '.globl _verified_root_sample_lengths', '_verified_root_sample_lengths:',
         '.quad ' + ','.join(map(str, os_metadata['sample_lengths'])),
         '.globl _verified_root_samples', '_verified_root_samples:', f'.incbin "{samples_file}"', '.balign 8',
         '.globl _verified_root_policies, _verified_root_policy_count', '_verified_root_policies:']
for name in WINDOWS:
    lines.append(f'.quad _verified_root_{name.lower()}_manifest, {manifest_file[name].stat().st_size}')
lines += ['_verified_root_policy_count:', '.quad ' + str(len(WINDOWS))]
assembly = OUT / 'inputs.S'
assembly.write_text('\n'.join(lines) + '\n')

recipe = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/bsd_init.o.json'
record_cmd = json.loads(recipe.read_text().strip().rstrip(','))
objects = []
for source in (ROOT / 'kits/VerifiedRoot/src/verified_root.c', ROOT / 'kits/VerifiedRoot/adapters/xnu_crypto.c',
               ROOT / 'scripts/qemu-entry/verified-root-probe.c'):
    destination = OUT / (source.stem + '.o')
    command, index = [], 0
    arguments = record_cmd['arguments']
    while index < len(arguments):
        value = arguments[index]
        if value == record_cmd['file']:
            command.append(str(source))
        elif value == '-o':
            command += ['-o', str(destination)]
            index += 1
        elif value in ('-MF', '-MJ', '-MD', '-MP'):
            pass
        else:
            command.append(value)
        index += 1
    command += ['-I' + str(ROOT / 'kits/VerifiedRoot/include'), '-Werror', '-Wno-missing-include-dirs']
    result = subprocess.run([str(v) for v in command], cwd=record_cmd['directory'],
                            capture_output=True, text=True)
    (OUT / (source.stem + '.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise SystemExit(result.stderr[-3500:])
    objects.append(destination)

sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
subprocess.run(['xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-isysroot', sdk, '-c', str(assembly),
                '-o', str(OUT / 'inputs.o')], check=True)
objects.append(OUT / 'inputs.o')
for obj in objects:
    data = obj.read_bytes()
    if struct.unpack_from('<4I', data) != (0xfeedfacf, 0x100000c, 0, 1):
        raise SystemExit(f'{obj.name} is not an ordinary ARM64 object')
    cursor, deployment = 32, None
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, length = struct.unpack_from('<II', data, cursor)
        if command == 0x32:
            deployment = struct.unpack_from('<II', data, cursor + 8)
        cursor += length
    if deployment != (2, 0x110000):
        raise SystemExit(f'{obj.name} does not target iOS 17')
archive = OUT / 'libVerifiedRootProbe.a'
archive.unlink(missing_ok=True)
run(['xcrun', 'ar', 'rcs', archive, *[str(o) for o in objects]])
(OUT / 'inputs.json').write_text(json.dumps(dict(
    policies=[dict(window=name, image_bytes=found[name][1], blocks=policies[name]['blocks_verified'],
                   manifest_sha256=policies[name]['manifest_sha256'],
                   window_sha256=policies[name]['window_sha256']) for name in WINDOWS],
    volume_uuid=UUID, block_bytes=BLOCK,
    archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest()), indent=2) + '\n')
print(f'verified-root: {len(WINDOWS)} policies, block {BLOCK}, archive {archive.stat().st_size} bytes')
