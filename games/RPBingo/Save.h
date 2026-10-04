// Saving options, lifetime stats, the jackpot and a game in progress (the
// purse, and a round if one is being played: its cards and draw come back
// from the round's seed).
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHBN"); this is what goes in
// it and back out.
#pragma once
#include <rpgame/Save.h>

class Bingo;

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Bingo &g, bool &hasGame); // options, stats, jackpot; the game too if hasGame
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Bingo &g, bool withGame);
void allowWrites(bool on);          // debug builds on the board: off until a script turns it on

}  // namespace save
