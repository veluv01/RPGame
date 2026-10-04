"""Validate the processor family and block structure of RPGame UF2 files."""
import struct

FAMILIES = {"rp2040": 0xE48BFF56, "rp2350-arm": 0xE48BFF59, "rp2350-riscv": 0xE48BFF5A}
ABSOLUTE_FAMILY = 0xE48BFF57


def inspect_uf2(data, target):
    if not data or len(data) % 512:
        raise ValueError("UF2 must contain complete 512-byte blocks")
    expected = FAMILIES[target]
    families = set()
    groups = {}
    for offset in range(0, len(data), 512):
        start0, start1, flags, address, size, number, count, family = struct.unpack_from("<8I", data, offset)
        end = struct.unpack_from("<I", data, offset + 508)[0]
        if (start0, start1, end) != (0x0A324655, 0x9E5D5157, 0x0AB16F30):
            raise ValueError(f"Bad UF2 magic at block {offset // 512}")
        if not flags & 0x2000 or not 0 < size <= 476 or not number < count:
            raise ValueError(f"Bad UF2 block metadata at block {offset // 512}")
        families.add(family)
        numbers = groups.setdefault((family, count), set())
        if number in numbers:
            raise ValueError("Duplicate UF2 block number")
        numbers.add(number)
        if family == expected and not 0x10000000 <= address < address + size <= 0x11000000:
            raise ValueError("Application UF2 block lies outside RP flash address space")
    allowed = {expected} if target == "rp2040" else {expected, ABSOLUTE_FAMILY}
    if expected not in families or not families <= allowed:
        raise ValueError(f"UF2 processor families {sorted(hex(x) for x in families)} do not match {target}")
    for (family, count), numbers in groups.items():
        # Picotool emits an intentionally incomplete ABSOLUTE family guard
        # with an RP2 IGNORE_BLOCK extension before the RP2350 application.
        if family == expected and len(numbers) != count:
            raise ValueError("UF2 is missing blocks")
    return {"target": target, "blocks": len(data) // 512,
            "families": sorted(hex(x) for x in families)}
