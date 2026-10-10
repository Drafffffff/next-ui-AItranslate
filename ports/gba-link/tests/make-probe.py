#!/usr/bin/env python3
"""Build the original ARM probe using Apple's clang; extract ELF .text.
No linker, Nintendo logo, BIOS or third-party game data required."""
import pathlib, struct, subprocess, sys
root = pathlib.Path(__file__).resolve().parent
out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build/gba-link/link-probe.gba')
out.parent.mkdir(parents=True, exist_ok=True)
source = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else root / 'link-probe.S'
obj = out.with_suffix('.o')
subprocess.run(['clang', '-target', 'armv4t-none-eabi', '-c', str(source), '-o', str(obj)], check=True)
data = obj.read_bytes()
assert data[:6] == b'\x7fELF\x01\x01'
off = struct.unpack_from('<I', data, 32)[0]
size, count, names = struct.unpack_from('<HHH', data, 46)
sections = [struct.unpack_from('<10I', data, off + i * size) for i in range(count)]
s = sections[names]; strings = data[s[4]:s[4] + s[5]]
for s in sections:
    name = strings[s[0]:].split(b'\0')[0]
    if name == b'.text':
        rom = bytearray(data[s[4]:s[4]+s[5]])
# Resolve only in-section ARM branch relocations; refuse every other type.
for section in sections:
    if section[1] not in (4, 9) or not section[5]:
        continue
    if section[1] != 9 or sections[section[7]][1] != 1:
        raise SystemExit('Unsupported relocation section')
    symbols = sections[section[6]]
    for off in range(section[4], section[4]+section[5], 8):
        location, info = struct.unpack_from('<II', data, off)
        typ, sym = info & 255, info >> 8
        _, value, _, _, _, target = struct.unpack_from('<IIIBBH', data, symbols[4]+sym*16)
        if typ not in (28, 29) or target != section[7]:
            raise SystemExit('Unsupported branch relocation')
        word = struct.unpack_from('<I', rom, location)[0]
        addend = word & 0xffffff
        if addend & 0x800000: addend -= 0x1000000
        delta = value + addend*4 - location
        assert delta % 4 == 0 and -(1<<25) <= delta < (1<<25)
        struct.pack_into('<I', rom, location, (word & 0xff000000) | ((delta//4) & 0xffffff))
rom[0xa0:0xac] = b'NEXTUI LINK '
rom[0xac:0xb0] = b'NLNK'
rom[0xb2] = 0x96
rom[0xbd] = (-sum(rom[0xa0:0xbd]) - 0x19) & 255
rom.extend(bytes(32768 - len(rom)))
out.write_bytes(rom)
print(out)
