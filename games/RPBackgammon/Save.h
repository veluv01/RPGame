// Saving options, lifetime stats and a game in progress.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHBG"); this is what goes in
// it and back out.
//
// A saved game is the position as the turn began, its roll and the dice
// generator's state (match::Record), so CONTINUE can never change a roll.
#pragma once
#include <rpgame/Save.h>
#include "Match.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t felt;       // board colour theme (pal::Theme)
    uint8_t speed;      // 0 fun, 1 quick (no close-ups, shorter pauses)
    uint8_t level;      // last opponent chosen
    uint8_t mirror;     // 1: the home boards on the left
    uint8_t coach;      // (unused: the coach was cut for room; kept for the record's layout)
    uint8_t length;     // last match length chosen (an index: single game, 3, 5, 7)
    uint8_t rules;      // RULE_* bits (0: the defaults)
};
enum : uint8_t {
    RULE_MANUAL = 1,    // no automatic throws or forced plays
    RULE_BEAVERS = 2,   // beavers and raccoons
};

// Against the CPU: games, and matches longer than one point.
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
