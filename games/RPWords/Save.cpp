// Saving (Save.h): the record's layout and its two loads and one store.
#pragma GCC optimize("Os", "no-ipa-sra", "no-caller-saves")   // cold code: size over speed
#include <RPGame.h>
#include "config.h"
#include "Save.h"

namespace save {

#if CHWD_LEAN
// A build without saving (config.h).
bool load(Options &, Stats &, bool &hasGame) { hasGame = false; return false; }
bool loadGame() { return false; }
bool store(const Options &, const Stats &, bool) { return false; }
#else
static const uint32_t MAGIC = save::magic("CHWD");
static const uint8_t VERSION = 1;

// The record's data (the library adds the header, with the "game in
// progress" flag, and the CRC).
struct Data {
    Options  opt;
    Stats    stats;
    game::Record game;
};

// One call to read() for load() and loadGame(): the compiler copies the
// library's "is saving available" test into every caller (80 B less so).
__attribute__((noinline)) static const Data *find(uint8_t &flag) {
    return (const Data *)read(MAGIC, VERSION, sizeof(Data), &flag);
}

bool load(Options &o, Stats &s, bool &hasGame) {
    uint8_t flag = 0;
    const Data *r = find(flag);
    hasGame = false;
    if (!r) return false;
    o = r->opt;
    s = r->stats;
    hasGame = flag != 0;
    return true;
}

bool loadGame() {
    uint8_t flag = 0;
    const Data *r = find(flag);
    return r && flag && game::load(r->game);
}

bool store(const Options &o, const Stats &s, bool withGame) {
    if (!available()) return false;
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = o;
    rec.stats = s;
    if (withGame) game::save(rec.game);
    return write(MAGIC, VERSION, sizeof rec, withGame);
}
#endif

}  // namespace save
