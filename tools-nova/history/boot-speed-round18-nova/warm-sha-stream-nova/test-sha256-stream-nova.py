#!/usr/bin/env python3
"""R18 warm-Data streaming SHA-256 validation.

New file; there is no prior version.

The harness compiles the actual production adapter from corecrypto_sha2.c, the
actual scalar corecrypto sources and the actual ARM64 SHA-256 assembly.  Only
the privileged ID_AA64ISAR0_EL1 read and preemption primitives are replaced by
host instrumentation.  It also builds a WarmLoad fixture against the real
production WarmLoad.h template and the production streaming adapter.

Usage:
  test-sha256-stream-nova.py --phase red   --output <fresh-dir>
  test-sha256-stream-nova.py --phase green --output <fresh-dir>
"""

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
from datetime import datetime, timezone

HERE = pathlib.Path(__file__).resolve().parent
NOVA = HERE.parents[2]
XNU = NOVA / 'unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work'
CC = NOVA / 'unixnu-main-nova/out/feeds/corecrypto-2023-5ef10490'
NRB = NOVA / 'unixnu-main-nova/drivers/NativeRamBlock'
CORE_SOURCES = [
    'ccsha2/src/ccsha256_ltc_compress.c',
    'ccsha2/src/ccsha256_ltc_di.c',
    'ccsha2/src/ccsha256_initial_state.c',
    'ccsha2/src/ccsha256_K.c',
    'ccdigest/src/ccdigest_init.c',
    'ccdigest/src/ccdigest_update.c',
    'ccdigest/src/ccdigest_final_64be.c',
]

parser = argparse.ArgumentParser()
parser.add_argument('--phase', choices=('red', 'green'), required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
OUT = pathlib.Path(args.output).resolve()
OUT.mkdir(parents=True, exist_ok=False)

COMMANDS = []
LOG_INDEX = 0
FAILURES = []


def run(cmd, label, cwd=None, required=True):
    """Run a command, retain its raw output and exit code in the evidence dir."""
    global LOG_INDEX
    LOG_INDEX += 1
    cmd = [str(x) for x in cmd]
    cwd = pathlib.Path(cwd or NOVA)
    COMMANDS.append({'label': label, 'cwd': str(cwd), 'argv': cmd})
    try:
        p = subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)
        code, stdout, stderr = p.returncode, p.stdout, p.stderr
    except OSError as e:
        code, stdout, stderr = 127, '', f'{type(e).__name__}: {e}\n'
    (OUT / (f'{LOG_INDEX:02d}-{label}.log')).write_text(
        'ARGV: ' + json.dumps(cmd) + '\nCWD: ' + str(cwd) + f'\nEXIT: {code}\n--- stdout ---\n'
        + stdout + '\n--- stderr ---\n' + stderr)
    (OUT / 'commands-nova.json').write_text(json.dumps(COMMANDS, indent=2) + '\n')
    result = {'label': label, 'exit_code': code, 'stdout': stdout, 'stderr': stderr,
              'argv': cmd, 'cwd': str(cwd)}
    if code and required:
        FAILURES.append(result)
        if args.phase == 'green':
            raise RuntimeError(f'{label} failed ({code}):\n{stderr[-6000:]}\n{stdout[-2000:]}')
    return result


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def prepare_includes():
    include = OUT / 'include/corecrypto'
    include.mkdir(parents=True)
    for p in sorted(CC.glob('*/corecrypto/*.h')):
        q = include / p.name
        if q.exists():
            if q.read_bytes() != p.read_bytes():
                raise RuntimeError(f'conflicting header {p}')
        else:
            q.symlink_to(p)


def generate_adapter(name):
    """Slice the production adapter from its unchanged hardware marker.

    The replacement text changes only privileged operations and the assembly
    symbol used to observe the real compressor; no digest algorithm is copied.
    """
    source = (XNU / 'libkern/crypto/corecrypto_sha2.c').read_text()
    marker = '/* Keep the hardware path local'
    if source.count(marker) != 1:
        raise RuntimeError('production hardware marker not unique')
    adapter = source[source.index(marker):]
    needle = '__asm__ volatile("mrs %0, ID_AA64ISAR0_EL1" : "=r" (isar0));'
    if adapter.count(needle) != 1:
        raise RuntimeError('production feature-read statement not unique')
    adapter = adapter.replace(
        '#include <kern/cpu_data.h>', '/* preemption declarations supplied by harness */')
    adapter = adapter.replace(needle, 'isar0 = test_read_isar0();')
    adapter = adapter.replace('native_sha256_compress_arm64', 'test_hardware_compress')
    adapter = adapter.replace('extern void test_hardware_compress',
                              'static void test_hardware_compress')
    (OUT / name).write_text(adapter)
    return adapter


