#pragma GCC optimize("Os")   // cold code: size over speed (the pixel loops are RPGfx's and the RPGame library's)
// Drawing the machines (Machine.h): the cabinets' tops, the reels and the
// bonus wheel, the strip under the reels, and the paytable.
#include <RPGame.h>
#include <string.h>
#include "Machine.h"
#include "Layout.h"
#include "Fx.h"
#include "src/assets/Assets.h"

namespace mach {

using namespace lay;

const uint16_t THEMES[THEME_COUNT][3] = {
    {0x042, 0x173, 0x4B5},   // the casino's green
    {0x401, 0x2A6, 0xF82},   // DRAGON FORTUNE: maroon, jade, orange
    {0xF7A, 0x7DB, 0xA6E},   // SWEET: pink, mint, lilac
};

int reelX(uint8_t machine, uint8_t reel) {
    if (machine == M_FORTUNE) return F_WIN_X + reel * CELL;
    return (machine == M_CLASSIC ? C_WIN_X : S_WIN_X) + 2 + reel * C_PITCH;
}
int winY(uint8_t machine) { return machine == M_FORTUNE ? F_WIN_Y : C_WIN_Y; }

// Where each machine's symbols start in the sheet.
static const uint8_t SYM_BASE[M_COUNT] = {0, SYM_FORTUNE, SYM_SWEET};

static const char *const TAG[3] = {"MINI", "MINOR", "MAJOR"};

void symbol(const Slots &g, uint8_t sym, int x, int y, uint8_t k) {
    sprite4(SYMBOLS + SYMBOL_AT[SYM_BASE[g.machine] + sym], x, y);
    if (!k) return;
    char buf[10];
    if (k >= K_MINI) fmtStr(buf, TAG[k - K_MINI]);
    else fmtMoney(buf, g.coinValue(k));
    text35(x + 11 - text35Width(buf) / 2, y + 11, buf, k >= K_MINI ? RED : INK);     // under the coin's $
}

static void right35(int x, int y, const char *s, uint8_t c) { text35(x - text35Width(s) + 1, y, s, c); }
static void right57(int x, int y, const char *s, uint8_t c) { gfx_text(x - gfx_textWidth(s) + 1, y, s, c); }

// ---------------------------------------------------------------------------
// The top: LUCKY 7's marquee, DRAGON FORTUNE's meters
// ---------------------------------------------------------------------------
void top(const Slots &g, const View &v, uint32_t frame) {
    if (g.machine == M_CLASSIC) {
        gfx_fillRect(0, 0, 128, C_TOP_H, INK);
        panel(0, 0, 128, C_TOP_H, 4, WINE, GOLD);
        // Chasing bulbs round the sign.
        uint8_t ph = (uint8_t)(frame >> 3);
        for (uint8_t i = 0; i < 15; i++) {
            static const uint8_t BULB[4] = {WHITE, GOLD, RED, GOLD};
            int x = 7 + i * 8;
            gfx_hline(x, 2, 2, BULB[(i + ph) & 3]);
            gfx_hline(x, 17, 2, BULB[(i + ph + 2) & 3]);
        }
        Mask m = maskBegin(60, 12);
        maskText35(m, 0, 0, "LUCKY 7", 2);
        static const uint8_t RAMP[12] = {FX_B, FX_B, FX_B, GOLD, GOLD, GOLD, GOLD, GOLD, WOOD, WOOD, WOOD, WOOD};
        maskDraw(m, 64 - text35WidthScaled("LUCKY 7", 2) / 2, 4, GOLD, INK, -1, RAMP);
        symbol(g, C_SEVEN, 8, -2); symbol(g, C_SEVEN, 98, -2);
        return;
    }
    if (g.machine == M_SWEET) {
        // A candy-striped sign.
        panel(0, 0, 128, C_TOP_H, 4, FELT_DK, WHITE);
        for (int x = 6; x < 122; x += 8)                            // the stripes swap, like running lights
            gfx_fillRect(x, 1, 3, C_TOP_H - 2, ((x >> 3) + (frame >> 4)) & 1 ? FELT_LT : FELT);
        Mask m = maskBegin(44, 12);
        maskText35(m, 0, 0, "SWEET", 2);
        static const uint8_t RAMP[12] = {FX_B, FX_B, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, CYAN, CYAN, CYAN, CYAN};
        int w = text35WidthScaled("SWEET", 2);
        fillRound(64 - w / 2 - 5, 2, w + 9, 16, 3, RED);
        maskDraw(m, 64 - w / 2, 4, WHITE, INK, WINE, RAMP);
        symbol(g, S_BEAR, 6, -2); symbol(g, S_ICECREAM, 100, -2);
        return;
    }
    gfx_fillRect(0, 0, 128, F_TOP_H, FELT_DK);
    static const char *const NAME[J_COUNT] = {"MINI", "MINOR", "MAJOR", "GRAND"};
    static const uint8_t TINT[J_COUNT] = {WHITE, CYAN, FELT_LT, FX_A};
    for (uint8_t j = 0; j < J_COUNT; j++) {
        // GRAND and MAJOR on the top row, MINOR and MINI under them.
        int x = (j == J_GRAND || j == J_MINOR) ? 2 : 65, y = j >= J_MAJOR ? 1 : 13;
        panel(x, y, 61, 12, 3, j == J_GRAND ? INK : WINE, GOLD);
        text35(x + 4, y + 4, NAME[j], TINT[j]);
        char buf[12];
        fmtMoney(buf, j >= J_MAJOR ? v.meter[j - J_MAJOR] : g.meter(j));
        right35(x + 56, y + 4, buf, j == J_GRAND ? FX_B : GOLD);
    }
}

// ---------------------------------------------------------------------------
// The reels
// ---------------------------------------------------------------------------
static void box(int x, int y, uint8_t c) {
    roundRect(x, y, CELL, CELL, 3, c);
    roundRect(x + 1, y + 1, CELL - 2, CELL - 2, 2, INK);
}

static void reel(const Slots &g, const View &v, uint8_t i, uint32_t frame) {
    bool classic = g.machine != M_FORTUNE;                         // the three-reel cabinets: wide white strips
    int cx = reelX(g.machine, i), y0 = winY(g.machine);
    int sx = classic ? cx - 2 : cx, w = classic ? C_REEL_W : CELL;
    uint8_t bg = classic ? WHITE : FELT_DK;
    gfx_fillRect(sx, y0, w, WIN_H, bg);
    if (g.machine == M_CLASSIC) gfx_hline(sx, y0 + WIN_H / 2, w, RED);     // the pay line
    int rise = v.wildH[i];
    if (rise) {
        // The dragon fills the reel from the bottom up, fire at its leading edge.
        int yt = y0 + WIN_H - rise;
        gfx_setClip(sx, yt, w, rise);
        gfx_fillRect(sx, yt, w, rise, FX_B);
        for (uint8_t row = 0; row < ROWS; row++) symbol(g, F_DRAGON, cx + 1, y0 + row * CELL + 1);
        gfx_resetClip();
        if (rise < WIN_H) { gfx_hline(sx, yt, w, RED); dither(sx, yt - 3, w, 3, FELT_LT, (uint8_t)(frame & 1)); }
        if (rise >= WIN_H) return;
    }
    gfx_setClip(sx, y0, w, WIN_H - rise);
    int n = g.stripLen();
    int top = (int)(v.pos[i] >> 8), frac = (int)(v.pos[i] & 255);
    for (int k = 0; k < 4; k++) {
        int j = (top + k) % n;
        int d = j - v.stop[i];
        if (d < 0) d += n;
        uint8_t s, coin = K_NONE;
        if (v.state[i] != SPINNING && d < ROWS) { s = v.cell[i][d]; coin = v.coin[i][d]; }
        else s = g.stripSym(i, j);
        int y = y0 + k * CELL - ((frac * CELL) >> 8) + 1;
        // A coin's value shows only once it is wholly in the window (never cut in half).
        if (y + 11 < y0 || y + 16 > y0 + WIN_H - rise) coin = K_NONE;
        symbol(g, s, cx + 1, y, coin);
    }
    gfx_resetClip();
    if (v.state[i] == SPINNING) dither(sx, y0, w, WIN_H, bg, (uint8_t)(frame & 1));
    else if (classic) {
        // The drum curves away at the top and bottom.
        uint8_t shade = g.machine == M_SWEET ? FELT_DK : SILVER;
        dither(sx, y0, w, 5, shade, 0);
        dither(sx, y0 + WIN_H - 5, w, 5, shade, 1);
    }
    if (v.antic == i + 1) gfx_rect(sx, y0, w, WIN_H, FX_A);
}

static void holdCells(const Slots &g, const View &v, uint32_t frame) {
    for (uint8_t c = 0; c < CELLS; c++) {
        int x = reelX(M_FORTUNE, (uint8_t)(c / 3)), y = F_WIN_Y + (c % 3) * CELL;
        bool lit = v.flashCell == c + 1;
        gfx_fillRect(x, y, CELL, CELL, lit ? FX_B : FELT_DK);
        uint16_t bit = (uint16_t)(1u << c);
        if (g.held[c] && !(v.cellHide & bit)) {
            symbol(g, F_COIN, x + 1, y + 1, g.held[c]);
            if (lit) roundRect(x, y, CELL, CELL, 3, WHITE);
        } else if (v.cellSpin & bit) {
            gfx_setClip(x, y, CELL, CELL);
            symbol(g, F_COIN, x + 1, y + 1 + (int)((frame * 7 + c * 11) % 48) - 24);
            gfx_resetClip();
            dither(x, y, CELL, CELL, FELT_DK, (uint8_t)(frame & 1));
        } else {
            dither(x + 3, y + 3, CELL - 6, CELL - 6, WINE, 0);
        }
        gfx_rect(x, y, CELL, CELL, WINE);
    }
}

// The bonus wheel, over the reels: twelve wedges, prizes round the rim, a
// pointer at the top.
static void wheel(const Slots &g, const View &v, uint32_t frame) {
    const int cx = C_WIN_X + 3 * C_PITCH / 2 - 1, cy = C_WIN_Y + WIN_H / 2, R = 35;
    gfx_fillCircle(cx, cy, R + 3, INK);
    gfx_fillCircle(cx, cy, R + 2, GOLD);
    // Which wedge is under the pointer (the top, 192/256 of a turn).
    uint8_t at = (uint8_t)(((uint32_t)(uint16_t)(49152u - v.wheelAngle) * WHEEL_SEGS) >> 16);
    for (uint8_t sgm = 0; sgm < WHEEL_SEGS; sgm++) {
        static const uint8_t TINT[3] = {RED, NAVY, FELT};
        uint8_t prize = Slots::wheelPrize(sgm);
        uint8_t c = prize >= 60 ? GOLD : (prize >= 20 ? CYAN : TINT[sgm % 3]);
        if (v.wheelLit && sgm == at && (frame & 4)) c = WHITE;
        int16_t xy[8];
        xy[0] = (int16_t)(cx * 16 + 8); xy[1] = (int16_t)(cy * 16 + 8);
        int mx = 0, my = 0;
        for (int k = 0; k < 3; k++) {
            int a = (int)((v.wheelAngle + ((uint32_t)sgm * 2 + k) * (65536 / (WHEEL_SEGS * 2))) >> 8);
            int x = cx * 16 + 8 + ((fx::isin(a + 64) * R) >> 4), y = cy * 16 + 8 + ((fx::isin(a) * R) >> 4);
            xy[2 + k * 2] = (int16_t)x; xy[3 + k * 2] = (int16_t)y;
            if (k == 1) { mx = cx + ((fx::isin(a + 64) * (R - 9)) >> 8); my = cy + ((fx::isin(a) * (R - 9)) >> 8); }
        }
        fillConvex(xy, 4, c);
        char buf[4];
        fmtInt(buf, prize);
        text35(mx - text35Width(buf) / 2 + 1, my - 2, buf, (c == GOLD || c == CYAN || c == WHITE) ? INK : WHITE);
    }
    gfx_fillCircle(cx, cy, 6, INK);
    gfx_fillCircle(cx, cy, 5, GOLD);
    gfx_fillCircle(cx, cy, 2, WOOD);
    for (int k = 0; k < 6; k++) {                           // the pointer
        gfx_hline(cx - 5 + k, cy - R - 3 + k, 11 - 2 * k, INK);
        if (k < 5) gfx_hline(cx - 4 + k, cy - R - 3 + k, 9 - 2 * k, k ? RED : WHITE);
    }
}

// Darken every cell of DRAGON FORTUNE's window that is not in `keep`.
static void dim(uint16_t keep) {
    if (!keep) return;
    for (uint8_t c = 0; c < CELLS; c++)
        if (!(keep >> c & 1))
            dither(reelX(M_FORTUNE, (uint8_t)(c / 3)), F_WIN_Y + (c % 3) * CELL, CELL, CELL, INK, (uint8_t)(c & 1));
}

static uint16_t lineCells(const Slots &g, uint8_t l) {
    uint16_t m = 0;
    for (uint8_t r = 0; r < g.res.lineCount[l]; r++) m |= (uint16_t)(1u << (r * 3 + Slots::lineRow(l, r)));
    return m;
}

static void lineMarks(const Slots &g, uint8_t l, bool path) {
    uint8_t n = g.res.lineCount[l], m = g.machine;
    int px = 0, py = 0;
    for (uint8_t r = 0; r < g.reels(); r++) {
        int x = reelX(m, r), y = winY(m) + Slots::lineRow(l, r) * CELL;
        if (path && r) {
            gfx_line(px, py + 1, x + CELL / 2, y + CELL / 2 + 1, INK);
            gfx_line(px, py, x + CELL / 2, y + CELL / 2, FX_A);
        }
        px = x + CELL / 2; py = y + CELL / 2;
    }
    for (uint8_t r = 0; r < n; r++)
        box(reelX(m, r), winY(m) + Slots::lineRow(l, r) * CELL, m == M_SWEET ? RED : FX_B);
}

// SWEET: candy-striped wall, the rush ladder on the left, three reels.
static void sweetWindow(const Slots &g, const View &v, uint32_t frame, bool full) {
    const Result &res = g.res;
    if (full) {
        gfx_fillRect(0, C_TOP_H, 128, C_LOW_Y - C_TOP_H, FELT_DK);
        for (int x = 2; x < 128; x += 8) gfx_fillRect(x, C_TOP_H, 3, C_LOW_Y - C_TOP_H, WHITE);
        fillRound(S_WIN_X - 3, C_WIN_Y - 2, 3 * C_PITCH + 4, WIN_H + 4, 2, INK);
        // The ladder: x5 at the top, the step in play lit.
        panel(RUSH_X - 1, C_WIN_Y - 2, RUSH_W + 2, WIN_H + 4, 3, INK, WHITE);
        text35(RUSH_X + 2, C_WIN_Y + 1, "RUSH", WHITE);
        for (uint8_t k = 0; k < RUSH_STEPS; k++) {
            bool lit = k == v.rush;
            int y = C_WIN_Y + 9 + (RUSH_STEPS - 1 - k) * 16;
            fillRound(RUSH_X + 1, y, RUSH_W - 2, 14, 3, lit ? (k ? FX_B : WHITE) : (k < v.rush ? FELT : FELT_LT));
            char buf[4] = {'X', (char)('0' + Slots::rushMult(k)), 0, 0};
            gfx_text(RUSH_X + 4, y + 4, buf, lit ? INK : WHITE);
        }
    }
    for (uint8_t i = 0; i < 3; i++) reel(g, v, i, frame);
    if (v.line == LINE_NONE) return;
    for (uint8_t l = 0; l < SWEET_LINES; l++)
        if (res.lineCount[l] && (v.line == LINE_ALL || v.line == l)) lineMarks(g, l, v.line == l);
}

// The wall behind the arm: a glow round the mount that falls away to the
// corners, in rings of solid and dithered green. Drawn as row spans (one
// square root a ring a row), not per pixel.
static void armWall() {
    static const uint8_t RING[5] = {9, 17, 26, 36, 47};
    const int x0 = C_BODY_W, cx = C_BODY_W + 4, w = 128 - C_BODY_W;
    for (int y = C_TOP_H; y < C_LOW_Y; y++) {
        int dy = y - ARM_PIVOT;
        gfx_hline(x0, y, w, INK);
        dither(x0, y, w, 1, FELT_DK, 0);
        for (int k = 4; k >= 0; k--) {
            int rr = RING[k] * RING[k] - dy * dy;
            if (rr <= 0) continue;
            int dx = 0;
            while ((dx + 1) * (dx + 1) <= rr) dx++;
            int n = cx + dx - x0 + 1;
            if (n > w) n = w;
            switch (k) {
                case 4: gfx_hline(x0, y, n, FELT_DK); break;
                case 3: dither(x0, y, n, 1, FELT, 0); break;
                case 2: gfx_hline(x0, y, n, FELT); break;
                case 1: dither(x0, y, n, 1, FELT_LT, 1); break;
                default: gfx_hline(x0, y, n, FELT_LT); dither(x0, y, n, 1, FELT, 0); break;
            }
        }
    }
}

void window(const Slots &g, const View &v, uint32_t frame, bool full) {
    const Result &res = g.res;
    if (g.machine == M_SWEET) { sweetWindow(g, v, frame, full); return; }
    if (g.machine == M_CLASSIC) {
        if (!full) {
            // Only the reels are turning: the body, the wall and the arm stand.
            for (uint8_t i = 0; i < 3; i++) reel(g, v, i, frame);
            if (v.wheelOn) wheel(g, v, frame);
            return;
        }
        // Chrome body, the reel recess, the arm's side of the wall.
        gfx_fillRect(0, C_TOP_H, C_BODY_W, C_LOW_Y - C_TOP_H, SILVER);
        gfx_vline(1, C_TOP_H, C_LOW_Y - C_TOP_H, WHITE);
        gfx_vline(C_BODY_W - 2, C_TOP_H, C_LOW_Y - C_TOP_H, NAVY);
        gfx_vline(0, C_TOP_H, C_LOW_Y - C_TOP_H, INK);
        gfx_vline(C_BODY_W - 1, C_TOP_H, C_LOW_Y - C_TOP_H, INK);
        fillRound(C_WIN_X - 3, C_WIN_Y - 2, 3 * C_PITCH + 4, WIN_H + 4, 2, v.antic ? FX_A : INK);
        for (uint8_t i = 0; i < 3; i++) reel(g, v, i, frame);
        if (v.line != LINE_NONE && res.lineCount[0] && (frame & 8)) {
            bool three = res.lineCount[0] == 3;
            for (uint8_t i = 0; i < 3; i++)
                if (three || res.grid[i][1] == C_CHERRY) {
                    int x = reelX(M_CLASSIC, i);
                    roundRect(x - 2, C_WIN_Y + CELL - 1, C_REEL_W, CELL + 2, 3, RED);
                    roundRect(x - 1, C_WIN_Y + CELL, C_REEL_W - 2, CELL, 2, FX_A);
                }
        }
        if (v.wheelOn) wheel(g, v, frame);
        // The arm: a slot in the wall, the shaft from the pivot, the knob.
        armWall();
        fillRound(C_BODY_W, ARM_PIVOT - 8, 9, 16, 2, SILVER);
        gfx_hline(C_BODY_W, ARM_PIVOT - 7, 8, WHITE);
        dither(C_BODY_W, ARM_PIVOT + 1, 9, 7, NAVY, 0);                 // the mount, lit from above
        gfx_fillRect(C_BODY_W + 6, ARM_PIVOT + 5, 3, 3, NAVY);
        gfx_rect(C_BODY_W - 1, ARM_PIVOT - 8, 10, 16, INK);
        // The body's edge falls into shadow where the mount joins it.
        dither(C_BODY_W - 5, ARM_PIVOT - 20, 3, 40, NAVY, 0);
        gfx_fillRect(C_BODY_W - 4, ARM_PIVOT - 9, 2, 18, NAVY);
        dither(C_BODY_W - 5, ARM_PIVOT - 9, 3, 18, SILVER, 1);
        int ky = ARM_TOP + (ARM_BOTTOM - ARM_TOP) * v.arm / 64;
        // The arm swings toward you through the middle of its travel: the knob grows.
        int kd = ky > ARM_PIVOT ? ky - ARM_PIVOT : ARM_PIVOT - ky, kr = kd < 9 ? 7 : (kd < 20 ? 6 : 5);
        int y0 = ky < ARM_PIVOT ? ky : ARM_PIVOT, y1 = ky < ARM_PIVOT ? ARM_PIVOT : ky;
        gfx_fillRect(C_BODY_W + 6, ARM_PIVOT - 2, ARM_X - C_BODY_W - 5, 4, INK);       // the elbow
        gfx_fillRect(ARM_X - 2, y0, 5, y1 - y0 + 1, INK);
        gfx_fillRect(ARM_X - 1, y0, 2, y1 - y0 + 1, SILVER);
        gfx_vline(ARM_X - 1, y0, y1 - y0 + 1, WHITE);
        gfx_fillCircle(ARM_X, ky, kr + 1, INK);
        gfx_fillCircle(ARM_X, ky, kr, RED);
        gfx_fillRect(ARM_X - 3, ky - 3, 2, 2, WHITE);
        gfx_fillRect(ARM_X + 1, ky + 2, 3, 2, WINE);
        // Pay line pointers (last: the right one sits on the shaded edge).
        int my = C_WIN_Y + WIN_H / 2;
        for (int k = 0; k < 3; k++) {
            gfx_vline(C_WIN_X - 6 + k, my - 2 + k, 5 - 2 * k, RED);
            gfx_vline(C_WIN_X + 3 * C_PITCH + 3 - k, my - 2 + k, 5 - 2 * k, RED);
        }
        return;
    }
    gfx_fillRect(0, F_TOP_H, 128, F_LOW_Y - F_TOP_H, FELT_DK);
    gfx_rect(2, F_TOP_H, 124, F_LOW_Y - F_TOP_H, WOOD);
    // The frame burns through the free games.
    bool hot = v.antic || g.freeLeft || g.res.wasFree;
    gfx_rect(3, F_TOP_H + 1, 122, F_LOW_Y - F_TOP_H - 2, hot ? FX_A : GOLD);
    if (hot) gfx_rect(2, F_TOP_H, 124, F_LOW_Y - F_TOP_H, FX_B);
    if (v.holdMode) { holdCells(g, v, frame); return; }
    for (uint8_t i = 0; i < 5; i++) reel(g, v, i, frame);
    for (uint8_t i = 1; i < 5; i++) gfx_vline(F_WIN_X + i * CELL, F_WIN_Y, WIN_H, WINE);
    if (v.line == LINE_NONE) return;
    if (v.line == LINE_ALL) {
        // Everything that won stays bright; the rest steps back.
        uint16_t keep = 0, feature = 0;
        for (uint8_t l = 0; l < LINES; l++) keep |= lineCells(g, l);
        for (uint8_t c = 0; c < CELLS; c++) {
            uint8_t s = res.grid[c / 3][c % 3];
            if ((s == F_GONG && res.freeTrigger) || (s == F_COIN && res.holdTrigger)) feature |= (uint16_t)(1u << c);
        }
        dim(keep | feature);
        for (uint8_t l = 0; l < LINES; l++) if (res.lineCount[l]) lineMarks(g, l, false);
        for (uint8_t c = 0; c < CELLS; c++)                 // what set a feature off
            if (feature >> c & 1) box(reelX(M_FORTUNE, (uint8_t)(c / 3)), F_WIN_Y + (c % 3) * CELL, FX_A);
    } else if (res.lineCount[v.line]) {
        dim(lineCells(g, (uint8_t)v.line));
        lineMarks(g, (uint8_t)v.line, true);
    }
}

// ---------------------------------------------------------------------------
// Under the reels: the win, a message, and the bar
// ---------------------------------------------------------------------------
void low(const Slots &g, const View &v, uint32_t frame) {
    bool classic = g.machine != M_FORTUNE;          // the three-reel cabinets share the plaque
    char buf[16];
    int y;
    if (classic) {
        gfx_fillRect(0, C_LOW_Y, 128, BAR_Y - C_LOW_Y, g.machine == M_SWEET ? FELT_LT : WOOD);
        gfx_hline(0, C_LOW_Y, 128, INK);
        panel(3, C_LOW_Y + 2, 122, 11, 2, INK, GOLD);
        y = C_LOW_Y + 5;
    } else {
        gfx_fillRect(0, F_LOW_Y, 128, BAR_Y - F_LOW_Y, INK);
        y = F_LOW_Y + 3;
    }
    text35(7, y, "WIN", v.shownWin ? WHITE : SILVER);
    if (v.shownWin) {
        fmtMoney(buf, v.shownWin);
        gfx_text(22, y - 1, buf, FX_B);
    }
    if (v.msg) right35(120, y, v.msg, classic ? SILVER : FELT_LT);

    gfx_fillRect(0, BAR_Y, 128, BAR_H, NAVY);
    gfx_hline(0, BAR_Y, 128, INK);
    text35(3, BAR_Y + 2, "BET", SILVER);
    fmtMoney(buf, g.bet());
    gfx_text(3, BAR_Y + 8, buf, WHITE);
    if (v.canSpin && !g.inFeature()) {
        int w = gfx_textWidth(buf);
        text35(5 + w, BAR_Y + 9, "<>", CYAN);
    }
    right35(124, BAR_Y + 2, "PURSE", SILVER);
    fmtMoney(buf, v.shownPurse);
    right57(124, BAR_Y + 8, buf, v.shownPurse != g.purse ? WHITE : GOLD);
    // The button: SPIN on the modern machine, a reminder of the arm on the old one.
    const char *label = g.machine == M_CLASSIC ? "PULL" : (g.freeLeft ? "FREE" : "SPIN");
    int by = BAR_Y + 2 + (v.pressed ? 1 : 0);
    bool live = v.canSpin;
    fillRound(44, by, 40, 12, 3, live ? GOLD : NAVY);
    if (live && !v.pressed) gfx_hline(46, by + 10, 36, WOOD);
    roundRect(44, by, 40, 12, 3, live && (frame & 16) ? FX_B : INK);
    gfx_text(64 - gfx_textWidth(label) / 2, by + 3, label, live ? INK : SILVER);
}

// ---------------------------------------------------------------------------
// The paytable (B): a scrolling list, every symbol at full size with what it
// pays at the bet in play
// ---------------------------------------------------------------------------
constexpr int PAY_TOP = 13, PAY_BOT = 116, PAY_ROW = 26;

static const uint8_t PAY_ROWS[M_COUNT] = {C_COUNT + 2, F_COUNT, S_COUNT + 1};

int payScrollMax(const Slots &g) { return PAY_ROWS[g.machine] * PAY_ROW - (PAY_BOT - PAY_TOP); }

// A line shows only while it is wholly inside the list (never cut in half).
static void ptext(int x, int y, const char *t, uint8_t c, bool right = false) {
    if (y < PAY_TOP || y + 5 > PAY_BOT) return;
    if (right) right35(x, y, t, c); else text35(x, y, t, c);
}

// One row: the symbol on a tile, its name, a line under it, an amount at the right.
static void payRow(const Slots &g, int y, int sym, const char *name, const char *under, int32_t amount, bool top) {
    bool fortune = g.machine == M_FORTUNE;
    gfx_hline(4, y + PAY_ROW - 1, 116, INK);
    panel(4, y + 1, 24, 24, 3, fortune ? FELT_DK : WHITE, top ? FX_B : SILVER);
    if (sym >= 0) symbol(g, (uint8_t)sym, 5, y + 2);
    else gfx_text(8, y + 9, "X5", INK);
    ptext(33, y + 6, name, top ? FX_B : WHITE);
    ptext(33, y + 15, under, SILVER);
    if (amount) {
        char buf[12];
        fmtMoney(buf, amount);
        right57(119, y + 4, buf, top ? FX_B : GOLD);
    }
}

void paytable(const Slots &g, int scroll) {
    static const char *const TITLE[M_COUNT] = {"LUCKY 7 PAYS", "DRAGON FORTUNE PAYS", "SWEET PAYS"};
    int32_t bet = g.bet(), unit = bet / 5;
    char buf[28];
    gfx_clear(NAVY);
    gfx_setClip(0, PAY_TOP, 128, PAY_BOT - PAY_TOP);
    for (uint8_t i = 0; i < PAY_ROWS[g.machine]; i++) {
        int y = PAY_TOP + i * PAY_ROW - scroll;
        if (y <= PAY_TOP - PAY_ROW || y >= PAY_BOT) continue;
        if (g.machine == M_CLASSIC) {
            // Dearest first.
            static const uint8_t ORDER[C_COUNT] = {C_CHEST, C_SEVEN, C_DIAMOND, C_BAR, C_HORSESHOE, C_CLOVER, C_BELL,
                                                   C_WATERMELON, C_GRAPE, C_BANANA, C_PLUM, C_APPLE, C_CHERRY,
                                                   C_LEMON, C_ORANGE};
            static const char *const NAME[C_COUNT] = {"TREASURE", "SEVENS", "DIAMONDS", "BARS", "HORSESHOES", "CLOVERS",
                                                      "BELLS", "MELONS", "GRAPES", "BANANAS", "PLUMS", "APPLES",
                                                      "CHERRIES", "LEMONS", "ORANGES"};
            if (i < C_COUNT) {
                uint8_t c = ORDER[i];
                payRow(g, y, c, NAME[i], "3 ON THE LINE", Slots::classicPay(c, c, c) * bet, i < 2);
            } else if (i == C_COUNT) {
                fmtMoney(fmtStr(fmtMoney(fmtStr(buf, "ONE "), Slots::classicPay(C_CHERRY, C_LEMON, C_BELL) * bet), "  TWO "),
                         Slots::classicPay(C_CHERRY, C_CHERRY, C_BELL) * bet);
                payRow(g, y, C_CHERRY, "ANY CHERRY PAYS", buf, 0, false);
            } else payRow(g, y, C_CLOVER, "2 LUCKY CHARMS", "SPIN THE BONUS WHEEL", 0, true);
        } else if (g.machine == M_FORTUNE) {
            static const char *const NAME[F_DRAGON] = {"JADE", "FAN", "LANTERN", "RED ENVELOPE", "KOI", "INGOT", "LUCKY CAT"};
            if (i < F_DRAGON) {
                uint8_t c = (uint8_t)(F_CAT - i);
                char *q = buf;
                for (uint8_t n = 3; n <= 5; n++) {
                    *q++ = (char)('0' + n); *q++ = ':';
                    q = fmtMoney(q, Slots::fortunePay(c, n) * unit);
                    *q++ = '~'; *q++ = '~'; *q = 0;
                }
                payRow(g, y, c, NAME[c], buf, 0, i == 0);
            } else {
                static const char *const WHAT[3][2] = {{"DRAGON: WILD", "FILLS ITS WHOLE REEL"},
                                                       {"3 GONGS", "8 FREE GAMES, PAYS X2"},
                                                       {"6 COINS", "LOCK FOR HOLD AND SPIN"}};
                payRow(g, y, i, WHAT[i - F_DRAGON][0], WHAT[i - F_DRAGON][1], 0, true);
            }
        } else {
            static const char *const NAME[S_COUNT] = {"GUMDROPS", "CANDIES", "CHOCOLATE", "DONUTS", "CUPCAKES", "ICE CREAM",
                                                      "GUMMY BEARS", "LOLLIPOP: WILD"};
            if (i < S_COUNT) {
                uint8_t c = (uint8_t)(S_LOLLY - i);
                payRow(g, y, c, NAME[c], i ? "3 ON ANY OF 5 LINES" : "STANDS FOR ANY SWEET", Slots::sweetPay(c) * unit, i < 2);
            } else payRow(g, y, -1, "SUGAR RUSH", "WINS IN A ROW PAY MORE", 0, true);
        }
    }
    gfx_resetClip();
    // Header, footer, and where you are in the list.
    gfx_fillRect(0, 0, 128, PAY_TOP, INK);
    gfx_fillRect(0, PAY_BOT, 128, 128 - PAY_BOT, INK);
    gfx_hline(0, PAY_TOP - 1, 128, GOLD);
    gfx_hline(0, PAY_BOT, 128, GOLD);
    text35(64 - text35Width(TITLE[g.machine]) / 2, 4, TITLE[g.machine], GOLD);
    text35(4, 120, "A/B: PAGE", SILVER);
    right35(123, 120, "START: CLOSE", SILVER);
    int span = PAY_BOT - PAY_TOP - 2, total = PAY_ROWS[g.machine] * PAY_ROW;
    gfx_vline(125, PAY_TOP + 1, span, INK);
    gfx_fillRect(124, PAY_TOP + 1 + scroll * span / total, 3, span * span / total + 1, GOLD);
}

}  // namespace mach
