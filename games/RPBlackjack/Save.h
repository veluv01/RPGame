// Saving the purse, options and lifetime stats.
//
// The Arduboy keeps these in EEPROM. The RPGame library keeps the record in
// flash instead (rpgame/Save.h: two pages that survive re-uploads, used in
// turn, a CRC, this game's own magic "CHBJ"; tools/probes/FlashProbe proved
// on hardware that the pages survive); this is what goes in it and back out.
//
// A record holds the options, the lifetime stats and the purse with a
// "game in progress" mark, so CONTINUE picks up the purse SAVE & QUIT left.
#pragma once
#include <rpgame/Save.h>

class Round;

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Round &r, bool &hasGame); // options + stats always; purse if hasGame
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Round &r, bool hasGame);

}  // namespace save
