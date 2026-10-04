// What a save holds (Save.h); the RPGame library writes it to flash.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Save.h"

namespace save {

#if CHBW_LEAN
// Device debug builds (the serial protocol) don't fit with this code and a
// page left over to save in, so saving is left out of them.
bool load(Options &, Stats &, bool &hasGame) { hasGame = false; return false; }
bool loadGame() { return false; }
bool store(const Options &, const Stats &, Game) { return false; }
#else
static const uint32_t MAGIC = save::magic("CHBW");
static const uint8_t VERSION = 1;

// The record's data (the library adds the header, with the "game in
// progress" flag, and the CRC).
struct Data {
    Options  opt;
    Stats    stats;
    game::State game;
};

bool load(Options &o, Stats &s, bool &hasGame) {
    uint8_t flag = 0;
    const Data *r = (const Data *)read(MAGIC, VERSION, sizeof(Data), &flag);
    hasGame = false;
    if (!r) return false;
    o = r->opt;
    s = r->stats;
    hasGame = flag != 0;
    return true;
}

bool loadGame() {
    uint8_t flag = 0;
    const Data *r = (const Data *)read(MAGIC, VERSION, sizeof(Data), &flag);
    return r && flag && game::restore(r->game);
}

bool store(const Options &o, const Stats &s, Game game) {
    if (!available()) return false;
    uint8_t oldFlag = 0;
    const Data *old = (const Data *)read(MAGIC, VERSION, sizeof(Data), &oldFlag);   // in flash
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    bool withGame = game == THIS_GAME;
    if (game == SAVED_GAME && old && oldFlag) { rec.game = old->game; withGame = true; game = NO_GAME; }
    rec.opt = o;
    rec.stats = s;
    if (game == THIS_GAME) rec.game = game::checkpoint();
    return write(MAGIC, VERSION, sizeof rec, withGame);
}
#endif

}  // namespace save
