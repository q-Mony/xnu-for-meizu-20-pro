#!/usr/bin/env python3
"""Put boot-progress probes into a COPY of the source XNU kernel (never the original).

There is no serial console on the device and the kernel cannot draw before its
own video console exists, so the early boot stages are reported by side
channels. The stage code lives in the zero padding after the kernel entry
(_start is one `b`) and displaced instructions are re-executed there:

  stage 1 (kernel entry, MMU off)       blue bar, then continues
  stage 2 (just before the MMU is on)   magenta bar, then continues
  stage 3 (right after the MMU is on)   waits --reset-delay seconds, then PSCI
                                        SYSTEM_RESET. A reboot by itself after
                                        that time shows the CPU got through the
                                        MMU enable; the frame buffer is not
                                        mapped by then, so time is the signal.
  panic screen (--panic-screen)         panic() and its common panic funnel draw
                                        their format string on the
                                        kernel's own frame buffer mapping and
                                        stops (scripts/xnu-panic-screen.c).
                                        --no-reset leaves out stages 3 and 4.
  stage 4 (--reset-after SYMBOL)        the same wait and reset, but placed after
                                        the call to SYMBOL in --in FUNCTION (the
                                        Nth call, --occurrence). It replaces
                                        stage 3, so one run asks one question:
                                        did the kernel get past that call?

  exception screen (--exception-screen)
                                        white bar right after the MMU is on, then
                                        VBAR_EL1 points at a synchronous vector
                                        that draws ESR_EL1, ELR_EL1, FAR_EL1,
                                        SCTLR_EL1 and TCR_EL1 on the frame
                                        buffer and stops
                                        (scripts/xnu-exception-screen.c). It
                                        replaces stages 3 and 4, which share
                                        the padding.

  boot log console (--blog, needs --exception-screen)
                                        the bars become a text console on the
                                        frame buffer: stage 1 draws "ENTRY" at
                                        the kernel entry, stage 3 prints the
                                        MMU state and the console rebases itself
                                        onto the bootstrap window, and --hook /
                                        --hook-kv / --hook-irq add a line for
                                        each checked function, switch the
                                        console to the kernel's own video
                                        mapping and count interrupts. Every
                                        line is numbered in the order drawn
                                        (drivers/NativeBootLog/BootLog.c).

The bars are the 1/16th bands of the frame buffer below the green one that
DarwinBoot paints (band index = stage). Each patch site is checked against its
expected original instruction first, and the tool refuses a kernel that does
not match. Offsets are specific to the kernel built by build-xnu-4k-baseline.sh.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ENTRY_VA = 0xFFFFFFF00B9D0488        # LC_UNIXTHREAD pc, `b start_first_cpu`
START_FIRST_CPU = 0xFFFFFFF00B9D205C
MMU_ENABLE_VA = 0xFFFFFFF00B9D2330   # msr SCTLR_EL1, x0
AFTER_MMU_VA = 0xFFFFFFF00B9D2350    # mov x0, #0x300000
PANIC_VA = 0xFFFFFFF00BF69B04      # _panic
VINFO_VA = 0xFFFFFFF00C0A2970      # _vinfo
PANIC_CAVE_END = 0xFFFFFFF00B9D0FF0
CAVE_SIZE = 0x60
CAVES = {1: 0xFFFFFFF00B9D0500, 2: 0xFFFFFFF00B9D0560, 3: 0xFFFFFFF00B9D05C0, 4: 0xFFFFFFF00B9D0620}
TIMEBASE = 19200000

PAINT = """
  ldr x1, [{args}, #0x28]
  ldr x2, [{args}, #0x38]
  ldr x3, [{args}, #0x48]
  mul x2, x2, x3
  lsr x2, x2, #6
  bic x2, x2, #7
  mov x4, #{band}
  mul x5, x2, x4
  add x1, x1, x5
  movz w6, #{lo}
  movk w6, #{hi}, lsl #16
1:
  stp w6, w6, [x1], #8
  subs x2, x2, #8
  b.hi 1b
  dsb sy
"""

SOURCES = {
    1: ".text\n.global _start\n_start:\n" + PAINT.format(args='x0', band=1, lo='0x00FF', hi='0xFF00') + "  b target\n",
    2: ".text\n.global _start\n_start:\n" + PAINT.format(args='x20', band=2, lo='0x00FF', hi='0xFFFF')
       + "  msr SCTLR_EL1, x0\n  b target\n",
    3: """.text
.global _start
_start:
  mov x0, #0x300000
  mrs x1, cntpct_el0
  ldr x2, ={ticks}
  add x2, x1, x2
1:
  isb
  mrs x1, cntpct_el0
  cmp x1, x2
  b.lo 1b
  movz w0, #0x0009
  movk w0, #0x8400, lsl #16
  mov x1, xzr
  mov x2, xzr
  mov x3, xzr
  smc #0
2:
  wfe
  b 2b
""" + ".ltorg\n",
    4: """.text
.global _start
_start:
  bl callee
  mrs x1, cntpct_el0
  ldr x2, ={ticks}
  add x2, x1, x2
1:
  isb
  mrs x1, cntpct_el0
  cmp x1, x2
  b.lo 1b
  movz w0, #0x0009
  movk w0, #0x8400, lsl #16
  mov x1, xzr
  mov x2, xzr
  mov x3, xzr
  smc #0
2:
  wfe
  b 2b
""" + ".ltorg\n",
}

RESET_BEFORE = """.text
.global _start
_start:
  movz w0, #0x0009
  movk w0, #0x8400, lsl #16
  mov x1, xzr
  mov x2, xzr
  mov x3, xzr
  smc #0
1:
  wfe
  b 1b
"""

# --exception-screen: a different stage 2-4 chain. Stage 2 is the MMU site, as
# before, but it installs the vector table first so a fault during or after the
# MMU enable is reported. Stage 3 is the post-MMU checkpoint (white bar, boot
# args saved), stage 4 cleans the bar so the display sees it and returns.
EXC_TABLE_OFF = 0x300      # relative to cave 1: 2 KiB-aligned vector table
EXC_CODE_OFF = 0x600       # relative to cave 1: reporter code and font
EXC_SCRATCH_OFF = 0x190    # relative to cave 1: boot args pointer slot
MAP_FLAG_OFF = 0x1A0       # relative to cave 1: did the frame buffer get mapped
MAP_CODE_OFF = 0x1A8       # relative to cave 1: frame buffer bootstrap mapping
BAND_CODE_OFF = 0x2B8      # relative to cave 1: band painter for the post-MMU checkpoints
HOOK_AREA = 0x800          # checkpoint stubs after the low vector table (zero padding)

STAGE2_EXCEPTION = """
.text
.global _start
_start:
  adr x9, .
  add x10, x9, #{scratch_delta}
  str x20, [x10]
  add x9, x9, #{table_delta}
  msr VBAR_EL1, x9
  isb
""" + PAINT.format(args='x20', band=2, lo='0x00FF', hi='0xFFFF') + """
  b map
"""

# Extend the V=P bootstrap map with the frame buffer (Device memory) so the
# console can draw from the instant the MMU is on. The window map lives at the
# kernel's physical base, above the frame buffer, so its L1 slot and L2 table
# already exist: L1[S] is a table, and the blocks below it are free. The map
# covers the panel only, is clamped so it can never spill into the window
# blocks, and sets FLAG so the console knows the panel is writable.
STAGE2_MAP = """
.text
.global _start
_start:
  ldr x1, [x20, #0x28]
  cbz x1, .Ldone
  tst x1, #0x1fffff
  b.ne .Ldone
  ldr x2, [x20, #0x38]
  ldr x3, [x20, #0x48]
  mul x2, x2, x3
  mov x14, #0x1fffff
  add x2, x2, x14
  lsr x2, x2, #21
  cbz x2, .Ldone
  mrs x9, ttbr0_el1
  lsr x9, x9, #12
  lsl x9, x9, #12
  ubfx x10, x1, #30, #9
  ldr x11, [x9, x10, lsl #3]
  and x14, x11, #3
  cbz x14, 4f
  cmp x14, #3
  b.eq 5f
  b .Lflag
4:
  /* The window map starts above 4 GiB, so the frame buffer's 1 GiB slot has no
   * table: install a 1 GiB Device block instead. */
  lsr x11, x1, #30
  lsl x11, x11, #30
  mov x13, #0x40d
  orr x11, x11, x13
  str x11, [x9, x10, lsl #3]
  dsb sy
  b .Lflag
5:
  lsr x11, x11, #12
  lsl x11, x11, #12
  ubfx x12, x1, #21, #9
  mov x15, #12
  cmp x2, x15
  csel x2, x2, x15, lo
  cbz x2, .Ldone
  mov x13, #0x40d
1:
  lsr x14, x1, #21
  lsl x14, x14, #21
  orr x14, x14, x13
  str x14, [x11, x12, lsl #3]
  add x1, x1, #0x200000
  add x12, x12, #1
  subs x2, x2, #1
  b.ne 1b
  dsb sy
.Lflag:
  adr x9, FLAG
  mov w10, #1
  strb w10, [x9]
  /* Paint band 3 now, still with the MMU off: a white strip proves the frame
   * buffer is in the bootstrap map even if everything after the enable fails. */
  ldr x3, [x20, #0x28]
  ldr x4, [x20, #0x38]
  ldr x5, [x20, #0x48]
  mul x4, x4, x5
  lsr x4, x4, #4
  bic x4, x4, #7
  add x5, x4, x4, lsl #1
  add x3, x3, x5
  movz w6, #0xffff
  movk w6, #0xffff, lsl #16
3:
  stp w6, w6, [x3], #8
  subs x4, x4, #8
  b.hi 3b
  dsb sy
.Ldone:
  msr SCTLR_EL1, x0
  isb
  b post
"""

# Band painter used after the MMU is on: x0 selects the 1/16 strip, w1 is the
# ARGB colour. Called only when the bootstrap map reported success.
BLOG_BAND = """
.text
.global _start
_start:
  ldr x3, [x20, #0x28]
  ldr x4, [x20, #0x38]
  ldr x5, [x20, #0x48]
  mul x4, x4, x5
  lsr x4, x4, #4
  bic x4, x4, #7
  mov x5, x4
  mul x5, x5, x0
  add x3, x3, x5
1:
  stp w1, w1, [x3], #8
  subs x4, x4, #8
  b.hi 1b
  dsb sy
  ret
"""

STAGE3_CHECKPOINT = """
.text
.global _start
_start:
  ldr x10, =scratch
  str x20, [x10]
""" + PAINT.format(args='x20', band=4, lo='0xFFFF', hi='0xFFFF') + """
  b next
"""

# --blog: the kernel-side frame buffer console takes over once the MMU is on.
# Stage 1 stays pure assembly: at the kernel entry the frame buffer is still
# Device memory and a call 6 MB into the image is the first code those pages
# ever run, so the proven colour band marks the entry instead.
STAGE1_BLOG = """
.text
.global _start
_start:
""" + PAINT.format(args='x0', band=1, lo='0x00FF', hi='0xFF00') + """  b target
"""

# Stage 3 runs with the MMU on and SP_EL1 mapped. When stage 2 managed to add
# the frame buffer to the bootstrap map, the console draws here and for every
# checkpoint until arm_vm_init drops that map; otherwise it only records them.
STAGE3_BLOG = """
.text
.global _start
_start:
  stp x0, x30, [sp, #-0x20]!
  ldr x1, =FLAG
  ldrb w1, [x1]
  mov x0, x20
  bl native_blog_start
  ldr x9, =FLAG
  ldrb w9, [x9]
  cbz w9, 1f
  mov w0, #4
  movz w1, #0xffff
  movk w1, #0x00ff, lsl #16
  bl BAND
1:
  mrs x1, sctlr_el1
  adr x0, .Lenable
  bl native_blog_stage
  ldp x0, x30, [sp], #0x20
  b next
.Lenable:
  .asciz "SCTLR"
"""

# The console owns the screen; stage 4 only restores the register the displaced
# instruction would have set.
STAGE4_BLOG = """
.text
.global _start
_start:
  mov x0, #0x300000
  b target
"""

# One checkpoint stub: save the argument registers and the return address, draw
# the line, then replay the displaced first instruction and continue. x8 and
# NZCV travel too, because a callee may read either before writing it.
HOOK_STUB = """
.text
.global _start
_start:
  stp x0, x1, [sp, #-0x60]!
  stp x2, x3, [sp, #0x10]
  stp x4, x5, [sp, #0x20]
  stp x6, x7, [sp, #0x30]
  stp x8, x30, [sp, #0x40]
  mrs x9, nzcv
  str x9, [sp, #0x50]
{kv}{keep}{vec}
  ldr x1, [sp]
  adr x0, .Llabel
  bl native_blog_stage
  ldr x9, [sp, #0x50]
  msr nzcv, x9
  ldp x8, x30, [sp, #0x40]
  ldp x6, x7, [sp, #0x30]
  ldp x4, x5, [sp, #0x20]
  ldp x2, x3, [sp, #0x10]
  ldp x0, x1, [sp], #0x60
  .inst {original:#010x}
  b back
.Llabel:
  .asciz "{label}"
"""

# The interrupt entry runs on every IRQ, so its stub only bumps a counter; the
# console draws that counter every 64 interrupts.
IRQ_STUB = """
.text
.global _start
_start:
  stp x0, x1, [sp, #-0x40]!
  stp x2, x3, [sp, #0x10]
  stp x4, x5, [sp, #0x20]
  mrs x9, nzcv
  stp x30, x9, [sp, #0x30]
  bl native_blog_irq
  ldp x30, x9, [sp, #0x30]
  msr nzcv, x9
  ldp x4, x5, [sp, #0x20]
  ldp x2, x3, [sp, #0x10]
  ldp x0, x1, [sp], #0x40
  .inst {original:#010x}
  b back
"""

FIQ_STUB = """
.text
.global _start
_start:
  stp x0, x1, [sp, #-0x40]!
  stp x2, x3, [sp, #0x10]
  stp x4, x5, [sp, #0x20]
  mrs x9, nzcv
  stp x30, x9, [sp, #0x30]
  bl native_blog_fiq
  ldp x30, x9, [sp, #0x30]
  msr nzcv, x9
  ldp x4, x5, [sp, #0x20]
  ldp x2, x3, [sp, #0x10]
  ldp x0, x1, [sp], #0x40
  .inst {original:#010x}
  b back
"""

STAGE4_CLEAN = """
.text
.global _start
_start:
  ldr x1, [x20, #0x28]
  ldr x2, [x20, #0x38]
  ldr x3, [x20, #0x48]
  mul x2, x2, x3
  lsr x2, x2, #4
  bic x2, x2, #7
  add x1, x1, x2, lsl #2
  add x3, x1, x2
1:
  dc civac, x1
  add x1, x1, #64
  cmp x1, x3
  b.lo 1b
  dsb sy
  mov x0, #0x300000
  b target
"""


def kernel_symbols(kernel):
    """Address -> name for the kernel's symbols, from llvm-nm."""
    nm = shutil.which('llvm-nm') or '/opt/homebrew/opt/llvm/bin/llvm-nm'
    table = {}
    for line in subprocess.run([nm, '-n', kernel], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table.setdefault(int(parts[0], 16), parts[2])
    return table


def kernel_symbol_address(kernel, name):
    """The address of one symbol by name; the address->name map keeps only one symbol per address."""
    nm = shutil.which('llvm-nm') or '/opt/homebrew/opt/llvm/bin/llvm-nm'
    for line in subprocess.run([nm, '-n', kernel], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    return None


def function_range(symbols, name):
    """Start and end (next symbol) of a function by its symbol name."""
    starts = sorted(symbols)
    for start, symbol in symbols.items():
        if symbol == name:
            return start, next(address for address in starts if address > start)
    raise SystemExit(f'No symbol {name} in the kernel')


def find_call(data, symbols, function, name, occurrence):
    """The VA and target of the Nth `bl name` inside FUNCTION."""
    hits = []
    low, high = function_range(symbols, function)
    for va in range(low, high, 4):
        word = struct.unpack_from('<I', data, file_offset(va))[0]
        if word >> 26 == 0b100101:
            delta = word & 0x3FFFFFF
            delta -= (1 << 26) if delta & (1 << 25) else 0
            target = va + delta * 4
            if symbols.get(target) == name:
                hits.append((va, target))
    if len(hits) < occurrence:
        raise SystemExit(f'{function} has {len(hits)} call(s) to {name}, not {occurrence}')
    return hits[occurrence - 1]


def assemble_c(source, va, workdir, symbols):
    """Build a freestanding C file for VA; returns (bytes, load address of the first byte, address of panic_screen).

    The linker may start .text a little after VA (it keeps the ELF headers in front), so the
    load address is read back from the ELF and is what the caller must write the bytes to."""
    clang = shutil.which('clang') or '/opt/homebrew/opt/llvm/bin/clang'
    lld = shutil.which('ld.lld') or '/opt/homebrew/opt/llvm/bin/ld.lld'
    objcopy = shutil.which('llvm-objcopy') or '/opt/homebrew/opt/llvm/bin/llvm-objcopy'
    nm = shutil.which('llvm-nm') or '/opt/homebrew/opt/llvm/bin/llvm-nm'
    # UNIXNU_UEFI is the Mu-Silicium checkout when the unixnu driver runs this script.
    font_dir = Path(os.environ.get('UNIXNU_UEFI') or ROOT / 'uefi-sheng') / 'Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib'
    obj, elf, binary = (workdir / name for name in ('panic.o', 'panic.elf', 'panic.bin'))
    definitions = []
    for macro, name, width in (('MAX_MEM', '_max_mem', 8),
                               ('PAGE_TOTAL', '_vm_page_pages', 4),
                               ('PAGE_FREE', '_vm_page_free_count', 4)):
        address = symbols.get(name)
        if address is None or address % width or not any(
                base <= address and address + width <= base + size
                for base, size, _, _ in SEGMENTS):
            raise SystemExit(f'Missing or invalid diagnostic symbol {name}')
        definitions.append(f'-D{macro}={address:#x}UL')
    # Optional: the board's banner buffer for the panic message (kits/sheng/BootProgress.c); kernels without it get 0 (no copy).
    text = symbols.get('_boot_progress_panic_text')
    if text is not None:
        if not any(base <= text and text + 256 <= base + size for base, size, _, _ in SEGMENTS):
            raise SystemExit('Invalid _boot_progress_panic_text')
        definitions.append(f'-DPANIC_TEXT={text:#x}UL')
    subprocess.run([clang, '--target=aarch64-none-elf', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                    '-mgeneral-regs-only', '-fno-pic', '-mcmodel=tiny', '-Oz', f'-DVINFO={VINFO_VA:#x}UL',
                    *definitions, f'-I{font_dir}', '-c', source, '-o', obj], check=True)
    script = workdir / 'panic.ld'
    script.write_text('SECTIONS { . = %#x; .text : { *(.text.a_entry) *(.text.a_common) *(.text*) *(.rodata*) } /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.*) } }\n' % va)
    size = shutil.which('llvm-size') or '/opt/homebrew/opt/llvm/bin/llvm-size'
    sizes = subprocess.run([size, '--format=berkeley', obj], capture_output=True, text=True, check=True).stdout.splitlines()[-1].split()
    if int(sizes[1]) or int(sizes[2]):
        # The code runs from the kernel's read-only text: a global would fault on its first store.
        raise SystemExit(f'{source.name} has writable data (data {sizes[1]}, bss {sizes[2]}); use the stack')
    subprocess.run([lld, '-T', script, '-e', 'panic_screen', obj, '-o', elf], check=True)
    # Only .text: a plain binary dump would start at the ELF headers kept in front of it.
    subprocess.run([objcopy, '-O', 'binary', '-j', '.text', elf, binary], check=True)
    readelf = shutil.which('llvm-readelf') or '/opt/homebrew/opt/llvm/bin/llvm-readelf'
    load = None
    for line in subprocess.run([readelf, '-S', elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.replace('[', ' ').replace(']', ' ').split()
        if len(parts) >= 4 and parts[1] == '.text':
            load = int(parts[3], 16)
    if load is None or load < va:
        raise SystemExit('Cannot find where the linker put .text')
    entry = None
    for line in subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == 'panic_screen':
            entry = int(parts[0], 16)
    if entry != load:
        raise SystemExit(f'panic_screen at {entry:#x} is not the first byte {load:#x}')
    return binary.read_bytes(), load, entry


def panic_common_entry(workdir, code, load):
    """Resolve the real alternate entry and verify its distinct argument ABI."""
    nm = shutil.which('llvm-nm') or '/opt/homebrew/opt/llvm/bin/llvm-nm'
    names = {parts[2]: int(parts[0], 16)
             for line in subprocess.check_output([nm, workdir / 'panic.elf'], text=True).splitlines()
             if len(parts := line.split()) == 3}
    entry = names.get('panic_screen_common')
    main = names.get('panic_screen_main')
    if entry is None or main is None or not load <= entry <= load + len(code) - 8:
        raise SystemExit('Common panic renderer entry is missing or out of bounds')
    at = entry - load
    if code[at:at + 8] != struct.pack('<II', 0xf9400021, branch(entry + 4, main)):
        raise SystemExit('Common panic entry does not dereference the Darwin va_list pointer and branch to the renderer')
    return entry


ROOT = Path(__file__).resolve().parents[1]


SEGMENTS = []   # (vmaddr, vmsize, fileoff, filesize) of the kernel being patched


def load_segments(data):
    """Read the Mach-O segment table; the file layout differs between kernel builds."""
    magic, _, _, _, ncmds = struct.unpack_from('<IiiII', data, 0)
    if magic != 0xFEEDFACF:
        raise SystemExit('Not a 64-bit Mach-O')
    offset = 32
    SEGMENTS.clear()
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, offset)
        if cmd == 0x19:
            _, vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<16sQQQQ', data, offset + 8)
            if filesize:
                SEGMENTS.append((vmaddr, vmsize, fileoff, filesize))
        offset += size


def unixthread_pc(data):
    """LC_UNIXTHREAD entry pc (arm64 thread state: flavor, count, x0..x28, fp, lr, sp, pc)."""
    offset = 32
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        cmd, size = struct.unpack_from('<II', data, offset)
        if cmd == 0x5:
            return struct.unpack_from('<Q', data, offset + 16 + 32 * 8)[0]
        offset += size
    raise SystemExit('No LC_UNIXTHREAD in kernel')


def rebase(kernel, data):
    """The probe sites are fixed offsets from the entry; shift them for another kernel build.

    _panic and _vinfo come from the symbol table when the kernel has one (the plain source
    kernel does); the retained candidate layout is the unshifted case.
    """
    global ENTRY_VA, START_FIRST_CPU, MMU_ENABLE_VA, AFTER_MMU_VA, PANIC_VA, VINFO_VA, PANIC_CAVE_END, CAVES
    delta = unixthread_pc(data) - ENTRY_VA
    if delta:
        ENTRY_VA += delta
        START_FIRST_CPU += delta
        MMU_ENABLE_VA += delta
        AFTER_MMU_VA += delta
        PANIC_CAVE_END += delta
        CAVES = {stage: va + delta for stage, va in CAVES.items()}
        PANIC_VA += delta
        VINFO_VA += delta
    names = {name: va for va, name in kernel_symbols(kernel).items()}
    if '_start_first_cpu' in names:
        START_FIRST_CPU = names['_start_first_cpu']
        # Native bootstrap table-capacity checks can grow before SCTLR setup.
        # Locate the unique instruction sequence in this bounded entry body;
        # do not assume a constant offset from the Mach-O entry point.
        matches = []
        for va in range(START_FIRST_CPU, START_FIRST_CPU + 0x1000, 4):
            def instruction(at):
                return struct.unpack_from('<I', data, file_offset(at))[0]
            if (instruction(va) == 0xD5181000 and
                    instruction(va + 4) == 0xD5033FDF and
                    instruction(va + 0x20) == 0xD2A00600 and
                    instruction(va + 0x24) == 0xD5181040):
                matches.append(va)
        if len(matches) != 1:
            raise SystemExit('Require one verified SCTLR/ISB/CPACR entry sequence')
        MMU_ENABLE_VA = matches[0]
        AFTER_MMU_VA = MMU_ENABLE_VA + 0x20
    if '_panic' in names and '_vinfo' in names:
        PANIC_VA = names['_panic']
        # Board diagnostics seed this ABI-compatible prefix from valid console
        # mappings, then pin it to their retained GOP mapping. Ordinary source
        # kernels without the observer retain the original vinfo path.
        VINFO_VA = names.get('_boot_progress_panic_vinfo', names['_vinfo'])
    return names


def file_offset(va):
    for vmaddr, vmsize, fileoff, filesize in SEGMENTS:
        if vmaddr <= va < vmaddr + min(vmsize, filesize):
            return fileoff + (va - vmaddr)
    raise SystemExit(f'{va:#x} is not in a file-backed segment of this kernel')


def branch(from_va, to_va):
    delta = (to_va - from_va) // 4
    if (to_va - from_va) % 4 or not -(1 << 25) <= delta < (1 << 25):
        raise SystemExit('Branch out of range')
    return 0x14000000 | (delta & 0x3FFFFFF)


def assemble(source, va, symbols, workdir):
    asm, obj, elf, binary = (workdir / name for name in ('cave.S', 'cave.o', 'cave.elf', 'cave.bin'))
    asm.write_text(source)
    clang = shutil.which('clang') or '/opt/homebrew/opt/llvm/bin/clang'
    lld = shutil.which('ld.lld') or '/opt/homebrew/opt/llvm/bin/ld.lld'
    objcopy = shutil.which('llvm-objcopy') or '/opt/homebrew/opt/llvm/bin/llvm-objcopy'
    subprocess.run([clang, '--target=aarch64-none-elf', '-c', asm, '-o', obj], check=True)
    subprocess.run([lld, f'-Ttext={va:#x}', *[f'--defsym={name}={value:#x}' for name, value in symbols.items()], '-e', '_start', obj, '-o', elf], check=True)
    subprocess.run([objcopy, '-O', 'binary', elf, binary], check=True)
    return binary.read_bytes()


def assemble_exception_screen(workdir, va, stack_top, scratch, frame, geom=0, phase=0):
    """Build the early fault reporter for VA; returns (bytes, load address, entry address)."""
    clang = shutil.which('clang') or '/opt/homebrew/opt/llvm/bin/clang'
    lld = shutil.which('ld.lld') or '/opt/homebrew/opt/llvm/bin/ld.lld'
    objcopy = shutil.which('llvm-objcopy') or '/opt/homebrew/opt/llvm/bin/llvm-objcopy'
    nm = shutil.which('llvm-nm') or '/opt/homebrew/opt/llvm/bin/llvm-nm'
    size = shutil.which('llvm-size') or '/opt/homebrew/opt/llvm/bin/llvm-size'
    readelf = shutil.which('llvm-readelf') or '/opt/homebrew/opt/llvm/bin/llvm-readelf'
    obj, elf, binary = (workdir / name for name in ('exc.o', 'exc.elf', 'exc.bin'))
    subprocess.run([clang, '--target=aarch64-none-elf', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                    '-mgeneral-regs-only', '-fno-pic', '-mcmodel=tiny', '-Oz',
                    f'-DSTACK_TOP={stack_top:#x}UL', f'-DSCRATCH={scratch:#x}UL',
                    f'-DFRAME={frame:#x}UL', f'-DGEOM={geom:#x}UL', f'-DPHASE={phase:#x}UL',
                    '-c', ROOT / 'scripts/xnu-exception-screen.c', '-o', obj], check=True)
    script = workdir / 'exc.ld'
    script.write_text('SECTIONS { . = %#x; .text : { *(.text.a_entry) *(.text*) *(.rodata*) } /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.*) } }\n' % va)
    sizes = subprocess.run([size, '--format=berkeley', obj], capture_output=True, text=True, check=True).stdout.splitlines()[-1].split()
    if int(sizes[1]) or int(sizes[2]):
        raise SystemExit(f'exception screen has writable data (data {sizes[1]}, bss {sizes[2]})')
    subprocess.run([lld, '-T', script, '-e', 'exc_entry', obj, '-o', elf], check=True)
    subprocess.run([objcopy, '-O', 'binary', '-j', '.text', elf, binary], check=True)
    load = None
    for line in subprocess.run([readelf, '-S', elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.replace('[', ' ').replace(']', ' ').split()
        if len(parts) >= 4 and parts[1] == '.text':
            load = int(parts[3], 16)
    if load is None or load < va:
        raise SystemExit('Cannot find where the exception screen .text was placed')
    entry = None
    for line in subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == 'exc_entry':
            entry = int(parts[0], 16)
    code = binary.read_bytes()
    if entry is None or not load <= entry < load + len(code):
        raise SystemExit('exc_entry is missing or outside the exception screen')
    return code, load, entry


PC_RELATIVE = ('a branch', 'an ADR', 'an ADRP', 'a literal load')


def relocatable_at(instruction):
    """Why a first instruction cannot be replayed at another address, or None."""
    if instruction >> 26 in (0b000101, 0b100101):
        return PC_RELATIVE[0]
    if instruction & 0x9F000000 in (0x90000000, 0x10000000):
        return PC_RELATIVE[1] if instruction & 0x9F000000 == 0x10000000 else PC_RELATIVE[2]
    if instruction & 0x3B000000 == 0x18000000:
        return PC_RELATIVE[3]
    return None


def patch_hooks(data, args, kernel_names, word, cursor, limit):
    """Checkpoint stubs in the padding after the low vector table; returns the new cursor."""
    blog_stage = kernel_symbol_address(args.kernel, '_native_blog_stage')
    blog_kv = kernel_symbol_address(args.kernel, '_native_blog_kv_ready')
    blog_irq = kernel_symbol_address(args.kernel, '_native_blog_irq')
    blog_fiq = kernel_symbol_address(args.kernel, '_native_blog_fiq')
    blog_keep = kernel_symbol_address(args.kernel, '_native_blog_keep_identity')
    blog_vec = kernel_symbol_address(args.kernel, '_native_blog_restore_vectors')
    for name, address in (('_native_blog_stage', blog_stage), ('_native_blog_kv_ready', blog_kv),
                          ('_native_blog_irq', blog_irq), ('_native_blog_fiq', blog_fiq),
                          ('_native_blog_keep_identity', blog_keep),
                          ('_native_blog_restore_vectors', blog_vec)):
        if address is None:
            raise SystemExit(f'{name} is not linked into the kernel; build the boot log archive first')
    targets = ([(name, 'draw') for name in args.hook] + [(name, 'kv') for name in args.hook_kv]
               + [(name, 'keep') for name in args.hook_keep] + [(name, 'vec') for name in args.hook_vec])
    irq = list(args.hook_irq)
    fiq = list(args.hook_fiq)
    for name, _ in targets + [(name, 'draw') for name in irq + fiq]:
        if kernel_symbol_address(args.kernel, name) is None:
            raise SystemExit(f'No symbol {name} to place a checkpoint on')
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for name, kind in targets + [(name, 'draw') for name in irq + fiq]:
            va = kernel_symbol_address(args.kernel, name)
            if not any(vmaddr <= va < vmaddr + min(vmsize, filesize) for vmaddr, vmsize, _, filesize in SEGMENTS):
                raise SystemExit(f'{name} at {va:#x} is not in a file-backed segment')
            original = word(va)
            reason = relocatable_at(original)
            if reason:
                raise SystemExit(f'{name} starts with {reason} ({original:#010x}); a checkpoint cannot replay it')
            label = name[1:] if name.startswith('_') else name
            if name in irq:
                source = IRQ_STUB.format(original=original)
            elif name in fiq:
                source = FIQ_STUB.format(original=original)
            else:
                source = HOOK_STUB.format(original=original, label=label,
                                          kv='  bl native_blog_kv_ready\n' if kind == 'kv' else '',
                                          keep='  bl native_blog_keep_identity\n' if kind == 'keep' else '',
                                          vec='  bl native_blog_restore_vectors\n' if kind == 'vec' else '')
            code = assemble(source, cursor, {'native_blog_stage': blog_stage, 'native_blog_kv_ready': blog_kv,
                                             'native_blog_irq': blog_irq, 'native_blog_fiq': blog_fiq,
                                             'native_blog_keep_identity': blog_keep,
                                             'native_blog_restore_vectors': blog_vec,
                                             'back': va + 4}, tmp)
            if cursor + len(code) > limit:
                raise SystemExit(f'checkpoint stubs run past {limit:#x} at {cursor:#x}')
            data[file_offset(cursor):file_offset(cursor) + len(code)] = code
            struct.pack_into('<I', data, file_offset(va), branch(va, cursor))
            print(f'checkpoint {label} at {va:#x} -> stub {cursor:#x} ({len(code)} bytes)')
            cursor = (cursor + len(code) + 15) & ~15
    return cursor


def patch_exception_screen(data, args, kernel_names, word):
    """Stage 1/2 plus the post-MMU checkpoint and the early fault reporter."""
    table = CAVES[1] + EXC_TABLE_OFF
    code_va = CAVES[1] + EXC_CODE_OFF
    scratch = CAVES[1] + EXC_SCRATCH_OFF
    map_flag = CAVES[1] + MAP_FLAG_OFF
    map_code = CAVES[1] + MAP_CODE_OFF
    band_code = CAVES[1] + BAND_CODE_OFF
    if table % 0x800:
        raise SystemExit(f'Vector table {table:#x} is not 2 KiB aligned')
    stack_top = kernel_symbol_address(args.kernel, '_excepstack_top')
    if stack_top is None or not any(
            vmaddr <= stack_top - 1 < vmaddr + min(vmsize, filesize) for vmaddr, vmsize, _, filesize in SEGMENTS):
        raise SystemExit('No mapped _excepstack_top symbol for the fault reporter stack')
    # The kernel's own table, set just before this site and never reset again in this build
    # (ARM_KERNEL_PROTECT is off). Its non-synchronous entries keep running stock handlers,
    # so an ordinary timer interrupt does not turn into a fault report.
    real_base = kernel_symbol_address(args.kernel, '_ExceptionVectorsBase')
    if real_base is None or real_base % 0x800 or not any(
            vmaddr <= real_base < vmaddr + min(vmsize, filesize) for vmaddr, vmsize, _, filesize in SEGMENTS):
        raise SystemExit('No mapped, 2 KiB-aligned _ExceptionVectorsBase to chain the interrupt vectors to')
    if word(real_base) >> 26 != 0b000101:
        raise SystemExit(f'{real_base:#x} is not the exception vector table (first slot is not a branch)')
    if any(data[file_offset(code_va):file_offset(PANIC_CAVE_END)]):
        raise SystemExit('Exception screen padding is not empty')
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        if args.blog:
            for name in ('_native_blog_start', '_native_blog_stage', '_native_blog_irq'):
                address = kernel_symbol_address(args.kernel, name)
                if address is None:
                    raise SystemExit(f'{name} is not linked into the kernel; build the boot log archive first')
            cave1 = assemble(STAGE1_BLOG, CAVES[1], {'target': START_FIRST_CPU}, tmp)
            stage3 = STAGE3_BLOG
            stage4 = STAGE4_BLOG
            defsyms3 = {'next': CAVES[4], 'FLAG': map_flag, 'BAND': band_code,
                        'native_blog_start': kernel_symbol_address(args.kernel, '_native_blog_start'),
                        'native_blog_stage': kernel_symbol_address(args.kernel, '_native_blog_stage')}
        else:
            cave1 = assemble(SOURCES[1].format(ticks=0), CAVES[1], {'target': START_FIRST_CPU}, tmp)
            stage3 = STAGE3_CHECKPOINT
            stage4 = STAGE4_CLEAN
            defsyms3 = {'scratch': scratch, 'next': CAVES[4]}
        cave2 = assemble(STAGE2_EXCEPTION.format(table_delta=table - CAVES[2], scratch_delta=scratch - CAVES[2]),
                         CAVES[2], {'post': CAVES[3], 'map': map_code}, tmp)
        cave_map = assemble(STAGE2_MAP, map_code, {'post': CAVES[3], 'FLAG': map_flag}, tmp)
        cave_band = assemble(BLOG_BAND, band_code, {}, tmp)
        cave3 = assemble(stage3, CAVES[3], defsyms3, tmp)
        cave4 = assemble(stage4, CAVES[4], {'target': AFTER_MMU_VA + 4}, tmp)
        frame = kernel_symbol_address(args.kernel, '_native_blog_frame') or 0
        geom = kernel_symbol_address(args.kernel, '_native_blog_geometry') or 0
        phase = kernel_symbol_address(args.kernel, '_native_blog_phase') or 0
        code, load, entry = assemble_exception_screen(tmp, code_va, stack_top, scratch, frame, geom, phase)
    if any(data[file_offset(map_flag):file_offset(table)]):
        raise SystemExit('Bootstrap-map padding is not empty')
    if len(cave_map) > BAND_CODE_OFF - MAP_CODE_OFF:
        raise SystemExit(f'Bootstrap map is {len(cave_map):#x} bytes, over {BAND_CODE_OFF - MAP_CODE_OFF:#x}')
    if len(cave_band) > EXC_TABLE_OFF - BAND_CODE_OFF:
        raise SystemExit(f'Band painter is {len(cave_band):#x} bytes, over {EXC_TABLE_OFF - BAND_CODE_OFF:#x}')
    for stage, code_bytes in ((1, cave1), (2, cave2), (3, cave3), (4, cave4)):
        if len(code_bytes) > CAVE_SIZE:
            raise SystemExit(f'Exception-screen stage {stage} code is {len(code_bytes):#x} bytes, over {CAVE_SIZE:#x}')
        data[file_offset(CAVES[stage]):file_offset(CAVES[stage]) + len(code_bytes)] = code_bytes
    data[file_offset(map_code):file_offset(map_code) + len(cave_map)] = cave_map
    data[file_offset(band_code):file_offset(band_code) + len(cave_band)] = cave_band
    if load + len(code) > PANIC_CAVE_END:
        raise SystemExit(f'Exception screen is {len(code):#x} bytes at {load:#x}, past {PANIC_CAVE_END:#x}')
    data[file_offset(load):file_offset(load) + len(code)] = code
    # Synchronous entries of EL1t (SP_EL0) and EL1h (SP_EL1) land on the reporter; the IRQ,
    # FIQ and SError entries that do not overlap the reporter code keep the kernel's handlers.
    # The reporter starts at table+0x300, so entries from 0x300 up cover it and are left alone.
    slots = [(0x000, entry), (0x200, entry),
             *((offset, real_base + offset) for offset in (0x080, 0x100, 0x180, 0x280))]
    for offset, target in slots:
        base = table + offset
        if any(data[file_offset(base):file_offset(base) + 4]):
            raise SystemExit(f'Vector slot at {base:#x} is not empty')
        struct.pack_into('<I', data, file_offset(base), branch(base, target))
    if args.hook or args.hook_kv or args.hook_keep or args.hook_vec or args.hook_irq or args.hook_fiq:
        if not args.blog:
            raise SystemExit('--hook checkpoints need --blog: the console is what draws them')
        # The padding after the exception screen only holds a few bytes, so the stubs go in the
        # zero block after the low vector table (16 slots of 0x80 end at +0x800).
        low = kernel_symbol_address(args.kernel, '_LowExceptionVectorBase')
        if low is None:
            raise SystemExit('No _LowExceptionVectorBase: cannot place the checkpoint stubs')
        cursor = low + 0x800
        limit = cursor + HOOK_AREA
        if cursor % 16 or not any(vmaddr <= cursor and limit <= vmaddr + min(vmsize, filesize)
                                  for vmaddr, vmsize, _, filesize in SEGMENTS):
            raise SystemExit(f'Checkpoint padding {cursor:#x} is not mapped and aligned')
        if any(data[file_offset(cursor):file_offset(limit)]):
            raise SystemExit(f'Checkpoint padding at {cursor:#x} is not empty')
        patch_hooks(data, args, kernel_names, word, cursor, limit)
    struct.pack_into('<I', data, file_offset(ENTRY_VA), branch(ENTRY_VA, CAVES[1]))
    struct.pack_into('<I', data, file_offset(MMU_ENABLE_VA), branch(MMU_ENABLE_VA, CAVES[2]))
    struct.pack_into('<I', data, file_offset(AFTER_MMU_VA), branch(AFTER_MMU_VA, CAVES[3]))
    what = ('boot log console' if args.blog else 'white bar') + f' at {AFTER_MMU_VA:#x}'
    return f'post-MMU {what}, exception screen {len(code):#x} bytes at {entry:#x} (table {table:#x})'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('kernel', type=Path, help='the COPY of the kernel to patch in place')
    parser.add_argument('--reset-delay', type=int, default=6, help='seconds before the reset')
    parser.add_argument('--reset-after', metavar='SYMBOL', help='place the reset after this call in arm_init instead of stage 3')
    parser.add_argument('--reset-before', action='store_true', help='reset at the call site before invoking SYMBOL (requires --reset-after)')
    parser.add_argument('--panic-screen', action='store_true', help='draw panic() messages on the screen')
    parser.add_argument('--exception-screen', action='store_true', help='report ESR/ELR/FAR from the synchronous vectors after the MMU is on')
    parser.add_argument('--blog', action='store_true', help='start the kernel frame buffer console at the post-MMU checkpoint (needs --exception-screen)')
    parser.add_argument('--hook', action='append', default=[], metavar='SYMBOL', help='draw the entry of SYMBOL as a checkpoint (repeatable)')
    parser.add_argument('--hook-kv', action='append', default=[], metavar='SYMBOL', help='checkpoint that first switches the console to the mapped frame buffer')
    parser.add_argument('--hook-keep', action='append', default=[], metavar='SYMBOL', help='checkpoint that puts the bootstrap TTBR0 back so the console keeps drawing after arm_vm_init')
    parser.add_argument('--hook-vec', action='append', default=[], metavar='SYMBOL', help='checkpoint that hands the synchronous vectors back to the kernel (ends the fault reporter window)')
    parser.add_argument('--hook-irq', action='append', default=[], metavar='SYMBOL', help='count interrupts in SYMBOL (repeatable)')
    parser.add_argument('--hook-fiq', action='append', default=[], metavar='SYMBOL', help='count FIQs in SYMBOL (repeatable)')
    parser.add_argument('--no-reset', action='store_true', help='no reset probe at all')
    parser.add_argument('--occurrence', type=int, default=1, help='which call to SYMBOL (1 = first)')
    parser.add_argument('--in', dest='function', default='_arm_init', help='function that makes the call (default _arm_init)')
    args = parser.parse_args()
    if args.reset_before and not args.reset_after:
        raise SystemExit('--reset-before requires --reset-after')
    data = bytearray(args.kernel.read_bytes())
    load_segments(data)
    kernel_names = rebase(args.kernel, data)

    def word(va):
        return struct.unpack_from('<I', data, file_offset(va))[0]

    expected = {ENTRY_VA: branch(ENTRY_VA, START_FIRST_CPU), MMU_ENABLE_VA: 0xD5181000, AFTER_MMU_VA: 0xD2A00600}
    for va, value in expected.items():
        if word(va) != value:
            raise SystemExit(f'{va:#x} holds {word(va):#010x}, expected {value:#010x}: not the kernel these probes are for')
    for va in CAVES.values():
        if any(data[file_offset(va):file_offset(va) + CAVE_SIZE]):
            raise SystemExit(f'Padding at {va:#x} is not empty')

    if args.blog and not args.exception_screen:
        raise SystemExit('--blog starts the console from the post-MMU checkpoint: pass --exception-screen too')
    if args.exception_screen:
        if args.panic_screen or args.reset_after or args.no_reset:
            raise SystemExit('--exception-screen replaces stages 3 and 4: drop --panic-screen, --reset-after and --no-reset')
        description = patch_exception_screen(data, args, kernel_names, word)
        args.kernel.write_bytes(data)
        entry_bar = 'ENTRY line' if args.blog else 'blue bar'
        print(f'patched {args.kernel.name}: stage 1 {entry_bar}, stage 2 magenta bar, {description}')
        return

    # (site, cave stage, symbols the cave is linked with)
    sites = [(ENTRY_VA, 1, {'target': START_FIRST_CPU}),
             (MMU_ENABLE_VA, 2, {'target': MMU_ENABLE_VA + 4})]
    if args.no_reset:
        description = 'no reset probe'
    elif args.reset_after:
        symbols = kernel_symbols(args.kernel)
        call_va, callee = find_call(data, symbols, args.function, args.reset_after, args.occurrence)
        sites.append((call_va, 4, {'callee': callee, 'target': call_va + 4}))
        description = f'stage 4 reset {"before" if args.reset_before else "after"} call #{args.occurrence} to {args.reset_after} in {args.function} (at {call_va:#x})'
    else:
        sites.append((AFTER_MMU_VA, 3, {'target': AFTER_MMU_VA + 4}))
        description = f'stage 3 reset {args.reset_delay} s after the MMU is on'

    with tempfile.TemporaryDirectory() as tmp:
        for site, stage, symbols in sites:
            source = RESET_BEFORE if (stage == 4 and args.reset_before) else SOURCES[stage].format(ticks=TIMEBASE * args.reset_delay)
            code = assemble(source, CAVES[stage], symbols, Path(tmp))
            if len(code) > CAVE_SIZE:
                raise SystemExit(f'Stage {stage} code is too large')
            data[file_offset(CAVES[stage]):file_offset(CAVES[stage]) + len(code)] = code
    for site, stage, _ in sites:
        struct.pack_into('<I', data, file_offset(site), branch(site, CAVES[stage]))
    if args.panic_screen:
        # Right after the last probe cave in use, so the padding is shared as tightly as possible.
        PANIC_CAVE = max(CAVES[stage] for _, stage, _ in sites) + CAVE_SIZE
        if word(PANIC_VA) in (0, 0xFFFFFFFF) or any(data[file_offset(PANIC_CAVE):file_offset(PANIC_CAVE_END)]):
            raise SystemExit('panic() or the panic screen padding is not as expected')
        with tempfile.TemporaryDirectory() as tmp:
            code, load, entry = assemble_c(ROOT / 'scripts/xnu-panic-screen.c', PANIC_CAVE, Path(tmp), kernel_names)
            common_entry = panic_common_entry(Path(tmp), code, load)
        if load + len(code) > PANIC_CAVE_END:
            raise SystemExit(f'panic screen is {len(code):#x} bytes at {load:#x}, the padding ends at {PANIC_CAVE_END:#x}')
        if any(data[file_offset(load):file_offset(load) + len(code)]):
            raise SystemExit(f'Padding at {load:#x} is not empty')
        data[file_offset(load):file_offset(load) + len(code)] = code
        struct.pack_into('<I', data, file_offset(PANIC_VA), branch(PANIC_VA, entry))
        common = kernel_names.get('_panic_trap_to_debugger')
        if common is None or word(common) in (0, 0xFFFFFFFF):
            raise SystemExit('Common panic funnel is missing; refuse a partially covered panic screen')
        struct.pack_into('<I', data, file_offset(common), branch(common, common_entry))
        description += f', panic screen ({len(code):#x} bytes at {entry:#x}; common funnel at {common:#x})'
    args.kernel.write_bytes(data)
    print(f'patched {args.kernel.name}: stage 1 blue bar, stage 2 magenta bar, {description}')

if __name__ == '__main__':
    main()
