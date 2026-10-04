// The save record's contents, in and out of the library's flash pages
// (see Save.h).
#pragma GCC optimize("Os")   // cold code: size over speed
#include <RPGame.h>
#include "config.h"
#include "Save.h"

namespace save {

#if CHSO_LEAN
// CHSO_LEAN (config.h, off so far): a device debug build without saving,
// for when the serial protocol no longer fits beside it.
bool load(Options &, Stats &, Klondike &) { return false; }
bool store(const Options &, const Stats &, const Klondike &) { return false; }
#else
static const uint32_t MAGIC = save::magic("CHSO");
static const uint8_t VERSION = 1;

// The record's data (the library adds the header and the CRC; its flag
// byte is unused here: always 0).
struct Data {
    Options  opt;
    Stats    stats;
    Klondike game;           // live = 0: none in progress
};
static_assert(sizeof(Data) <= MAX_DATA, "save record must fit one flash page");

bool load(Options &o, Stats &s, Klondike &game) {
    const Data *r = (const Data *)read(MAGIC, VERSION, sizeof(Data));
    if (!r) return false;
    o = r->opt;
    s = r->stats;
    game = r->game;
    return true;
}

bool store(const Options &o, const Stats &s, const Klondike &game) {
    if (!available()) return false;
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = o;
    rec.stats = s;
    rec.game = game;
    return write(MAGIC, VERSION, sizeof rec, 0);
}
#endif

}  // namespace save
