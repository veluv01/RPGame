// Drawing a card: its face, its back (a weave or a picture), the empty slot
// and the strip that shows of a covered card. See CardArt.h.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library)
// The card face derives from Press-Play-On-Tape/Blackjack (Apache-2.0),
// PlayGameState_Render.cpp drawCard(), via CHBlackjack and CHPoker.
#include <RPGame.h>
#include "CardArt.h"
#include "Klondike.h"
#include "src/assets/Assets.h"

namespace art {

bool fourColour = false;
uint8_t backStyle = 0;
uint8_t clock = 0;
bool flat = false;

const char *const BACK_NAME[BACKS] = {"CLASSIC", "BLUE", "ROBOT", "ROSES", "CASTLE", "ISLAND",
                                      "FISH", "SHELL", "CHERRY", "DICE", "LUCKY 7", "CHIP"};

uint8_t suitColour(uint8_t c) {
    static const uint8_t TWO[4] = {INK, RED, INK, RED}, FOUR[4] = {FELT_DK, BLUE, INK, RED};
    return (fourColour ? FOUR : TWO)[suitOf(c)];
}

static void span1(int x, int y, const uint8_t *d, uint8_t c) {
    uint8_t h = d[1];
    d += 2;
    for (int j = 0; j < h; j++) {
        uint8_t n = *d++;
        while (n--) { gfx_hline(x + d[0], y + j, d[1], c); d += 2; }
    }
}

// What shows of each back while it is squashed mid-flip, under the weaves,
// and of a face-down card under another.
static const uint8_t BACK_BASE[BACKS] = {WINE, NAVY, NAVY, INK, NAVY, CYAN, BLUE, WINE, NAVY, FELT_DK, INK, FELT};
static const uint8_t BACK_LINE[BACKS] = {WHITE, WHITE, SILVER, RED, SILVER, WHITE, CYAN, SKIN, RED, WHITE, GOLD, WHITE};

// The Windows decks had a few that moved: the robot's lights, the castle's
// bats, the sun over the island. Here, a pixel or two on a slow clock.
static void gag(int x, int y, uint8_t style) {
    uint8_t c = clock;
    switch (style) {
        case 2:                                         // robot: the chest lights swap, the eyes blink
            if (c & 2) { gfx_pixel(x + 6, y + 12, RED); gfx_pixel(x + 8, y + 12, GOLD); }
            if ((c & 15) == 15) gfx_hline(x + 6, y + 5, 3, WHITE);
            gfx_pixel(x + 7, y + 1, c & 1 ? RED : GOLD);
            break;
        case 4:                                         // castle: the bats flap
            if (c & 1) {
                gfx_hline(x + 1, y + 2, 3, NAVY); gfx_hline(x + 1, y + 3, 3, INK); gfx_pixel(x + 2, y + 2, INK);
                gfx_hline(x + 6, y + 5, 3, NAVY); gfx_hline(x + 6, y + 6, 3, INK); gfx_pixel(x + 7, y + 5, INK);
            }
            break;
        case 5:                                         // island: the sun's rays come and go
            if (c & 2) { gfx_pixel(x + 9, y + 2, GOLD); gfx_pixel(x + 14, y + 4, GOLD); gfx_pixel(x + 12, y + 5, GOLD); gfx_pixel(x + 12, y, GOLD); }
            break;
        case 6:                                         // fish: a bubble rises
            gfx_pixel(x + 8, y + 11 - (c % 12), WHITE);
            break;
        case 10:                                        // lucky 7: it sparkles
            gfx_pixel(x + 2 + (c & 3) * 3, y + 1 + ((c >> 1) & 1), c & 4 ? WHITE : GOLD);
            break;
        case 11:                                        // chip: the centre flashes
            if ((c & 7) == 0) gfx_fillRect(x + 6, y + 8, 3, 3, WHITE);
            break;
    }
}

void back(int x, int y, int w, uint8_t style, uint8_t edge) {
    static const uint8_t *const ART[BACKS - 2] = {BACK_ROBOT, BACK_ROSES, BACK_CASTLE, BACK_ISLAND, BACK_FISH,
                                                 BACK_SHELL, BACK_CHERRY, BACK_DICE, BACK_LUCKY, BACK_CHIP};
    if (style >= BACKS) style = 0;
    fillRound(x, y, w, SH, 2, BACK_BASE[style]);
    if (style >= 2) {
        if (w == SW) { sprite4(ART[style - 2], x + 1, y + 1); gag(x + 1, y + 1, style); }
    } else if (w > 6) {
        dither(x + 2, y + 2, w - 4, SH - 4, style ? BLUE : RED, 0);
        gfx_rect(x + 2, y + 2, w - 4, SH - 4, WHITE);
        if (w >= 12) {
            // A small diamond in the middle.
            int cx = x + w / 2, cy = y + SH / 2;
            uint8_t c = style ? CYAN : GOLD;
            for (int i = 0; i < 4; i++) gfx_hline(cx - i, cy - 3 + i, 2 * i + 1, c);
            for (int i = 0; i < 3; i++) gfx_hline(cx - 2 + i, cy + 1 + i, 5 - 2 * i, c);
        }
    }
    roundRect(x, y, w, SH, 2, edge);
}

// Drop shadow on the felt, 1 px right of and below the card, following its
// rounded corner.
static void shadow(int x, int y, int w) {
    if (flat) return;
    gfx_vline(x + w, y + 2, SH - 4, FELT_DK);
    gfx_hline(x + 2, y + SH, w - 4, FELT_DK);
    if (w < 5) return;
    gfx_pixel(x + w - 1, y + SH - 2, FELT_DK);
    gfx_pixel(x + w - 2, y + SH - 1, FELT_DK);
}

// The top `rows` of a card under another: its edge, and the fill between.
static void strip(int x, int y, int rows, uint8_t fill, uint8_t edge) {
    gfx_hline(x + 2, y, SW - 4, edge);
    gfx_hline(x + 1, y + 1, SW - 2, fill);
    gfx_pixel(x + 1, y + 1, edge);
    gfx_pixel(x + SW - 2, y + 1, edge);
    if (rows <= 2) return;
    gfx_fillRect(x + 1, y + 2, SW - 2, rows - 2, fill);
    gfx_vline(x, y + 2, rows - 2, edge);
    gfx_vline(x + SW - 1, y + 2, rows - 2, edge);
    if (!flat) gfx_vline(x + SW, y + 2, rows - 2, FELT_DK);
}

void small(int x, int y, uint8_t c, bool faceUp, int w, int rows, uint8_t edge) {
    if (w < SW) x += (SW - w) / 2;
    if (w <= 0) return;
    uint8_t col = suitColour(c), r = rankOf(c), s = suitOf(c);
    if (rows < SH) {
        if (!faceUp) {
            uint8_t st = backStyle < BACKS ? backStyle : 0;
            strip(x, y, rows, BACK_BASE[st], edge);
            gfx_hline(x + 2, y + 1, SW - 4, BACK_LINE[st]);      // so a pile of them reads as cards
            return;
        }
        strip(x, y, rows, WHITE, edge);
    } else {
        shadow(x, y, w);
        if (!faceUp) { back(x, y, w, backStyle, edge); return; }
        panel(x, y, w, SH, 2, WHITE, edge);
        if (w < SW) return;
    }
    glyph(x + 2, y + 2, RANK_GLYPH + r * 7, RANK_WIDTH[r], col);
    glyph(x + 10, y + 2, SUIT_SMALL + s * 5, 5, col);
    if (rows < SH) return;
    if (r >= RJ) {
        static const uint8_t *const BUST[3] = {BUST_JACK, BUST_QUEEN, BUST_KING};
        uint8_t remap[16];
        for (uint8_t i = 0; i < 16; i++) remap[i] = i;
        remap[RED] = col == INK || col == FELT_DK ? BLUE : col;   // robe in the suit colour
        sprite4(BUST[r - RJ], x + 2, y + 11, remap);
    } else {
        span1(x + 4, y + 11, PIP9 + PIP9_AT[s], col);
    }
}

void slot(int x, int y, uint8_t suit) {
    roundRect(x, y, SW, SH, 2, FELT_DK);
    if (suit < 4) span1(x + 4, y + 7, PIP9 + PIP9_AT[suit], FELT_DK);
}

}  // namespace art
