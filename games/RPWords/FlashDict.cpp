#pragma GCC optimize("Os", "no-ipa-sra", "no-caller-saves")
// The flash list's decoder. The format is tools/dict/build_dict.py's (its
// decode() is the reference for this file): base words, sorted, front-coded
// in blocks of DICT_BLOCK, each with the set of rules (add an S, drop the E
// and add ING, ...) that make more words from it; every symbol Huffman
// coded with a table chosen by the letter before.
#include <string.h>
#include "Dict.h"
#include "src/dict/DictData.h"
#include <rpgame/RamFunc.h>

namespace dict {

static uint32_t pos;                // the bit being read

// One symbol with context ctx's canonical table: the longest code length,
// the number of codes of each length, the symbols in code order.
RAMFUNC(dictsym) static uint8_t sym(uint8_t ctx) {
    const uint8_t *t = DICT_HUFF + DICT_HUFF_AT[ctx];
    uint8_t mx = t[0];
    uint32_t p = pos;
    int code = 0, first = 0, idx = 0;
    for (uint8_t len = 1; len <= mx; len++) {
        code |= (DICT_BITS[p >> 3] >> (7 - (p & 7))) & 1;
        p++;
        int count = t[len];
        if (code - count < first) {
            pos = p;
            return t[1 + mx + idx + code - first];
        }
        idx += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    pos = p;
    return 26;                      // not reached with good data
}

// The entry at pos into w (len letters of the entry before are still in
// it; 0 at a block's start). Returns its rule set.
static dictmask_t entry(uint8_t *w, uint8_t &len, bool first) {
    if (!first) len = sym(0);
    for (;;) {
        uint8_t s = sym(len ? (uint8_t)(1 + w[len - 1]) : 1);
        if (s >= 26) {
            const uint8_t *t = DICT_TERM + (s - 26) * DICT_TERM_BYTES;
            dictmask_t m = 0;
            for (uint8_t k = DICT_TERM_BYTES; k--;) m = m << 8 | t[k];
            return m;
        }
        if (len < 15) w[len++] = (uint8_t)(s + 1);
    }
}

static int compare(const uint8_t *a, uint8_t na, const uint8_t *b, uint8_t nb) {
    uint8_t n = na < nb ? na : nb;
    int c = memcmp(a, b, n);
    return c ? c : (int)na - (int)nb;
}

// Is w a base word? Its rule set if so.
static bool findBase(const uint8_t *w, uint8_t n, dictmask_t &mask) {
    uint8_t cur[16], len;
    const uint16_t blocks = (uint16_t)((DICT_ENTRIES + DICT_BLOCK - 1) / DICT_BLOCK);
    // The last block whose first word is not after w.
    uint16_t lo = 0, hi = (uint16_t)(blocks - 1);
    while (lo < hi) {
        uint16_t mid = (uint16_t)((lo + hi + 1) / 2);
        pos = (uint32_t)DICT_INDEX[mid] * 8;
        len = 0;
        entry(cur, len, true);
        if (compare(cur, len, w, n) <= 0) lo = mid; else hi = (uint16_t)(mid - 1);
    }
    pos = (uint32_t)DICT_INDEX[lo] * 8;
    len = 0;
    uint16_t left = (uint16_t)(DICT_ENTRIES - lo * DICT_BLOCK);
    if (left > DICT_BLOCK) left = DICT_BLOCK;
    for (uint16_t i = 0; i < left; i++) {
        mask = entry(cur, len, i == 0);
        int c = compare(cur, len, w, n);
        if (c == 0) return true;
        if (c > 0) break;
    }
    return false;
}

bool hasCore(const uint8_t *w, uint8_t n) {
    dictmask_t mask;
    if (n < 2 || n > 15) return false;
    if (findBase(w, n, mask)) return true;
    // Or a base word with a rule applied: undo each rule that could have
    // made it and look the base up.
    for (uint8_t i = 0; i < DICT_RULES; i++) {
        const DictRule &r = DICT_RULE[i];
        uint8_t al = (uint8_t)strlen(r.add);
        if (n < al + 2 + r.twice) continue;
        uint8_t k = (uint8_t)(n - al);                 // the stem's letters
        bool fits = true;
        for (uint8_t j = 0; j < al; j++) fits &= w[k + j] == (uint8_t)(r.add[j] - 'a' + 1);
        if (r.twice) { fits &= w[k - 1] == w[k - 2]; k--; }
        if (!fits) continue;
        uint8_t base[16];
        memcpy(base, w, k);
        if (r.strip) base[k++] = r.strip;
        if (findBase(base, k, mask) && (mask >> i & 1)) return true;
    }
    return false;
}

void rewind(Cursor &c) {
    c.bit = 0;
    c.entry = 0;
    c.len = 0;
    c.mask = 0;
    c.rule = DICT_RULES;
}

bool next(Cursor &c, uint8_t *word, uint8_t &n) {
    // A form of the base word still to give?
    while (c.rule < DICT_RULES) {
        uint8_t i = c.rule++;
        if (!(c.mask >> i & 1)) continue;
        const DictRule &r = DICT_RULE[i];
        n = (uint8_t)(c.len - (r.strip != 0));
        memcpy(word, c.base, n);
        if (r.twice) { word[n] = word[n - 1]; n++; }
        for (const char *s = r.add; *s; s++) word[n++] = (uint8_t)(*s - 'a' + 1);
        return true;
    }
    if (c.entry >= DICT_ENTRIES) return false;
    bool first = c.entry % DICT_BLOCK == 0;
    if (first) { c.bit = (uint32_t)DICT_INDEX[c.entry / DICT_BLOCK] * 8; c.len = 0; }
    pos = c.bit;
    c.mask = entry(c.base, c.len, first);
    c.bit = pos;
    c.entry++;
    c.rule = 0;
    memcpy(word, c.base, c.len);
    n = c.len;
    return true;
}

uint16_t progress(const Cursor &c) { return (uint16_t)((uint32_t)c.entry * 256u / DICT_ENTRIES); }

}  // namespace dict
