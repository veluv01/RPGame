// Saving (Save.h): the record's layout, load and store.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Save.h"
#include "Craps.h"

namespace save {

#if CHCR_LEAN
// Device debug builds (the serial protocol) may not fit with this code and a
// page left over to save in, so saving is left out of them.
bool load(Craps &, bool &hasGame) { hasGame = false; return false; }
bool store(const Craps &, bool) { return false; }
#else
static const uint32_t MAGIC = save::magic("CHCR");
static const uint8_t VERSION = 1;

// The record's data (the library adds the header, with the "game in
// progress" flag, and the CRC).
struct Data {
    Options  opt;
    Stats    stats;
    int32_t  purse;
    uint16_t bet[BET_COUNT];
    uint8_t  point, handPoints;
    uint16_t handRolls;
    uint8_t  hist[6], histKind[6];
};

bool load(Craps &g, bool &hasGame) {
    uint8_t flag = 0;
    const Data *r = (const Data *)read(MAGIC, VERSION, sizeof(Data), &flag);
    hasGame = false;
    if (!r) return false;
    g.opt = r->opt;
    g.stats = r->stats;
    hasGame = flag != 0;
    if (hasGame) {
        g.newGame();
        g.purse = r->purse;
        memcpy(g.bet, r->bet, sizeof g.bet);
        g.point = r->point;
        g.handPoints = r->handPoints;
        g.handRolls = r->handRolls;
        memcpy(g.hist, r->hist, sizeof g.hist);
        memcpy(g.histKind, r->histKind, sizeof g.histKind);
        if (g.hist[0]) { g.d1 = (uint8_t)(g.hist[0] >> 4); g.d2 = (uint8_t)(g.hist[0] & 15); }
    }
    return true;
}

bool store(const Craps &g, bool withGame) {
    if (!available()) return false;
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = g.opt;
    rec.stats = g.stats;
    if (withGame) {
        rec.purse = g.purse;
        memcpy(rec.bet, g.bet, sizeof rec.bet);
        rec.point = g.point;
        rec.handPoints = g.handPoints;
        rec.handRolls = g.handRolls;
        memcpy(rec.hist, g.hist, sizeof rec.hist);
        memcpy(rec.histKind, g.histKind, sizeof rec.histKind);
    }
    return write(MAGIC, VERSION, sizeof rec, withGame);
}
#endif

}  // namespace save
