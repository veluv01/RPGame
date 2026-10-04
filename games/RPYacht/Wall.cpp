#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The wall, dealer and rail are CHBlackjack's (its Table.cpp).
#include <RPGame.h>
#include <string.h>
#include "Wall.h"
#include "Layout.h"
#include "src/assets/Assets.h"

namespace wall {

using namespace lay;

void backdrop(int rows) {
    // Pinstripe wallpaper: build one row, copy it down the wall (vertical
    // lines drawn pixel by pixel cost over a millisecond). gfx_copyRow copies
    // words from SRAM when both rows are word aligned; memcpy is a byte loop.
    uint8_t row[GFX_FB_STRIDE] __attribute__((aligned(4)));
    memset(row, NAVY | (NAVY << 4), sizeof row);
    for (int x = 3; x < 128; x += 8) row[x >> 1] = (uint8_t)((row[x >> 1] & 0x0F) | (INK << 4));
    for (int y = 0; y < rows; y++) gfx_copyRow(y, row, 0, GFX_W);
}

void dealer(uint8_t expr, uint8_t look, int x, int y) {
    sprite4(DEALER, x, y);
    int fx = x + (FACE_X - DEALER_X), fy = y + (FACE_Y - DEALER_Y);
    sprite4(FACE_NORMAL, fx, fy);
    if (expr > E_TALK) expr = E_NORMAL;
    if (expr) {
        for (uint16_t i = FACE_EDIT_AT[expr - 1]; i < FACE_EDIT_AT[expr]; i++) {
            uint16_t w = FACE_EDITS[i];
            uint16_t idx = w >> 4;
            gfx_pixel(fx + idx % 24, fy + idx / 24, w & 15);
        }
    }
    // Pupils glance toward whatever is moving (look: 0 left, 1 centre, 2 right).
    if (expr != E_BLINK && look != 1) {
        int dx = look == 0 ? -1 : 1;
        for (int e = 0; e < 2; e++) {
            int ex = fx + (e ? 15 : 5);                  // sclera, 4 px wide
            gfx_fillRect(ex, fy + 6, 4, 2, WHITE);
            gfx_fillRect(ex + 1 + dx, fy + 6, 2, 2, INK);
        }
    }
}

void rail(int y) {
    gfx_hline(0, y, 128, GOLD);
    gfx_fillRect(0, y + 1, 128, 2, WOOD);
    gfx_hline(0, y + 3, 128, INK);
}

}  // namespace wall
