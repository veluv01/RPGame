"""Build WORD WHEEL's puzzle banks from tools/phrases/phrases.txt.

    python tools/phrases/build_bank.py              # flash bank trimmed to FLASH_BYTES
    python tools/phrases/build_bank.py --bytes 6000 # ... or to fit 6000 B (0: everything)
    python tools/phrases/build_bank.py --curve      # bytes against puzzles kept

Checks every puzzle (characters, blocked words, duplicates, that it wraps
onto the 12/14/14/12 board), wraps it, sorts it into a section (round,
toss-up, bonus), and writes:

  src/bank/BankData.{h,cpp}   the flash bank: Huffman-coded text, grouped by
                              section and category (FlashBank.cpp reads it)
  sdcard/PHRASES.BNK          the SD card bank (kept in the repository, as it goes on the card): every puzzle, 64-byte records
  tools/phrases/build/bank_ref.txt, sd_ref.txt
                              what each bank must read back as (host test)

The flash bank keeps an even share of every category when it is trimmed.
"""
import argparse
import codecs
import heapq
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
ROWS = (12, 14, 14, 12)
PUNCT = "&'-?!."
SECTIONS = ("ROUND", "TOSS", "BONUS")
CAT_MAX = 19
GROUP = 8                       # a byte-aligned restart every GROUP puzzles
# What the built-in bank may take of the game's flash: set from what
# tools/check_size.py says is left (see the README's "How it fits").
FLASH_BYTES = 1376
SD_MAGIC = b"WWPB"
SD_VERSION = 1


# ---------------------------------------------------------------------------
# Wrapping: the fewest lines that fit, then the most even. One or two lines
# go on the middle rows (14 wide); three start on the top row (12, 14, 14).
# ---------------------------------------------------------------------------
def widths(n):
    return (14,) * n if n <= 2 else ROWS[:n]


def wraps(words, n):
    """Every way to split words into n lines, in order."""
    if n == 1:
        yield [" ".join(words)]
        return
    for i in range(1, len(words) - n + 2):
        for rest in wraps(words[i:], n - 1):
            yield [" ".join(words[:i])] + rest


def wrap(text):
    words = text.split()
    for n in range(1, 5):
        if n > len(words):
            break
        w = widths(n)
        fits = [ls for ls in wraps(words, n) if all(len(l) <= w[i] for i, l in enumerate(ls))]
        if fits:
            # most even: smallest spread, then the widest line narrowest
            return min(fits, key=lambda ls: (max(map(len, ls)) - min(map(len, ls)),
                                             max(map(len, ls))))
    return None


# ---------------------------------------------------------------------------
# Reading and checking the list
# ---------------------------------------------------------------------------
def stable_hash(s):
    return zlib.crc32(s.encode("ascii"))