def header_flags():
    return ['xcrun', 'clang', '-arch', 'arm64', '-O2', '-std=gnu11', '-fblocks',
            '-DCC_USE_ASM=0', '-DCC_DIT_MAYBE_SUPPORTED=0', '-Wno-deprecated-declarations',
            '-I', OUT / 'include', '-I', CC / 'cc/corecrypto', '-I', CC / 'ccsha2/corecrypto',
            '-I', CC / 'ccdigest/corecrypto', '-I', CC / 'ccn/corecrypto', '-I', OUT,
            '-I', XNU / 'libkern']


def cxx_flags():
    return ['xcrun', 'clang++', '-arch', 'arm64', '-O2', '-std=gnu++2b',
            '-DCC_USE_ASM=0', '-DCC_DIT_MAYBE_SUPPORTED=0', '-Wno-deprecated-declarations',
            '-I', OUT / 'include', '-I', CC / 'cc/corecrypto', '-I', CC / 'ccsha2/corecrypto',
            '-I', CC / 'ccdigest/corecrypto', '-I', CC / 'ccn/corecrypto', '-I', OUT,
            '-I', XNU / 'libkern', '-I', NRB]


def build_assembly():
    asm = XNU / 'osfmk/arm64/corecrypto/native_sha256_arm64.s'
    run(['xcrun', 'clang', '-arch', 'arm64', '-x', 'assembler-with-cpp', '-c', asm,
         '-o', OUT / 'native-sha256-host-nova.o'], 'assembly-host-compile-nova')
    dis = run(['xcrun', 'otool', '-tvV', OUT / 'native-sha256-host-nova.o'],
              'assembly-host-disassembly-nova')
    if dis['exit_code'] == 0:
        for instruction in ('sha256h', 'sha256h2', 'sha256su0', 'sha256su1'):
            if instruction not in dis['stdout']:
                raise RuntimeError(f'missing {instruction} in host assembly')
        for regs in ('v0.4s, v1.4s, v2.4s, v3.4s', 'v4.4s, v5.4s, v6.4s, v7.4s',
                     'v16.4s, v17.4s, v18.4s, v19.4s', 'v20.4s, v21.4s, v22.4s, v23.4s',
                     'v24.4s'):
            if not re.search(r'st1(?:\.4s)?\s+\{\s*' + re.escape(regs.replace('.4s', '')) +
                             r'\s*\}', dis['stdout']):
                raise RuntimeError(f'missing vector store {regs}')
    return dis


def check_wiring():
    text = (NRB / 'NativeRamBlock.cpp').read_text()
    anchor = 'NativeRamBlock::warmLoad()'
    if anchor not in text:
        raise RuntimeError('warmLoad implementation not found')
    start = text.index('struct Hash {', text.index(anchor))
    end = text.index('};', start)
    block = text[start:end]
    details = {
        'include_present': '#include <libkern/crypto/native_sha256.h>' in text,
        'stream_init': 'native_sha256_stream_init' in block,
        'stream_update': 'native_sha256_stream_update' in block,
        'stream_final': 'native_sha256_stream_final' in block,
        'legacy_sha256_calls': [n for n in ('SHA256_Init(', 'SHA256_Update(', 'SHA256_Final(')
                                if n in block],
    }
    ok = (details['include_present'] and details['stream_init'] and details['stream_update']
          and details['stream_final'] and not details['legacy_sha256_calls'])
    return ok, details, block


def build_header_probe():
    return run(cxx_flags() + ['-fsanitize=address,undefined', '-g', '-c',
                              HERE / 'header-probe-nova.cpp', '-o', OUT / 'header-probe-nova.o'],
               'header-probe-compile-nova')


CORE_OBJECTS = {}


def compile_core_objects():
    """Compile the real scalar corecrypto sources once, as C."""
    for index, relative in enumerate(CORE_SOURCES):
        source = CC / relative
        obj = OUT / f'core-{index:02d}-{source.stem}.o'
        CORE_OBJECTS[relative] = obj
        rc = run(header_flags() + ['-fsanitize=address,undefined', '-g', '-c', source,
                                   '-o', obj], f'host-core-compile-{index:02d}-{source.stem}-nova',
                 required=False)
        if rc['exit_code'] != 0:
            return rc
    return None


