#!/usr/bin/env python3
"""Offline contract checks for the Meizu display ADT; no hardware access."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('make-meizu-adt.py')
SPEC = importlib.util.spec_from_file_location('meizu_adt', SCRIPT)
ADT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ADT)


def parse(data):
    def read(offset):
        count, children_count = struct.unpack_from('<II', data, offset)
        offset += 8
        properties = {}
        for _ in range(count):
            name, length = struct.unpack_from('<32sI', data, offset)
            offset += 36
            value = data[offset:offset + length]
            if len(value) != length:
                raise ValueError('truncated property')
            properties[name.split(b'\0', 1)[0].decode()] = value
            offset += (length + 3) & ~3
        children = []
        for _ in range(children_count):
            child, offset = read(offset)
            children.append(child)
        return (properties, children), offset

    tree, consumed = read(0)
    if consumed != len(data):
        raise ValueError('trailing bytes')
    return tree


def child(tree, name):
    return next(node for node in tree[1] if node[0]['name'] == name.encode() + b'\0')


class MeizuDisplayADT(unittest.TestCase):
    def generate(self, *flags):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'kernel.macho.adt'
            result = subprocess.run([sys.executable, str(SCRIPT), '--output', str(output),
                                     '--dram-size', '0x80000000', *flags],
                                    capture_output=True, text=True)
            return result, parse(output.read_bytes()) if output.exists() else None

    def test_default_board_output_has_a_single_display_and_zeroed_handoff(self):
        result, tree = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        chosen = child(tree, 'chosen')[0]
        self.assertEqual(chosen['negroni-gop-handoff'], bytes(56))
        self.assertNotIn('display-scale', chosen)
        display = [n for n in child(tree, 'arm-io')[1]
                   if n[0].get('device_type') == b'display-subsystem\0']
        self.assertEqual(len(display), 1)
        props = display[0][0]
        self.assertEqual(props['name'], b'disp0\0')
        self.assertEqual(props['compatible'], b'negroni,display\0')
        self.assertEqual(props['canvas-width'], struct.pack('<I', 1080))
        self.assertEqual(props['canvas-height'], struct.pack('<I', 2400))
        self.assertNotIn('reg', props)  # GOP base comes from the checked handoff.

    def test_iphone_identity_has_nonzero_display_scale(self):
        result, tree = self.generate('--iphone-identity')
        self.assertEqual(result.returncode, 0, result.stderr)
        chosen = child(tree, 'chosen')[0]
        self.assertEqual(chosen['display-scale'], struct.pack('<I', 3))
        self.assertEqual(chosen['negroni-gop-handoff'], bytes(56))

    def test_explicit_mode_is_encoded_without_claiming_a_physical_reservation(self):
        result, tree = self.generate('--display-width', '1440', '--display-height', '3200')
        self.assertEqual(result.returncode, 0, result.stderr)
        display = child(child(tree, 'arm-io'), 'disp0')[0]
        self.assertEqual(display['canvas-width'], struct.pack('<I', 1440))
        self.assertEqual(display['canvas-height'], struct.pack('<I', 3200))
        self.assertEqual(child(tree, 'chosen')[0]['negroni-gop-handoff'], bytes(56))

    def test_invalid_or_partial_mode_rejected(self):
        for flags in (('--display-width', '0', '--display-height', '2400'),
                      ('--display-width', '1440'),
                      ('--display-height', '3200'),
                      ('--display-width', '4294967296', '--display-height', '3200'),
                      ('--display-width', '-1', '--display-height', '2400')):
            with self.subTest(flags=flags):
                result, tree = self.generate(*flags)
                self.assertNotEqual(result.returncode, 0)
                self.assertIsNone(tree)

    def test_simplefb_gop_size_is_short_of_rounded_mapping(self):
        # SimpleFbDxe sets FrameBufferSize to Width * Height * 4 exactly.
        page = 0x4000
        reserved_base, reserved_size = 0xB8000000, 0x2B00000
        for width, height, expected_acceptance in ((1080, 2400, False),
                                                    (1440, 3200, True)):
            with self.subTest(mode=(width, height)):
                gop_size = width * height * 4
                mapped = (gop_size + page - 1) // page * page
                self.assertLessEqual(reserved_base + mapped,
                                     reserved_base + reserved_size)
                self.assertEqual(mapped <= gop_size, expected_acceptance)


if __name__ == '__main__':
    unittest.main(verbosity=2)
