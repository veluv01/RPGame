#pragma GCC optimize("Os", "no-ipa-sra", "no-caller-saves")
// The card's side of the dictionary: the file WORDS.DIC in the card's root
// folder (tools/dict/build_sd.py writes it, and describes it).
//
// The file is a hash table of 512-byte blocks, so a word is one block read:
// block 0 is a header, block 1 + (hash & mask) holds every word with that
// hash, each as its letters (1..26) with bit 7 set on the last, a 0 after
// the last word. The last two bytes of every block check the rest.
#include <string.h>
#include <RPGfx.h>
#include "Dict.h"
#include "src/dict/DictData.h"
#include <Fat.h>

namespace dict {

static const uint8_t MAX_RUNS = 6;          // a file in more pieces than this is not used
static fat::Run run[MAX_RUNS];
static uint8_t nRuns;
static bool live;
static uint32_t nWords, seed, mask;

// Block k of the file, checked, in the chunk scratch; nullptr if the card
// did not deliver it.
static const uint8_t *block(uint32_t k) {
    uint8_t *buf = gfx_chunkScratch();
    if (!fat::read(run, nRuns, k, buf)) return nullptr;
    uint16_t c = 0;
    for (uint16_t j = 0; j < 510; j++) c = (uint16_t)(c * 31 + buf[j]);
    return c == (uint16_t)(buf[510] | buf[511] << 8) ? buf : nullptr;
}

void begin() {
    live = false;
    nRuns = fat::open("WORDS   DIC", run, MAX_RUNS, gfx_chunkScratch());
    if (!nRuns) return;
    const uint8_t *h = block(0);
    if (!h || memcmp(h, "CHWD\x01", 5) || h[5] > 15) return;
    mask = (1ul << h[5]) - 1;
    memcpy(&nWords, h + 8, 4);
    memcpy(&seed, h + 12, 4);
    live = true;
}

bool card() { return live; }
uint32_t count() { return live ? nWords : DICT_WORDS; }

bool has(const uint8_t *w, uint8_t n) {
    if (!live) return hasCore(w, n);
    uint32_t h = seed;
    for (uint8_t i = 0; i < n; i++) h = (h ^ w[i]) * 16777619u;     // FNV-1a
    const uint8_t *p = block(1 + ((h >> 8) & mask));
    if (!p) {
        // The card has gone: the flash list from here on.
        live = false;
        return hasCore(w, n);
    }
    for (const uint8_t *end = p + 510; p < end && *p;) {
        uint8_t i = 0;
        bool same = true;
        for (;;) {
            uint8_t c = *p++;
            same &= i < n && w[i] == (c & 0x7F);
            i++;
            if (c & 0x80) break;
        }
        if (same && i == n) return true;
    }
    return false;
}

bool randomWord(char *out, uint32_t r) {
    for (uint8_t tries = 0; live && tries < 8; tries++, r = r * 1664525u + 1013904223u) {
        const uint8_t *p = block(1 + ((r >> 8) & mask));
        if (!p) { live = false; break; }
        uint8_t n = 0;
        for (uint16_t i = 0; i < 510 && p[i]; i++) n += p[i] >> 7;     // words in the bucket
        if (!n) continue;
        uint8_t k = (uint8_t)(r % n), len = 0;
        for (uint16_t i = 0; i < 510 && p[i]; i++) {
            if (!k && len < 15) out[len++] = (char)('A' - 1 + (p[i] & 0x7F));
            if ((p[i] & 0x80) && !k--) break;
        }
        out[len] = 0;
        return true;
    }
    return false;
}

}  // namespace dict
