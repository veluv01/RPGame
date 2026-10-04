#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed
// The built-in bank's decoder and the shuffled deal (the bank's front door
// is in SdBank.cpp).
#include <string.h>
#include "Bank.h"
#include "src/bank/BankData.h"

namespace bank {

uint16_t flashCount(uint8_t section) {
    if (section >= SECTIONS) return 0;
    return (uint16_t)(BANK_START[(section + 1) * BANK_CATS] - BANK_START[section * BANK_CATS]);
}

// Entry g of BANK_DATA (a puzzle, or a category name after them).
// Canonical Huffman, a bit at a time: at each length the codes are
// consecutive, so a code is known as soon as it falls inside its length's
// run. Puzzles cannot be entered mid-stream; every BANK_GROUP-th starts on a
// byte, and the ones before the wanted one are decoded and dropped.
static void decode(uint16_t g, char *text, uint8_t max) {
    uint32_t pos = (uint32_t)BANK_GROUPS[g / BANK_GROUP] * 8;
    for (uint8_t skip = (uint8_t)(g % BANK_GROUP + 1); skip--;) {
        uint8_t n = 0;
        for (;;) {
            uint16_t code = 0, first = 0, at = 0;
            uint8_t ch = 0;
            for (uint8_t len = 0; len < BANK_MAXLEN; len++) {
                code = (uint16_t)((code << 1) | ((BANK_DATA[pos >> 3] >> (7 - (pos & 7))) & 1));
                pos++;
                uint8_t k = BANK_COUNTS[len];
                if ((uint16_t)(code - first) < k) { ch = BANK_SYMBOLS[at + code - first]; break; }
                at = (uint16_t)(at + k);
                first = (uint16_t)((first + k) << 1);
            }
            if (!ch) break;
            if (n < max - 1) text[n++] = (char)ch;
        }
        text[n] = 0;
    }
}

void flashFetch(uint8_t section, uint16_t index, char *text, char *category) {
    text[0] = category[0] = 0;
    if (index >= flashCount(section)) return;
    uint16_t g = (uint16_t)(BANK_START[section * BANK_CATS] + index);
    uint8_t cat = 0;
    while (cat + 1 < BANK_CATS && BANK_START[section * BANK_CATS + cat + 1] <= g) cat++;
    decode(g, text, TEXT_MAX);
    decode((uint16_t)(BANK_PUZZLES + cat), category, CAT_MAX);
}

// Four Feistel rounds over the smallest even number of bits that holds n,
// walking past the values that fall outside (cycle walking keeps it a
// bijection on 0..n-1).
uint16_t permute(uint16_t i, uint16_t n, uint32_t key) {
    if (n < 2) return 0;
    uint8_t half = 1;
    while ((1u << (2 * half)) < n) half++;
    uint16_t mask = (uint16_t)((1u << half) - 1);
    uint16_t v = (uint16_t)(i % n);
    key = (key ^ (key >> 16)) * 0x9E3779B1u;
    do {
        uint16_t l = (uint16_t)(v >> half), r = (uint16_t)(v & mask);
        for (uint8_t round = 0; round < 4; round++) {
            uint32_t f = ((r + 1u + round) * 0x9E3779B1u) ^ (key >> (round * 5));
            f *= 0x85EBCA6Bu;
            f ^= f >> 13;
            uint16_t t = (uint16_t)(l ^ ((f >> 8) & mask));
            l = r;
            r = t;
        }
        v = (uint16_t)((l << half) | r);
    } while (v >= n);
    return v;
}

void Deck::shuffle(uint32_t s) {
    seed = s;
    bankId = id();
    memset(cursor, 0, sizeof cursor);
}

uint16_t Deck::draw(uint8_t section) {
    if (section >= SECTIONS) section = 0;
    uint16_t n = count(section);
    if (bankId != id()) shuffle(seed * 0x2545F491u + 1);
    if (!n) return 0;
    if (cursor[section] >= n) {                     // every puzzle seen: a new order
        cursor[section] = 0;
        seed = seed * 0x2545F491u + 0x3C6EF372u + section;
    }
    return permute(cursor[section]++, n, seed ^ (0x9E3779B9u * (section + 1)));
}

}  // namespace bank
