#pragma GCC optimize("Os")   // cold code: size over speed
// The action bar (Bar.h): seven rounded buttons, redrawn only when what they
// show changes.
#include <RPGame.h>
#include "Bar.h"
#include "Layout.h"
#include "ChipArt.h"

namespace bar {

using lay::BAR_Y;
using lay::BAR_H;

static uint32_t lastSig = 0;

static bool darkFace(uint8_t c) { return c == RED || c == BLUE || c == NAVY || c == WINE || c == INK; }

static void button(int x, int w, uint8_t face, const char *label, bool sel, bool on) {
    int y = BAR_Y + 2 - (sel ? 1 : 0), h = 12;
    fillRound(x, y, w, h, 3, on ? face : NAVY);
    if (on) gfx_hline(x + 2, y + h - 2, w - 4, darkFace(face) ? INK : WOOD);
    roundRect(x, y, w, h, 3, sel ? FX_B : INK);
    uint8_t tc = !on ? SILVER : (darkFace(face) ? WHITE : INK);
    if (sel && gfx_textWidth(label) <= w - 4) gfx_text(x + w / 2 - gfx_textWidth(label) / 2, y + 3, label, tc);
    else text35(x + w / 2 - text35Width(label) / 2, y + 4, label, tc);
}

bool draw(uint8_t active, uint8_t hover, bool armed, bool canSpin, int ox, bool force) {
    uint32_t sig = (uint32_t)active | (uint32_t)hover << 4 | (uint32_t)armed << 12 | (uint32_t)canSpin << 13 |
                   (uint32_t)(ox & 0xFF) << 16 | 1u << 31;
    if (!force && sig == lastSig) return false;
    lastSig = sig;
    gfx_fillRect(0, BAR_Y, 128, BAR_H, NAVY);
    gfx_hline(0, BAR_Y, 128, INK);
    button(ox, 17, armed ? RED : SILVER, armed ? "CLR?" : "CLR", hover == 0, true);
    static const char *const V[5] = {"$1", "$5", "$10", "$25", "$100"};
    for (uint8_t i = 0; i < 5; i++) {
        int x = 18 + 17 * i + ox, w = 16, cx = x + w / 2;
        bool sel = hover == i + 1;
        int y = BAR_Y + 2 - (sel ? 1 : 0);
        if (active == i) fillRound(x, y - 1, w, 14, 3, FX_B);
        art::chip(cx, y + 1, i, true);
        text35(cx - text35Width(V[i]) / 2, y + 7, V[i], active == i ? INK : WHITE);
    }
    button(104 + ox, 24, GOLD, "SPIN", hover == 6, canSpin);
    return true;
}

}  // namespace bar
