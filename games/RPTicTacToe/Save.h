// Saving options, lifetime stats and a run in progress (the purse, the table
// and the stake last chosen, the streak).
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHTT"); this is what goes in
// it and back out (the header's flag marks a run in progress).
#pragma once
#include <rpgame/Save.h>

struct Casino;

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Casino &c, bool &hasGame);     // options and stats; the run too if hasGame
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Casino &c, bool withGame);
void allowWrites(bool on);          // debug builds on the board: off until a script turns it on

}  // namespace save
