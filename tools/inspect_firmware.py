"""Print relevant strings without extracting credentials or uploading the dump."""
import pathlib
import re

dump = next(pathlib.Path('.').glob('*.bin'))
data = dump.read_bytes()
for match in re.finditer(rb'[ -~]{5,}', data):
    if re.search(rb'mpr|touch|i2c|scl|sda', match.group(), re.I):
        print(f'{match.start():08x}: {match.group().decode()}')