def load():
    blocked = [codecs.decode(l.strip(), "rot13").upper() for l in (HERE / "blocked.txt").read_text().splitlines()
               if l.strip() and not l.startswith("#")]
    cats, puzzles, seen, errors = [], [], {}, []
    for no, raw in enumerate((HERE / "phrases.txt").read_text(encoding="ascii").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue

        def bad(msg):
            errors.append(f"phrases.txt:{no}: {msg}: {line}")

        pin = None
        if line[:2] in ("R:", "T:", "B:"):
            pin, line = "RTB".index(line[0]), line[2:]
        if line.count("|") != 1:
            bad("expected CATEGORY|PUZZLE")
            continue
        cat, text = (s.strip() for s in line.split("|"))
        text = " ".join(text.split())
        if len(cat) > CAT_MAX:
            bad(f"category over {CAT_MAX} characters")
        if any(not (c.isupper() and c.isascii() or c == " " or c in PUNCT) for c in text):
            bad("only A-Z, spaces and & ' - ? ! . allowed")
            continue
        if not any(c.isalpha() for c in text):
            bad("no letters")
            continue
        if any(b in text for b in blocked):
            bad("blocked word")
            continue
        if text in seen:
            bad(f"duplicate of line {seen[text]}")
            continue
        seen[text] = no
        lines = wrap(text)
        if not lines:
            bad("does not fit the board")
            continue
        letters = sum(c.isalpha() for c in text)
        if pin is None:
            # Short puzzles are shared out between the bonus round, the
            # toss-ups and the rounds; a quarter of the long ones are toss-ups.
            h = stable_hash(text)
            if letters <= 14 and len(text.split()) <= 3:
                pin = (2, 1, 0)[h % 3]
            else:
                pin = 1 if letters <= 26 and h % 4 == 0 else 0
        if cat not in cats:
            cats.append(cat)
        puzzles.append((pin, cats.index(cat), "\n".join(lines)))
    if errors:
        sys.exit("\n".join(errors))
    return cats, puzzles


# ---------------------------------------------------------------------------
# Flash bank
# ---------------------------------------------------------------------------
END = "\0"


def huffman(texts):
    freq = {}
    for t in texts:
        for ch in t + END:
            freq[ch] = freq.get(ch, 0) + 1
    heap = [(n, i, (ch,)) for i, (ch, n) in enumerate(sorted(freq.items()))]
    heapq.heapify(heap)
    length = dict.fromkeys(freq, 0)
    tie = len(heap)
    while len(heap) > 1:
        a = heapq.heappop(heap)
        b = heapq.heappop(heap)
        for ch in a[2] + b[2]:
            length[ch] += 1
        heapq.heappush(heap, (a[0] + b[0], tie, a[2] + b[2]))
        tie += 1
    if len(length) == 1:
        length = dict.fromkeys(length, 1)
    assert max(length.values()) <= 15
    # canonical codes: by length, then by symbol
    order = sorted(length, key=lambda ch: (length[ch], ch))
    codes, code, prev = {}, 0, 0
    for ch in order:
        code <<= length[ch] - prev
        prev = length[ch]
        codes[ch] = (code, length[ch])
        code += 1
    counts = [sum(1 for ch in order if length[ch] == l) for l in range(1, prev + 1)]
    return codes, counts, order


def encode(puzzles, cats):
    """puzzles: sorted by (section, category). The category names follow the
    puzzles in the same code: name c is entry len(puzzles) + c. Returns the
    tables."""
    ncat = len(cats)
    texts = [t for _, _, t in puzzles] + list(cats)
    codes, counts, order = huffman(texts)
    data, groups = bytearray(), []
    acc = nbits = 0
    for i, text in enumerate(texts):
        if i % GROUP == 0:
            if nbits:
                data.append((acc << (8 - nbits)) & 0xFF)
                acc = nbits = 0
            groups.append(len(data))
        for ch in text + END:
            c, l = codes[ch]
            acc = (acc << l) | c
            nbits += l
            while nbits >= 8:
                nbits -= 8
                data.append((acc >> nbits) & 0xFF)
            acc &= (1 << nbits) - 1
    if nbits:
        data.append((acc << (8 - nbits)) & 0xFF)
    assert len(data) < 65536
    # start[s * ncat + c] = index of the first puzzle of section s, category c
    start, k = [], 0
    for s in range(3):
        for c in range(ncat):
            while k < len(puzzles) and puzzles[k][:2] < (s, c):
                k += 1
            start.append(k)
    start.append(len(puzzles))
    return bytes(data), groups, start, counts, order


def decode(data, groups, counts, order, index):
    """The reference decoder: what FlashBank.cpp does."""
    pos = groups[index // GROUP] * 8
    out = ""
    for skip in range(index % GROUP + 1):
        out = ""
        while True:
            code = first = at = 0
            for n in counts:
                code = (code << 1) | ((data[pos >> 3] >> (7 - (pos & 7))) & 1)
                pos += 1
                if code - first < n:
                    ch = order[at + code - first]
                    break
                at += n
                first = (first + n) << 1
            else:
                raise ValueError("bad code")
            if ch == END:
                break
            out += ch
    return out


def flash_size(data, groups, start, counts, order):
    return (len(data) + 2 * len(groups) + (1 if start[-1] < 256 else 2) * len(start) + len(counts) +
            len(order))


def trim(puzzles, keep):
    """An even share of every (section, category), earliest lines first."""
    if keep >= len(puzzles):
        return puzzles
    frac = keep / len(puzzles)
    out, by = [], {}
    for p in puzzles:
        by.setdefault(p[:2], []).append(p)
    for key in sorted(by):
        n = max(1, round(len(by[key]) * frac))
        out += by[key][:n]
    return out


def c_array(name, typ, vals, per=16):
    rows = [", ".join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
    body = ",\n    ".join(rows)
    return f"const {typ} {name}[{len(vals)}] = {{\n    {body},\n}};\n"


def sym(ch):
    return 0 if ch == END else ord(ch)


def write_flash(puzzles, cats, tables):
    data, groups, start, counts, order = tables
    bank_id = zlib.crc32(data) ^ len(puzzles)
    start_t = "uint8_t" if len(puzzles) < 256 else "uint16_t"
    h = f"""// GENERATED by tools/phrases/build_bank.py - do not edit.
// The built-in puzzle bank: {len(puzzles)} puzzles in {len(cats)} categories.
#pragma once
#include <stdint.h>

constexpr uint16_t BANK_PUZZLES = {len(puzzles)};
constexpr uint8_t BANK_CATS = {len(cats)};
constexpr uint8_t BANK_GROUP = {GROUP};          // puzzles per byte-aligned group
constexpr uint8_t BANK_MAXLEN = {len(counts)};         // longest code, bits
constexpr uint32_t BANK_ID = 0x{bank_id:08X}u;
extern const uint8_t BANK_DATA[{len(data)}];       // canonical Huffman, MSB first, 0 ends a puzzle
extern const uint16_t BANK_GROUPS[{len(groups)}];      // byte offset of each group
extern const {start_t} BANK_START[{len(start)}];       // first puzzle of (section, category); then the total
extern const uint8_t BANK_COUNTS[{len(counts)}];        // codes of each length, 1..BANK_MAXLEN
extern const uint8_t BANK_SYMBOLS[{len(order)}];       // symbols in code order
// BANK_DATA holds the category names too, after the puzzles: name c is entry
// BANK_PUZZLES + c.
"""
    c = ('// GENERATED by tools/phrases/build_bank.py - do not edit.\n#include "BankData.h"\n\n' +
         c_array("BANK_DATA", "uint8_t", list(data), 20) +
         c_array("BANK_GROUPS", "uint16_t", groups, 12) +
         c_array("BANK_START", start_t, start, 12) +
         c_array("BANK_COUNTS", "uint8_t", counts) +
         c_array("BANK_SYMBOLS", "uint8_t", [sym(ch) for ch in order]))
    out = ROOT / "src" / "bank"
    out.mkdir(parents=True, exist_ok=True)
    (out / "BankData.h").write_text(h, encoding="ascii", newline="\n")
    (out / "BankData.cpp").write_text(c, encoding="ascii", newline="\n")
    ref = HERE / "build"
    ref.mkdir(exist_ok=True)
    with open(ref / "bank_ref.txt", "w", encoding="ascii", newline="\n") as f:
        for s, cat, text in puzzles:
            f.write(f"{s}|{cats[cat]}|{text.replace(chr(10), '/')}\n")
    return bank_id


# ---------------------------------------------------------------------------
# SD bank: block 0 is the header, then 64-byte records, eight a block.
#   header: "WWPB", u16 version, u16 categories, u32 count[3], u32 first[3]
#           (record index of each section), u32 id, u32 crc32 of the 36 bytes
#           before it; from byte 64, 20 bytes a category name (NUL padded).
#   record: u8 category, u8 section, 56 bytes of text (lines split by \n, NUL
#           padded), 5 spare, u8 check (sum of the other 63 bytes, inverted).
# ---------------------------------------------------------------------------
def write_sd(puzzles, cats):
    assert len(cats) <= 22
    counts = [sum(1 for p in puzzles if p[0] == s) for s in range(3)]
    first = [sum(counts[:s]) for s in range(3)]
    body = bytearray()
    for s, cat, text in puzzles:
        rec = bytes([cat, s]) + text.encode("ascii").ljust(56, b"\0") + bytes(5)
        body += rec + bytes([~sum(rec) & 0xFF])
    bank_id = zlib.crc32(bytes(body)) ^ len(puzzles)
    head = SD_MAGIC + struct.pack("<HH3I3II", SD_VERSION, len(cats), *counts, *first, bank_id)
    head += struct.pack("<I", zlib.crc32(head))
    block0 = head.ljust(64, b"\0") + b"".join(c.encode("ascii").ljust(20, b"\0") for c in cats)
    img = block0.ljust(512, b"\0") + bytes(body)
    img = img.ljust((len(img) + 511) // 512 * 512, b"\0")
    out = ROOT / "sdcard"
    out.mkdir(exist_ok=True)
    (out / "PHRASES.BNK").write_bytes(img)
    (HERE / "build").mkdir(exist_ok=True)
    with open(HERE / "build" / "sd_ref.txt", "w", encoding="ascii", newline="\n") as f:
        for s, cat, text in puzzles:
            f.write(f"{s}|{cats[cat]}|{text.replace(chr(10), '/')}\n")
    return len(img), counts


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bytes", type=int, default=FLASH_BYTES, help="trim the flash bank to this many bytes")
    ap.add_argument("--curve", action="store_true", help="print bytes against puzzles kept")
    args = ap.parse_args()

    cats, puzzles = load()
    puzzles.sort(key=lambda p: p[:2])           # stable: file order within a category

    def build(keep):
        ps = trim(puzzles, keep)
        t = encode(ps, cats)
        return ps, t, flash_size(*t)

    if args.curve:
        for keep in range(100, len(puzzles) + 99, 100):
            ps, _, size = build(min(keep, len(puzzles)))
            print(f"{len(ps):5d} puzzles  {size:6d} B  {size / len(ps):.2f} B/puzzle")
        return

    keep = len(puzzles)
    ps, tables, size = build(keep)
    while args.bytes and size > args.bytes and keep > 30:
        keep -= max(1, (size - args.bytes) // 12)
        ps, tables, size = build(keep)

    # the flash bank must decode to exactly what went in
    data, groups, start, counts, order = tables
    for i, (_, _, text) in enumerate(ps):
        assert decode(data, groups, counts, order, i) == text, text
    for c, name in enumerate(cats):
        assert decode(data, groups, counts, order, len(ps) + c) == name, name
    write_flash(ps, cats, tables)
    sd_bytes, sd_counts = write_sd(puzzles, cats)
    per = [sum(1 for p in ps if p[0] == s) for s in range(3)]
    print(f"flash bank: {len(ps)} puzzles ({per[0]} round, {per[1]} toss-up, {per[2]} bonus), "
          f"{size} B, {size / len(ps):.2f} B/puzzle, codes up to {len(counts)} bits")
    print(f"SD bank:    {len(puzzles)} puzzles ({sd_counts[0]} round, {sd_counts[1]} toss-up, "
          f"{sd_counts[2]} bonus), {len(cats)} categories, sdcard/PHRASES.BNK {sd_bytes} B")
    if min(per) < 8:
        sys.exit("a section has fewer than 8 puzzles")


if __name__ == "__main__":
    main()