def build_streaming_tests():
    rc = run(header_flags() + ['-fsanitize=address,undefined', '-g', '-c',
                               HERE / 'sha256-stream-cases-nova.c',
                               '-o', OUT / 'sha256-stream-cases-nova.o'],
             'host-streaming-compile-nova', required=False)
    if rc['exit_code'] != 0:
        return rc
    rc = run(header_flags() + ['-fsanitize=address,undefined', '-g',
                               OUT / 'sha256-stream-cases-nova.o',
                               *CORE_OBJECTS.values(),
                               OUT / 'native-sha256-host-nova.o',
                               HERE / 'preserve-vectors-nova.s',
                               '-o', OUT / 'sha256-stream-tests-nova'],
             'host-streaming-link-nova', required=False)
    if rc['exit_code'] == 0:
        rc = run([OUT / 'sha256-stream-tests-nova'], 'host-streaming-test-nova', required=False)
    return rc


def build_warmload():
    rc = run(header_flags() + ['-fsanitize=address,undefined', '-g', '-c',
                               HERE / 'warmload-adapter-nova.c',
                               '-o', OUT / 'warmload-adapter-nova.o'],
             'warmload-adapter-compile-nova', required=False)
    if rc['exit_code'] != 0:
        return rc
    rc = run(cxx_flags() + ['-fsanitize=address,undefined', '-g',
                            HERE / 'warmload-fixture-nova.cpp',
                            OUT / 'warmload-adapter-nova.o',
                            *CORE_OBJECTS.values(),
                            OUT / 'native-sha256-host-nova.o',
                            HERE / 'preserve-vectors-nova.s',
                            '-o', OUT / 'warmload-fixture-nova'],
             'warmload-fixture-compile-nova', required=False)
    if rc['exit_code'] == 0:
        rc = run([OUT / 'warmload-fixture-nova'], 'warmload-fixture-test-nova', required=False)
    return rc


def compile_production_tus():
    results = {}
    cc_records = json.loads((NOVA / 'unixnu-main-nova/out/meizu20pro/tree/build/xnu/ios-obj/'
                             'DEVELOPMENT_ARM64_BCM2837/compile_commands.json').read_text())
    row = next((r for r in cc_records if r['file'].endswith('/corecrypto_sha2.c')), None)
    if row is None:
        raise RuntimeError('corecrypto_sha2.c compile record missing')
    cmd = row['arguments'][:]
    cmd[cmd.index('-o') + 1] = str(OUT / 'corecrypto-sha2-production-nova.o')
    results['corecrypto_sha2'] = run(cmd, 'production-corecrypto-sha2-compile-nova',
                                     pathlib.Path(row['directory']), required=False)

    nrb_recipe = json.loads((NOVA / 'unixnu-main-nova/out/meizu20pro/tree/build/native-ram-block/'
                             'compile-command.json').read_text())
    cmd = nrb_recipe[:]
    cmd[cmd.index('-o') + 1] = str(OUT / 'NativeRamBlock-production-nova.o')
    nrb_cwd = (NOVA / 'unixnu-main-nova/out/meizu20pro/tree/build/xnu/ios-obj/'
               'DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT')
    results['NativeRamBlock'] = run(cmd, 'production-native-ram-block-compile-nova', nrb_cwd,
                                    required=False)
    return results


def manifest(extra):
    files = [XNU / 'libkern/crypto/corecrypto_sha2.c',
             XNU / 'libkern/libkern/crypto/native_sha256.h',
             NRB / 'NativeRamBlock.cpp',
             XNU / 'osfmk/arm64/corecrypto/native_sha256_arm64.s',
             HERE / 'test-sha256-stream-nova.py',
             HERE / 'sha256-stream-cases-nova.c',
             HERE / 'warmload-fixture-nova.cpp',
             HERE / 'warmload-adapter-nova.c',
             HERE / 'header-probe-nova.cpp',
             HERE / 'preserve-vectors-nova.s',
             *[CC / p for p in CORE_SOURCES]]
    data = {'status': 'pass', 'phase': args.phase, 'host': 'arm64 macOS',
            'generated_at': datetime.now(timezone.utc).isoformat(),
            'production_source_sha256': {str(p): sha256(p) for p in files},
            'limits': [
                'The host replaces only the privileged ID_AA64ISAR0_EL1 read and preemption primitives with instrumentation.',
                'The real production adapter, scalar corecrypto sources, ARM64 SHA instructions and WarmLoad template are compiled and executed.',
                'Host timing is not a phone boot-time measurement and no handset saving is claimed.',
                'The WarmLoad image is synthetic; success does not claim APFS or capture validity.',
            ]}
    data.update(extra)
    (OUT / 'results-nova.json').write_text(json.dumps(data, indent=2) + '\n')
    return data


