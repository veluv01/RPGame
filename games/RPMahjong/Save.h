// Saving options, the best results and a game in progress.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHMJ"); this is what goes in
// it and back out.
//
// A saved game is the deal's seed and layout, the pairs taken (shuffles
// marked) and the chips, streak and clock: CONTINUE replays the deal and the
// moves onto the board.
#pragma once
#include <rpgame/Save.h>
#include "MahjongBoard.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t felt;       // table colour (pal::Theme)
    uint8_t faces;      // 0 classic, 1 easy (numbers)
    uint8_t speed;      // 0 normal, 1 quick (no deal animation, faster matches)
    uint8_t layout;     // last layout chosen
    uint8_t view;       // 0 the whole table, 1 close up (B held: the other)
    uint8_t pad[2];
};

struct Stats {          // per layout
    uint16_t bestChips[board::LAYOUTS], bestSecs[board::LAYOUTS], cleared[board::LAYOUTS];
};

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Options &o, Stats &s, bool &hasGame);
bool loadGame();                    // the saved game onto the board (replayed)
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Options &o, const Stats &s, bool withGame);

}  // namespace save
