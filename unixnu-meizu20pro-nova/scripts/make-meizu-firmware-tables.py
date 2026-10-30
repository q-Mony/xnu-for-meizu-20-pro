#!/usr/bin/env python3
"""Build the Meizu 20 Pro UEFI memory and configuration tables from this phone's own data.

Sources, both read-only:
  * the stock uefiplat.cfg inside the uefi_a partition backup (the memory layout ABL gives UEFI),
  * the device tree the same firmware hands to the OS (DRAM ranges and reserved-memory nodes).

The generated MemoryMapLib/ConfigurationMapLib replace the QRD8550 candidates that
sheng's package starts from. Every descriptor name is preserved so the Silicium
libraries that look regions up by name ("Display Reserved", "UEFI Stack", ...) keep working.
"""
import configparser
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / '.cache/firmware/meizu20pro'
CONFIG_FILE = FIRMWARE / 'uefiplat.cfg'
DTB = ROOT.parent / 'meizu20pro-dt.dtb' if (ROOT.parent / 'meizu20pro-dt.dtb').is_file() else ROOT / 'meizu20pro-dt.dtb'
PLATFORM = ROOT.parent / 'uefi-sheng/Platforms/Meizu/meizu20proPkg'

if not CONFIG_FILE.is_file():
    sys.exit(f'missing {CONFIG_FILE}; run import-meizu-uefi.py first')
if not DTB.is_file():
    sys.exit(f'missing device tree {DTB}')

text = CONFIG_FILE.read_text()

# ---------------------------------------------------------------- stock rows
def rows(section, following):
    body = text.split(section, 1)[1].split(following, 1)[0]
    result = []
    for line in body.splitlines():
        line = line.strip()
        if not line.startswith('0x'):
            continue
        row = next(csv.reader([line], skipinitialspace=True))
        result.append({'base': int(row[0], 16), 'size': int(row[1], 16), 'name': row[2].strip('"'),
                       'hob': row[3], 'restype': row[4], 'attr': row[5],
                       'memtype': row[6], 'cache': row[7]})
    return result

stock_mem = rows('[MemoryMap]', '[RegisterMap]')
stock_reg = rows('[RegisterMap]', '[ConfigParameters]')

config = configparser.ConfigParser(interpolation=None)
config.optionxform = str
config.read_string('[ConfigParameters]\n' + text.split('[ConfigParameters]', 1)[1])

# ---------------------------------------------------------------- DT rows
def fdt_cells(node, prop):
    out = subprocess.run(['fdtget', '-t', 'x', str(DTB), node, prop],
                         capture_output=True, text=True)
    if out.returncode:
        return []
    return [int(word, 16) for word in out.stdout.split()]

def ranges(cells):
    for index in range(0, len(cells), 4):
        base = (cells[index] << 32) | cells[index + 1]
        size = (cells[index + 2] << 32) | cells[index + 3]
        if size:
            yield base, size

dt_memory = list(ranges(fdt_cells('/memory', 'reg')))
dt_reserved = []
for node in subprocess.check_output(['fdtget', '-l', str(DTB), '/reserved-memory'], text=True).split():
    for base, size in ranges(fdt_cells('/reserved-memory/' + node, 'reg')):
        dt_reserved.append({'base': base, 'size': size, 'name': node.split('@')[0]})

# ---------------------------------------------------------------- classify stock
reservations = []
allocations = []
registers = []
for row in stock_mem:
    if row['size'] == 0:
        continue
    if row['hob'] in ('NoMap', 'AddDynamicMem'):
        # AddDynamicMem: TZ fills the size at runtime; reserve the full declared extent.
        reservations.append(dict(row, kind='nomap' if row['hob'] == 'NoMap' else 'reserv', priority=0))
    elif row['memtype'] == 'Reserv':
        reservations.append(dict(row, kind='reserv', priority=0))
    elif row['hob'] == 'NoHob':
        continue
    else:
        allocations.append(row)
for row in stock_reg:
    if row['size'] == 0:
        continue
    row = dict(row)
    registers.append(row)
for row in dt_reserved:
    reservations.append({'base': row['base'], 'size': row['size'], 'name': row['name'],
                         'kind': 'reserv', 'cache': 'WRITE_BACK_XN', 'priority': -1})

