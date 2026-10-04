// Saving the purse, options and lifetime stats.
//
// The RPGame library keeps the record in flash (rpgame/Save.h: two pages
// used in turn, a CRC, this game's own magic "CHPK"); this is what goes in
// it and back out: the purse (the chips at the table included), the
// options and the lifetime stats. Every game shares the two pages, so saving
// here can replace another game's save.
#pragma once
#include <rpgame/Save.h>
#include "Table.h"

namespace save {

// (save::available(), false when the image is too big or a write failed,
// is the library's.)
bool load(Options &o, Stats &s, int32_t &purse);
// Call after gfx_wait(): the page is built in RPGfx's chunk scratch.
bool store(const Options &o, const Stats &s, int32_t purse);

}  // namespace save
