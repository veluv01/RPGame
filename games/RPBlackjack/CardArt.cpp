#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// Card rendering derived from Press-Play-On-Tape/Blackjack (Apache-2.0),
// PlayGameState_Render.cpp drawCard(); rebuilt 2026 in colour for RPGame.
#include <RPGame.h>
#include "CardArt.h"
#include "Round.h"
#include "src/assets/Assets.h"

namespace art {

bool fourColour = false;

uint8_t suitColour(uint8_t c) {
    uint8_t s = cardSuit(c);
    if (fourColour) { static const uint8_t F[4] = {RED, BLUE, INK, FELT_DK}; return F[s]; }
    return s < 2 ? RED : INK;
}

static void span1(int x, int y, const uint8_t *d, uint8_t c) {
    uint8_t h = d[1];
    d += 2;
    for (int j = 0; j < h; j++) {
        uint8_t n = *d++;
        while (n--) { gfx_hline(x + d[0], y + j, d[1], c); d += 2; }
    }
}

// Rotated 90 degrees clockwise: row j of the shape becomes column (h-1-j).
static void span1Rot(int x, int y, const uint8_t *d, uint8_t c) {
    uint8_t h = d[1];
    d += 2;
    for (int j = 0; j < h; j++) {
        uint8_t n = *d++;
        while (n--) { gfx_vline(x + h - 1 - j, y + d[0], d[1], c); d += 2; }
    }
}

static void glyphRot(int x, int y, const uint8_t *cols, uint8_t n, uint8_t h, uint8_t c) {
    for (uint8_t i = 0; i < n; i++)
        for (uint8_t b = 0; b < h; b++)
            if (cols[i] & (1u << b)) gfx_pixel(x + h - 1 - b, y + i, c);
}

static void back(int x, int y, int w, int h) {
    fillRound(x, y, w, h, 2, WINE);
    if (w > 6) {
        dither(x + 2, y + 2, w - 4, h - 4, RED, 0);
        gfx_rect(x + 2, y + 2, w - 4, h - 4, WHITE);
    }
    if (w >= 12) {
        // A small gold diamond in the middle.
        int cx = x + w / 2, cy = y + h / 2;
        for (int i = 0; i < 4; i++) gfx_hline(cx - i, cy - 3 + i, 2 * i + 1, GOLD);
        for (int i = 0; i < 3; i++) gfx_hline(cx - 2 + i, cy + 1 + i, 5 - 2 * i, GOLD);
    }
    roundRect(x, y, w, h, 2, INK);
}

// Drop shadow on the felt, 1 px right of and below a w x h card. It follows
// the card's 2 px rounded corner: both edges stop 3 px short of the corner
// and two pixels step diagonally round it, instead of a square corner that
// leaves a gap.
static void shadow(int x, int y, int w, int h) {
    gfx_vline(x + w, y + 2, h - 4, FELT_DK);
    gfx_hline(x + 2, y + h, w - 4, FELT_DK);
    if (w < 5) return;                                  // mid-flip sliver: edges only
    gfx_pixel(x + w - 1, y + h - 2, FELT_DK);
    gfx_pixel(x + w - 2, y + h - 1, FELT_DK);
}

void card(int x, int y, uint8_t c, bool faceUp, int w, bool full) {
    const int H = 28;
    if (w < 22) x += (22 - w) / 2;
    if (w <= 0) return;
    shadow(x, y, w, H);
    if (!faceUp) { back(x, y, w, H); return; }

    panel(x, y, w, H, 2, WHITE, INK);
    if (w < 10) return;
    uint8_t col = suitColour(c), r = cardRank(c), s = cardSuit(c);
    int ix = x + 2 - (22 - w) / 4;                       // corner index slides in as it squashes
    if (w >= 16) {
        glyph(ix, y + 3, RANK_GLYPH + r * 7, RANK_WIDTH[r], col);
        glyph(ix, y + 12, SUIT_SMALL + s * 5, 5, col);
    }
    if (!full || w < 20) return;
    if (r == 0) {
        span1(x + 7, y + 9, PIP13 + PIP13_AT[s], col);
    } else if (r >= 10) {
        static const uint8_t *const COURT[3] = {COURT_JACK, COURT_QUEEN, COURT_KING};
        uint8_t remap[16];
        for (uint8_t i = 0; i < 16; i++) remap[i] = i;
        remap[RED] = col == INK ? BLUE : col;            // robe in the suit colour
        sprite4(COURT[r - 10], x + 7, y + 5, remap);
        glyph(x + 16, y + 20, SUIT_SMALL + s * 5, 5, col);
    } else {
        span1(x + 9, y + 11, PIP9 + PIP9_AT[s], col);
    }
}

void cardSideways(int x, int y, uint8_t c) {
    const int W = 28, H = 22;
    shadow(x, y, W, H);
    panel(x, y, W, H, 2, WHITE, INK);
    uint8_t col = suitColour(c), r = cardRank(c), s = cardSuit(c);
    // Rotated clockwise: the corner index lands top-right.
    glyphRot(x + W - 10, y + 2, RANK_GLYPH + r * 7, RANK_WIDTH[r], 7, col);
    glyphRot(x + W - 18, y + 2, SUIT_SMALL + s * 5, 5, 6, col);
    if (r == 0) span1Rot(x + 5, y + 5, PIP13 + PIP13_AT[s], col);
    else span1Rot(x + 6, y + 7, PIP9 + PIP9_AT[s], col);
}

void dimCard(int x, int y, int w, int h) {
    static const uint8_t DIM[16] = {INK, SILVER, INK, FELT_DK, FELT, SILVER, WINE, WINE,
                                    WOOD, WOOD, NAVY, INK, WOOD, SILVER, FX_A, FX_B};
    remapRect(x, y, w, h, DIM);
}

// ---------------------------------------------------------------------------
// Chips: $1 white, $5 red, $10 blue, $25 green, $100 black.
// ---------------------------------------------------------------------------
static const uint8_t CHIP_BODY[5] = {WHITE, RED, BLUE, FELT_LT, INK};
static const uint8_t CHIP_EDGE[5] = {BLUE, WHITE, WHITE, WHITE, GOLD};
static const uint8_t CHIP_SHADE[5] = {SILVER, WINE, NAVY, FELT_DK, INK};
static const int32_t CHIP_VALUE[5] = {1, 5, 10, 25, 100};

int chipDenom(int32_t amount) {
    for (int i = 4; i >= 0; i--) if (amount >= CHIP_VALUE[i]) return i;
    return 0;
}

void chip(int cx, int y, uint8_t d, bool top) {
    uint8_t b = CHIP_BODY[d], e = CHIP_EDGE[d], sh = CHIP_SHADE[d];
    // Edge band (2 rows) with the classic stripes.
    gfx_hline(cx - 6, y + 2, 13, sh);
    gfx_hline(cx - 6, y + 3, 13, sh);
    gfx_pixel(cx - 7, y + 2, INK); gfx_pixel(cx + 7, y + 2, INK);
    gfx_pixel(cx - 7, y + 1, INK); gfx_pixel(cx + 7, y + 1, INK);
    for (int i = -4; i <= 4; i += 4) gfx_vline(cx + i, y + 2, 2, e);
    gfx_hline(cx - 5, y + 4, 11, INK);
    if (!top) return;
    gfx_fillEllipse(cx, y + 1, 6, 2, b);
    gfx_ellipse(cx, y + 1, 7, 2, INK);
    gfx_pixel(cx - 4, y + 1, e); gfx_pixel(cx + 4, y + 1, e);
    gfx_pixel(cx, y, e); gfx_pixel(cx, y + 2, e);
}

void chipStack(int cx, int baseY, int32_t amount, uint8_t maxChips) {
    uint8_t chips[24], n = 0;
    for (int d = 4; d >= 0 && n < 24; d--)
        while (amount >= CHIP_VALUE[d] && n < 24) { chips[n++] = (uint8_t)d; amount -= CHIP_VALUE[d]; }
    if (!n) return;
    uint8_t first = n > maxChips ? (uint8_t)(n - maxChips) : 0;   // show the top of tall stacks
    for (uint8_t i = first; i < n; i++)
        chip(cx, baseY - 2 * (i - first), chips[i], i == n - 1);
}

// ---------------------------------------------------------------------------
// Hand total badge: a pill with 5x7 text.
// ---------------------------------------------------------------------------
int badgeWidth(const char *t) { return gfx_textWidth(t) + 5; }

void badge(int x, int y, const char *t, uint8_t bg, uint8_t fg) {
    int w = badgeWidth(t);
    panel(x, y, w, 11, 3, bg, INK);
    gfx_text(x + 3, y + 2, t, fg);
}

}  // namespace art
