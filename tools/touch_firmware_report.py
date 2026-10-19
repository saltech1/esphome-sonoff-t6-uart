"""Reproduce critical touch-interface findings for the exact supplied dump."""
import hashlib
import pathlib
import struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB

path = next(pathlib.Path('.').glob('*.bin'))
data = path.read_bytes()
expected = '28967f3075b3be6bd2c1033b5939e908a1c653cf9510861cc4f86e59ded28ffc'
if hashlib.sha256(data).hexdigest() != expected:
    raise SystemExit('Unrecognized firmware hash; these addresses must be re-analyzed.')


def check(condition, message):
    if not condition:
        raise SystemExit('Verification FAILED: ' + message)


def read(address, length):
    return data[address-0x10000:address-0x10000+length]


decoder = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
for address, target in ((0x2436E, 0x28D8C), (0x28DDE, 0x37518),
                        (0x37538, 0x120E60), (0x28DE8, 0x37590),
                        (0x28E10, 0x375BE), (0x375D4, 0x184B0),
                        (0x28B6E, 0x375DC)):
    instruction = next(decoder.disasm(read(address, 4), address, count=1))
    check(instruction.mnemonic == 'bl' and int(instruction.op_str[1:], 0) == target,
          f'call at 0x{address:x} should target 0x{target:x}')
    print(f'Verified call 0x{address:08X} -> 0x{target:08X}')

source, destination, end = struct.unpack('<III', read(0x10B50, 12))
check((source, destination, end) == (0x158E34, 0x4020E0, 0x4037B4), 'RAM copy table')
startup_source = source + 0x4028B0 - destination
startup = read(startup_source, 6)
settings = read(0x12E645, 12)
check(startup == bytes.fromhex('AA 55 01 01 00 01'), 'startup command')
check(settings == bytes.fromhex('AA 55 01 03 06 03 52 03 84 03 52 3A'), 'settings command')
for name, packet in (('Startup', startup), ('Settings', settings)):
    check(sum(packet[:-1]) & 0xFF == packet[-1], name + ' checksum')
    check(len(packet) == packet[4] + 6, name + ' frame length')
    print(f'{name}: {packet.hex(" ").upper()} (checksum verified)')

check(read(0x12AAC4, 1) == b'\x01', 'mux end GPIO')
check(read(0x12AAEB, 1) == b'\x00', 'mux start GPIO')
check(read(0x12AA76, 1) == b'\x00', 'mux function')
check(read(0x12AB12, 1) == b'\x05', 'mux GPIO configuration')
check(read(0x12A956, 6) == b'uart2\x00', 'UART device name')
check(struct.unpack('<I', read(0x184D8, 4))[0] == 0x80220C, 'UART2 TX FIFO literal')
print('UART2 mux: GPIO0..GPIO1, function 0; baud constant 0xE1 << 9 = 115200')
print('Chip model remains unidentified; physical wiring requires hardware confirmation.')
