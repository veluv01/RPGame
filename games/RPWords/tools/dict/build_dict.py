#!/usr/bin/env python3
"""Build the flash dictionary: src/dict/DictData.{h,cpp} and tools/dict/core.txt.

    python tools/dict/build_dict.py --bytes 15800     # as many words as fit
    python tools/dict/build_dict.py --stems 7500
    python tools/dict/build_dict.py --bytes 15800 --dry    # measure only

Which words: every 2- and 3-letter ENABLE word and the most frequent 4-8
letter ones (wordlist.py) are the stems; then every ENABLE word that one of
the RULES below makes from a word already in (walk -> walks, walked,
walking, walker -> walkers ...) comes in too, whatever its frequency. Those
cost almost nothing to store, which is why there are about two and a half
times as many words as stems.

The format (measure.py chose it: a third of the size of a DAWG):

  * A word that a rule makes from another word is folded into it: each entry
    is a base word and the set of rules that make words from it.
  * Entries are sorted and front-coded in blocks of 32: the length of the
    prefix shared with the entry before (none for a block's first), then
    the remaining letters, then a terminator that names the rule set.
  * Every symbol is Huffman coded, the letters and terminators with a table
    chosen by the letter before (28 tables: prefix lengths, word start,
    after a..z).  Tables are canonical: the longest length, the number of
    codes of each length, then the symbols in code order.
  * Blocks start on a byte; DICT_INDEX holds each block's offset.

The decoder is FlashDict.cpp; decode() below is its reference and
round-trips every word before anything is written.
"""
import argparse
import heapq
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
from wordlist import load_enable, ranked_core, ranking  # noqa: E402

BLOCK = 32
MIN_COMBO = 6           # rule sets rarer than this are split up
MAX_LEN = 15            # longest Huffman code, and longest word
CTX_P, CTX_START = 0, 1  # then 2 + letter

# (strip, add, double): the base word must end with `strip`, which comes
# off; `double` repeats the last letter; then `add` goes on.
#   walk + s, bake - e + ing, party - y + ies, run + n + ing
RULES = [
    ("", "s", 0), ("", "ed", 0), ("", "ing", 0), ("", "d", 0), ("", "er", 0), ("", "ly", 0), ("", "es", 0),
    ("", "ers", 0), ("", "r", 0), ("", "rs", 0), ("", "y", 0), ("y", "ies", 0), ("e", "ing", 0),
    ("", "ize", 0), ("", "ings", 0), ("y", "iest", 0), ("y", "ier", 0), ("", "ingly", 0), ("", "en", 0),
    ("y", "ied", 0), ("e", "ings", 0), ("", "e", 0), ("y", "ily", 0), ("", "edly", 0),
]
# (Chosen greedily from about a hundred candidates: each in turn the rule
# that added the most words per byte. Doubling rules - run, running - were
# among the candidates and did not make the cut, but the decoder has them.)


def derive(base, rule):
    strip, add, double = rule
    if strip and not base.endswith(strip):
        return None
    stem = base[: len(base) - len(strip)]
    if len(stem) < 2:
        return None
    if double:
        stem += stem[-1]
    return stem + add


def closure(words, allowed):
    """words, plus every allowed word the rules make from them (and so on)."""
    have = set(words)
    frontier = list(words)
    while frontier:
        new = []
        for b in frontier:
            for r in RULES:
                d = derive(b, r)
                if d and len(d) <= MAX_LEN and d in allowed and d not in have:
                    have.add(d)
                    new.append(d)
        frontier = new
    return sorted(have)


# ---------------------------------------------------------------------------
# Folding
# ---------------------------------------------------------------------------
def fold(words):
    """[(base, mask)] sorted by base; mask bit i: RULES[i] makes a word from base."""
    have = set(words)
    made_by = {}
    for b in words:
        for i, r in enumerate(RULES):
            d = derive(b, r)
            if d and d in have and d != b and d not in made_by:
                made_by[d] = (b, i)
    mask = defaultdict(int)
    removed, keep = {}, set()
    for w in sorted(words, key=lambda w: (-len(w), w)):
        if w in made_by and w not in keep:
            b, i = made_by[w]
            removed[w] = (b, i)
            mask[b] |= 1 << i
            keep.add(b)
    # A rare rule set costs a symbol in every table: reduce it to the nearest
    # common subset and give the dropped words entries of their own.
    count = Counter(mask[w] for w in words if w not in removed)
    allowed = {m for m, n in count.items() if n >= MIN_COMBO} | {0}
    for b in [b for b, m in mask.items() if m not in allowed]:
        m = mask[b]
        best = max((a for a in allowed if a & ~m == 0), key=lambda a: (bin(a).count("1"), a))
        for i in range(len(RULES)):
            if (m & ~best) >> i & 1:
                del removed[derive(b, RULES[i])]
        mask[b] = best
    return [(w, mask[w]) for w in words if w not in removed]


