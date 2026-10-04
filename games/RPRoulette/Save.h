// Saving options, lifetime stats and a game in progress (the purse, the
// layout on the felt and the tote board).
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHRL"); this is what goes in
// it and back out.
#pragma once
#include <rpgame/Save.h>

class Roulette;

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Roulette &r, bool &hasGame);   // options and stats; the game too if hasGame
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Roulette &r, bool withGame);
void allowWrites(bool on);          // debug builds on the board: off until a script turns it on

}  // namespace save
