#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed
// The card's side of the puzzle bank, and the bank's front door: the file
// PHRASES.BNK in the card's root folder (tools/phrases/build_bank.py writes
// it, and describes it) if a card with one is in the slot, the built-in bank
// if not.
//
// The file is a header block and then 64-byte records, eight a block, so a
// puzzle is one block read (and one more, the header again, for its
// category's name). A record carries its own check byte; a card that stops
// answering, or answers nonsense, is dropped until begin() finds it again.
//
// The card reader is the RPGameSD library, shared with CHWords and CHCrossword.
#include <string.h>
#include "Bank.h"
#include "src/bank/BankData.h"
#include <Fat.h>
#ifdef CHTEST
static uint8_t scratch[512];
static uint8_t *sector() { return scratch; }
#else
#include <RPGfx.h>
static uint8_t *sector() { return gfx_chunkScratch(); }     // idle between gfx_wait() and the flush
#endif

namespace bank {

static const uint8_t MAX_RUNS = 4;          // a file in more pieces than this is not used
static fat::Run run[MAX_RUNS];
static uint8_t nRuns, nCats;
static bool live;
static uint16_t counts[SECTIONS], firsts[SECTIONS];
static uint32_t cardId;

// Block k of the file, or nullptr if the card did not deliver it.
static const uint8_t *block(uint32_t k) {
    uint8_t *buf = sector();
    return fat::read(run, nRuns, k, buf) ? buf : nullptr;
}

void begin() {
    live = false;
    nRuns = fat::open("PHRASES BNK", run, MAX_RUNS, sector());
    if (!nRuns) return;
    const uint8_t *h = block(0);
    if (!h || memcmp(h, "WWPB\x01", 6)) return;         // magic, version 1
    nCats = h[6];
    for (uint8_t s = 0; s < SECTIONS; s++) {
        uint32_t n, first;
        memcpy(&n, h + 8 + 4 * s, 4);
        memcpy(&first, h + 20 + 4 * s, 4);
        if (!n || n > 0xFFFF || first > 0xFFFF) return;
        counts[s] = (uint16_t)n;
        firsts[s] = (uint16_t)first;
    }
    memcpy(&cardId, h + 32, 4);
    live = true;
}

bool card() { return live; }
uint16_t count(uint8_t section) { return live && section < SECTIONS ? counts[section] : flashCount(section); }
uint32_t id() { return live ? cardId : BANK_ID; }

bool fetch(uint8_t section, uint16_t index, char *text, char *category) {
    if (!live) { flashFetch(section, index, text, category); return true; }
    if (section < SECTIONS && index < counts[section]) {
        uint32_t rec = (uint32_t)firsts[section] + index;
        const uint8_t *p = block(1 + rec / 8);
        if (p) {
            p += (rec % 8) * 64;
            uint8_t sum = 0;
            for (uint8_t i = 0; i < 64; i++) sum = (uint8_t)(sum + p[i]);
            uint8_t cat = p[0];
            if (sum == 0xFF && p[1] == section && cat < nCats) {
                memcpy(text, p + 2, TEXT_MAX);
                text[TEXT_MAX - 1] = 0;
                p = block(0);
                if (p) {
                    memcpy(category, p + 64 + cat * 20, CAT_MAX);
                    category[CAT_MAX - 1] = 0;
                    return true;
                }
            }
        }
    }
    live = false;                   // the card has gone: the built-in bank from here on
    return false;
}

}  // namespace bank
