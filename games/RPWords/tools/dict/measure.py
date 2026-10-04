#!/usr/bin/env python3
"""Words-vs-bytes curves for the candidate flash dictionary formats.

Prints, for several core-list sizes, the total bytes (payload + model +
index) of each format so the flash format is chosen from real numbers.

    python tools/dict/measure.py
"""
import heapq
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from wordlist import load_enable, ranked_core  # noqa: E402

BLOCK = 32


def huff_lengths(freq):
    """Symbol -> code length for a Huffman code over freq (a Counter)."""
    if len(freq) == 1:
        return {next(iter(freq)): 1}
    heap = [(n, i, (s,)) for i, (s, n) in enumerate(sorted(freq.items()))]
    heapq.heapify(heap)
    depth = Counter()
    tick = len(heap)
    while len(heap) > 1:
        a = heapq.heappop(heap)
        b = heapq.heappop(heap)
        for s in a[2] + b[2]:
            depth[s] += 1
        heapq.heappush(heap, (a[0] + b[0], tick, a[2] + b[2]))
        tick += 1
    return dict(depth)


def entropy_bits(freq):
    tot = sum(freq.values())
    return -sum(n * math.log2(n / tot) for n in freq.values())


def lcp(a, b):
    n = 0
    while n < len(a) and n < len(b) and a[n] == b[n]:
        n += 1
    return n


def fold_s(words):
    """Drop W+'S' where W is present; return [(word, hasS)]."""
    have = set(words)
    out = []
    for w in words:
        if w.endswith("s") and w[:-1] in have and len(w) > 2:
            continue
        out.append((w, (w + "s") in have and len(w) >= 2))
    return out


def events(entries, order):
    """Yield (context, symbol) for the front-coded stream.

    Symbols: ('P', n) shared-prefix length (context 'P'), letters, and the
    terminators '$' / '$s'.  Letter context is the previous `order` letters
    of the word ('' padded with '^').
    """
    prev = ""
    for i, (w, has_s) in enumerate(entries):
        if i % BLOCK == 0:
            n = 0
        else:
            n = lcp(prev, w)
            if n == len(w):          # cannot happen in a sorted unique list
                raise AssertionError
            yield ("P", n)
        for k in range(n, len(w) + 1):
            sym = w[k] if k < len(w) else ("$s" if has_s else "$")
            if order == 0:
                ctx = "L"
            else:
                ctx = ("^" * order + w[:k])[-order:]
            yield (ctx, sym)
        prev = w


def model_bytes_canon(tables):
    """Flash bytes for canonical tables: per context a count-per-length
    list (5 bits x maxlen, with a 4-bit maxlen) plus 5-bit symbols."""
    bits = 0
    for lens in tables.values():
        mx = max(lens.values())
        bits += 4 + 5 * mx + 5 * len(lens)
    return (bits + 7) // 8 + 2 * len(tables) // 2  # + ~1 B offset per table


def measure_front(words, order, sfold, coder):
    entries = fold_s(words) if sfold else [(w, False) for w in words]
    freq = defaultdict(Counter)
    for ctx, sym in events(entries, order):
        freq[ctx][sym] += 1
    if coder == "5bit":
        bits = 0
        for ctx, c in freq.items():
            bits += sum(c.values()) * (3 if ctx == "P" else 5)
        model = 0
    elif coder == "huff":
        tables = {ctx: huff_lengths(c) for ctx, c in freq.items()}
        bits = sum(n * tables[ctx][s] for ctx, c in freq.items() for s, n in c.items())
        model = model_bytes_canon(tables)
    else:  # entropy bound (arithmetic coder), same model cost guess
        bits = sum(entropy_bits(c) for c in freq.values())
        model = sum(len(c) for c in freq.values())  # ~1 B per used (ctx,sym)
    nblocks = (len(entries) + BLOCK - 1) // BLOCK
    return int(bits + 7) // 8 + model + 2 * nblocks, len(entries)


def dawg_edges(words):
    """Edge count of the minimal DAWG (suffix-merged trie)."""
    trie = {}
    for w in words:
        node = trie
        for ch in w:
            node = node.setdefault(ch, {})
        node["$"] = {}
    seen = {}

    def canon(node):
        key = tuple(sorted((ch, canon(ch_node)) for ch, ch_node in node.items()))
        if key not in seen:
            seen[key] = len(seen)
        return seen[key]

    sys.setrecursionlimit(10000)
    canon(trie)
    return sum(sum(1 for ch, _ in key if ch != "$") for key in seen), len(seen)


def main():
    enable = load_enable()
    print(f"ENABLE: {len(enable)} words")
    sizes = [3000, 5000, 7000, 9000, 12000]
    rows = [
        ("front 5-bit", 0, False, "5bit"),
        ("front huff o0", 0, False, "huff"),
        ("front huff o0 +S", 0, True, "huff"),
        ("front huff o1", 1, False, "huff"),
        ("front huff o1 +S", 1, True, "huff"),
        ("front huff o2 +S", 2, True, "huff"),
        ("front arith o1 +S", 1, True, "arith"),
        ("front arith o2 +S", 2, True, "arith"),
    ]
    print(f"{'format':20s}" + "".join(f"{n:>16d}" for n in sizes))
    cores = {n: ranked_core(enable, n) for n in sizes}
    for name, order, sfold, coder in rows:
        line = f"{name:20s}"
        for n in sizes:
            b, _ = measure_front(cores[n], order, sfold, coder)
            line += f"{b:>9d} {b / n:5.2f}B"
        print(line)
    line = f"{'DAWG edges':20s}"
    for n in sizes:
        e, nodes = dawg_edges(cores[n])
        # 5-bit letter + EOW + last + pointer of ceil(log2(edges)) bits
        ptr = max(1, math.ceil(math.log2(e)))
        b = (e * (7 + ptr) + 7) // 8
        line += f"{b:>9d} {b / n:5.2f}B"
    print(line + "   (fixed-width pointers)")


if __name__ == "__main__":
    main()
