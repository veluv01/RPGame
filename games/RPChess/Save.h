// Saving options, lifetime stats and a game in progress.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHCS"); this is what goes in
// it and back out: the options, the stats per opponent and, with the "game
// in progress" flag, the game (match::Record, replayed by loadGame()).
#pragma once
#include <rpgame/Save.h>
#include "Match.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t felt;       // board colour theme (pal::Theme)
    uint8_t unused;     // was hints (always on now): kept so saves keep their layout
    uint8_t unused2;    // was coords (always on now): the same
    uint8_t speed;      // 0 normal, 1 fast (the CPU's camera and moves)
    uint8_t level;      // last opponent chosen
    uint8_t side;       // last side chosen: 0 white, 1 black, 2 random
    uint8_t pad;
};

struct Stats {
    uint16_t won[match::LEVELS], lost[match::LEVELS], drawn[match::LEVELS];
};

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Options &o, Stats &s, bool &hasGame);
bool loadGame();                    // the saved game into match (replayed)
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Options &o, const Stats &s, bool withGame);

}  // namespace save