# Ordered least to most permissive; the last match wins when rows overlap. WRITE_BACK (no _XN) is
# executable and is what the stock cfg marks UEFI FD, UEFI FD Reserved and CPU Vectors with.
CACHE_ORDER = ['UNCACHED_UNBUFFERED_XN', 'WRITE_THROUGH_XN', 'WRITE_BACK_XN', 'WRITE_BACK']

def attributes(row):
    cache = row.get('cache') or 'WRITE_BACK_XN'
    if row['kind'] == 'nomap':
        return ['HobOnlyNoCacheSetting', 'MEM_RES', 'UNCACHEABLE', 'Reserv', 'UNCACHED_UNBUFFERED_XN']
    if cache not in CACHE_ORDER:
        cache = 'WRITE_BACK_XN'
    memtype = 'Reserv'
    return ['AddMem', row.get('restype', 'MEM_RES'), row.get('attr', 'SYS_MEM_CAP'), memtype, cache]

def priority(row):
    return (row.get('priority', 0), -CACHE_ORDER.index(row.get('cache') or 'WRITE_BACK_XN')
            if (row.get('cache') or 'WRITE_BACK_XN') in CACHE_ORDER else -3)

# ---------------------------------------------------------------- geometry
def pieces(lo, hi, cuts):
    for point in sorted({c for c in cuts if lo < c < hi}):
        yield lo, point
        lo = point
    if lo < hi:
        yield lo, hi

def coverage(rows):
    """Disjoint segments with the highest-priority covering row, or None."""
    cuts = []
    for row in rows:
        cuts += [row['base'], row['base'] + row['size']]
    lo, hi = min(cuts), max(cuts)
    for start, end in pieces(lo, hi, cuts):
        hit = [row for row in rows if row['base'] <= start < row['base'] + row['size']]
        if hit:
            best = max(hit, key=priority)
            yield start, end, best

def overlaps_any(ranges_list, base, size):
    return any(max(base, s) < min(base + size, e) for s, e, _ in ranges_list)

allocated = list(coverage(allocations))
alloc_ranges = [(start, end) for start, end, _ in allocated]
reserved = []
for start, end, row in coverage(reservations):
    cuts = [start, end] + [x for pair in alloc_ranges for x in pair]
    for lo, hi in pieces(start, end, cuts):
        if not overlaps_any([(s, e, None) for s, e in alloc_ranges], lo, hi - lo):
            reserved.append((lo, hi, row))

descriptors = []
for start, end, row in reserved:
    name = row['name'] if row['kind'] == 'nomap' else row['name']
    if row['kind'] == 'nomap' and not any(other['name'] == row['name'] for other in stock_mem if other['hob'] != 'NoMap'):
        name = f'NOMAP {start:08X}' if row['name'] == 'NOMAP' else row['name']
    descriptors.append({'name': name, 'base': start, 'size': end - start, 'attrs': attributes(row)})
for row in allocations:
    descriptors.append({'name': row['name'], 'base': row['base'], 'size': row['size'],
                        'attrs': ['AddMem', row['restype'], row['attr'], row['memtype'],
                                  row['cache'] if row['cache'] in CACHE_ORDER else 'WRITE_BACK_XN']})
claimed = reserved + [(row['base'], row['base'] + row['size'], row) for row in allocations]

dram = []
for base, size in dt_memory:
    cuts = [base, base + size]
    for start, end, _ in claimed:
        cuts += [start, end]
    for start, end in pieces(base, base + size, cuts):
        if not overlaps_any(claimed, start, end - start):
            dram.append((start, end))
# coalesce adjacent usable pieces
merged = []
for start, end in sorted(dram):
    if merged and merged[-1][1] == start:
        merged[-1] = (merged[-1][0], end)
    else:
        merged.append((start, end))
for start, end in merged:
    name = 'DRAM' if end - start < 0x10000000 else f'DRAM {start:09X}'
    descriptors.append({'name': name, 'base': start, 'size': end - start,
                        'attrs': ['AddMem', 'SYS_MEM', 'SYS_MEM_CAP', 'Conv', 'WRITE_BACK_XN']})
