#!/usr/bin/env python3
"""Verify the board's final ARM64 copyin/copyout PAN instruction contract."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def verify(disassembly):
    functions = {}
    current = None
    for line in disassembly.splitlines():
        header = re.fullmatch(r'([0-9a-f]+) <([^>]+)>:', line.strip())
        if header:
            current = header[2]
            functions[current] = []
        instruction = re.match(r'\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(.+)', line)
        if instruction and current:
            functions[current].append((int(instruction[1], 16), int(instruction[2], 16), instruction[3]))
    result = {}
    for name, helper in (('_copyin', '__bcopyin'), ('_copyout', '__bcopyout')):
        instructions = functions.get(name, [])
        clears = [i for i, (_, word, _) in enumerate(instructions) if word == 0xd500409f]
        restores = [i for i, (_, word, _) in enumerate(instructions) if word == 0xd500419f]
        calls = [i for i, (_, word, text) in enumerate(instructions)
                 if word >> 26 == 0b100101 and f'<{helper}>' in text]
        if len(clears) != 1 or len(restores) != 1 or len(calls) != 1:
            raise ValueError(f'{name}: expected exactly one PAN clear, restore and BL {helper}; '
                             f'found {len(clears)}, {len(restores)}, {len(calls)}')
        clear, restore, call = clears[0], restores[0], calls[0]
        if not clear < call or restore != call + 1:
            raise ValueError(f'{name}: PAN must clear before the copy and restore immediately on return')
        # Permit only register MOVs between the clear and call: no exit/branch
        # can bypass the call or its immediate PAN restoration in this sequence.
        if any(not text.startswith('mov\t') and not text.startswith('mov ')
               for _, _, text in instructions[clear + 1:call]):
            raise ValueError(f'{name}: unexpected control/instruction between PAN clear and copy')
        if not any(word & 0xffffffe0 == 0xd5384260 for _, word, _ in instructions[:clear]):
            raise ValueError(f'{name}: missing PAN read before clear')
        result[name] = {label: f'0x{instructions[index][0]:016x}'
                        for label, index in (('clear', clear), ('copy_call', call), ('restore', restore))}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kernel', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='JSON evidence; adjacent .disassembly.txt is also written')
    parser.add_argument('--objdump', default=shutil.which('llvm-objdump') or '/opt/homebrew/opt/llvm/bin/llvm-objdump')
    args = parser.parse_args()
    command = [args.objdump, '--disassemble-symbols=_copyin,_copyout', str(args.kernel)]
    text = subprocess.check_output(command, text=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.with_suffix('.disassembly.txt').write_text(text)
    evidence = {'kernel': str(args.kernel.resolve()), 'sha256': hashlib.sha256(args.kernel.read_bytes()).hexdigest(), 'command': command}
    try:
        evidence['functions'] = verify(text)
        evidence['passed'] = True
    except ValueError as error:
        evidence.update(passed=False, error=str(error))
    args.output.write_text(json.dumps(evidence, indent=2) + '\n')
    if not evidence['passed']:
        raise SystemExit(evidence['error'])
    print(f'PAN verified: copyin and copyout clear/call/restore ({evidence["sha256"]})')


if __name__ == '__main__':
    main()
