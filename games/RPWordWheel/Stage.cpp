#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The wall band, from CHBlackjack's Table.cpp by way of CHRoulette,
// with the used-letter rack where their plaque and shoe were.
#include <RPGame.h>
#include <string.h>
#include "Stage.h"
#include "Layout.h"
#include "Puzzle.h"
#include "src/assets/Assets.h"
#include "Shapes.h"

namespace stage {

using namespace lay;

void wall() {
    // Pinstripe wallpaper: build one row, copy it down the wall (vertical
    // lines drawn pixel by pixel cost over a millisecond). gfx_copyRow copies
    // words from SRAM when both rows are word aligned; memcpy is a byte loop.
    uint8_t row[GFX_FB_STRIDE] __attribute__((aligned(4)));
    memset(row, NAVY | (NAVY << 4), sizeof row);
    for (int x = 3; x < 128; x += 8) row[x >> 1] = (uint8_t)((row[x >> 1] & 0x0F) | (INK << 4));
    for (int y = 0; y < WALL_H; y++) gfx_copyRow(y, row, 0, GFX_W);
    dither(0, 0, 128, 3, INK, 0);                       // darker ceiling
    dither(DEALER_X + 6, 2, 36, 30, WOOD, 1);           // warm spotlight behind the host
}

void dealer(uint8_t expr, uint8_t look, bool alt, int x, int y) {
    const uint8_t *rm = alt ? DEALER_ALT_REMAP : RM_ID;
    sprite4(DEALER, x, y, rm);
    int fx = x + (FACE_X - DEALER_X), fy = y + (FACE_Y - DEALER_Y);
    sprite4(FACE_NORMAL, fx, fy, rm);
    if (expr > E_TALK) expr = E_NORMAL;
    if (expr) {
        for (uint16_t i = FACE_EDIT_AT[expr - 1]; i < FACE_EDIT_AT[expr]; i++) {
            uint16_t w = FACE_EDITS[i];
            uint16_t idx = w >> 4;
            gfx_pixel(fx + idx % 24, fy + idx / 24, rm[w & 15]);
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

void speechBubble(const char *src, int typed) {
    int x = RACK_X, y = RACK_Y, w = RACK_W, h = RACK_H;
    dropShadow(x, y, w, h);
    edgedRound(x, y, w, h, 4, WHITE, INK);
    // Tail toward the host's mouth.
    for (int i = 0; i < 5; i++) {
        gfx_hline(x - 5 + i, y + 22 + i, 6 - i, WHITE);
        gfx_pixel(x - 6 + i, y + 22 + i, INK);
    }
    gfx_vline(x, y + 21, 4, WHITE);
    int lines = 1;
    for (const char *p = src; *p; p++) if (*p == '\n') lines++;
    int ty = y + h / 2 - (lines * 7) / 2 + 1;
    for (const char *p = src; *p;) {
        const char *e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        char line[20];
        int n = len < 19 ? len : 19;
        memcpy(line, p, n);
        line[n] = 0;
        int lx = x + w / 2 - text35Width(line) / 2;
        int show = typed < n ? (typed > 0 ? typed : 0) : n;
        line[show] = 0;
        text35(lx, ty, line, INK);
        typed -= len + 1;
        ty += 7;
        if (!e) break;
        p = e + 1;
    }
}

void rack(uint32_t used, const char *status, const char *right, bool wild, bool flash) {
    int x = RACK_X, y = RACK_Y;
    dropShadow(x, y, RACK_W, RACK_H);
    edgedRound(x, y, RACK_W, RACK_H, 3, INK, GOLD);
    text35(x + 4, y + 3, status, FELT_LT);
    if (right[0]) text35(x + RACK_W - 4 - text35Width(right), y + 3, right, flash ? WHITE : GOLD);
    gfx_hline(x + 3, y + 10, RACK_W - 6, NAVY);
    char s[2] = {0, 0};
    for (uint8_t i = 0; i < 26; i++) {
        s[0] = (char)('A' + i);
        uint8_t c = ((used >> i) & 1) ? NAVY : (pz::isVowel(s[0]) ? CYAN : WHITE);
        text35(x + 4 + (i % 9) * 8, y + 13 + (i / 9) * 8, s, c);
    }
    if (wild) {
        gfx_fillRect(x + 4 + 8 * 8 - 1, y + 13 + 16 - 1, 5, 7, FX_B);
        text35(x + 4 + 8 * 8, y + 13 + 16, "W", INK);
    }
}

}  // namespace stage
