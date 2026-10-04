// What the save record holds (Save.h); the RPGame library writes it.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <RPGame.h>
#include "config.h"
#include "Save.h"

namespace save {

#if CHPK_LEAN
// Device debug builds (the serial protocol) don't fit with this code and a
// page left over to save in, so saving is left out of them.
bool load(Options &, Stats &, int32_t &) { return false; }
bool store(const Options &, const Stats &, int32_t) { return false; }
#else
static const uint32_t MAGIC = save::magic("CHPK");
static const uint8_t VERSION = 1;

// The record's data (the library adds the header and the CRC; the header's
// flag byte is unused here: always 0).
struct Data {
    int32_t  purse;          // your money, the chips at the table included
    Options  opt;
    Stats    stats;
};

bool load(Options &o, Stats &s, int32_t &purse) {
    const Data *r = (const Data *)read(MAGIC, VERSION, sizeof(Data));
    if (!r) return false;
    o = r->opt;
    s = r->stats;
    purse = r->purse;
    return true;
}

bool store(const Options &o, const Stats &s, int32_t purse) {
    if (!available()) return false;
    Data &rec = *(Data *)buffer();              // zeroed, in RPGfx's chunk scratch
    rec.purse = purse;
    rec.opt = o;
    rec.stats = s;
    return write(MAGIC, VERSION, sizeof rec);
}
#endif

}  // namespace save
