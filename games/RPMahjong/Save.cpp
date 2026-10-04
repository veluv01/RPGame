#pragma GCC optimize("Os")   // cold code: size over speed
// What goes in the save record and back out (Save.h).
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Save.h"

namespace save {

static const uint32_t MAGIC = save::magic("CHMJ");
// Saved games replay the deal from its seed: bump this whenever the deal or a
// layout changes (tools/tests checks the deals against known ones).
static const uint8_t VERSION = 1;

// The record's data (the library adds the header, with the "game in
// progress" flag, and the CRC).
struct Data {
    Options  opt;
    Stats    stats;
    board::Record game;
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
    return r && flag && board::restore(r->game);
}

bool store(const Options &o, const Stats &s, bool withGame) {
    if (!available()) return false;
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = o;
    rec.stats = s;
    if (withGame) board::save(rec.game);
    return write(MAGIC, VERSION, sizeof rec, withGame);
}

}  // namespace save
