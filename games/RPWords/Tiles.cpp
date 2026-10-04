// The tiles' serif letters (Tiles.h), plotted pixel by pixel into the
// 4-bit framebuffer: shadow, then half ink, then ink.
#pragma GCC optimize("Os", "no-ipa-sra", "no-caller-saves")
#include <RPGame.h>
#include "Tiles.h"
#include "src/assets/Assets.h"

static inline void plot(uint8_t *p, int x, uint8_t c) {
    if (x & 1) *p = (uint8_t)((*p & 0x0F) | (c << 4));
    else       *p = (uint8_t)((*p & 0xF0) | c);
}

static const uint8_t *tileGlyph(uint8_t letter) {
    const uint8_t *g = TILEFONT;
    for (uint8_t k = 1; k < letter; k++) g += 1 + (((g[0] & 15) * ((g[0] >> 4) + 8) * 2 + 7) >> 3);
    return g;
}

// The glyph at (x, y): its shadow, its half ink, its ink.
RAMFUNC(tileglyph) static void tileDraw(const uint8_t *g, int x, int y, uint8_t c, uint8_t mid, uint8_t shadow) {
    int gw = g[0] & 15, rows = (g[0] >> 4) + 8, n = gw * rows;
    static const int8_t PLANE[3] = {0, 1, 0}, D[3] = {1, 0, 0};
    for (int pass = 0; pass < 3; pass++) {
        uint8_t col = (pass == 2 ? c : pass ? mid : shadow) & 0x0F;
        int d = D[pass];
        uint16_t bit = (uint16_t)(PLANE[pass] * n);
        for (int r = 0; r < rows; r++)
            for (int i = 0; i < gw; i++, bit++)
                if ((g[1 + (bit >> 3)] << (bit & 7)) & 0x80) {
                    int xx = x + i + d, yy = y + r + d;
                    if ((unsigned)xx < GFX_W && (unsigned)yy < GFX_H) plot(gfx_fb + yy * GFX_FB_STRIDE + (xx >> 1), xx, col);
                }
    }
}

void tileLetter(int x, int y, int w, uint8_t letter, uint8_t c, uint8_t mid, uint8_t shadow) {
    const uint8_t *g = tileGlyph(letter);
    int gw = g[0] & 15;
    // A pixel left of centre (the shadow balances it), but the wide letters
    // (9 px and more: A B D G H K M N O Q R U V W X) a pixel further right,
    // where they look centred; for M and W the shadow then runs onto the
    // bevel, which is its colour.
    int left = (w - gw + 1) / 2 - 1 + (gw >= 9);
    if (left > w - 1 - gw) left = w - 1 - gw;
    if (left < 0) left = 0;
    tileDraw(g, x + left, y, c, mid, shadow);
}

int tileText(int x, int y, const char *s, uint8_t c, uint8_t mid, uint8_t shadow, bool draw) {
    int x0 = x;
    for (; *s; s++) {
        if (*s < 'A') { x += 4; continue; }
        const uint8_t *g = tileGlyph((uint8_t)(*s - 'A' + 1));
        if (draw) tileDraw(g, x, y, c, mid, shadow);
        x += (g[0] & 15) + 1;
    }
    return x - x0;
}
