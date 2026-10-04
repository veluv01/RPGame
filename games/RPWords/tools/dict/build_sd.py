#!/usr/bin/env python3
"""Build WORDS.DIC, the full word list for the SD card.

    python tools/dict/build_sd.py            # -> sdcard/WORDS.DIC (about 4 MB)

The repository keeps the built file in sdcard/, laid out as it goes on the
card: copy it to the root folder of a FAT16 or FAT32 card. With it in the
slot the game checks your words against all of ENABLE (every word of 2 to
15 letters: about 168,000) instead of the list in flash.

The file is a hash table, so the game finds a word with one block read and
no index in RAM:

  block 0            "CHWD", version 1, log2(buckets), 2 spare bytes,
                     the word count (u32), the hash seed (u32)
  block 1 + bucket   every word whose hash lands in the bucket: its letters
                     as 1..26 with bit 7 set on the last one; a 0 ends the
                     list
  every block        its last two bytes are a check on the first 510:
                     c = c * 31 + byte, 16 bits

  bucket = (FNV-1a(seed, letters) >> 8) & (buckets - 1)

Every block is written, so the card never stalls on a block it has not seen
before. After writing, every word is looked up again the way the game does
it (Dict.cpp), and so are as many non-words.
"""
import random
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
from wordlist import load_enable  # noqa: E402

LOG2 = 13
BLOCK = 512


def fnv(seed, letters):
    h = seed
    for v in letters:
        h = ((h ^ v) * 16777619) & 0xFFFFFFFF
    return h


def check(block):
    c = 0
    for b in block[:510]:
        c = (c * 31 + b) & 0xFFFF
    return c


def seal(body):
    body = bytes(body) + bytes(510 - len(body))
    return body + struct.pack("<H", check(body))


def letters(word):
    return [ord(ch) - 96 for ch in word]


def build(words):
    for seed in range(0x811C9DC5, 0x811C9DC5 + 64):
        buckets = [bytearray() for _ in range(1 << LOG2)]
        for w in words:
            ls = letters(w)
            ls[-1] |= 0x80
            buckets[(fnv(seed, letters(w)) >> 8) & ((1 << LOG2) - 1)] += bytes(ls)
        if max(len(b) for b in buckets) <= 509:          # room for the 0 that ends the list
            break
    else:
        raise SystemExit("no seed fits every bucket: raise LOG2")
    header = b"CHWD" + bytes([1, LOG2, 0, 0]) + struct.pack("<II", len(words), seed)
    return seal(header) + b"".join(seal(b) for b in buckets), seed, max(len(b) for b in buckets)


def lookup(data, word):
    """As Dict.cpp does it."""
    h = data[:BLOCK]
    assert h[:5] == b"CHWD\x01" and check(h) == struct.unpack("<H", h[510:])[0]
    mask = (1 << h[5]) - 1
    seed = struct.unpack("<I", h[12:16])[0]
    k = 1 + ((fnv(seed, letters(word)) >> 8) & mask)
    blk = data[k * BLOCK:(k + 1) * BLOCK]
    assert check(blk) == struct.unpack("<H", blk[510:])[0]
    want, p = letters(word), 0
    while p < 510 and blk[p]:
        i, same = 0, True
        while True:
            c = blk[p]
            p += 1
            same = same and i < len(want) and want[i] == (c & 0x7F)
            i += 1
            if c & 0x80:
                break
        if same and i == len(want):
            return True
    return False


def main():
    words = load_enable()
    data, seed, fullest = build(words)
    out = ROOT / "sdcard" / "WORDS.DIC"
    out.parent.mkdir(exist_ok=True)
    out.write_bytes(data)
    print(f"{out}: {len(words)} words, {len(data)} bytes, seed {seed:#x}, fullest bucket {fullest} of 509 bytes")
    data = out.read_bytes()
    missing = sum(not lookup(data, w) for w in words)
    have = set(words)
    rng = random.Random(1)
    ghosts = tried = 0
    for w in words:
        s = list(w)
        s[rng.randrange(len(s))] = chr(97 + rng.randrange(26))
        s = "".join(s)
        if s not in have:
            tried += 1
            ghosts += lookup(data, s)
    print(f"checked: {missing} words missing, {ghosts} of {tried} non-words found")
    if missing or ghosts:
        raise SystemExit("WORDS.DIC is wrong")


if __name__ == "__main__":
    main()
