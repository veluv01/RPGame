#!/usr/bin/env python3
"""Generate the Arduino-Pico map with a bounded RPGame flash region."""
import argparse
import re
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--input', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--offset', type=lambda n: int(n, 0), required=True)
p.add_argument('--length', type=lambda n: int(n, 0), required=True)
p.add_argument('--sub', nargs=2, action='append', default=[])
a = p.parse_args()
assert a.offset in (0, 0x80000) and 0 < a.length <= 0x3f0000 - a.offset
s = a.input.read_text()
# Arduino-Pico's prebuilt OTA blob contains absolute addresses at flash 0.
# Enter the SDK CRT0 directly instead; its image metadata relocates correctly.
s = re.sub(r'    \.ota : \{.*?\} > FLASH', '    /DISCARD/ : { *ota.o(*) }', s, flags=re.S)
s = re.sub(r'    \.partition : \{.*?\} > FLASH', '', s, flags=re.S)
s = s.replace('ORIGIN = 0x10000000, LENGTH = __FLASH_LENGTH__',
              f'ORIGIN = {0x10000000 + a.offset:#x}, LENGTH = {a.length:#x}')
for key, value in a.sub:
    s = s.replace(key, value)
if '__FLASH_LENGTH__' in s or '__RAM_LENGTH__' in s:
    raise SystemExit('Missing linker substitutions')
a.out.write_text(s)
