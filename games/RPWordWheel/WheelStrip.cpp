// The wheel in close-up (WheelStrip.h). The row loop runs from SRAM
// (RAMFUNC); everything after it is compiled for size.
#include <RPGame.h>
#include <string.h>
#include "WheelStrip.h"
#include "Layout.h"
#include "Show.h"
#include "Spin.h"

namespace wheelstrip {

using namespace lay;

constexpr int HUB_Y = 250;                          // where the wedges' edges meet
constexpr int DEN = HUB_Y - WHEEL_Y0;
constexpr int MAXSEG = 12;

static inline __attribute__((always_inline)) void px(uint8_t *row, int x, uint8_t c) {
    if ((unsigned)x >= GFX_W) return;
    uint8_t *p = row + (x >> 1);
    *p = (x & 1) ? (uint8_t)((*p & 0x0F) | (c << 4)) : (uint8_t)((*p & 0xF0) | c);
}

// One row at a time: fill between the edges, then the divider - a metal
// strip, dark on its shadowed side - and step the edges toward the hub.
// edge[] is Q16 screen x.
RAMFUNC(wedges) static void wedges(int32_t *edge, const int32_t *step, const uint8_t *colour, uint8_t n) {
    for (int y = WHEEL_Y0; y <= WHEEL_Y1; y++) {
        uint8_t *row = gfx_fb + y * GFX_FB_STRIDE;
        int xa = edge[0] >> 16;
        for (uint8_t k = 0; k < n; k++) {
            int xb = edge[k + 1] >> 16;
            if (xb > 0 && xa < GFX_W) {
                gfx_hline(xa + 2, y, xb - xa - 2, colour[k]);
                px(row, xa, INK);
                px(row, xa + 1, SILVER);
            }
            xa = xb;
        }
        for (uint8_t k = 0; k <= n; k++) edge[k] += step[k];
    }
}

#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // the rest is cold: size over speed

static const uint8_t CYCLE[8] = {RED, GOLD, BLUE, CYAN, FELT_LT, SKIN, WINE, SILVER};

static uint8_t colourOf(uint8_t kind, uint8_t index) {
    switch (kind) {
        case wedge::TOP: return FX_B;
        case wedge::BANKRUPT: return INK;
        case wedge::LOSE: return WHITE;
        case wedge::FREE: return FELT;
        case wedge::WILD: return FX_A;
        case wedge::PRIZE: return CYAN;
        case wedge::MYSTERY: return NAVY;
        case wedge::ENVELOPE: return (index & 1) ? WINE : NAVY;
        default: return CYCLE[index & 7];
    }
}

static bool dark(uint8_t c) { return c == RED || c == BLUE || c == WINE || c == INK || c == NAVY || c == FELT; }

// Screen x of the point d rim pixels right of the pointer, at row y.
static int at(int d, int y) { return POINTER_X + d * (HUB_Y - y) / DEN; }

// Printed on the wheel, standing a little off it: a shadow under dark
// lettering's right, a pale one under black lettering's.
static void printed(int x, int y, const char *s, uint8_t c) {
    text35x2(x + 1, y + 1, s, c == INK ? SILVER : INK);
    text35x2(x, y, s, c);
}

// Characters down the wedge's centre line, in the doubled 3x5 font.
static void stack(int mid, const char *s, uint8_t c, int y, int pitch) {
    char one[2] = {0, 0};
    for (; *s; s++, y += pitch) {
        one[0] = *s;
        printed(at(mid, y + 5) - 3, y, one, c);
    }
}

// Whole words, one a line.
static void words(int mid, const char *s, uint8_t c, int y) {
    char line[8];
    for (;;) {
        uint8_t n = 0;
        while (*s && *s != ' ' && n < 7) line[n++] = *s++;
        line[n] = 0;
        printed(at(mid, y + 5) - n * 4 + 1, y, line, c);
        if (!*s) return;
        s++;
        y += 13;
    }
}

static void label(const wedge::Wedge &w, int mid, uint8_t c) {
    uint8_t ink = dark(c) ? WHITE : INK;
    char buf[8];
    switch (w.kind) {
        case wedge::BANKRUPT: {
            // The narrow letters of the built-in font, struck twice.
            static const char B[] = "BANKRUPT";
            char one[2] = {0, 0};
            for (uint8_t i = 0; i < 8; i++) {
                int y = WHEEL_Y0 + 3 + i * 8;
                one[0] = B[i];
                int x = at(mid, y + 3) - 3;
                gfx_text(x, y, one, WHITE);
                gfx_text(x + 1, y, one, WHITE);
            }
            break;
        }
        case wedge::LOSE: words(mid, "LOSE A TURN", INK, WHEEL_Y0 + 8); break;
        case wedge::FREE: words(mid, "FREE PLAY", WHITE, WHEEL_Y0 + 12); break;
        case wedge::WILD: words(mid, "WILD CARD", INK, WHEEL_Y0 + 12); break;
        case wedge::PRIZE: words(mid, "WIN A TRIP", INK, WHEEL_Y0 + 8); break;
        case wedge::MYSTERY: stack(mid, "?$1000", WHITE, WHEEL_Y0 + 2, 11); break;
        case wedge::ENVELOPE: {
            int y = WHEEL_Y0 + 14, x = at(mid, y + 6) - 9;
            gfx_fillRect(x, y, 18, 12, WHITE);
            gfx_rect(x, y, 18, 12, INK);
            for (int i = 0; i < 9; i++) {                 // the flap
                gfx_pixel(x + i, y + i * 2 / 3, INK);
                gfx_pixel(x + 17 - i, y + i * 2 / 3, INK);
            }
            break;
        }
        default:
            buf[0] = '$';
            fmtInt(buf + 1, w.value);
            stack(mid, buf, ink, WHEEL_Y0 + 4, 12);
    }
}

void draw(const Show &s, int32_t posQ8, int speed, uint32_t frame) {
    int pos = (int)(posQ8 >> 8);
    int first = (pos - 110) / spin::WEDGE;
    if (pos - 110 < 0) first--;                         // floor

    // The segments on screen: a wedge, or the thirds of the split one.
    int32_t edge[MAXSEG + 1], step[MAXSEG + 1];
    uint8_t colour[MAXSEG], n = 0;
    struct Lab { wedge::Wedge w; int16_t mid; uint8_t c; } lab[7];
    uint8_t nLab = 0;
    for (int k = first; k < first + 7 && n < MAXSEG - 2; k++) {
        uint8_t index = (uint8_t)(((k % wedge::COUNT) + wedge::COUNT) % wedge::COUNT);
        wedge::Wedge w = s.wedgeAt(index);
        int d = k * spin::WEDGE - pos;
        uint8_t c = colourOf(w.kind, index);
        if (w.kind == wedge::THIRDS) {
            for (uint8_t t = 0; t < 3; t++) {
                edge[n] = d + t * spin::PEG;
                colour[n++] = t == 1 ? FX_B : INK;
            }
        } else {
            edge[n] = d;
            colour[n++] = c;
        }
        lab[nLab++] = {w, (int16_t)(d + spin::WEDGE / 2), c};
    }
    edge[n] = (first + 7) * spin::WEDGE - pos;
    for (uint8_t k = 0; k <= n; k++) {
        int32_t d = edge[k];
        edge[k] = ((int32_t)POINTER_X << 16) + d * 65536;
        step[k] = -(d * 65536) / DEN;
    }
    wedges(edge, step, colour, n);

    if (speed >= 10) dither(0, WHEEL_Y0, 128, WHEEL_Y1 - WHEEL_Y0 + 1, SILVER, (uint8_t)(frame & 1));
    else if (speed < 6) {
        for (uint8_t i = 0; i < nLab; i++) {
            int x = POINTER_X + lab[i].mid;
            if (x < -16 || x > 144) continue;
            if (lab[i].w.kind == wedge::THIRDS) stack(lab[i].mid, "$10K", INK, WHEEL_Y0 + 6, 13);
            else label(lab[i].w, lab[i].mid, lab[i].c);
        }
    }

    // The face is a drum: it turns away into shadow at both sides, and
    // darkens toward the hub below.
    const int H = WHEEL_Y1 - WHEEL_Y0 + 1;
    dither(0, WHEEL_Y0, 14, H, INK, 0);
    dither(114, WHEEL_Y0, 14, H, INK, 0);
    dither(0, WHEEL_Y0, 5, H, INK, 1);
    dither(123, WHEEL_Y0, 5, H, INK, 1);
    dither(14, WHEEL_Y1 - 6, 100, 7, INK, 1);

    // The rim with its pegs, and the pointer's flipper: a peg coming up on
    // its right bends it over until the peg slips past. The rim throws a
    // shadow across the top of the face.
    gfx_fillRect(0, RIM_Y, 128, 5, NAVY);
    gfx_hline(0, RIM_Y, 128, GOLD);
    gfx_hline(0, RIM_Y + 1, 128, BLUE);
    gfx_fillRect(0, RIM_Y + 5, 128, 4, WOOD);
    gfx_hline(0, RIM_Y + 5, 128, GOLD);
    gfx_hline(0, WHEEL_Y0, 128, INK);
    dither(0, WHEEL_Y0 + 1, 128, 1, INK, 0);
    int phase = ((pos % spin::PEG) + spin::PEG) % spin::PEG;
    for (int x = POINTER_X - phase - 5 * spin::PEG; x < 130; x += spin::PEG) {
        gfx_fillRect(x - 1, RIM_Y + 6, 3, 3, FX_B);         // a brass peg, lit from the top left
        gfx_pixel(x - 1, RIM_Y + 6, WHITE);
        gfx_pixel(x + 1, RIM_Y + 8, WOOD);
    }
    int bend = phase >= 9 ? phase - 8 : 0;
    for (int j = 0; j < 13; j++) {
        int half = 5 - j / 2 - (j > 9 ? 1 : 0);
        if (half < 0) half = 0;
        int x = POINTER_X - bend * j / 12;
        gfx_hline(x - half, RIM_Y + 1 + j, 2 * half + 1, j ? RED : WHITE);
        if (j) gfx_hline(x + 1, RIM_Y + 1 + j, half, WINE);  // its far side in shade
        gfx_pixel(x - half - 1, RIM_Y + 1 + j, INK);
        gfx_pixel(x + half + 1, RIM_Y + 1 + j, INK);
    }
    gfx_fillRect(POINTER_X - 6, RIM_Y, 13, 2, SILVER);
    gfx_hline(POINTER_X - 5, RIM_Y, 11, WHITE);
    gfx_hline(POINTER_X - 6, RIM_Y + 2, 13, INK);
}

}  // namespace wheelstrip
