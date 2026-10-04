// Saving options, lifetime stats, the jackpot meters and the purse.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHSL"); this is what goes in
// it and back out.
//
// A saved game is the purse, the machine you were sitting at and your bets.
// The game only saves between spins, never inside the free games or Hold and
// Spin: a feature is settled spin by spin, and a save in the middle of one
// would let a power cycle take it back. The MAJOR and GRAND meters are kept
// whether or not a game is in progress: they belong to the machine.
#pragma once
#include <rpgame/Save.h>

class Slots;

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Slots &g, bool &hasGame); // options, stats, meters; the purse too if hasGame
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Slots &g, bool withGame);

}  // namespace save
