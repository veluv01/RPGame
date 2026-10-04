// Saving options, best times and a puzzle in progress.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHCW"); this is what goes in
// it and back out: the options, the best time, stars and score of each
// built-in puzzle and which puzzles of the last card packs are solved, and
// the puzzle in progress (game::Record), if any.
#pragma once
#include <rpgame/Save.h>
#include "Game.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t felt;       // table colour theme (pal::Theme)
    uint8_t check;      // 0: words lock as they are completed; 1: no checking
    uint8_t skip;       // 0: typing steps over filled cells; 1: it goes cell by cell
    uint8_t view;       // 0: the whole grid, B held for the close-up; 1: the other way round
    uint8_t pad[3];
};

// What has been solved, and how well.
struct Progress {
    static const uint8_t BUILTIN = 20, CARD = 2;
    // A built-in puzzle's best: seconds (14 bits; 0 = not solved yet) with the
    // stars above them, and the best score in hundreds.
    struct Best { uint8_t timeLo, timeHi, score; } best[BUILTIN];
    // The last packs played from the card: which of their puzzles are solved.
    struct Card { uint32_t solved; uint16_t id, pad; } card[CARD];
};

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Options &o, Progress &p, bool &hasGame);
bool loadGame(game::Record &g);     // the saved puzzle in progress
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Options &o, const Progress &p, const game::Record *g);

}  // namespace save
