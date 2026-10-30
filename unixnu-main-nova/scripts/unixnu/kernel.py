"""XNU kernel: build from the patched Apple source, then prelink against the original kernelcache."""
import os
import shlex
from pathlib import Path

from . import ipsw
from .util import Error, expand, log, run, sha256_file, tool


def xnu_env(ctx):
    """CONFIG_XNU_* of the defconfig become XNU_* variables of the legacy kernel build script."""
    config = ctx.need_config()
    if ctx.board.get('kernel', {}).get('require_native_pan') and config.get('XNU_NATIVE_PAN') is not True:
        raise Error(f'{ctx.config_path}: this board requires CONFIG_XNU_NATIVE_PAN=y; preserve other selected settings')
    env = {}
    for key, value in config.items():
        if not key.startswith('XNU_'):
            continue
        if isinstance(value, str):
            value = value.replace('{tree}', str(ctx.tree))
        env[key] = '1' if value is True else '0' if value is False else str(value)
    return env


def version_env(modules):
    """Source versions of the Apple references, from feeds.toml, for the kernel scripts."""
    env = {}
    for name, prefix in (('xnu', 'XNU'), ('libplatform', 'LIBPLATFORM'), ('libdispatch', 'LIBDISPATCH'), ('AvailabilityVersions', 'AVAILABILITYVERSIONS')):
        module = modules.get(('refer', name))
        entry = module.source if module and module.source else {}
        if entry.get('ref'):
            env[f'UNIXNU_{prefix}_TAG'] = entry['ref']
        if entry.get('commit'):
            env[f'UNIXNU_{prefix}_COMMIT'] = entry['commit']
    xnu = (modules.get(('refer', 'xnu')) or None) and modules[('refer', 'xnu')].source or {}
    for key, variable in (('darwin', 'UNIXNU_XNU_DARWIN'), ('version', 'UNIXNU_XNU_VERSION'),
                          ('driverkit_tag', 'UNIXNU_DRIVERKIT_TAG'), ('trustcache_tag', 'UNIXNU_TRUSTCACHE_TAG')):
        if xnu.get(key):
            env[variable] = xnu[key]
    return env


def toolchain_path():
    """PATH with the Homebrew LLVM and LLD first, as the archive builders expect."""
    extra = [d for d in ('/opt/homebrew/opt/llvm/bin', '/opt/homebrew/opt/lld/bin', '/usr/local/opt/llvm/bin', '/usr/local/opt/lld/bin')
             if Path(d).is_dir()]
    return os.pathsep.join(extra + [os.environ.get('PATH', '')])


def build_archives(ctx, env):
    """Run the board's archive builders ([kernel] archives of board.toml), in order, then check the prebuilt archives.

    Each entry is a script of the build tree with its arguments. The kernel link needs their archives, and some of
    them read the compile records that an XNU pass leaves behind. A configuration opts in with
    CONFIG_XNU_BUILD_ARCHIVES; the bootstrap pass and first-console configurations have no archives to build.
    """
    if not ctx.need_config().get('XNU_BUILD_ARCHIVES'):
        return
    entries = ctx.board.get('kernel', {}).get('archives', [])
    env = dict(env, PATH=toolchain_path())
    for index, entry in enumerate(entries, 1):
        parts = shlex.split(entry)
        script = ctx.tree / 'scripts' / parts[0]
        if not script.is_file():
            raise Error(f'[kernel] archives: {script} missing')
        log(f'kernel: archive {index}/{len(entries)}: {entry}')
        run(['python3', script, *parts[1:]], cwd=ctx.tree, env=env, log_file=ctx.logs / f'archive-{index:02d}-{Path(parts[0]).stem}.log')
    if ctx.need_config().get('XNU_NEGRONI_DISPLAY') and not any(
            shlex.split(entry)[0] == 'build-negroni-display.py' for entry in entries):
        log('kernel: Negroni display archive')
        run(['python3', ctx.tree / 'scripts/build-negroni-display.py'], cwd=ctx.tree, env=env,
            log_file=ctx.logs / 'archive-negroni-display.log')
    # Archives that no builder produces (they need inputs outside the repositories) must be supplied.
    for key, value in sorted(xnu_env(ctx).items()):
        if key.endswith('_ARCHIVE') and value.endswith('.a') and not Path(value).is_file():
            raise Error(f'{key}: {value} not found; no builder makes it, supply the archive')


