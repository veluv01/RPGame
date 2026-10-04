#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// Button bar derived from Press-Play-On-Tape/Blackjack (Apache-2.0),
// PlayGameState_Render.cpp drawButtons(); redrawn 2026 in colour for RPGame.
#include <RPGame.h>
#include <string.h>
#include "Bar.h"
#include "Layout.h"
#include "CardArt.h"
#include "Round.h"

namespace bar {

using namespace lay;

static int16_t widthQ4[6];          // animated widths (x16) for the accordion
static Bar lastBar = Bar::None;
static uint32_t lastSig = 0;
static bool force = true;

void reset() { lastBar = Bar::None; force = true; }
void invalidate() { force = true; }

static bool darkFace(uint8_t c) { return c == RED || c == BLUE || c == NAVY || c == WINE || c == INK; }

static void button(int x, int w, uint8_t face, const char *label, bool sel, bool on, uint32_t frame) {
    int y = BAR_Y + 2 - (sel ? 1 : 0), h = 12;
    fillRound(x, y, w, h, 3, on ? face : NAVY);
    if (on) gfx_hline(x + 2, y + h - 2, w - 4, darkFace(face) ? INK : WOOD);
    roundRect(x, y, w, h, 3, sel ? FX_B : INK);
    uint8_t tc = !on ? SILVER : (darkFace(face) ? WHITE : INK);
    bool big = sel && gfx_textWidth(label) <= w - 4;
    if (big) gfx_text(x + w / 2 - gfx_textWidth(label) / 2, y + 3, label, tc);
    else text35(x + w / 2 - text35Width(label) / 2, y + 4, label, tc);
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
    int gap = 2;
    int x = (128 - total - gap * (n - 1)) / 2;
    for (uint8_t i = 0; i < n; i++) {
        ws[i] = (int16_t)((widthQ4[i] + 8) >> 4);
        xs[i] = (int16_t)x;
        x += ws[i] + gap;
    }
}

bool draw(const Round &r, uint32_t frame) {
    // Redraw only when something on the bar changed (palette animation
    // keeps the highlight pulsing without a redraw).
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix((uint32_t)r.bar); mix(r.sel); mix(r.insureAmt); mix((uint32_t)r.phase);
    for (uint8_t i = 0; i < r.slotCount(); i++) mix(r.slotEnabled(i) | (widthQ4[i] << 1));
    if (r.bar == Bar::None) mix((frame >> 3) % 4);
    if (!force && h == lastSig && r.bar == lastBar) {
        bool easing = false;
        for (uint8_t i = 0; i < 6; i++) if (widthQ4[i] & 15) easing = true;
        if (!easing) return false;
    }
    force = false;
    lastSig = h;
    gfx_fillRect(0, BAR_Y, 128, BAR_H, NAVY);
    gfx_hline(0, BAR_Y, 128, INK);
    if (r.bar != lastBar) {
        lastBar = r.bar;
        uint8_t n = r.slotCount();
        for (uint8_t i = 0; i < 6; i++) widthQ4[i] = (int16_t)((i < n ? 128 / (n ? n : 1) - 2 : 0) << 4);
    }
    int16_t xs[6], ws[6];
    switch (r.bar) {
        case Bar::Bet: {
            static const uint8_t W[6] = {16, 16, 16, 16, 30, 22};
            layout(W, 6, xs, ws);
            static const char *const V[4] = {"$1", "$5", "$10", "$25"};
            static const uint8_t D[4] = {0, 1, 2, 3};
            for (uint8_t i = 0; i < 4; i++) {
                bool sel = r.sel == i, on = r.slotEnabled(i);
                int cx = xs[i] + ws[i] / 2, y = BAR_Y + 2 - (sel ? 1 : 0);
                if (sel) fillRound(xs[i], y, ws[i], 13, 3, FX_B);
                art::chip(cx, y + 1, D[i], true);
                text35(cx - text35Width(V[i]) / 2, y + 7, V[i], on ? (sel ? INK : WHITE) : SILVER);
                if (!on) dither(xs[i], y, ws[i], 13, NAVY, 0);
            }
            button(xs[4], ws[4], GOLD, "DEAL", r.sel == B_DEAL, r.slotEnabled(B_DEAL), frame);
            button(xs[5], ws[5], SILVER, "CLR", r.sel == B_CLEAR, r.slotEnabled(B_CLEAR), frame);
            break;
        }
        case Bar::Play: {
            static const char *const FULL[4] = {"HIT", "STAND", "DOUBLE", "SPLIT"};
            static const char *const SHORT[4] = {"HIT", "STAND", "DBL", "SPLIT"};
            static const uint8_t FACE[4] = {FELT_LT, RED, GOLD, BLUE};
            uint8_t W[4];
            for (uint8_t i = 0; i < 4; i++) W[i] = r.sel == i ? 40 : 26;
            layout(W, 4, xs, ws);
            for (uint8_t i = 0; i < 4; i++) {
                bool sel = r.sel == i;
                button(xs[i], ws[i], FACE[i], sel ? FULL[i] : SHORT[i], sel, r.slotEnabled(i), frame);
            }
            break;
        }
        case Bar::Insurance: {
            static const uint8_t W[2] = {74, 44};
            layout(W, 2, xs, ws);
            char buf[16];
            fmtMoney(fmtStr(buf, "INSURE "), r.insureAmt);
            button(xs[0], ws[0], CYAN, buf, r.sel == I_YES, r.slotEnabled(I_YES), frame);
            button(xs[1], ws[1], SILVER, "NO", r.sel == I_NO, true, frame);
            if (r.sel == I_YES) {                            // up/down arrows: adjust
                int ax = xs[0] + ws[0] - 6;
                gfx_pixel(ax, BAR_Y + 3, WHITE); gfx_hline(ax - 1, BAR_Y + 4, 3, WHITE);
                gfx_hline(ax - 1, BAR_Y + 11, 3, WHITE); gfx_pixel(ax, BAR_Y + 12, WHITE);
            }
            break;
        }
        case Bar::End: {
            static const uint8_t W[2] = {70, 48};
            layout(W, 2, xs, ws);
            button(xs[0], ws[0], FELT_LT, "NEXT HAND", r.sel == E_CONTINUE, true, frame);
            button(xs[1], ws[1], RED, "QUIT", r.sel == E_QUIT, true, frame);
            break;
        }
        default: {
            const char *msg = "";
            switch (r.phase) {
                case Phase::InitDeal: case Phase::SplitCards: msg = "DEALING"; break;
                case Phase::Shuffle: msg = "SHUFFLING THE SHOE"; break;
                case Phase::Peeking: case Phase::PeekResult: msg = "DEALER PEEKS"; break;
                case Phase::PlayDealerHand: case Phase::RevealHole: msg = "DEALER PLAYS"; break;
                case Phase::CheckForWins: case Phase::OverallWinOrLose: msg = "PAYING OUT"; break;
                default: break;
            }
            int w = text35Width(msg);
            text35(64 - w / 2, BAR_Y + 6, msg, SILVER);
            // Little animated dots.
            for (int i = 0; i < 3; i++)
                if (*msg && ((frame >> 3) % 4) > (uint32_t)i) gfx_pixel(64 + w / 2 + 2 + i * 2, BAR_Y + 10, SILVER);
            break;
        }
    }
    return true;
}

}  // namespace bar
