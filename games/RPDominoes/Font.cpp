// The serif lettering (Font.h): glyphs looked up in AAFONT (src/assets),
// drawn into masks or straight onto the screen.
#pragma GCC optimize("Os", "no-ipa-sra")
#include <RPGame.h>
#include "Font.h"

// The glyph, or none (a space): char << 8 | width, rows, the ink, the half ink.
static const uint16_t *glyphOf(char ch) {
    for (const uint16_t *g = AAFONT; *g; g += 2 + 2 * g[1])
        if ((g[0] >> 8) == (uint8_t)ch) return g;
    return nullptr;
}

int fontWidth(const char *s, uint8_t gap) {
    int w = 0;
    for (; *s; s++) {
        const uint16_t *g = glyphOf(*s);
        w += g ? (g[0] & 255) + gap : 4;
    }
    return w > 0 ? w - gap : 0;
}

// Each glyph row is one 16-bit pattern ORed into its mask row: a few byte
// ORs a row rather than a call per pixel.
void maskFont(Mask &m, int x, int y, const char *s, const int8_t *dy, uint8_t gap) {
    for (int k = 0; s[k]; k++) {
        const uint16_t *g = glyphOf(s[k]);
        if (!g) { x += 4; continue; }                       // a space
        int rows = g[1], bx = x + 1;                         // + the margin
        int top = y + 1 + (dy ? dy[k] : 0);
        for (int r = 0; r < rows; r++) {
            uint32_t pat = g[2 + r];
            int row = top + r;
            if (!pat || bx < 0 || (unsigned)row >= (unsigned)(m.h + 2)) continue;
            pat = (pat << 16) >> (bx & 7);
            uint8_t *p = m.bits + row * m.stride;
            for (int b = 0; b < 3 && (bx >> 3) + b < m.stride; b++) p[(bx >> 3) + b] |= (uint8_t)(pat >> (24 - 8 * b));
        }
        x += (g[0] & 255) + gap;
    }
}

// Each glyph's ink, or half ink, straight onto the screen.
static int layer(int x, int y, const char *s, const int8_t *dy, uint8_t gap, uint8_t c, bool half) {
    int x0 = x;
    for (int k = 0; s[k]; k++) {
        const uint16_t *g = glyphOf(s[k]);
        if (!g) { x += 4; continue; }
        glyph16(x, y + (dy ? dy[k] : 0), g + 2 + (half ? g[1] : 0), (uint8_t)g[1], c);
        x += (g[0] & 255) + gap;
    }
    return x - x0 - gap;
}

void fontHalf(int x, int y, const char *s, const int8_t *dy, uint8_t gap, uint8_t c) { layer(x, y, s, dy, gap, c, true); }

int fontText(int x, int y, const char *s, uint8_t c, uint8_t gap) {
    // The tone between: white on navy or felt, silver; gold and its
    // shimmer, wood; the felt's light green, the felt.
    uint8_t mid = c == WHITE ? SILVER : c == GOLD || c == FX_B ? WOOD : c == FELT_LT ? FELT : 0xFF;
    if (mid != 0xFF) layer(x, y, s, nullptr, gap, mid, true);
    return layer(x, y, s, nullptr, gap, c, false);
}
