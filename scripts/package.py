"""RPGame v1 SD package, RP2350 RISC-V at flash offset 0x80000."""
import argparse
from pathlib import Path
import struct
import zlib

MAGIC = b'RPGAME1\0'
FAMILY = 0xe48bff5a
LAYOUT = 0x52504703
OFFSET, END, SECTOR = 0x80000, 0x3f0000, 4096

def inspect_image(binary, origin):
    """Validate the SDK RISC-V IMAGE_DEF and the closed metadata block loop.

    Arduino-Pico 6.2.0 emits one IMAGE_DEF followed by one ignored end block.
    Addresses in ENTRY_POINT are absolute; next-block offsets are relative.
    This deliberately accepts that known layout rather than arbitrary images.
    """
    if len(binary) < 32:
        raise ValueError('Missing ROM image metadata')
    marker, image, entry, pc, sp, last, nxt, end = struct.unpack_from('<8I',binary)
    if (marker,image,entry,last,end) != (0xffffded3,0x11010142,0x344,0x4ff,0xab123579):
        raise ValueError('Expected RP2350 RISC-V SDK IMAGE_DEF')
    if not origin <= pc < origin+len(binary) or pc & 1 or sp != 0x20082000:
        raise ValueError('Entry point/stack do not fit the linked image')
    if nxt < 32 or nxt+20 > len(binary) or nxt % 4:
        raise ValueError('Invalid metadata block link')
    words = struct.unpack_from('<5I',binary,nxt)
    if words != (0xffffded3,0x1fe,0x1ff,(-nxt)&0xffffffff,0xab123579):
        raise ValueError('Metadata block loop does not close')
    return {'entry':hex(pc),'stack':hex(sp),'metadata_end':hex(origin+nxt)}

def make_package(binary, title, author='RPGame port', version='0.3.1'):
    if not binary or len(binary) > END - OFFSET:
        raise ValueError('Application does not fit the game region')
    inspect_image(binary, 0x10000000+OFFSET)
    payload = binary + b'\xff' * (-len(binary) % SECTOR)
    if len(payload) > END - OFFSET:
        raise ValueError('Padded application exceeds the game region')
    h = bytearray(512)
    struct.pack_into('<8s6I', h, 0, MAGIC, 1, FAMILY, LAYOUT, OFFSET,
                     len(payload), zlib.crc32(payload))
    for off, length, text in ((32,32,title),(64,32,author),(96,16,version)):
        raw = text.encode('ascii')
        if len(raw) >= length:
            raise ValueError('Package label too long')
        h[off:off+len(raw)] = raw
    struct.pack_into('<I', h, 508, zlib.crc32(h[:508]))
    return bytes(h) + payload

def inspect(data):
    if len(data) < 512:
        raise ValueError('Truncated package')
    magic, ver, family, layout, offset, length, crc = struct.unpack_from('<8s6I',data)
    if (magic,ver,family,layout,offset) != (MAGIC,1,FAMILY,LAYOUT,OFFSET):
        raise ValueError('Wrong format, processor or flash layout')
    if not length or length % SECTOR or length > END-OFFSET or len(data) != 512+length:
        raise ValueError('Invalid package length')
    if zlib.crc32(data[:508]) != struct.unpack_from('<I',data,508)[0]:
        raise ValueError('Header CRC mismatch')
    if zlib.crc32(data[512:]) != crc:
        raise ValueError('Payload CRC mismatch')
    inspect_image(data[512:],0x10000000+OFFSET)
    return dict(title=data[32:64].split(b'\0')[0].decode('ascii'), image_bytes=length,
                payload_crc=hex(crc), family=hex(family), flash_offset=hex(offset))

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('file',type=Path)
    print(inspect(p.parse_args().file.read_bytes()))
