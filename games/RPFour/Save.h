// Saving options, lifetime stats and a game in progress.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHF4"); this is what goes in
// it and back out: the options, the record against each dealer and, after
// SAVE+QUIT, the board and whose turn it is.
#pragma once
#include <rpgame/Save.h>
#include "Game.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t speed;      // 0 fun, 1 quick (no close-up, shorter pauses)
    uint8_t level;      // last opponent chosen
    uint8_t first;      // who moves first against the dealer: 0 you, 1 the dealer, 2 take turns
    uint8_t pad[4];
};

// Against the dealer, at each level.
struct Stats {
    uint16_t won[game::LEVELS], lost[game::LEVELS], drawn[game::LEVELS];
};

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Options &o, Stats &s, bool &hasGame);
bool loadGame();                    // the saved game into game::
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Options &o, const Stats &s, bool withGame);

}  // namespace save
