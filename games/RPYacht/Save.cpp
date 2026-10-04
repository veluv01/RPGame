// What the save record holds (Save.h); the RPGame library writes it.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Save.h"
#include "Yacht.h"

namespace save {

#if CHYD_LEAN
// Device debug builds (the serial protocol) may not fit with this code and a
// page left over to save in, so saving is left out of them.
bool load(Yacht &, bool &hasGame) { hasGame = false; return false; }
bool store(const Yacht &, bool) { return false; }
#else
static const uint32_t MAGIC = save::magic("CHYD");   // its own: every game shares the pages
static const uint8_t VERSION = 1;

// The record's data (the library adds the header, with the "game in
// progress" flag, and the CRC).
struct Data {
    Options  opt;
    Stats    stats;
    int32_t  purse;
    uint8_t  mode, cur, round, rollsLeft, held;
    uint8_t  dice[5];
    uint16_t ante;
    Card     card[4];
};

bool load(Yacht &g, bool &hasGame) {
    uint8_t flag = 0;
    const Data *r = (const Data *)read(MAGIC, VERSION, sizeof(Data), &flag);
    hasGame = false;
    if (!r) return false;
    g.opt = r->opt;
    g.stats = r->stats;
    g.purse = r->purse;
    hasGame = flag != 0;
    if (hasGame) {
        int32_t purse = g.purse;
        g.newGame(r->mode);                     // seats and the rest from the mode
        g.purse = purse;
        g.cur = r->cur; g.round = r->round; g.rollsLeft = r->rollsLeft; g.held = r->held;
        g.ante = r->ante;
        memcpy(g.dice, r->dice, sizeof g.dice);
        memcpy(g.card, r->card, sizeof g.card);
    }
    return true;
}

bool store(const Yacht &g, bool withGame) {
    if (!available()) return false;
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = g.opt;
    rec.stats = g.stats;
    rec.purse = g.purse;
    if (withGame) {
        rec.mode = g.mode; rec.cur = g.cur; rec.round = g.round; rec.rollsLeft = g.rollsLeft; rec.held = g.held;
        rec.ante = g.ante;
        memcpy(rec.dice, g.dice, sizeof rec.dice);
        memcpy(rec.card, g.card, sizeof rec.card);
    }
    return write(MAGIC, VERSION, sizeof rec, withGame);
}
#endif

}  // namespace save
