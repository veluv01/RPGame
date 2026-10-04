// Agent: the "secret agent" chrome. See Agent.h. (CHSDtoUSB and CHStlView
// carry the same file: change both together.)
#pragma GCC optimize("Os")
#include "Agent.h"
#include <string.h>

namespace agent {

void wipe(int y0, int y1, uint8_t c) {
    uint32_t v = c * 0x11111111u;
    uint32_t *p = (uint32_t *)(gfx_fb + y0 * GFX_FB_STRIDE), *e = (uint32_t *)(gfx_fb + y1 * GFX_FB_STRIDE);
    for (; p < e; p += 8) { p[0] = v; p[1] = v; p[2] = v; p[3] = v; p[4] = v; p[5] = v; p[6] = v; p[7] = v; }
}

int tiny(int x, int y, const char *s, uint8_t c) { return x + text35(x, y, s, c); }
int tinyW(const char *s) { return text35Width(s) - 1; }
void tinyR(int xr, int y, const char *s, uint8_t c) { text35(xr - tinyW(s), y, s, c); }

void centred(int y, const char *s, uint8_t c, uint8_t scale) {
    gfx_textScaled(64 - gfx_textWidthScaled(s, scale) / 2, y, s, c, scale);
}

void outlined(int y, const char *s, uint8_t c, uint8_t ring) {
    int x = 64 - gfx_textWidthScaled(s, 2) / 2;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) gfx_textScaled(x + dx, y + dy, s, ring, 2);
    gfx_textScaled(x, y, s, c, 2);
}

void seg7(int x, int y, int d, uint8_t on, uint8_t off) {
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    uint8_t m = d < 0 ? 0 : SEG[d];
    gfx_hline(x + 1, y, 3, m & 0x01 ? on : off);
    gfx_vline(x + 4, y + 1, 3, m & 0x02 ? on : off);
    gfx_vline(x + 4, y + 5, 3, m & 0x04 ? on : off);
    gfx_hline(x + 1, y + 8, 3, m & 0x08 ? on : off);
    gfx_vline(x, y + 5, 3, m & 0x10 ? on : off);
    gfx_vline(x, y + 1, 3, m & 0x20 ? on : off);
    gfx_hline(x + 1, y + 4, 3, m & 0x40 ? on : off);
}

void seg7Num(int x, int y, uint32_t v, int digits, uint8_t on, uint8_t off) {
    uint32_t p = 1;
    for (int i = 1; i < digits; i++) p *= 10;
    for (int i = 0; i < digits; i++, p /= 10)
        seg7(x + 6 * i, y, (v >= p || p == 1) ? (int)(v / p % 10) : -1, on, off);
}

void statusBar(const char *word, uint8_t c) {
    wipe(0, 11, PANEL);
    gfx_text(13, 2, word, c);
}

void readout(uint32_t v, uint8_t on, const char *unit1, const char *unit2) {
    if (v > 999) v = 999;
    seg7Num(95, 1, v, 3, v ? on : DIM);
    tiny(115, 0, unit1, DIM);
    tiny(115, 6, unit2, DIM);
}

void chip(int x, int y, int w, const char *tag, uint8_t c) {
    gfx_fillRect(x, y, w, 7, c);
    tiny(x + (w - tinyW(tag)) / 2, y + 1, tag, BG);
}

void keys(int y, const char *hints) {
    char word[12];
    int x = 0;
    for (const char *p = hints; *p;) {
        uint32_t n = 0;
        for (; *p && *p != ':' && n < sizeof word - 1; p++) word[n++] = *p;
        word[n] = 0;
        if (*p == ':') p++;
        int w = tinyW(word) + 4;
        chip(x, y, w, word, MID);
        x += w + 2;
        for (n = 0; *p && *p != ' ' && n < sizeof word - 1; p++) word[n++] = *p;
        word[n] = 0;
        x = tiny(x, y + 1, word, DIM) + 4;
        while (*p == ' ') p++;
    }
}

int tabs(int y, const char *const *names, int n, int sel) {
    int x = 0;
    for (int i = 0; i < n; i++) {
        int w = tinyW(names[i]);
        if (i == sel) {
            gfx_fillRect(x, y - 1, w + 4, 7, MID);
            tiny(x + 2, y, names[i], BG);
        } else {
            tiny(x + 2, y, names[i], DIM);
        }
        x += w + 6;
    }
    if (x < 128) gfx_hline(x, y + 5, 128 - x, GRID);
    return x;
}

void brackets(int x, int y, int w, int h, int len, uint8_t c) {
    int r = x + w - 1, b = y + h - 1;
    gfx_hline(x, y, len, c);           gfx_vline(x, y, len, c);
    gfx_hline(r - len + 1, y, len, c); gfx_vline(r, y, len, c);
    gfx_hline(x, b, len, c);           gfx_vline(x, b - len + 1, len, c);
    gfx_hline(r - len + 1, b, len, c); gfx_vline(r, b - len + 1, len, c);
}

void alert(int top, const char *title, const char *l1, const char *l2, uint8_t c) {
    int h = l2 ? 36 : 28;
    gfx_fillRect(10, top, 108, h, PANEL);
    gfx_rect(10, top, 108, h, GRID);
    brackets(10, top, 108, h, 5, c);
    outlined(top + 4, title, c, BG);
    if (l1) centred(top + 19, l1, PALE);
    if (l2) centred(top + 27, l2, PALE);
}

void gauge(int x, int y, int w, int h, uint32_t num, uint32_t den, uint8_t on, uint8_t off) {
    gfx_fillRect(x, y, w, h, off);
    int lit = den ? (int)((uint64_t)w * num / den) : 0;
    if (num && !lit) lit = 1;                // something is there: show it
    if (lit) gfx_fillRect(x, y, lit, h, on);
}

void typed(int x, int y, const char *s, int n, uint8_t c, uint8_t scale, bool cursor) {
    char buf[24];
    int len = (int)strlen(s);
    if (n > len) n = len;
    if (n > (int)sizeof buf - 1) n = sizeof buf - 1;
    memcpy(buf, s, (size_t)n);
    buf[n] = 0;
    gfx_textScaled(x, y, buf, c, scale);
    if (cursor) gfx_fillRect(x + gfx_textWidthScaled(buf, scale) + scale, y, 5 * scale, 7 * scale, c);
}

void sweep(int y) {
    static const uint8_t TAILC[TAIL] = {PANEL, GRID, MID, LIVE, PALE};
    for (int i = 0; i < TAIL; i++) {
        int r = y - TAIL + 1 + i;
        if (r >= 0 && r < 128) gfx_hline(0, r, 128, TAILC[i]);
    }
}

}  // namespace agent
