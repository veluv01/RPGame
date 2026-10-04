// Saving options, lifetime stats and a game in progress.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHCK"); this is what goes in
// it and back out.
//
// A saved game is the match's record (match::Record: the position it
// started from, the setup and every step played since, as indices into the
// steps legal at the time), so CONTINUE replays it to where SAVE & QUIT
// left off.
#pragma once
#include <rpgame/Save.h>
#include "Match.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t felt;       // board colour theme (pal::Theme)
    uint8_t music;      // 0 off, 1 on (the title's tune)
    uint8_t rules;      // house rules for the next game (eng::R_* bits)
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
