// Saving options, lifetime stats and a game in progress.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHSN"); this is what goes in
// it and back out: the options, the stats and, when a game was saved, the
// whole game state as its turn began (so CONTINUE starts that turn again).
#pragma once
#include <rpgame/Save.h>
#include "Game.h"

struct Options {
    uint8_t sound;                  // 0 off, 1 on
    uint8_t pace;                   // 0 fun, 1 quick
    uint8_t seat[game::SEATS];      // the last table: game::Kind (+ the CPU's level) per seat
    uint8_t mode;                   // game::Mode
    uint8_t pad;
};

struct Stats {
    uint16_t games, humanWins, cpuWins;
    uint16_t fastest;               // the fewest rounds a game was won in (0: none yet)
    uint16_t ladders, snakes;       // climbed and slid down by players, ever
};

namespace save {

enum Game : uint8_t { NO_GAME, THIS_GAME, SAVED_GAME };

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Options &o, Stats &s, bool &hasGame);
bool loadGame();                    // the saved game, as its turn began
// With no game, the game being played (as its turn began), or the game
// already saved kept as it is. Call after gfx_wait(): the page is built in
// RPGfx's chunk scratch.
bool store(const Options &o, const Stats &s, Game game);

}  // namespace save
