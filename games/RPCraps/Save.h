// Saving options, lifetime stats and the table in progress.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHCR"); this is what goes in
// it and back out.
//
// A saved game is the whole table - purse, every chip, the point, the
// shooter's hand - so CONTINUE picks up exactly where SAVE & QUIT left off,
// even in the middle of a hand (contract bets and all).
#pragma once
#include <rpgame/Save.h>

class Craps;

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Craps &g, bool &hasGame); // options and stats; the table too if hasGame
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Craps &g, bool withGame);

}  // namespace save
