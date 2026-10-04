// Drawing the table's fixed parts (Table.h): the wall and the dealer, the
// rail and chip rack, the felt and its printing, the shoe, the plaque.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
#include <RPGame.h>
#include <string.h>
#include "Table.h"
#include "Layout.h"
#include "CardArt.h"
#include "Round.h"
#include "src/assets/Assets.h"

namespace table {

using namespace lay;

void wall(uint32_t frame) {
    (void)frame;
    // Pinstripe wallpaper: build one row, copy it down the wall (vertical
    // lines drawn pixel by pixel cost over a millisecond). gfx_copyRow copies
    // words from SRAM when both rows are word aligned; memcpy is a byte loop.
    uint8_t row[GFX_FB_STRIDE] __attribute__((aligned(4)));
    memset(row, NAVY | (NAVY << 4), sizeof row);
    for (int x = 3; x < 128; x += 8) row[x >> 1] = (uint8_t)((row[x >> 1] & 0x0F) | (INK << 4));
    for (int y = 0; y < WALL_H; y++) gfx_copyRow(y, row, 0, GFX_W);
    dither(0, 0, 128, 3, INK, 0);                    // darker ceiling
    // Warm spotlight behind the dealer.
    dither(DEALER_X + 6, 2, 36, 30, WOOD, 1);
}

void dealer(uint8_t expr, uint8_t look, bool alt, int x, int y) {
    sprite4(DEALER, x, y, alt ? DEALER_ALT_REMAP : nullptr);
    int fx = x + (FACE_X - DEALER_X), fy = y + (FACE_Y - DEALER_Y);
    sprite4(FACE_NORMAL, fx, fy, alt ? DEALER_ALT_REMAP : nullptr);
    if (expr > E_TALK) expr = E_NORMAL;
    if (expr) {
        for (uint16_t i = FACE_EDIT_AT[expr - 1]; i < FACE_EDIT_AT[expr]; i++) {
            uint16_t w = FACE_EDITS[i];
            uint16_t idx = w >> 4;
            uint8_t c = w & 15;
            if (alt) c = DEALER_ALT_REMAP[c];
            gfx_pixel(fx + idx % 24, fy + idx / 24, c);
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

void rail(uint32_t frame) {
    (void)frame;
    gfx_hline(0, RAIL_Y, 128, GOLD);
    gfx_fillRect(0, RAIL_Y + 1, 128, 2, WOOD);
    gfx_hline(0, RAIL_Y + 3, 128, INK);
    // Dealer's chip rack: columns of chips seen edge-on.
    static const uint8_t RACK[5] = {WHITE, RED, BLUE, FELT_LT, INK};
    gfx_fillRect(TRAY_X - 1, RAIL_Y, TRAY_W + 2, 4, INK);
    for (int i = 0; i < 11; i++) {
        uint8_t c = RACK[i % 5];
        int x = TRAY_X + i * 4;
        gfx_fillRect(x, RAIL_Y, 3, 3, c);
        gfx_pixel(x + 1, RAIL_Y + 1, c == WHITE ? SILVER : WHITE);
    }
}

// Felt print arc: y offset per character column.
static int arcDy(int x) {
    int d = x - 64;
    return (d * d) / 900;
}

static void arcText(int y, const char *s, uint8_t c) {
    int w = text35Width(s);
    int x = 64 - w / 2;
    for (; *s; s++) {
        if (*s != ' ') {
            const uint8_t *g = glyph35(*s);
            if (g) glyph(x, y - arcDy(x + 1), g, 3, c);
        }
        x += 4;
    }
}

static void arcLine(int x0, int x1, int y, uint8_t c) {
    int runStart = x0, runY = y - arcDy(x0);
    for (int x = x0 + 1; x <= x1 + 1; x++) {
        int yy = x <= x1 ? y - arcDy(x) : -999;
        if (yy != runY) { gfx_hline(runStart, runY, x - runStart, c); runStart = x; runY = yy; }
    }
}

void felt(const Round &r) {
    // Darker edges give the felt some depth.
    dither(0, RAIL_Y + 4, 3, TRIM_Y - RAIL_Y - 4, FELT_DK, 0);
    dither(125, RAIL_Y + 4, 3, TRIM_Y - RAIL_Y - 4, FELT_DK, 1);
    gfx_hline(0, RAIL_Y + 4, 128, FELT_DK);
    // Table printing, the way a real layout reads.
    arcLine(10, 118, PRINT_Y - 2, FELT_LT);
    arcText(PRINT_Y, "BLACKJACK PAYS 3 TO 2", GOLD);
    arcLine(10, 118, PRINT_Y + 7, FELT_LT);
    const char *rule = r.opt.rules == RULES_CASINO ? "DEALER STANDS ON ALL 17S" : "DEALER DRAWS TO 16";
    text35(64 - text35Width(rule) / 2, DEALER_CARDS_Y + 11, rule, FELT_LT);
    text35(64 - text35Width("INSURANCE PAYS 2 TO 1") / 2, DEALER_CARDS_Y + 18, "INSURANCE PAYS 2 TO 1", FELT_LT);
    // Betting circle.
    gfx_fillEllipse(BET_CX, BET_CY, BET_RX, BET_RY, FELT_DK);
    gfx_ellipse(BET_CX, BET_CY, BET_RX, BET_RY, FELT_LT);
    gfx_ellipse(BET_CX, BET_CY, BET_RX - 2, BET_RY - 2, FELT);
}

void shoe(uint8_t left, uint8_t shuf) {
    int x = SHOE_X, y = SHOE_Y;
    // Wooden box, card backs visible through the top, a slot at the front.
    panel(x, y, SHOE_W, SHOE_H, 2, WOOD, INK);
    gfx_fillRect(x + 3, y + 3, SHOE_W - 6, 12, WINE);
    int depth = 1 + (left * 10) / 100;                   // how full the shoe looks
    for (int i = 0; i < depth; i++) gfx_hline(x + 4, y + 14 - i, SHOE_W - 8, (i & 1) ? RED : WINE);
    if (shuf) {                                          // riffle: cards jump
        for (int i = 0; i < 5; i++) {
            int hy = y + 4 + ((shuf * 3 + i * 5) % 9);
            gfx_hline(x + 4 + i * 3, hy, 3, WHITE);
        }
    }
    // Cut card: a red sliver sticking out where the reshuffle comes.
    gfx_fillRect(x + SHOE_W - 5, y + 4, 2, 3 + (left / 20), RED);
    gfx_hline(x + 2, y + 17, SHOE_W - 4, INK);           // mouth
    gfx_hline(x + 2, y + 18, SHOE_W - 4, GOLD);
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

}  // namespace table