def build(ctx, modules):
    log('kernel: XNU')
    for name in ('git', 'make', 'clang', 'cmake', 'ninja', 'unifdef'):
        tool(name, 'brew install ' + name)
    tool('xcodebuild', 'the kernel step builds with the Xcode iphoneos SDK')
    script = ctx.tree / 'scripts/build-xnu-4k-baseline.sh'
    if not script.is_file():
        raise Error(f'{script} missing')
    env = dict(os.environ)
    env.update(xnu_env(ctx))
    env.update(version_env(modules))
    env['JOBS'] = str(ctx.jobs)
    # Invalidate before testing for compile records: archive builders must never
    # replay records from a different PAN generation. The wrapper also guards
    # direct invocations, before any helper can consume the old products.
    run(['python3', ctx.tree / 'scripts/xnu-pan-mode.py', '--root', ctx.tree / 'build/xnu',
         '--mode', env.get('XNU_NATIVE_PAN', '')], cwd=ctx.tree, log_file=ctx.logs / 'pan-mode.log')
    record = ctx.tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json'
    if not record.is_file():
        # The archive builders and the boot progress layer read compile records of the XNU build, which only an earlier
        # pass leaves behind. A first build from scratch therefore runs one pass without them; XNU_BOOTSTRAP makes it
        # skip the archive checks and link without the archives, so it stops at the link and only the records are
        # required of it.
        log('kernel: bootstrap pass for the compile records')
        subprocess_env = {**env, 'XNU_BOOT_PROGRESS': '0', 'XNU_BOOTSTRAP': '1'}
        try:
            run(['bash', script], cwd=ctx.tree, env=subprocess_env, log_file=ctx.logs / 'kernel-bootstrap.log')
        except Error:
            if not record.is_file():
                raise
    storage = ctx.tree / 'build/storage-abi/include/IOKit/storage'
    if (ctx.tree / 'refer/IOStorageFamily').is_dir() and not storage.is_dir():
        log('kernel: prepare storage headers')
        run(['python3', ctx.tree / 'scripts/prepare-storage-headers.py'], cwd=ctx.tree, env=env,
            log_file=ctx.logs / 'storage-headers.log')
    build_archives(ctx, env)
    run(['bash', script], cwd=ctx.tree, env=env, log_file=ctx.logs / 'kernel.log')


def prelink(ctx):
    """Produce out/<device>/kernel/kernel.macho from the XNU build and the original kernelcache."""
    log('kernel: prelink')
    xnu_env(ctx)  # Enforce board requirements for standalone prelink as well.
    spec = ctx.board.get('kernel', {}).get('prelink')
    if not spec:
        raise Error('board.toml has no [kernel.prelink]')
    cache = ctx.option('kernelcache', 'UNIXNU_KERNELCACHE') or (expand(ctx.tree / spec['kernelcache']) if spec.get('kernelcache') else None)
    if not cache:
        # Not given: take it from the IPSW (--ipsw PATH_OR_URL, or the url of board.toml [ipsw]).
        cache = ipsw.kernelcache(ctx, ipsw.Ipsw(ctx))
    if not cache.is_file():
        raise Error(f'kernelcache not found: {cache}')
    wanted = spec.get('kernelcache_sha256')
    if wanted and sha256_file(cache) != wanted:
        raise Error(f'{cache} does not match the kernelcache this board was qualified against (sha256 {wanted})')
    kernel = ctx.tree / spec['kernel']
    if not kernel.is_file():
        raise Error(f'{kernel} not found; run "unixnu build kernel" first')
    output = ctx.work / 'kernel/kernel.macho'
    roots = spec.get('roots', [])
    if ctx.config.get('XNU_NEGRONI_DISPLAY'):
        excluded = {'com.apple.iokit.IOSurface', 'com.apple.iokit.IOMobileGraphicsFamily',
                    'com.apple.iokit.IOGPUFamily', 'com.apple.driver.AppleM2ScalerCSCDriver'}
        conflict = excluded.intersection(roots)
        if conflict:
            raise Error(f'Negroni display conflicts with prelink roots: {", ".join(sorted(conflict))}')
    output.parent.mkdir(parents=True, exist_ok=True)
    # Keep a previous output until all inputs and the root contract have been validated.
    for stale in (output, output.with_suffix('.json')):
        stale.unlink(missing_ok=True)
    command = ['python3', ctx.tree / spec['tool'], '--kernel', kernel, '--prelinked', cache,
               f'--slide={spec["slide"]}', '--output', output]
    for root in roots:
        command += ['--root', root]
    if spec.get('early_security_init'):
        command += ['--early-security-init', spec['early_security_init']]
    command += spec.get('extra_args', [])
    run(command, cwd=ctx.tree, log_file=ctx.logs / 'prelink.log')
    if ctx.board.get('kernel', {}).get('require_native_pan'):
        run(['python3', ctx.tree / 'scripts/verify-xnu-pan.py', output,
             '--output', output.with_name('kernel.pan.json')], cwd=ctx.tree,
            log_file=ctx.logs / 'kernel-pan.log')
    log(f'kernel: {output} sha256 {sha256_file(output)}')
    return output
