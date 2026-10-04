// The action bar (Bar.h).
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library)
// Button bar from CHBlackjack (itself after Press-Play-On-Tape/Blackjack's
// drawButtons()), with poker's choices.
#include <RPGame.h>
#include "Bar.h"
#include "Layout.h"

namespace bar {

using namespace lay;

static int16_t widthQ4[4];          // animated widths (x16): the accordion
static Bar lastBar = Bar::None;

void reset() { lastBar = Bar::None; }

bool easing() {
    for (auto w : widthQ4) if (w & 15) return true;
    return false;
}

static bool darkFace(uint8_t c) { return c == RED || c == BLUE || c == NAVY || c == WINE || c == INK; }

static void button(int x, int w, uint8_t face, const char *label, bool sel, bool on) {
    int y = BAR_Y + 2 - (sel ? 1 : 0), h = 12;
    fillRound(x, y, w, h, 3, on ? face : NAVY);
    if (on) gfx_hline(x + 2, y + h - 2, w - 4, darkFace(face) ? INK : WOOD);
    roundRect(x, y, w, h, 3, sel ? FX_B : INK);
    uint8_t tc = !on ? SILVER : (darkFace(face) ? WHITE : INK);
    int tw = text35Width(label);
    if (tw <= w - 3) text35(x + w / 2 - tw / 2, y + 4, label, tc);
    if (!on) dither(x + 1, y + 1, w - 2, h - 2, INK, 0);
}

static void layout(const uint8_t *target, uint8_t n, int16_t *xs, int16_t *ws) {
    int total = 0;
    for (uint8_t i = 0; i < n; i++) {
        int16_t t = (int16_t)(target[i] << 4);
        widthQ4[i] = (int16_t)(widthQ4[i] + (t - widthQ4[i]) / 3);
        if (widthQ4[i] - t < 8 && t - widthQ4[i] < 8) widthQ4[i] = t;
        total += (widthQ4[i] + 8) >> 4;
    }
    int x = (128 - total - 2 * (n - 1)) / 2;
    for (uint8_t i = 0; i < n; i++) {
        ws[i] = (int16_t)((widthQ4[i] + 8) >> 4);
        xs[i] = (int16_t)x;
        x += ws[i] + 2;
    }
}

static void arrows(int x, int y) {
    gfx_pixel(x, y, WHITE); gfx_hline(x - 1, y + 1, 3, WHITE);
    gfx_hline(x - 1, y + 8, 3, WHITE); gfx_pixel(x, y + 9, WHITE);
}

void draw(const Table &t, uint32_t frame) {
    (void)frame;
    if (t.bar != lastBar) {
        lastBar = t.bar;
        for (auto &w : widthQ4) w = (int16_t)(30 << 4);
    }
    int16_t xs[4], ws[4];
    char a[16], b[16];
    switch (t.bar) {
        case Bar::Bet: {
            uint8_t n = t.slots(), W[4];
            bool fl = VARIANTS[t.game].limit == FIXED_LIMIT;
            for (uint8_t i = 0; i < n; i++) W[i] = t.sel == i ? (fl ? 52 : 50) : (uint8_t)(fl ? 35 : 23);
            layout(W, n, xs, ws);
            int32_t call = t.toCall(YOU);
            bool opening = t.curBet == 0;
            // FOLD | CHECK / CALL $x | BET / RAISE $y | ALL IN / POT
            button(xs[0], ws[0], RED, "FOLD", t.sel == B_FOLD, t.slotEnabled(B_FOLD));
            if (!call) fmtStr(a, "CHECK");
            else if (t.sel == B_CALL) fmtShort(fmtStr(a, "CALL "), call);
            else fmtStr(a, "CALL");
            button(xs[1], ws[1], FELT_LT, a, t.sel == B_CALL, true);
            int32_t to = fl ? t.minTo(YOU) : t.raiseTo;
            const char *word = opening ? "BET" : "RAISE";
            if (t.sel == B_RAISE) fmtShort(fmtStr(fmtStr(b, word), " "), to);
            else fmtStr(b, word);
            button(xs[2], ws[2], GOLD, b, t.sel == B_RAISE, t.slotEnabled(B_RAISE));
            if (t.sel == B_RAISE && !fl && t.minTo(YOU) < t.maxTo(YOU)) arrows(xs[2] + ws[2] - 4, BAR_Y + 2);
            if (n > 3) {
                bool pot = VARIANTS[t.game].limit == POT_LIMIT;
                if (t.sel == B_MAX) fmtShort(fmtStr(a, pot ? "POT " : "ALL IN "), t.maxTo(YOU));
                else fmtStr(a, pot ? "POT" : "MAX");
                button(xs[3], ws[3], pot ? BLUE : WINE, a, t.sel == B_MAX, t.slotEnabled(B_MAX));
            }
            break;
        }
        case Bar::Draw: {
            uint8_t k = 0;
            for (uint8_t i = 0; i < 5; i++) k += (t.drawMask >> i) & 1;
            char *p = fmtStr(a, "THROW UP TO ");
            fmtInt(p, t.drawLimit());
            text35(4, BAR_Y + 6, a, SILVER);
            if (k) fmtInt(fmtStr(b, "DRAW "), k); else fmtStr(b, "STAND PAT");
            button(70, 54, GOLD, b, t.glove == 5, true);
            break;
        }
        case Bar::Next:
        case Bar::Rebuy: {
            static const uint8_t W[2] = {74, 44};
            layout(W, 2, xs, ws);
            if (t.bar == Bar::Next) fmtStr(a, "NEXT HAND");
            else fmtMoney(fmtStr(a, "REBUY "), t.rebuyAmount());
            button(xs[0], ws[0], t.bar == Bar::Next ? FELT_LT : GOLD, a, t.sel == N_NEXT, true);
            button(xs[1], ws[1], RED, "LEAVE", t.sel == N_LEAVE, true);
            break;
        }
        default: break;
    }
}

}  // namespace bar
