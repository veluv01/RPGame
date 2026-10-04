#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The felt: the cards (the one in play and its neighbours on the carousel),
// the buy-in's row of cards, and the bar under them.
#include <RPGame.h>
#include "Cards.h"
#include "Layout.h"
#include "Bingo.h"

namespace cards {

using namespace lay;

const uint8_t LETTER_COL[5] = {CYAN, RED, WHITE, FELT_LT, GOLD};
const uint8_t DAUB[DAUBERS] = {RED, BLUE, FELT_LT, CYAN, SKIN};

uint8_t daub(const Bingo &g) { return DAUB[g.opt.dauber < DAUBERS ? g.opt.dauber : 0]; }

// The glove's art has a gold cuff (GOLD, WOOD underneath).
const uint8_t *cuff(uint8_t colour) {
    static uint8_t rm[16];
    for (uint8_t i = 0; i < 16; i++) rm[i] = i;
    rm[GOLD] = colour;
    rm[WOOD] = DARKER[colour & 15];
    return rm;
}

void cellXY(uint8_t cell, int &x, int &y) {
    x = CARD_X + 2 + (cell % 5) * CELL_W + CELL_W / 2;
    y = CARD_Y + 2 + HEAD_H + (cell / 5) * CELL_H + CELL_H / 2;
}

void draw(const Bingo &g, uint8_t k, int x, const Look &look) {
    if (x >= GFX_W || x + CARD_W <= 0) return;
    const int y = CARD_Y;
    bool pending = g.pend[k] != 0;
    gfx_fillRect(x + 3, y + 3, CARD_W, CARD_H, FELT_DK);   // its shadow on the felt
    // The frame: rainbow while a called number waits on this card; a raised
    // rim otherwise.
    uint8_t edge = look.deny ? RED : pending ? FX_A : look.focused ? GOLD : WOOD;
    gfx_fillRect(x, y, CARD_W, CARD_H, edge);
    if (!pending && !look.deny) {
        gfx_hline(x + 1, y + CARD_H - 1, CARD_W - 1, DARKER[edge]);   // the rim's shaded side
        gfx_vline(x + CARD_W - 1, y + 1, CARD_H - 1, DARKER[edge]);
        gfx_rect(x + 1, y + 1, CARD_W - 2, CARD_H - 2, INK);
    }
    // The header, lit along its top; a card playing for double shows it in
    // wine and gold.
    bool dbl = g.doubleCard == k;
    uint8_t head = dbl ? WINE : NAVY;
    gfx_fillRect(x + 2, y + 2, 5 * CELL_W, HEAD_H, head);
    gfx_hline(x + 2, y + 2, 5 * CELL_W, LIGHTER[head]);
    for (int c = 0; c < 5; c++)
        glyph(x + 2 + c * CELL_W + 5, y + 3, glyph35("BINGO"[c]), 3, dbl ? FX_B : LETTER_COL[c]);
    // The cells: a white and silver checker, sunk a pixel under the header;
    // a raised blot on each daubed one, lit top left, shaded underneath.
    const uint8_t *card = g.cards[k];
    for (int r = 0; r < 5; r++) {
        int cy = y + 2 + HEAD_H + r * CELL_H;
        gfx_fillRect(x + 2, cy, 5 * CELL_W, CELL_H, WHITE);
        for (int c = 0; c < 5; c++) {
            int cx = x + 2 + c * CELL_W, i = r * 5 + c;
            if ((r + c) & 1) gfx_fillRect(cx, cy, CELL_W, CELL_H, SILVER);
            uint8_t ink = INK;
            if ((g.daub[k] >> i) & 1) {
                uint8_t blot = ((look.win >> i) & 1) ? FX_A : ((look.flash >> i) & 1) ? FX_B : i == 12 ? GOLD : daub(g);
                fillRound(cx + 1, cy + 1, CELL_W - 2, CELL_H - 2, 3, DARKER[blot]);
                fillRound(cx + 1, cy + 1, CELL_W - 2, CELL_H - 3, 3, blot);
                gfx_hline(cx + 4, cy + 2, 3, LIGHTER[blot]);
                if (blot == RED || blot == BLUE) ink = WHITE;
            }
            if (i == 12) { text35(cx + 5, cy + 3, "*", ink); continue; }
            char s[4];
            *fmtInt(s, card[i]) = 0;
            text35(cx + (card[i] < 10 ? 5 : 3), cy + 3, s, ink);
        }
    }
    gfx_hline(x + 2, y + 2 + HEAD_H, 5 * CELL_W, SILVER);  // the header's shadow on the cells
}

int buyX(uint8_t i) { return 7 + i * 13; }

static void centred(int y, const char *s, uint8_t c) { text35(64 - text35Width(s) / 2, y, s, c); }

void buyIn(const Bingo &g) {
    uint8_t n = g.buyN, most = g.maxCards();
    // Nine cards in a row: the ones being bought face up, on their shadows;
    // the glove points at the last of them (Presenter).
    for (uint8_t i = 0; i < MAX_CARDS; i++) {
        int x = buyX(i), y = BUY_Y;
        if (i < n) {
            fillRound(x + 1, y - 1, 13, 17, 2, FELT_DK);
            fillRound(x - 1, y - 3, 13, 17, 2, GOLD);
            gfx_hline(x + 1, y - 3, 9, LIGHTER[GOLD]);
            gfx_hline(x + 1, y + 13, 9, WOOD);
            gfx_fillRect(x + 1, y - 1, 9, 13, WHITE);
            gfx_fillRect(x + 1, y - 1, 9, 3, NAVY);
            for (int d = 0; d < 3; d++) gfx_fillRect(x + 2 + ((i + d * 2) % 3) * 3, y + 3 + d * 3, 2, 2, RED);
        } else {
            roundRect(x, y, 11, 14, 2, i < most ? FELT_LT : FELT_DK);
        }
    }
    char buf[32], *p = fmtInt(buf, n);
    *fmtStr(p, n == 1 ? " CARD" : " CARDS") = 0;
    int w = gfx_textWidth(buf);
    const int py = BUY_Y + 20;
    panelLit(64 - w / 2 - 14, py, w + 28, 11, 3, NAVY, FX_B);
    gfx_text(64 - w / 2, py + 2, buf, GOLD);
    text35(64 - w / 2 - 10, py + 3, "<", n > 1 ? WHITE : BLUE);
    text35(64 + w / 2 + 7, py + 3, ">", n < most ? WHITE : BLUE);
    p = fmtMoney(fmtStr(buf, "COST "), (int32_t)g.priceNow() * n);
    *fmtMoney(fmtStr(p, "   POT "), g.potFor(n)) = 0;
    centred(py + 14, buf, GOLD);
    *fmtStr(fmtInt(fmtStr(buf, "THE HALL HOLDS "), RIVALS[g.opt.hall < 3 ? g.opt.hall : 0]), " MORE") = 0;
    centred(py + 21, buf, FELT_LT);
}

void bar(const Bingo &g, bool frozen) {
    gfx_fillRect(0, BAR_Y, 128, BAR_H, NAVY);
    gfx_hline(0, BAR_Y, 128, INK);
    gfx_hline(0, BAR_Y + 1, 128, BLUE);                   // the bar's lit edge
    bool buying = g.phase == Phase::Buy || g.phase == Phase::Welcome;
    // A pip per card: gold for the one in play, rainbow where a number waits;
    // each a little raised tile.
    uint8_t n = buying ? g.buyN : g.nCards;
    for (uint8_t i = 0; i < n; i++) {
        int x = 3 + i * 6;
        bool here = !buying && i == g.focus;
        uint8_t c = here ? GOLD : (!buying && g.pend[i]) ? FX_A : SILVER;
        int py = BAR_Y + (here ? 3 : 5), ph = here ? 10 : 6;
        gfx_hline(x + 1, py + ph, 5, INK);
        gfx_fillRect(x, py, 5, ph, c);
        bevel(x, py, 5, ph, c);
        if (here && g.pend[i]) gfx_fillRect(x + 1, BAR_Y + 5, 3, 6, FX_A);
        if (!buying && g.doubleCard == i) gfx_hline(x, BAR_Y + 13, 5, RED);
    }
    // The middle: what A or B does now.
    static const char *const POWER[4] = {"", "B:WILD", "B:FREEZE", "B:2X POT"};
    const char *label = buying ? "A:BUY IN" : frozen ? "FROZEN" : POWER[g.power <= POWER_KINDS ? g.power : 0];
    if (label[0]) {
        panelLit(58, BAR_Y + 2, 38, 13, 3, INK, frozen ? CYAN : FX_B);
        text35(77 - text35Width(label) / 2, BAR_Y + 6, label, frozen ? CYAN : FX_B);
    } else {
        text35(60, BAR_Y + 3, "POWER", SILVER);
        gfx_fillRect(60, BAR_Y + 10, 34, 4, INK);          // sunk into the bar: its lip lit below
        gfx_hline(60, BAR_Y + 14, 34, BLUE);
        int fill = 32 * g.meter / POWER_FULL;
        gfx_hline(61, BAR_Y + 11, fill, WHITE);
        gfx_hline(61, BAR_Y + 12, fill, CYAN);
    }
    text35(99, BAR_Y + 3, "JACKPOT", SILVER);
    char buf[12];
    *fmtMoney(buf, g.jackpot) = 0;
    text35(126 - text35Width(buf), BAR_Y + 10, buf, FX_B);
}

}  // namespace cards
