#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The wall, dealer, rail and plaque are CHBlackjack's (its Table.cpp);
// the stick and the roll history are new.
#include <RPGame.h>
#include <string.h>
#include "Wall.h"
#include "Layout.h"
#include "Chips.h"
#include "Craps.h"
#include "src/assets/Assets.h"

namespace wall {

using namespace lay;

void backdrop() {
    // Pinstripe wallpaper: build one row, copy it down the wall (vertical
    // lines drawn pixel by pixel cost over a millisecond). gfx_copyRow copies
    // words from SRAM when both rows are word aligned; memcpy is a byte loop.
    uint8_t row[GFX_FB_STRIDE] __attribute__((aligned(4)));
    memset(row, NAVY | (NAVY << 4), sizeof row);
    for (int x = 3; x < 128; x += 8) row[x >> 1] = (uint8_t)((row[x >> 1] & 0x0F) | (INK << 4));
    for (int y = 0; y < WALL_H; y++) gfx_copyRow(y, row, 0, GFX_W);
    dither(0, 0, 128, 3, INK, 0);                        // darker ceiling
    dither(DEALER_X + 6, 2, 36, 30, WOOD, 1);            // warm spotlight behind the dealer
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

// The stickman's stick, held across him, its hooked end up by his shoulder.
void stick(int8_t waggle) {
    int x0 = 2, y0 = WALL_H - 1, x1 = 47, y1 = 37 + waggle;
    gfx_line(x0, y0 + 1, x1 + 1, y1 + 1, INK);          // shadow
    gfx_line(x0, y0, x1, y1, WOOD);
    gfx_line(x0 + 1, y0 - 1, x1, y1 - 1, GOLD);
    // The hook.
    gfx_pixel(x1 + 1, y1 - 1, WOOD); gfx_pixel(x1 + 2, y1 - 2, WOOD);
    gfx_pixel(x1 + 2, y1 - 3, WOOD); gfx_pixel(x1 + 1, y1 - 4, GOLD);
    gfx_pixel(x1 + 3, y1 - 2, INK); gfx_pixel(x1 + 3, y1 - 3, INK);
}

void plaque(int32_t purse, uint8_t flash, const char *name, const char *pays, int32_t bet, bool off) {
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
    if (!name || !*name) return;
    text35(x + 4, y + 22, name, WHITE);
    if (off) text35(x + 4, y + 28, "OFF", RED);
    else if (pays) text35(x + 4, y + 28, pays, CYAN);
    if (bet > 0) {
        fmtMoney(buf, bet);
        text35(x + PLAQUE_W - 4 - text35Width(buf), y + 28, buf, GOLD);
    }
}

// The last roll as two dice, the three before it as totals, coloured by
// what they did: winners gold, a seven-out red, craps wine.
void board(const Craps &g, uint8_t skip) {
    int x = BOARD_X, y = BOARD_Y;
    panel(x, y, BOARD_W, BOARD_H, 3, INK, GOLD);
    if (!g.hist[skip]) {                                 // a new table: two dice, waiting
        roundRect(x + 4, y + 3, 7, 7, 1, NAVY);
        roundRect(x + 12, y + 3, 7, 7, 1, NAVY);
        text35(x + BOARD_W / 2 - text35Width("NEW") / 2, y + 15, "NEW", FELT_LT);
        text35(x + BOARD_W / 2 - text35Width("DICE") / 2, y + 22, "DICE", FELT_LT);
        return;
    }
    for (uint8_t i = 0; i < 4 && i + skip < 6; i++) {
        uint8_t h = g.hist[i + skip], k = g.histKind[i + skip];
        if (!h) break;
        uint8_t a = h >> 4, b = h & 15;
        if (i == 0) {
            art::dieFace(x + 4, y + 3, 7, a);
            art::dieFace(x + 12, y + 3, 7, b);
            continue;
        }
        static const uint8_t KIND_COLOUR[5] = {WHITE, GOLD, WINE, RED, CYAN};
        char s[4];
        fmtInt(s, a + b);
        int ty = y + 6 + i * 7, tx = x + BOARD_W / 2 - text35Width(s) / 2;
        if (k == H_SEVEN_OUT) fillRound(x + 4, ty - 1, BOARD_W - 8, 7, 2, RED);
        text35(tx, ty, s, k == H_SEVEN_OUT ? WHITE : KIND_COLOUR[k < 5 ? k : 0]);
        if (a == b && a + b >= 4 && a + b <= 10) gfx_pixel(tx + text35Width(s) + 1, ty, GOLD);   // a hard number
    }
}

void rail() {
    gfx_hline(0, RAIL_Y, 128, GOLD);
    gfx_fillRect(0, RAIL_Y + 1, 128, 2, WOOD);
    gfx_hline(0, RAIL_Y + 3, 128, INK);
    // Dealer's chip rack: columns of chips seen edge-on.
    static const uint8_t RACK[4] = {WHITE, RED, FELT_LT, INK};
    gfx_fillRect(TRAY_X - 1, RAIL_Y, TRAY_W + 2, 4, INK);
    for (int i = 0; i < 11; i++) {
        uint8_t c = RACK[i % 4];
        int x = TRAY_X + i * 4;
        gfx_fillRect(x, RAIL_Y, 3, 3, c);
        gfx_pixel(x + 1, RAIL_Y + 1, c == WHITE ? SILVER : (c == INK ? NAVY : WHITE));
    }
}

}  // namespace wall
