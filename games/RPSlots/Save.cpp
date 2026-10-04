#pragma GCC optimize("Os")   // cold code: size over speed
// What a save holds (Save.h), packed into the library's record and read back.
#include <RPGame.h>
#include "config.h"
#include "Save.h"
#include "Slots.h"

namespace save {

#if CHSL_LEAN
// Device debug builds (the serial protocol) may not fit with this code and a
// page left over to save in, so saving is left out of them.
bool load(Slots &, bool &hasGame) { hasGame = false; return false; }
bool store(const Slots &, bool) { return false; }
#else
static const uint32_t MAGIC = save::magic("CHSL");
static const uint8_t VERSION = 1;

// The record's data (the library adds the header, with the "game in
// progress" flag, and the CRC).
struct Data {
    Options  opt;
    Stats    stats;
    uint32_t pot[2];
    int32_t  purse;
    uint8_t  machine, betIdx[M_COUNT], pad;
};

bool load(Slots &g, bool &hasGame) {
    uint8_t flag = 0;
    const Data *r = (const Data *)read(MAGIC, VERSION, sizeof(Data), &flag);
    hasGame = false;
    if (!r) return false;
    g.opt = r->opt;
    g.stats = r->stats;
    g.pot[0] = r->pot[0]; g.pot[1] = r->pot[1];
    hasGame = flag != 0;
    if (hasGame) {
        g.newGame();
        g.purse = r->purse;
        g.machine = r->machine < M_COUNT ? r->machine : 0;
        for (uint8_t m = 0; m < M_COUNT; m++) g.betIdx[m] = r->betIdx[m] & 3;
    }
    return true;
}

bool store(const Slots &g, bool withGame) {
    if (!available()) return false;
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = g.opt;
    rec.stats = g.stats;
    rec.pot[0] = g.pot[0]; rec.pot[1] = g.pot[1];
    rec.purse = g.purse;
    rec.machine = g.machine;
    for (uint8_t m = 0; m < M_COUNT; m++) rec.betIdx[m] = g.betIdx[m];
    return write(MAGIC, VERSION, sizeof rec, withGame);
}
#endif

}  // namespace save
