// The deed and the Chance / Community Chest cards, drawn in code (Cards.h).
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library)
#include <RPGame.h>
#include "Cards.h"
#include "Game.h"
#include "Text.h"
#include "Iso.h"
#include "Stage.h"

namespace cards {

using namespace board;

void lines(int cx, int y, char *s, uint8_t c) {
    for (;;) {
        char *e = s;
        while (*e && *e != '\n') e++;
        char end = *e;
        *e = 0;
        text35(cx - text35Width(s) / 2, y, s, c);
        if (!end) return;
        s = e + 1;
        y += 7;
    }
}

static uint8_t count(const char *s) {
    uint8_t n = 1;
    for (; *s; s++) n += *s == '\n';
    return n;
}

// A label on the left, money on the right; `hot`: the row that applies now.
static void row(int x, int y, const char *label, int value, bool hot) {
    char buf[8];
    if (hot) gfx_fillRect(x - 2, y - 1, DEED_W - 4, 7, GOLD);
    text35(x, y, label, INK);
    fmtMoney(buf, value);
    text35(x + DEED_W - 8 - text35Width(buf), y, buf, INK);
}

// The card itself, as wide as it has turned. False until it faces you.
static bool blank(int cx, int y, int w, int h, int open, uint8_t edge, int &x) {
    int ww = (w * open) >> 8;
    if (ww < 4) ww = 4;
    x = cx - ww / 2;
    dither(x + 2, y + 2, ww, h, INK, 0);
    fillRound(x, y, ww, h, 3, WHITE);
    roundRect(x, y, ww, h, 3, edge);
    return open >= 256;
}

void deed(int cx, int y, uint8_t t, int open) {
    int x;
    uint8_t o = game::owner(t), c = NAVY, c2 = NAVY;
    if (!blank(cx, y, DEED_W, DEED_H, open, o == game::BANK ? INK : stage::SEAT_COLOUR[o], x)) return;
    if (type(t) == STREET) iso::groupColour(group(t), c, c2);
    gfx_fillRect(x + 2, y + 2, DEED_W - 4, 16, c);
    if (c2 != c) dither(x + 2, y + 2, DEED_W - 4, 16, c2, 0);
    char buf[28];
    text::tile(buf, t, '\n');
    int ty = y + (count(buf) > 1 ? 4 : 7);
    char shadow[28];
    fmtStr(shadow, buf);
    lines(cx + 1, ty + 1, shadow, INK);
    lines(cx, ty, buf, WHITE);

    int rx = x + 4, ry = y + 21;
    row(rx, ry, "PRICE", price(t), false);
    gfx_hline(x + 3, ry + 7, DEED_W - 6, SILVER);
    ry += 10;
    if (type(t) == STREET) {
        static const char *const L[6] = {"RENT", "1 HOUSE", "2 HOUSES", "3 HOUSES", "4 HOUSES", "HOTEL"};
        for (uint8_t i = 0; i < 6; i++)
            row(rx, ry + i * 7, L[i], RENT[TILE[t].aux][i], o != game::BANK && game::level(t) == i);
        gfx_hline(x + 3, ry + 43, DEED_W - 6, SILVER);
        row(rx, ry + 46, "A HOUSE", houseCost(t), false);
    } else if (type(t) == RAIL) {
        static const char *const L[4] = {"RENT", "2 LINES", "3 LINES", "ALL 4"};
        for (uint8_t i = 0; i < 4; i++) row(rx, ry + i * 7, L[i], 25 << i, false);
    } else {
        text35(rx, ry, "RENT\n4X THE DICE\n\nWITH BOTH\n10X THE DICE", INK);
    }
}

void card(int cx, int y, uint8_t deck, uint8_t idx, int open) {
    int x;
    if (!blank(cx, y, CARD_W, CARD_H, open, INK, x)) return;
    gfx_fillRect(x + 2, y + 2, CARD_W - 4, 9, deck ? BLUE : GOLD);
    char buf[72];
    fmtStr(buf, text::DECK_NAME[deck]);
    lines(cx, y + 4, buf, deck ? WHITE : INK);
    text::card(buf, deck, idx);
    lines(cx, y + 14 + (4 - count(buf)) * 7 / 2, buf, INK);
}

}  // namespace cards
