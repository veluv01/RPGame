// What the save record holds (Save.h); the RPGame library writes it.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed
#include <RPGame.h>
#include "config.h"
#include "Save.h"

namespace save {

#if CHCW_LEAN
// A build without saving (config.h).
bool load(Options &, Progress &, bool &hasGame) { hasGame = false; return false; }
bool loadGame(game::Record &) { return false; }
bool store(const Options &, const Progress &, const game::Record *) { return false; }
#else
static const uint32_t MAGIC = save::magic("CHCW");
static const uint8_t VERSION = 1;

// The record's data (the library adds the header, with the "puzzle in
// progress" flag, and the CRC).
struct Data {
    Options  opt;
    game::Record game;
    Progress prog;
};

// (One call of read() for load() and loadGame(): smaller.)
__attribute__((noinline)) static const Data *best(uint8_t *flag) {
    return (const Data *)read(MAGIC, VERSION, sizeof(Data), flag);
}

bool load(Options &o, Progress &g, bool &hasGame) {
    uint8_t flag = 0;
    const Data *r = best(&flag);
    hasGame = false;
    if (!r) return false;
    o = r->opt;
    g = r->prog;
    hasGame = flag != 0;
    return true;
}

bool loadGame(game::Record &g) {
    uint8_t flag = 0;
    const Data *r = best(&flag);
    if (!r || !flag) return false;
    g = r->game;
    return true;
}

bool store(const Options &o, const Progress &g, const game::Record *game) {
    if (!available()) return false;
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = o;
    rec.prog = g;
    if (game) rec.game = *game;
    return write(MAGIC, VERSION, sizeof rec, game ? 1 : 0);
}
#endif

}  // namespace save
