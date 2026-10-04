#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed
// What goes in the save record and back out (Save.h).
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Show.h"
#include "Bank.h"
#include "Save.h"

namespace save {

static const uint32_t MAGIC = save::magic("CHWW");
static const uint8_t VERSION = 1;

// The record's data (the library adds the header, with the "episode in
// progress" flag, and the CRC).
struct Data {
    Options  opt;
    Stats    stats;
    // The episode: where it is, who plays, what is banked, and the deal.
    uint8_t  stepIdx, starter, kind[3], pad[3];
    int32_t  total[3];
    uint32_t rng;
    bank::Deck deck;
};

bool load(Show &g, bank::Deck &deck, bool &hasGame) {
    uint8_t flag = 0;
    const Data *rec = (const Data *)read(MAGIC, VERSION, sizeof(Data), &flag);
    hasGame = false;
    if (!rec) return false;
    g.opt = rec->opt;
    g.stats = rec->stats;
    deck = rec->deck;
    if (flag) {
        hasGame = true;
        g.stepIdx = rec->stepIdx;
        g.starter = (uint8_t)(rec->starter % 3);
        for (uint8_t p = 0; p < 3; p++) {
            g.kind[p] = rec->kind[p] < P_KINDS ? rec->kind[p] : (uint8_t)P_HUMAN;
            g.total[p] = rec->total[p];
        }
        g.seed(rec->rng);
    }
    return true;
}

#if CHGAME_DEBUG && !defined(CHSIM)
// Debug builds on the board write only when a script asks (the E hook): the
// pages are shared with whatever else the board runs, and with the release.
static bool writes = false;
void allowWrites(bool on) { writes = on; }
#else
void allowWrites(bool) {}
#endif

bool store(const Show &g, const bank::Deck &deck, bool withGame) {
    if (!available()) return false;
#if CHGAME_DEBUG && !defined(CHSIM)
    if (!writes) return false;
#endif
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = g.opt;
    rec.stats = g.stats;
    rec.deck = deck;
    if (withGame) {
        rec.stepIdx = g.stepIdx;
        rec.starter = g.starter;
        for (uint8_t p = 0; p < 3; p++) { rec.kind[p] = g.kind[p]; rec.total[p] = g.total[p]; }
        rec.rng = g.rngState();
    }
    return write(MAGIC, VERSION, sizeof rec, withGame);
}

}  // namespace save
