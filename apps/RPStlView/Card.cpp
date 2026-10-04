// The card through RPGameSD. See Card.h.
#pragma GCC optimize("Os")
#include <RPGfx.h>
#include "Card.h"

namespace card {

static State st = NONE;
static fat::Run runs[MAX_RUNS];
static uint8_t nRuns;

State state() { return st; }
uint8_t runCount() { return nRuns; }

State mount() {
    gfx_wait();
    nRuns = 0;
    st = NONE;
    if (!sd::init()) return st;
    int8_t rc = fat::mount(gfx_chunkScratch());
    st = rc == fat::OK ? READY : rc == fat::E_EXFAT ? EXFAT : rc == fat::E_READ ? NONE : NOFS;
    return st;
}

bool list(const fat::File &dir, fat::ListFn fn, void *ctx) {
    if (st != READY) return false;
    gfx_wait();
    if (fat::list(dir, fn, ctx, gfx_chunkScratch()) == fat::OK) return true;
    st = NONE;
    return false;
}

int8_t openFile(const fat::File &f) {
    gfx_wait();
    int8_t n = fat::runs(f, runs, MAX_RUNS, gfx_chunkScratch());
    nRuns = n > 0 ? (uint8_t)n : 0;
    return n;
}

const uint8_t *block(uint32_t k) {
    gfx_wait();
    uint8_t *b = gfx_chunkScratch();
    if (fat::read(runs, nRuns, k, b)) return b;
    st = NONE;
    return nullptr;
}

bool stream(uint32_t b, uint32_t n, sd::BlockFn fn, void *ctx) {
    gfx_wait();
    uint8_t *buf0 = gfx_chunkScratch(), *buf1 = buf0 + GFX_CHUNK_BYTES;
    for (uint32_t r = 0; r < nRuns && n; r++) {
        uint32_t len = runs[r].blocks;
        if (b < len) {
            uint32_t take = len - b < n ? len - b : n;
            if (!sd::stream(runs[r].lba + b, take, buf0, buf1, fn, ctx)) {
                st = NONE;
                return false;
            }
            n -= take;
            b = 0;
        } else {
            b -= len;
        }
    }
    return n == 0;
}

}  // namespace card
