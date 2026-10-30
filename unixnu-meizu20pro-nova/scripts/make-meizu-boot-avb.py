#!/usr/bin/env python3
"""Wrap a boot.img in the slot-A AVB layout the Meizu ABL expects.

The ABL parses the partition tail: a copy of the stock boot vbmeta followed by a
64-byte big-endian footer. The partition itself is fixed at 96 MiB.
"""
import argparse
import hashlib
from pathlib import Path
import struct

PART = 100663296
STOCK_VBMETA_OFFSET = 46891008
STOCK_VBMETA_SIZE = 2496


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('boot', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--stock', type=Path,
                        default=Path('/Users/ym/.qoder/backups/meizu20pro/20261023-000614/boot_b.img'))
    args = parser.parse_args()

    boot = args.boot.read_bytes()
    stock = args.stock.read_bytes()
    vbmeta = stock[STOCK_VBMETA_OFFSET:STOCK_VBMETA_OFFSET + STOCK_VBMETA_SIZE]
    if len(vbmeta) != STOCK_VBMETA_SIZE:
        raise SystemExit('stock image does not carry the boot vbmeta')
    if len(boot) % 4096 or len(boot) + STOCK_VBMETA_SIZE + 64 > PART:
        raise SystemExit(f'boot image of {len(boot)} bytes does not fit the partition')

    vbmeta_offset = len(boot)
    image = bytearray(boot)
    image += vbmeta
    image += bytes(PART - 64 - len(image))
    image += b'AVBf' + struct.pack('>IIQQQ', 1, 0, len(boot), vbmeta_offset, STOCK_VBMETA_SIZE) + bytes(28)
    if len(image) != PART:
        raise SystemExit('assembled image is not partition sized')

    args.output.write_bytes(image)
    print(f'{args.output}: boot {len(boot)} bytes, vbmeta at {vbmeta_offset}, sha256 {hashlib.sha256(image).hexdigest()}')


if __name__ == '__main__':
    main()
