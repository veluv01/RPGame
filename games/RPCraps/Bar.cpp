#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// CHBlackjack's button bar (its Bar.cpp, after Press-Play-On-Tape/
// Blackjack's drawButtons), cut down to a chip rack and one big button.
#include <RPGame.h>
#include "Bar.h"
#include "Layout.h"
#include "Zones.h"
#include "Chips.h"

namespace bar {

using namespace lay;

static uint32_t lastSig = 0;
static bool force = true;

void invalidate() { force = true; }

bool draw(uint8_t denom, int8_t cursor, bool rollOn, bool held, uint32_t frame) {
    (void)frame;
    uint32_t sig = (uint32_t)denom | (uint32_t)(cursor + 1) << 4 | (uint32_t)rollOn << 8 | (uint32_t)held << 9;
    if (!force && sig == lastSig) return false;
    force = false;
    lastSig = sig;
    gfx_fillRect(0, BAR_Y, 128, BAR_H, NAVY);
    gfx_hline(0, BAR_Y, 128, INK);
    static const char *const V[4] = {"$1", "$5", "$25", "$100"};
    for (uint8_t i = 0; i < 4; i++) {
        const Zone &z = zones::at(0, (uint8_t)(zones::count(0) - 5 + i));     // bar slots (same on both tables)
        bool inHand = i == denom, sel = cursor == i;
        int x = z.x, w = z.w, cx = x + w / 2, y = BAR_Y + 2 - (inHand ? 1 : 0);
        if (inHand) fillRound(x, y, w, 13, 3, FX_B);
        if (sel) roundRect(x - 1, y - 1, w + 2, 15, 3, FX_A);
        art::chip(cx, y + 1, i, true);
        text35(cx - text35Width(V[i]) / 2, y + 7, V[i], inHand ? INK : WHITE);
    }
    const Zone &r = zones::at(0, (uint8_t)(zones::count(0) - 1));
    bool sel = cursor == 4;
    int y = BAR_Y + 2 - (sel ? 1 : 0) + (held ? 1 : 0), h = 12;
    uint8_t face = rollOn ? GOLD : NAVY;
    fillRound(r.x, y, r.w, h, 3, face);
    if (rollOn && !held) gfx_hline(r.x + 2, y + h - 2, r.w - 4, WOOD);
    roundRect(r.x, y, r.w, h, 3, sel ? FX_A : INK);
    const char *label = held ? "SHAKE!" : "ROLL";
    uint8_t tc = rollOn ? INK : SILVER;
    gfx_text(r.x + r.w / 2 - gfx_textWidth(label) / 2, y + 3, label, tc);
    // Two little dice on the button.
    if (!held) { art::dieFace(r.x + 3, y + 3, 5, 5); art::dieFace(r.x + r.w - 8, y + 3, 5, 2); }
    if (!rollOn) dither(r.x + 1, y + 1, r.w - 2, h - 2, INK, 0);
    return true;
}

}  // namespace bar
