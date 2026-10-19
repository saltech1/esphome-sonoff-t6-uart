"""Local Thumb disassembly helper; offsets assume this dump's 0x10000 base."""
import pathlib
import struct
import sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB

data = next(pathlib.Path('.').glob('*.bin')).read_bytes()
start, end = (int(value, 0) for value in sys.argv[1:3])
for ins in Cs(CS_ARCH_ARM, CS_MODE_THUMB).disasm(data[start-0x10000:end-0x10000], start):
    note = ''
    if ins.mnemonic == 'ldr' and '[pc, #' in ins.op_str:
        displacement = int(ins.op_str.split('#')[1].split(']')[0], 0)
        offset = ((ins.address + 4) & ~3) + displacement - 0x10000
        note = f' ; literal=0x{struct.unpack_from("<I", data, offset)[0]:08x}'
    print(f'{ins.address:08x}: {ins.mnemonic:8} {ins.op_str}{note}')
