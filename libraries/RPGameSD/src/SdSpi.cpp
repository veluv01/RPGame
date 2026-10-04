#ifndef CHSIM
// The read-only sd:: API on the RP shared/dedicated-SPI CRC transport.
#include "SdSpi.h"
#include "RPGameSD.h"
#include <RPGfx.h>
namespace sd {
bool init() { gfx_wait(); return SD.begin(PIN_SD_CS, SPI_FULL_SPEED); }
bool read(uint32_t lba, uint8_t *dst) {
    gfx_wait();
    return dst && SD.rawCard().readBlock(lba, dst);
}
bool stream(uint32_t lba, uint32_t n, uint8_t *a, uint8_t *b, BlockFn fn, void *ctx) {
    if (!a || !b || !fn || lba > UINT32_MAX - n) return false;
    gfx_wait();
    while (n) {
        uint16_t count = n > 65535u ? 65535u : uint16_t(n);
        if (!SD.rawCard().readBlocksPipelined(lba, count, a, b, fn, ctx)) return false;
        lba += count; n -= count;
    }
    return true;
}
}

#endif
