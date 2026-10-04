// The save record's contents, in and out of the library's flash pages
// (see Save.h).
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed
#include <RPGame.h>
#include "config.h"
#include "Save.h"

namespace save {

#if CHBG_LEAN
// A build without saving (config.h).
bool load(Options &, Stats &, bool &hasGame) { hasGame = false; return false; }
bool loadGame() { return false; }
bool store(const Options &, const Stats &, bool) { return false; }
#else
static const uint32_t MAGIC = save::magic("CHBG");
static const uint8_t VERSION = 2;              // 2: matches, the cube, more options

// The record's data (the library adds the header, with the "game in
// progress" flag, and the CRC).
struct Data {
    Options  opt;
    Stats    stats;
    match::Record game;
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
    return r && flag && match::load(r->game);
}

bool store(const Options &o, const Stats &s, bool withGame) {
    if (!available()) return false;
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = o;
    rec.stats = s;
    if (withGame) match::save(rec.game);
    return write(MAGIC, VERSION, sizeof rec, withGame);
}
#endif

}  // namespace save
