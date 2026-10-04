// The card, through RPGameSD (the platform's SD library): mount, list a folder,
// and stream a file's blocks with RPGameSD's DMA multi-block read. Every call
// waits for the panel's flush first (even with a dedicated SD bus) and
// borrows RPGfx's chunk scratch, which is idle between flushes.
#pragma once
#include <Fat.h>
#include <SdSpi.h>

namespace card {

enum State : uint8_t { NONE, READY, EXFAT, NOFS };
State mount();                   // sd::init + fat::mount
State state();

// A folder's files and folders (fat::list).
bool list(const fat::File &dir, fat::ListFn fn, void *ctx);

// The open file: its runs of blocks. False: the chain is broken, or the
// file is in more than MAX_RUNS pieces (E_FRAG).
static const uint8_t MAX_RUNS = 16;
int8_t openFile(const fat::File &f);
uint8_t runCount();
// Block k of the open file into the chunk scratch; nullptr: not read.
const uint8_t *block(uint32_t k);
// Blocks [b, b + n) of the open file through fn, across its runs, each
// block handed over while the next arrives by DMA. False: the card failed
// (it is then unmounted: mount() again).
bool stream(uint32_t b, uint32_t n, sd::BlockFn fn, void *ctx);

}  // namespace card
