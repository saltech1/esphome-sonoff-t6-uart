"""Find literal pointers, Thumb BL callers, and selected bytes in the local dump.

Usage: python tools/firmware_xrefs.py refs ADDRESS...
       python tools/firmware_xrefs.py bytes ADDRESS LENGTH
Addresses use the apparent 0x10000 image mapping. BL matches are candidates;
data can decode as instructions, so inspect callers before drawing conclusions.
"""
import pathlib
import struct
import sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB

BASE = 0x10000
data = next(pathlib.Path('.').glob('*.bin')).read_bytes()
mode = sys.argv[1]
if mode == 'bytes':
    start, length = (int(x, 0) for x in sys.argv[2:])
    for offset in range(start - BASE, start - BASE + length, 16):
        chunk = data[offset:min(offset + 16, start - BASE + length)]
        printable = ''.join(chr(x) if 32 <= x < 127 else '.' for x in chunk)
        print(f'{offset + BASE:08x}: {chunk.hex(" "):47} {printable}')
elif mode == 'refs':
    targets = {int(x, 0) for x in sys.argv[2:]}
    for target in sorted(targets):
        needle = struct.pack('<I', target)
        matches = [hex(offset + BASE) for offset in range(0, len(data)-3, 2)
                   if data[offset:offset+4] == needle]
        print(f'POINTER 0x{target:x}: {", ".join(matches)}')
    decoder = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    for offset in range(0, len(data)-3, 2):
        first, second = struct.unpack_from('<HH', data, offset)
        if first & 0xf800 != 0xf000 or second & 0xd000 != 0xd000:
            continue
        for ins in decoder.disasm(data[offset:offset+4], offset+BASE, count=1):
            if ins.mnemonic == 'bl' and ins.op_str.startswith('#0x'):
                target = int(ins.op_str[1:], 0)
                if target in targets:
                    print(f'BL 0x{ins.address:x} -> 0x{target:x}')
else:
    raise SystemExit('Expected refs or bytes')
