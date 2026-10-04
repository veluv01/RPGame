// What a save holds (Save.h); the RPGame library writes it to flash.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <RPGame.h>
#include "config.h"
#include "Match.h"
#include "Save.h"

namespace save {

#if CHTT_LEAN
// Device debug builds: no saving (it does not fit beside the protocol).
bool load(Casino &, bool &hasGame) { hasGame = false; return false; }
bool store(const Casino &, bool) { return false; }
void allowWrites(bool) {}
#else

static const uint32_t MAGIC = save::magic("CHTT");
static const uint8_t VERSION = 2;           // 2: TOWER gone (stats per table)

// The record's data (the library adds the header, with the "run in
// progress" flag, and the CRC).
struct Data {
    int32_t  purse;
    uint8_t  mode, ante, streak, pad;
    Options  opt;
    Stats    stats;
};

bool load(Casino &c, bool &hasGame) {
    uint8_t flag = 0;
    const Data *rec = (const Data *)read(MAGIC, VERSION, sizeof(Data), &flag);
    hasGame = false;
    if (!rec) return false;
    c.opt = rec->opt;
    c.stats = rec->stats;
    if (flag && rec->purse > 0) {
        hasGame = true;
        c.purse = rec->purse;
        c.mode = rec->mode < MODE_COUNT ? rec->mode : 0;
        c.ante = rec->ante;
        c.streak = rec->streak;
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

bool store(const Casino &c, bool withGame) {
    if (!available()) return false;
#if CHGAME_DEBUG && !defined(CHSIM)
    if (!writes) return false;
#endif
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = c.opt;
    rec.stats = c.stats;
    rec.purse = c.purse;
    rec.mode = c.mode; rec.ante = c.ante; rec.streak = c.streak;
    return write(MAGIC, VERSION, sizeof rec, withGame);
}

#endif  // CHTT_LEAN

}  // namespace save
