// The save record (Save.h) in and out of the library's flash pages.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
#include <RPGame.h>
#include <string.h>
#include "Save.h"
#include "Round.h"

namespace save {

static const uint32_t MAGIC = save::magic("CHBJ");
// The record's header was {u32 magic; u16 version; u16 seq}: version 1 as a
// u16 is the bytes 01 00, which the library's {u8 version, flag} reads as
// version 1 with flag 0. So saves from before carry over: the flag is
// written 0 and ignored, and "a game in progress" stays in the data.
static const uint8_t VERSION = 1;

// The record's data (the library adds the header and the CRC).
struct Data {
    int32_t  purse;
    uint8_t  hasGame, pad[3];
    Options  opt;
    Stats    stats;
};

bool load(Round &r, bool &hasGame) {
    const Data *d = (const Data *)read(MAGIC, VERSION, sizeof(Data));
    hasGame = false;
    if (!d) return false;
    r.opt = d->opt;
    r.stats = d->stats;
    hasGame = d->hasGame && d->purse > 0;
    if (hasGame) r.purse = d->purse;
    return true;
}

bool store(const Round &r, bool hasGame) {
    if (!available()) return false;
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.purse = r.purse;
    rec.hasGame = hasGame ? 1 : 0;
    rec.opt = r.opt;
    rec.stats = r.stats;
    return write(MAGIC, VERSION, sizeof rec, 0);
}

}  // namespace save
