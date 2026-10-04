#pragma GCC optimize("Os")   // cold code: size over speed
// What a save holds (Save.h), packed into the library's record and read back.
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Roulette.h"
#include "Save.h"

namespace save {

static const uint32_t MAGIC = save::magic("CHRL");
static const uint8_t VERSION = 1;
static const uint8_t LAYOUT_MAX = 48;            // (spot, amount) pairs

// The record's data (the library adds the header, with the "game in
// progress" flag, and the CRC).
struct Data {
    int32_t  purse;
    uint8_t  nLayout, nHist, wheel, pad;     // wheel: the layout's (1 = American)
    Options  opt;
    Stats    stats;
    uint8_t  layout[LAYOUT_MAX * 2];
    uint8_t  history[8];
};

bool load(Roulette &r, bool &hasGame) {
    uint8_t flag = 0;
    const Data *rec = (const Data *)read(MAGIC, VERSION, sizeof(Data), &flag);
    hasGame = false;
    if (!rec) return false;
    r.opt = rec->opt;
    r.stats = rec->stats;
    if (flag && rec->purse > 0) {
        hasGame = true;
        r.purse = rec->purse;
        r.nHist = rec->nHist > 8 ? 8 : rec->nHist;
        memcpy(r.history, rec->history, sizeof r.history);
        // The purse holds the layout's money too: it goes back down if the
        // layout was made on the wheel now chosen.
        if (rec->wheel == (r.opt.wheel != 0))
            r.loadLayout(rec->layout, rec->nLayout > LAYOUT_MAX ? LAYOUT_MAX : rec->nLayout);
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

bool store(const Roulette &r, bool withGame) {
    if (!available()) return false;
#if CHGAME_DEBUG && !defined(CHSIM)
    if (!writes) return false;
#endif
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.opt = r.opt;
    rec.stats = r.stats;
    if (withGame) {
        // Everything the player has: after SAVE & QUIT the layout is already
        // back in the purse; otherwise it is still on the felt.
        rec.purse = r.purse + (r.phase == Phase::Quit ? 0 : r.onTable());
        rec.wheel = r.us ? 1 : 0;
        rec.nLayout = r.saveLayout(rec.layout, LAYOUT_MAX);
        rec.nHist = r.nHist;
        memcpy(rec.history, r.history, sizeof rec.history);
    }
    return write(MAGIC, VERSION, sizeof rec, withGame);
}

}  // namespace save