def main():
    prepare_includes()
    generate_adapter('adapter-hardware-nova.inc')
    generate_adapter('adapter-warmload-nova.inc')
    build_assembly()
    core_compile = compile_core_objects()
    wiring_ok, wiring_details, wiring_block = check_wiring()
    (OUT / 'warmload-hash-block-nova.txt').write_text(wiring_block + '\n')
    header_probe = build_header_probe()
    streaming = build_streaming_tests()
    warmload = build_warmload()
    production = compile_production_tus()

    if args.phase == 'red':
        observed = {
            'wiring_uses_streaming': wiring_ok,
            'header_probe_exit': header_probe['exit_code'],
            'streaming_test_exit': streaming['exit_code'],
            'warmload_test_exit': warmload['exit_code'],
            'production_corecrypto_exit': production['corecrypto_sha2']['exit_code'],
            'production_native_ram_block_exit': production['NativeRamBlock']['exit_code'],
        }
        expected = [
            ('wiring_uses_streaming', not wiring_ok,
             'NativeRamBlock warmload Hash still uses the old SHA256_* entry points'),
            ('header_probe_exit', header_probe['exit_code'] != 0,
             'production native_sha256.h does not declare the streaming interface'),
            ('streaming_test_exit', streaming['exit_code'] != 0,
             'production adapter lacks stream symbols, so the streaming harness fails'),
            ('warmload_test_exit', warmload['exit_code'] != 0,
             'WarmLoad fixture cannot call the missing production streaming interface'),
        ]
        matched = []
        for key, condition, reason in expected:
            if condition:
                matched.append({'check': key, 'reason': reason})
        result = {'status': 'red-confirmed' if len(matched) == len(expected) else 'red-unexpected',
                  'phase': 'red', 'matched_expected_failures': matched,
                  'observed': observed, 'wiring': wiring_details,
                  'commands': COMMANDS, 'source_sha256': {
                      str(p): sha256(p) for p in (
                          XNU / 'libkern/crypto/corecrypto_sha2.c',
                          XNU / 'libkern/libkern/crypto/native_sha256.h',
                          NRB / 'NativeRamBlock.cpp')}}
        (OUT / 'red-results-nova.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps({'status': result['status'], 'observed': observed,
                          'matched': [m['check'] for m in matched]}, indent=2))
        if result['status'] != 'red-confirmed':
            return 2
        return 0

    failed = []
    if not wiring_ok:
        failed.append({'check': 'wiring', 'details': wiring_details})
    for label, result in (('header_probe', header_probe), ('streaming', streaming),
                          ('warmload', warmload)):
        if result['exit_code'] != 0:
            failed.append({'check': label, 'exit_code': result['exit_code']})
    for label, result in production.items():
        if result['exit_code'] != 0:
            failed.append({'check': 'production_' + label, 'exit_code': result['exit_code']})
    if failed:
        (OUT / 'green-failures-nova.json').write_text(json.dumps(failed, indent=2) + '\n')
        print(json.dumps({'status': 'fail', 'failures': failed}, indent=2))
        return 1

    data = manifest({
        'status': 'pass',
        'wiring': wiring_details,
        'header_probe': {'exit_code': header_probe['exit_code']},
        'streaming_tests': {'exit_code': streaming['exit_code'], 'stdout': streaming['stdout']},
        'warmload_fixture': {'exit_code': warmload['exit_code'], 'stdout': warmload['stdout']},
        'production_tus': {k: {'exit_code': v['exit_code'], 'argv': v['argv']}
                           for k, v in production.items()},
        'commands': COMMANDS,
    })
    print(json.dumps({'status': data['status'],
                      'wiring': data['wiring'],
                      'streaming': data['streaming_tests']['stdout'].strip().splitlines(),
                      'warmload': data['warmload_fixture']['stdout'].strip().splitlines(),
                      'production_tus': {k: v['exit_code'] for k, v in production.items()}},
                     indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
