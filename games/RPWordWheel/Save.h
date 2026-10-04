// Saving options, lifetime stats and an episode in progress (which step it
// has reached, who is playing, what each has banked, and where the deal of
// puzzles stands).
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHWW"); this is what goes in
// it and back out.
#pragma once
#include <rpgame/Save.h>

class Show;
namespace bank { struct Deck; }

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Show &g, bank::Deck &deck, bool &hasGame);   // options, stats, the deal; the episode too if hasGame
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Show &g, const bank::Deck &deck, bool withGame);
void allowWrites(bool on);          // debug builds on the board: off until a script turns it on

}  // namespace save
