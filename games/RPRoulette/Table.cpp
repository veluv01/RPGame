#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The wall band, from CHBlackjack's Table.cpp, with a tote board
// where its shoe was.
#include <RPGame.h>
#include <string.h>
#include "Table.h"
#include "Layout.h"
#include "Remap.h"
#include "Wheel.h"
#include "src/assets/Assets.h"

namespace table {

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
    dither(DEALER_X + 6, 2, 36, 30, WOOD, 1);           // warm spotlight behind the croupier
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

void rail() {
    gfx_hline(0, RAIL_Y, 128, GOLD);
    gfx_fillRect(0, RAIL_Y + 1, 128, 2, WOOD);
    gfx_hline(0, RAIL_Y + 3, 128, INK);
    // The croupier's chip rack: columns of chips seen edge-on.
    static const uint8_t RACK[5] = {WHITE, RED, BLUE, FELT_LT, INK};
    gfx_fillRect(TRAY_X - 1, RAIL_Y, TRAY_W + 2, 4, INK);
    for (int i = 0; i < 11; i++) {
        uint8_t c = RACK[i % 5];
        int x = TRAY_X + i * 4;
        gfx_fillRect(x, RAIL_Y, 3, 3, c);
        gfx_pixel(x + 1, RAIL_Y + 1, c == WHITE ? SILVER : WHITE);
    }
}

void plaque(int32_t purse, int32_t bet, uint8_t flash) {
    int x = PLAQUE_X, y = PLAQUE_Y;
    panel(x, y, PLAQUE_W, PLAQUE_H, 3, INK, GOLD);
    text35(x + 4, y + 3, "PURSE", FELT_LT);
    char buf[12];
    fmtMoney(buf, purse);
    uint8_t c = flash ? ((flash & 4) ? WHITE : GOLD) : GOLD;
    int tw = gfx_textWidth(buf);
    int tx = x + PLAQUE_W - 4 - tw;
    gfx_text(tx, y + 10, buf, c);                        // double-struck = bold
    gfx_text(tx + 1, y + 10, buf, c);
    gfx_hline(x + 3, y + 19, PLAQUE_W - 6, NAVY);
    text35(x + 4, y + 22, "BET", SILVER);
    fmtMoney(buf, bet);
    gfx_text(x + PLAQUE_W - 4 - gfx_textWidth(buf), y + 22, buf, WHITE);
}

void tote(const uint8_t *history, uint8_t n) {
    int x = TOTE_X, y = TOTE_Y;
    panel(x, y, TOTE_W, TOTE_H, 3, INK, GOLD);
    if (n > 5) n = 5;
    for (uint8_t i = 0; i < n; i++) {
        char s[3];
        *wheel::name(s, history[i]) = 0;
        int ty = y + 4 + i * 6 + (i ? 1 : 0), w = text35Width(s);
        switch (wheel::colour(history[i])) {
            case wheel::GREEN:   text35(x + TOTE_W / 2 - w / 2, ty, s, FELT_LT); break;
            case wheel::RED_NUM: text35(x + TOTE_W - 4 - w, ty, s, RED); break;
            default:             text35(x + 4, ty, s, WHITE); break;
        }
        if (!i) gfx_hline(x + 3, ty + 6, TOTE_W - 6, NAVY);
    }
}

void speechBubble(const char *src, int typed) {
    int x = BUBBLE_X, y = BUBBLE_Y, w = BUBBLE_W, h = BUBBLE_H;
    panel(x, y, w, h, 4, WHITE, INK);
    // Tail toward the croupier's mouth.
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

}  // namespace table