for row in registers:
    descriptors.append({'name': row['name'], 'base': row['base'], 'size': row['size'],
                        'attrs': ['AddDev', 'MMAP_IO', 'UNCACHEABLE', 'MmIO', 'NS_DEVICE']})

descriptors.sort(key=lambda d: (d['base'], d['size']))
for index, d in enumerate(descriptors):
    if len(d['name']) >= 32:
        sys.exit(f"name too long: {d['name']}")
    if d['base'] % 4096 or d['size'] % 4096:
        sys.exit(f"unaligned: {d}")
    for other in descriptors[index + 1:]:
        if max(d['base'], other['base']) < min(d['base'] + d['size'], other['base'] + other['size']):
            sys.exit(f"overlap: {d['name']} {d['base']:#x} and {other['name']} {other['base']:#x}")
if len(descriptors) >= 128:
    sys.exit(f'{len(descriptors)} descriptors exceed the 128-entry capacity')

body = '\n'.join(f'  {{"{d["name"]}", 0x{d["base"]:X}, 0x{d["size"]:X}, {", ".join(d["attrs"])}}},'
                 for d in descriptors).rstrip(',')
header = '''// SPDX-License-Identifier: BSD-3-Clause
// Generated by scripts/make-meizu-firmware-tables.py from the phone's stock uefiplat.cfg
// and device tree. Do not edit by hand.
#include <Library/MemoryMapLib.h>

STATIC EFI_MEMORY_REGION_DESCRIPTOR gMemoryDescriptor[] = {
'''
footer = '''
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
'''
target = PLATFORM / 'Library/MemoryMapLib/MemoryMapLib.c'
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(header + body + '\n' + footer)

# ---------------------------------------------------------------- configuration
overrides = {'EarlyInitCoreCnt': 1, 'NumActiveCores': 1, 'EnableMultiThreading': 0,
             'EnableMultiCoreFvDecompression': 0, 'EnableDisplayThread': 0,
             'EnableDisplayImageFv': 0, 'EnableLogFsSyncInRetail': 0, 'EnableShell': 1}
numeric, strings = {}, {}
for key, value in config['ConfigParameters'].items():
    try:
        numeric[key] = int(value, 0)
    except ValueError:
        strings[key] = value
numeric.pop('ConfigParameterCount', None)
numeric.update(overrides)
entries = ',\n'.join(f'  {{"{key}", 0x{value:X}}}' for key, value in sorted(numeric.items()))
config_source = '''#include <Library/ConfigurationMapLib.h>

STATIC
EFI_CONFIGURATION_ENTRY_DESCRIPTOR
gConfigurationDescriptor[] = {
''' + entries + '''
};

VOID
GetConfigurationMap (
  OUT EFI_CONFIGURATION_ENTRY_DESCRIPTOR **ConfigurationDescriptor,
  OUT UINT8                               *ConfigurationDescriptorCount)
{
  *ConfigurationDescriptor      = gConfigurationDescriptor;
  *ConfigurationDescriptorCount = ARRAY_SIZE (gConfigurationDescriptor);
}
'''
target = PLATFORM / 'Library/ConfigurationMapLib/ConfigurationMapLib.c'
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(config_source)

report = {
    'stock_config_sha256': hashlib.sha256(CONFIG_FILE.read_bytes()).hexdigest(),
    'dtb': str(DTB), 'dtb_sha256': hashlib.sha256(DTB.read_bytes()).hexdigest(),
    'stock_memory_rows': len(stock_mem), 'dt_reserved_regions': len(dt_reserved),
    'dt_memory_ranges': len(dt_memory), 'descriptors': len(descriptors),
    'reserved_descriptors': len(reserved), 'dram_descriptors': len(merged),
    'numeric_parameters': len(numeric),
    'string_parameters_not_supported': strings,
    'overrides': overrides,
    'runtime_ram_verified': False, 'hardware_boot_demonstrated': False,
}
path = ROOT / 'build/meizu20pro/firmware-tables.json'
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(report, indent=2) + '\n')
print(f'{len(descriptors)} descriptors ({len(merged)} DRAM spans, {len(reserved)} reservations); '
      f'{len(numeric)} configuration parameters')
