#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// CHBlackjack's button bar (its Bar.cpp, after Press-Play-On-Tape/
// Blackjack's drawButtons), cut down to the purse and one big button.
#include <RPGame.h>
#include "Bar.h"
#include "Layout.h"
#include "Chips.h"

namespace bar {

using namespace lay;

static uint32_t lastSig = 0;
static bool force = true;

void invalidate() { force = true; }

bool draw(const View &v) {
    uint32_t sig = (uint32_t)v.purse * 31u + v.ante;
    sig = sig * 31u + (uint32_t)(uintptr_t)v.label + (uint32_t)(uintptr_t)v.who;
    sig = sig * 31u + ((v.flash >> 2) & 1) + (v.flash ? 2 : 0) + v.rollsLeft * 4 + v.rollOn * 32 + v.sel * 64 +
          v.held * 128;
    if (!force && sig == lastSig) return false;
    force = false;
    lastSig = sig;
    gfx_fillRect(0, BAR_Y, 128, BAR_H, NAVY);
    gfx_hline(0, BAR_Y, 128, INK);
    // The purse, with the ante riding beside it.
    int y = BAR_Y + 2;
    panel(PURSE_X, y, PURSE_W, 13, 3, INK, GOLD);
    if (v.purse < 0) {
        gfx_text(PURSE_X + PURSE_W / 2 - gfx_textWidth(v.who) / 2, y + 3, v.who, GOLD);
    } else {
        char buf[12];
        if (v.ante) art::stack(PURSE_X + 11, y + 7, v.ante, 3);
        else text35(PURSE_X + 4, y + 4, "PURSE", FELT_LT);
        fmtMoney(buf, v.purse);
        uint8_t c = (v.flash & 4) ? WHITE : GOLD;
        int tx = PURSE_X + PURSE_W - 4 - gfx_textWidth(buf);
        gfx_text(tx, y + 3, buf, c);                         // double-struck = bold
        gfx_text(tx + 1, y + 3, buf, c);
    }
    // ROLL, and a pip for each roll left.
    int by = y - (v.sel ? 1 : 0) + (v.held ? 1 : 0), h = 12;
    fillRound(ROLL_X, by, ROLL_W, h, 3, v.rollOn ? GOLD : NAVY);
    if (v.rollOn && !v.held) gfx_hline(ROLL_X + 2, by + h - 2, ROLL_W - 4, WOOD);
    roundRect(ROLL_X, by, ROLL_W, h, 3, v.sel ? FX_A : INK);
    gfx_text(ROLL_X + ROLL_W / 2 - gfx_textWidth(v.label) / 2, by + 3, v.label, v.rollOn ? INK : SILVER);
    if (!v.held) {
        art::dieFace(ROLL_X + 3, by + 3, 5, 5);
        for (uint8_t i = 0; i < 3; i++)
            gfx_fillRect(ROLL_X + ROLL_W - 7, by + 2 + i * 3, 3, 2,
                         i < v.rollsLeft ? (v.rollOn ? INK : SILVER) : (v.rollOn ? WOOD : INK));
    }
    return true;
}

}  // namespace bar
