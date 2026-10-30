#!/usr/bin/env python3
"""Offline CLI -> payload -> real board ADT checks; FAT packing is stubbed.

Run with --adt-script PATH pointing to the board's make-meizu-adt.py.
No kernel build, firmware build, private input, or device access is needed.
"""
import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from unixnu import cli, payload
from unixnu.util import Error

TEST_BYTES = bytes((2, 16, 32, 48, 171, 205))
TEST_TEXT = ':'.join(f'{part:02x}' for part in TEST_BYTES)
ADT_SCRIPT = None


class PayloadWifi(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.kernel = self.folder / 'input-kernel'
        self.kernel.write_bytes(b'offline fixture; never boot this')
        self.source = self.folder / 'private-input.txt'
        self.source.write_text(TEST_TEXT + '\n')
        self.logs = io.StringIO()
        self.staged = {}
        self.command = []

    def build(self, supplied=True):
        flags = ['--wifi-mac-file', str(self.source)] if supplied else []
        args = cli.parser().parse_args(['build', 'payload', '--kernel', str(self.kernel), *flags])
        ctx = SimpleNamespace(args=args, tree=self.folder, work=self.folder / 'work',
                              logs=self.folder / 'logs', uefi=None,
                              board={'payload': {'dram_size': '0x80000000',
                                                 'adt': {'tool': str(ADT_SCRIPT)}}})
        real_run = payload.run

        def run(command, **kwargs):
            self.command[:] = [str(part) for part in command]
            return real_run(command, **kwargs)

        def volume(stage, image, label, size):
            self.staged = {str(p.relative_to(stage)): p.read_bytes()
                           for p in stage.rglob('*') if p.is_file()}
            image.write_bytes(b'offline volume stub')

        with patch.dict(os.environ, {}, clear=True), patch.object(payload, 'run', side_effect=run), \
                patch.object(payload, 'write_volume', side_effect=volume), \
                contextlib.redirect_stdout(self.logs):
            image = payload.build(ctx)
        return image.with_suffix('.json')

    def test_cli_file_reaches_generator_as_exact_six_byte_property(self):
        report = self.build()
        flag = self.command.index('--wifi-mac-file')
        self.assertEqual(self.command[flag + 1], str(self.source.resolve()))
        adt = self.staged['EFI/UniXNU/Payloads/kernel.macho.adt']
        header = struct.pack('<32sI', b'mac-address-wifi0', 6)
        self.assertEqual(adt.count(header), 1)
        self.assertTrue(header + TEST_BYTES + bytes(2) in adt, 'wrong property value or padding')
        self.assertEqual(set(self.staged), {'EFI/UniXNU/Payloads/kernel.macho',
                                          'EFI/UniXNU/Payloads/kernel.macho.adt',
                                          'EFI/UniXNU/unixnu.toml'})
        self.assertFalse(json.loads(report.read_text())['hardware_tested'])
        output = self.logs.getvalue() + report.read_text()
        output += ''.join(p.read_text() for p in (self.folder / 'logs').glob('*'))
        self.assertTrue(TEST_TEXT not in output, 'private address leaked')
        self.assertTrue(TEST_TEXT not in ' '.join(self.command), 'private address in argv')

    def test_default_does_not_forward_or_add_property(self):
        self.build(supplied=False)
        self.assertNotIn('--wifi-mac-file', self.command)
        self.assertNotIn(b'mac-address-wifi0', self.staged['EFI/UniXNU/Payloads/kernel.macho.adt'])

    def test_missing_input_fails_before_staging(self):
        self.source.unlink()
        with self.assertRaisesRegex(Error, '--wifi-mac-file: input file not found'):
            self.build()
        self.assertFalse((self.folder / 'work/payload/stage').exists())
        self.assertFalse(self.command)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adt-script', type=Path, required=True)
    options, remaining = parser.parse_known_args()
    ADT_SCRIPT = options.adt_script.resolve()
    if not ADT_SCRIPT.is_file():
        parser.error('--adt-script must point to the board generator')
    unittest.main(argv=[__file__, *remaining], verbosity=2)
