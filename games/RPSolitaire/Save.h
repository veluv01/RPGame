// Saving the options, the lifetime stats and the game on the table.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHSO"); this is what goes in
// it and back out: the options, the stats (the Vegas bank among them) and
// the whole Klondike state, so CONTINUE picks up the game as it was (with
// live = 0 when none is in progress).
#pragma once
#include <rpgame/Save.h>
#include "Klondike.h"

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Options &o, Stats &s, Klondike &game);
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Options &o, const Stats &s, const Klondike &game);

}  // namespace save