def expand(entries):
    out = []
    for w, m in entries:
        out.append(w)
        out += [derive(w, r) for i, r in enumerate(RULES) if m >> i & 1]
    return sorted(out)


# ---------------------------------------------------------------------------
# Huffman
# ---------------------------------------------------------------------------
def huff_lengths(freq):
    freq = dict(freq)
    while True:
        if len(freq) == 1:
            return {next(iter(freq)): 1}
        heap = [(n, s, (s,)) for s, n in sorted(freq.items())]
        heapq.heapify(heap)
        depth = Counter()
        while len(heap) > 1:
            a = heapq.heappop(heap)
            b = heapq.heappop(heap)
            for s in a[2] + b[2]:
                depth[s] += 1
            heapq.heappush(heap, (a[0] + b[0], min(a[1], b[1]), a[2] + b[2]))
        if max(depth.values()) <= MAX_LEN:
            return dict(depth)
        freq = {s: (n + 1) // 2 for s, n in freq.items()}   # flatten and retry


def canonical(lengths):
    """symbol -> (code, length), and the table's bytes."""
    order = sorted(lengths, key=lambda s: (lengths[s], s))
    mx = max(lengths.values())
    counts = [0] * (mx + 1)
    for s in order:
        counts[lengths[s]] += 1
    codes, code, prev = {}, 0, 0
    for s in order:
        code <<= lengths[s] - prev
        prev = lengths[s]
        codes[s] = (code, lengths[s])
        code += 1
    return codes, [mx] + counts[1:] + order


class Bits:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, code, length):
        for k in range(length - 1, -1, -1):
            self.acc = (self.acc << 1) | (code >> k & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def align(self):
        while self.n:
            self.put(0, 1)


# ---------------------------------------------------------------------------
# Encode / decode
# ---------------------------------------------------------------------------
def lcp(a, b):
    n = 0
    while n < len(a) and n < len(b) and a[n] == b[n]:
        n += 1
    return n


def symbols(entries, terms):
    """Per entry: [(context, symbol)]."""
    prev = ""
    index = {m: i for i, m in enumerate(terms)}
    for i, (w, m) in enumerate(entries):
        row = []
        n = 0
        if i % BLOCK:
            n = lcp(prev, w)
            row.append((CTX_P, n))
        for k in range(n, len(w) + 1):
            ctx = CTX_START if k == 0 else 2 + ord(w[k - 1]) - 97
            row.append((ctx, ord(w[k]) - 97 if k < len(w) else 26 + index[m]))
        prev = w
        yield row


def encode(entries):
    terms = sorted({m for _, m in entries})
    freq = defaultdict(Counter)
    for row in symbols(entries, terms):
        for ctx, sym in row:
            freq[ctx][sym] += 1
    codes, tables = {}, {}
    for ctx in range(28):
        if ctx in freq:
            codes[ctx], tables[ctx] = canonical(huff_lengths(freq[ctx]))
        else:
            tables[ctx] = [0]
    bits, index = Bits(), []
    for i, row in enumerate(symbols(entries, terms)):
        if i % BLOCK == 0:
            bits.align()
            index.append(len(bits.out))
        for ctx, sym in row:
            bits.put(*codes[ctx][sym])
    bits.align()
    huff, at = [], []
    for ctx in range(28):
        at.append(len(huff))
        huff += tables[ctx]
    return {"terms": terms, "huff": huff, "at": at, "index": index, "bits": bytes(bits.out),
            "entries": len(entries)}


def decode(d):
    """The reference decoder: the entries back from the tables."""
    bits, pos = d["bits"], 0

    def sym(ctx):
        nonlocal pos
        t = d["at"][ctx]
        mx = d["huff"][t]
        code = first = idx = 0
        for ln in range(1, mx + 1):
            code |= bits[pos >> 3] >> (7 - (pos & 7)) & 1
            pos += 1
            count = d["huff"][t + ln]
            if code - count < first:
                return d["huff"][t + 1 + mx + idx + code - first]
            idx += count
            first = (first + count) << 1
            code <<= 1
        raise AssertionError("bad code")

    out, word = [], ""
    for i in range(d["entries"]):
        if i % BLOCK == 0:
            pos = d["index"][i // BLOCK] * 8
            word = ""
        else:
            word = word[: sym(CTX_P)]
        while True:
            s = sym(CTX_START if not word else 2 + ord(word[-1]) - 97)
            if s >= 26:
                out.append((word, d["terms"][s - 26]))
                break
            word += chr(97 + s)
    return out


def term_bytes():
    return (len(RULES) + 7) // 8


def flash_bytes(d):
    return (len(d["bits"]) + len(d["huff"]) + 2 * len(d["at"]) + 2 * len(d["index"])
            + term_bytes() * len(d["terms"]) + 8 * len(RULES))


def build(enable, allowed, rank, stems):
    words = closure(ranked_core(enable, stems, rank), allowed)
    entries = fold(words)
    assert expand(entries) == words
    d = encode(entries)
    assert decode(d) == entries, "reference decoder disagrees"
    return words, d


# ---------------------------------------------------------------------------
def c_array(ctype, name, data, per_line=24):
    lines = [f"const {ctype} {name}[{len(data)}] = {{"]
    for i in range(0, len(data), per_line):
        lines.append("    " + ", ".join(str(v) for v in data[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bytes", type=int, help="flash budget: fit as many words as possible")
    ap.add_argument("--stems", type=int, help="exact number of stems (the words chosen by frequency)")
    ap.add_argument("--dry", action="store_true", help="measure only")
    ap.add_argument("--min-combo", type=int, help="rule sets rarer than this are split up")
    a = ap.parse_args()
    if a.min_combo:
        global MIN_COMBO
        MIN_COMBO = a.min_combo
    enable = load_enable()
    from wordlist import load_blocked
    allowed = set(enable) - load_blocked()
    rank = ranking(enable)
    if a.stems:
        n = a.stems
    else:
        budget = a.bytes or 15800
        lo, hi = 1200, 30000
        while lo < hi:                       # the most stems that fit
            mid = (lo + hi + 1) // 2
            if flash_bytes(build(enable, allowed, rank, mid)[1]) <= budget:
                lo = mid
            else:
                hi = mid - 1
        n = lo
    words, d = build(enable, allowed, rank, n)
    assert set(words) <= allowed
    total = flash_bytes(d)
    print(f"{len(words)} words from {n} stems in {d['entries']} entries, {total} bytes "
          f"({total / len(words):.3f} B/word): stream {len(d['bits'])}, tables {len(d['huff']) + 56}, "
          f"index {2 * len(d['index'])}, {len(d['terms'])} rule sets, longest word {max(map(len, words))}")
    if a.dry:
        return
    assert len(d["terms"]) + 26 <= 255 and len(RULES) <= 32
    assert all(len(s) <= 1 and len(t) <= 5 for s, t, _ in RULES)
    (HERE / "core.txt").write_text("\n".join(words) + "\n", newline="\n")
    tb = term_bytes()
    terms = [m >> (8 * k) & 255 for m in d["terms"] for k in range(tb)]
    rules = ", ".join("{%d, %d, \"%s\"}" % (ord(s) - 96 if s else 0, dbl, t) for s, t, dbl in RULES)
    h = f"""// Generated by tools/dict/build_dict.py - do not edit.
#pragma once
#include <stdint.h>

constexpr uint16_t DICT_WORDS = {len(words)};       // words in the flash list
constexpr uint16_t DICT_ENTRIES = {d['entries']};      // base words (the rest are made from them by the rules)
constexpr uint8_t DICT_BLOCK = {BLOCK};
// A rule makes a word from a base word: `strip` (a letter 1..26 the base
// must end with, or 0) comes off, `twice` repeats the last letter, `add`
// goes on.
struct DictRule {{ uint8_t strip, twice; char add[6]; }};
constexpr uint8_t DICT_RULES = {len(RULES)};
extern const DictRule DICT_RULE[DICT_RULES];
typedef uint32_t dictmask_t;                    // bit i: rule i makes a word
constexpr uint8_t DICT_TERM_BYTES = {tb};
extern const uint8_t DICT_TERM[{len(terms)}];         // terminator symbol - 26 -> its rule set, little-endian
extern const uint16_t DICT_HUFF_AT[28];         // each context's table in DICT_HUFF
extern const uint8_t DICT_HUFF[{len(d['huff'])}];
extern const uint16_t DICT_INDEX[{len(d['index'])}];         // each block's byte in DICT_BITS
extern const uint8_t DICT_BITS[{len(d['bits'])}];
"""
    c = ("// Generated by tools/dict/build_dict.py - do not edit.\n"
         "// The words are in tools/dict/core.txt; the format is described in build_dict.py.\n"
         '#include "DictData.h"\n\n'
         f"const DictRule DICT_RULE[DICT_RULES] = {{{rules}}};\n\n"
         + c_array("uint8_t", "DICT_TERM", terms) + "\n\n"
         + c_array("uint16_t", "DICT_HUFF_AT", d["at"]) + "\n\n"
         + c_array("uint8_t", "DICT_HUFF", d["huff"]) + "\n\n"
         + c_array("uint16_t", "DICT_INDEX", d["index"]) + "\n\n"
         + c_array("uint8_t", "DICT_BITS", list(d["bits"])) + "\n")
    out = ROOT / "src" / "dict"
    out.mkdir(parents=True, exist_ok=True)
    (out / "DictData.h").write_text(h, newline="\n")
    (out / "DictData.cpp").write_text(c, newline="\n")
    print(f"wrote {out / 'DictData.cpp'} and tools/dict/core.txt")


if __name__ == "__main__":
    main()
