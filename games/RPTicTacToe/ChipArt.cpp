// Chips drawn in code (ChipArt.h): CHBlackjack's chips and stacks, and
// the small ones.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <RPGame.h>
#include "ChipArt.h"

namespace art {

// $1 white, $5 red, $10 blue, $25 green, $100 black (CHBlackjack's chips).
static const uint8_t CHIP_BODY[5] = {WHITE, RED, BLUE, FELT_LT, INK};
static const uint8_t CHIP_EDGE[5] = {BLUE, WHITE, WHITE, WHITE, GOLD};
static const uint8_t CHIP_SHADE[5] = {SILVER, WINE, NAVY, FELT_DK, INK};
const int32_t CHIP_VALUE[5] = {1, 5, 10, 25, 100};

int chipDenom(int32_t amount) {
    for (int i = 4; i >= 0; i--) if (amount >= CHIP_VALUE[i]) return i;
    return 0;
}

void chip(int cx, int y, uint8_t d, bool top) {
    uint8_t b = CHIP_BODY[d], e = CHIP_EDGE[d], sh = CHIP_SHADE[d];
    // Edge band (2 rows) with the classic stripes.
    gfx_hline(cx - 6, y + 2, 13, sh);
    gfx_hline(cx - 6, y + 3, 13, sh);
    gfx_pixel(cx - 7, y + 2, INK); gfx_pixel(cx + 7, y + 2, INK);
    gfx_pixel(cx - 7, y + 1, INK); gfx_pixel(cx + 7, y + 1, INK);
    for (int i = -4; i <= 4; i += 4) gfx_vline(cx + i, y + 2, 2, e);
    gfx_hline(cx - 5, y + 4, 11, INK);
    if (!top) return;
    gfx_fillEllipse(cx, y + 1, 7, 3, INK);                // the outline, then the face inside it
    gfx_fillEllipse(cx, y + 1, 6, 2, b);
    gfx_pixel(cx - 4, y + 1, e); gfx_pixel(cx + 4, y + 1, e);
    gfx_pixel(cx, y, e); gfx_pixel(cx, y + 2, e);
}

void chipStack(int cx, int baseY, int32_t amount, uint8_t maxChips) {
    uint8_t chips[24], n = 0;
    for (int d = 4; d >= 0 && n < 24; d--)
        while (amount >= CHIP_VALUE[d] && n < 24) { chips[n++] = (uint8_t)d; amount -= CHIP_VALUE[d]; }
    if (!n) return;
    uint8_t first = n > maxChips ? (uint8_t)(n - maxChips) : 0;   // show the top of tall stacks
    for (uint8_t i = first; i < n; i++)
        chip(cx, baseY - 2 * (i - first), chips[i], i == n - 1);
}

void miniStack(int x, int y, int32_t amount, int outline) {
    if (amount <= 0) return;
    uint8_t d = (uint8_t)chipDenom(amount);
    int n = 0;
    for (int k = 4; k >= 0 && n < 4; k--)
        while (amount >= CHIP_VALUE[k] && n < 4) { amount -= CHIP_VALUE[k]; n++; }
    uint8_t b = CHIP_BODY[d], e = CHIP_EDGE[d], sh = CHIP_SHADE[d];
    uint8_t o = outline >= 0 ? (uint8_t)outline : (d == 4 ? GOLD : INK);
    int top = y - n - 1;
    gfx_hline(x - 2, top, 5, o);                         // top outline
    for (int r = 1; r <= 2; r++) {                       // the face
        gfx_pixel(x - 3, top + r, o);
        gfx_hline(x - 2, top + r, 5, b);
        gfx_pixel(x + 3, top + r, o);
    }
    gfx_pixel(x, top + 1, e);
    for (int i = 0; i < n; i++) {                        // one edge band per chip
        int yy = top + 3 + i;
        gfx_pixel(x - 3, yy, o);
        gfx_pixel(x + 3, yy, o);
        for (int dx = -2; dx <= 2; dx++) gfx_pixel(x + dx, yy, (dx & 1) ? e : sh);
    }
    gfx_hline(x - 2, y + 2, 5, o);                       // bottom outline
}

void ghost(int x, int y, uint8_t c) {
    gfx_hline(x - 2, y - 2, 5, c);
    gfx_hline(x - 2, y + 2, 5, c);
    gfx_vline(x - 3, y - 1, 3, c);
    gfx_vline(x + 3, y - 1, 3, c);
}

}  // namespace art
