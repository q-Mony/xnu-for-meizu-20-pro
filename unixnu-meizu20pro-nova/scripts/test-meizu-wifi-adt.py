#!/usr/bin/env python3
"""Offline Wi-Fi ADT contracts, using synthetic locally administered addresses only."""
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest

HELPERS = runpy.run_path(str(Path(__file__).with_name('test-meizu-display-adt.py')))
parse, child = HELPERS['parse'], HELPERS['child']
SCRIPT = Path(__file__).with_name('make-meizu-adt.py')
TEST_BYTES = bytes((2, 16, 32, 48, 171, 205))
TEST_TEXT = ':'.join(f'{part:02x}' for part in TEST_BYTES).encode('ascii')


class MeizuWifiADT(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.source = self.folder / 'private-input.txt'
        self.output = self.folder / 'kernel.macho.adt'

    def generate(self, *flags):
        return subprocess.run([sys.executable, str(SCRIPT), '--output', str(self.output),
                               '--dram-size', '0x80000000', *flags],
                              capture_output=True)

    def test_absent_by_default_for_all_identities(self):
        for flags in ((), ('--ipad-identity',), ('--iphone-identity',)):
            with self.subTest(flags=flags):
                result = self.generate(*flags)
                self.assertEqual(result.returncode, 0)
                self.assertNotIn('mac-address-wifi0', child(parse(self.output.read_bytes()), 'chosen')[0])

    def test_exact_six_bytes_and_no_other_identity_changes(self):
        for flags in ((), ('--ipad-identity',), ('--iphone-identity',)):
            for text in (TEST_TEXT, TEST_TEXT + b'\n', TEST_TEXT.upper() + b'\r\n'):
                with self.subTest(flags=flags, input_length=len(text)):
                    self.assertEqual(self.generate(*flags).returncode, 0)
                    baseline = parse(self.output.read_bytes())
                    self.source.write_bytes(text)
                    result = self.generate(*flags, '--wifi-mac-file', str(self.source))
                    self.assertEqual(result.returncode, 0)
                    tree = parse(self.output.read_bytes())
                    chosen = child(tree, 'chosen')[0]
                    value = chosen.pop('mac-address-wifi0')
                    self.assertEqual(len(value), 6)
                    self.assertTrue(value == TEST_BYTES, 'encoded bytes differ')
                    chosen.pop('random-seed')
                    child(baseline, 'chosen')[0].pop('random-seed')
                    self.assertTrue(tree == baseline, 'unrelated ADT properties changed')
                    self.assertTrue(TEST_TEXT.lower() not in (result.stdout + result.stderr).lower(),
                                    'private address leaked')

    def test_rejects_invalid_input_without_overwriting_or_disclosing(self):
        cases = {
            'zero': ':'.join(['00'] * 6).encode(),
            'broadcast': ':'.join(['ff'] * 6).encode(),
            'multicast': b'03' + TEST_TEXT[2:],
            'short': TEST_TEXT[:-3],
            'long': TEST_TEXT + b':10',
            'short_octet': TEST_TEXT[1:],
            'invalid_hex': b'gg' + TEST_TEXT[2:],
            'hyphens': TEST_TEXT.replace(b':', b'-'),
            'compact': TEST_TEXT.replace(b':', b''),
            'leading_space': b' ' + TEST_TEXT,
            'trailing_space': TEST_TEXT + b' ',
            'extra_line': TEST_TEXT + b'\nextra',
            'double_newline': TEST_TEXT + b'\n\n',
            'non_ascii': TEST_TEXT[:-1] + bytes((255,)),
            'binary': TEST_BYTES,
            'empty': b'',
            'oversized': TEST_TEXT + b'\n' + b'x' * 1000,
        }
        for name, data in cases.items():
            with self.subTest(case=name):
                self.source.write_bytes(data)
                self.output.write_bytes(b'unchanged')
                result = self.generate('--wifi-mac-file', str(self.source))
                self.assertEqual(result.returncode, 2)
                self.assertEqual(self.output.read_bytes(), b'unchanged')
                self.assertIn(b'--wifi-mac-file:', result.stderr)
                if data:
                    self.assertTrue(data not in result.stdout + result.stderr, 'private input leaked')

    def test_missing_file_and_directory_fail_without_output(self):
        for source in (self.source, self.folder):
            result = self.generate('--wifi-mac-file', str(source))
            self.assertEqual(result.returncode, 2)
            self.assertIn(b'cannot read input file', result.stderr)
            self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
