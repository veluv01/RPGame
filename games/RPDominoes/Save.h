// Saving options, lifetime stats and a game in progress.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHDM"); this is what goes in
// it and back out: the options, the stats per CPU level and, with the "game
// in progress" flag, the match (match::Record: the round as it stands, the
// score and the generators).
#pragma once
#include <rpgame/Save.h>
#include "Match.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t felt;       // table colour theme (pal::Theme)
    uint8_t speed;      // 0 fun, 1 quick (no close-ups, shorter pauses)
    uint8_t level;      // last opponent chosen
    uint8_t game;       // last game chosen (dom::Game)
    uint8_t target;     // last target chosen (an index: short, usual, long)
    uint8_t tiles;      // the set of tiles (table::useSet)
    uint8_t pad;
};

// Against the CPU: rounds, and matches.
struct Stats {
    uint16_t won[match::LEVELS], lost[match::LEVELS];
    uint16_t mwon[match::LEVELS], mlost[match::LEVELS];
};

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Options &o, Stats &s, bool &hasGame);
bool loadGame();                    // the saved game into match
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Options &o, const Stats &s, bool withGame);

}  // namespace save
