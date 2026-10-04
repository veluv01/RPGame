// Saving options, lifetime stats and a game in progress.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHWD"); this is what goes in
// it and back out: the options, the record against each CPU opponent and,
// when one is saved, the game in progress (game::Record: the board packed
// 5 bits a square, both racks, the scores, whose turn and the random
// state; the bag is what the board and racks leave). The header's flag
// byte says whether there is a game.
#pragma once
#include <rpgame/Save.h>
#include "Game.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t felt;       // board colour theme (pal::Theme)
    uint8_t level;      // last opponent chosen
    uint8_t pad[5];
};

// Against the CPU.
struct Stats {
    uint16_t won[game::LEVELS], lost[game::LEVELS];
    uint16_t bestGame, bestPlay;        // your highest score in a game, and for one play
};

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Options &o, Stats &s, bool &hasGame);
bool loadGame();                    // the saved game into game::
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Options &o, const Stats &s, bool withGame);

}  // namespace save
