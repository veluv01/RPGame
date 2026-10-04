#pragma GCC optimize("Os")   // cold code: size over speed
// What a save holds (Save.h), packed into the library's record and read back.
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Bingo.h"
#include "Save.h"

namespace save {

static const uint32_t MAGIC = save::magic("CHBN");
static const uint8_t VERSION = 1;

// The record's data (the library adds the header, with the "game in
// progress" flag, and the CRC).
struct Data {
    int32_t   purse, jackpot;
    uint8_t   hasRound, buyN, pad[2];
    Options   opt;
    Stats     stats;
    RoundSave round;
};

bool load(Bingo &g, bool &hasGame) {
    uint8_t flag = 0;
    const Data *rec = (const Data *)read(MAGIC, VERSION, sizeof(Data), &flag);
    hasGame = false;
    if (!rec) return false;
    g.opt = rec->opt;
    g.stats = rec->stats;
    g.jackpot = rec->jackpot;
    if (flag && (rec->purse > 0 || rec->hasRound)) {
        hasGame = true;
        g.purse = rec->purse;
        g.buyN = rec->buyN;
        if (rec->hasRound) g.loadRound(rec->round);
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

bool store(const Bingo &g, bool withGame) {
    if (!available()) return false;
#if CHGAME_DEBUG && !defined(CHSIM)
    if (!writes) return false;
#endif
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = g.opt;
    rec.stats = g.stats;
    rec.jackpot = g.jackpot;
    if (withGame) {
        rec.purse = g.purse;                    // the cards in play are already paid for
        rec.buyN = g.buyN;
        if (g.hasRound()) { rec.hasRound = 1; g.saveRound(rec.round); }
    }
    return write(MAGIC, VERSION, sizeof rec, withGame);
}

}  // namespace save
