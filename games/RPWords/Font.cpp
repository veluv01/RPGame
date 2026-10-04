// The display font (Font.h): its glyphs are packed in Assets' FONT.
#pragma GCC optimize("Os", "no-ipa-sra", "no-caller-saves")
#include <RPGame.h>
#include "Font.h"
#include "src/assets/Assets.h"

// The glyph after its character byte, or none (a space).
static const uint8_t *glyphOf(char ch) {
    for (const uint8_t *g = FONT; *g; g += 3 + ((g[1] * (g[2] & 15) + 7) >> 3))
        if (*g == (uint8_t)ch) return g + 1;
    return nullptr;
}

int fontWidth(const char *s, uint8_t gap) {
    int w = 0;
    for (; *s; s++) {
        const uint8_t *g = glyphOf(*s);
        w += g ? g[0] + gap : 4;
    }
    return w > 0 ? w - gap : 0;
}

// Each glyph row is one bit pattern (up to 13 bits) ORed into its mask row:
// a few byte ORs a row rather than a call per pixel.
void maskFont(Mask &m, int x, int y, const char *s, const int8_t *dy, uint8_t gap) {
    for (int k = 0; s[k]; k++) {
        const uint8_t *g = glyphOf(s[k]);
        if (!g) { x += 4; continue; }                       // a space
        int w = g[0], rows = g[1] & 15, bx = x + 1;          // + the margin
        int top = y + 1 + (g[1] >> 4) + (dy ? dy[k] : 0);
        const uint8_t *bits = g + 2;
        uint32_t bit = 0;
        for (int r = 0; r < rows; r++) {
            uint32_t pat = 0;
            for (int i = 0; i < w; i++, bit++) pat = (pat << 1) | ((bits[bit >> 3] >> (7 - (bit & 7))) & 1);
            int row = top + r;
            if (!pat || bx < 0 || (unsigned)row >= (unsigned)(m.h + 2)) continue;
            pat <<= 32 - w - (bx & 7);
            uint8_t *p = m.bits + row * m.stride;
            for (int b = 0; b < 4 && (bx >> 3) + b < m.stride; b++) p[(bx >> 3) + b] |= (uint8_t)(pat >> (24 - 8 * b));
        }
        x += w + gap;
    }
}

int fontText(int x, int y, const char *s, uint8_t c, uint8_t gap) {
    int w = fontWidth(s, gap);
    Mask m = maskBegin(w, FONT_H);
    maskFont(m, 0, 0, s, nullptr, gap);
    maskPaint(m, x + 1, y, c);
    return w;
}
