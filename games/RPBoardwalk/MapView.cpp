// The board from above (MapView.h), SELECT's view.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <RPGame.h>
#include "MapView.h"
#include "Game.h"
#include "Iso.h"
#include "Stage.h"
#include "src/assets/Assets.h"

namespace mapview {

using namespace board;
using namespace game;

// Corners CW square, tiles TW along their side: 118 x 100 from (X0, Y0),
// the sides' tiles squashed to TH so it fits under the HUD.
enum { X0 = 5, Y0 = 12, CW = 14, TW = 10, TH = 8, RIGHT = X0 + CW + 9 * TW, BOTTOM = Y0 + CW + 9 * TH };

// A tile's box. As on the iso board: GO bottom right, Jail bottom left,
// Free Parking top left, Go To Jail top right.
static void box(uint8_t t, int &x, int &y, int &w, int &h) {
    int k = t % 10, s = t / 10;
    bool flat = s == 0 || s == 2;                   // along the bottom or the top
    w = flat ? TW : CW;
    h = flat ? CW : TH;
    if (!k) w = h = CW;
    int a = k ? CW + (k - 1) * (flat ? TW : TH) : 0;    // from the side's first corner
    switch (s) {
        case 0:  x = RIGHT - a - (k ? TW - CW : 0); y = BOTTOM; break;
        case 1:  x = X0; y = BOTTOM - a - (k ? TH - CW : 0); break;
        case 2:  x = X0 + a; y = Y0; break;
        default: x = RIGHT; y = Y0 + a; break;
    }
}

// A strip d px deep along the tile's inside (or its outside) edge.
static void edge(uint8_t t, bool inside, int d, int &x, int &y, int &w, int &h) {
    box(t, x, y, w, h);
    uint8_t s = (uint8_t)(t / 10);
    if (s == 0 || s == 2) { if (inside == (s == 2)) y += h - d; h = d; }
    else                  { if (inside == (s == 1)) x += w - d; w = d; }
}

void draw(uint32_t frame) {
    gfx_fillRect(0, 10, 128, 118, NAVY);
    gfx_fillRect(X0 - 1, Y0 - 1, 2 * CW + 9 * TW + 2, 2 * CW + 9 * TH + 2, INK);
    gfx_fillRect(X0 + CW, Y0 + CW, 9 * TW, 9 * TH, FELT);
    for (uint8_t t = 0; t < TILES; t++) {
        int x, y, w, h;
        box(t, x, y, w, h);
        gfx_fillRect(x, y, w, h, t & 1 ? SILVER : WHITE);
        const uint8_t *rm, *icon = iso::decalOf(t, rm);
        if (icon) sprite4(icon, x + (w - icon[0]) / 2, y + (h - icon[1]) / 2, rm);
        if (!isDeed(t)) continue;
        if (type(t) == STREET) {
            uint8_t c, c2;
            iso::groupColour(group(t), c, c2);
            edge(t, true, 4, x, y, w, h);
            gfx_fillRect(x, y, w, h, c);
            if (c2 != c) dither(x, y, w, h, c2, 0);
            // Houses: a dot each along the band; a hotel fills it.
            uint8_t lv = level(t);
            for (uint8_t i = 0; i < (lv == 5 ? 4 : lv); i++) {
                if (w > h) gfx_fillRect(x + 1 + i * 2, y + 1, lv == 5 ? 2 : 1, 2, INK);
                else       gfx_fillRect(x + 1, y + i * 2, 2, lv == 5 ? 2 : 1, INK);
            }
        }
        if (owner(t) != BANK) {
            edge(t, false, 2, x, y, w, h);
            gfx_fillRect(x, y, w, h, stage::SEAT_COLOUR[owner(t)]);
        }
    }
    // Tokens: a square each, in its seat's corner of the tile.
    for (uint8_t p = 0; p < st.players; p++) {
        int x, y, w, h;
        box(st.pl[p].pos, x, y, w, h);
        x += w / 2 - 4 + (p & 1) * 4;
        y += h / 2 - 4 + (p >> 1) * 4;
        gfx_fillRect(x, y, 5, 5, INK);
        gfx_fillRect(x + 1, y + 1, 3, 3, p == st.cur && (frame & 16) ? WHITE : stage::SEAT_COLOUR[p]);
    }
    // The table: cash and net worth.
    char buf[24], *q = fmtInt(fmtStr(buf, "ROUND "), st.round);
    if (st.roundCap) fmtInt(fmtStr(q, " OF "), st.roundCap);
    text35(64 - text35Width(buf) / 2, Y0 + CW + 3, buf, GOLD);
    fmtMoney(fmtStr(buf, "JACKPOT "), st.pot);
    text35(64 - text35Width(buf) / 2, BOTTOM - 8, buf, FX_B);
    text35(X0 + CW + 14, Y0 + CW + 12, "CASH", FELT_LT);
    text35(RIGHT - 3 - text35Width("WORTH"), Y0 + CW + 12, "WORTH", FELT_LT);
    for (uint8_t p = 0; p < st.players; p++) {
        int y = Y0 + CW + 21 + p * 11;
        uint8_t c = p == st.cur ? FX_B : WHITE;
        gfx_fillRect(X0 + CW + 4, y, 5, 5, stage::SEAT_COLOUR[p]);
        fmtMoney(buf, st.pl[p].cash);
        text35(X0 + CW + 14, y, buf, c);
        fmtMoney(buf, netWorth(p));
        text35(RIGHT - 3 - text35Width(buf), y, buf, c);
    }
}

}  // namespace mapview
