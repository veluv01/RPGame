// SdSpi - a read-only SPI-mode microSD block driver (RPGameSD, MIT: see NOTICE).
//
// Portable read-only API backed by RPGameSD's CRC-checked card transport.
// The writable SD wrapper remains separate. Whole-sector reads and streams
// can use SDK-allocated DMA channels, with polled fallback after a fault.
//
// The PiZero has LCD SPI0 / onboard SD SPI1; Pico 2 shares SPI0. Calls wait
// for outstanding LCD flushing before reusing graphics memory. Shared bus
// format/clock are restored; dedicated transactions leave the LCD alone.
//
// After a read fails, call init() before reading again: a block whose token
// came late may still be on its way, and init()'s CMD0 is what clears it.
#pragma once
#include <stdint.h>

namespace sd {

bool init();                                // false: no card, or not one this can read
bool read(uint32_t lba, uint8_t *dst);      // one block; false: the card did not deliver

// Streaming, for a sketch that reads a file repeatedly (RPStlView reads a
// whole model every frame): CMD18 blocks alternate between buf0 / buf1,
// with fn(block, ctx) processing each CRC-checked block in order. The next
// block may arrive by DMA while fn processes the previous one. fn must not
// issue card operations, start an LCD flush or touch the other buffer.
// Streams longer than 65535 blocks are split into multiple commands.
// False: delivery/CRC failed; init() before the next read. Card timing and
// throughput require physical measurements and depend on the card/clock.
typedef void (*BlockFn)(const uint8_t *block, void *ctx);
bool stream(uint32_t lba, uint32_t n, uint8_t *buf0, uint8_t *buf1, BlockFn fn, void *ctx);

}  // namespace sd
