// Drawing the board (see Table.h): where things are in world space, the
// board a row at a time, the checkers, the dice and the cube at any zoom.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (the board's rows are a RAMFUNC)
#include <RPGame.h>
#include "Table.h"
#include "Font.h"
#include "src/assets/Assets.h"

namespace table {

uint8_t zoom = 5;
int16_t camX = CX, camY = CY;
bool mirror;

void setCamera(int x, int y) {
    // Half the view, in world units: 64 across, 54 down at the plain size.
    int hx = 320 / zoom, hy = 270 / zoom;
    if (x < hx) x = hx;
    if (x > 128 - hx) x = 128 - hx;
    if (y < TOP + hy) y = TOP + hy;
    if (y > BOT - hy) y = BOT - hy;
    camX = (int16_t)x; camY = (int16_t)y;
}

// White's point -> its left column, before mirroring.
static int pointLeft(uint8_t w) {
    if (w <= 6) return RIGHT_X + (6 - w) * PW;
    if (w <= 12) return LEFT_X + (12 - w) * PW;
    if (w <= 18) return LEFT_X + (w - 13) * PW;
    return RIGHT_X + (w - 19) * PW;
}

int pointX(uint8_t w) { return mx(pointLeft(w), PW); }

void checkerAt(uint8_t side, uint8_t p, uint8_t k, uint8_t n, int &x, int &y) {
    if (p == bg::OFF) {
        x = mx(TRAY_X, CHIP);
        y = side == bg::WHITE ? FIELD_B - 3 - 3 * k : FIELD_T + 3 * k;
        return;
    }
    if (p == bg::BAR) {
        // Out from the cube in the middle of the bar; more than four share
        // the length of four.
        int off = (k * (n <= 4 ? PITCH * 16 : (3 * PITCH * 16) / (n - 1))) >> 4;
        x = mx(BAR_X, CHIP);
        y = side == bg::WHITE ? CY - 14 - off : CY + 6 + off;
        return;
    }
    // Five to a stack at the full step; more share the same length.
    int off = (k * (n <= 5 ? PITCH * 16 : (4 * PITCH * 16) / (n - 1))) >> 4;
    uint8_t w = whites(side, p);
    x = pointX(w);
    y = w > 12 ? FIELD_T + off : FIELD_B - CHIP - off;
}

void dieAt(uint8_t side, uint8_t i, uint8_t n, int &x, int &y) {
    int mid = side == bg::WHITE ? RIGHT_X + 3 * PW : LEFT_X + 3 * PW;
    if (mirror) mid = GFX_W - mid;
    x = mid - (n * (DIE + 2) - 2) / 2 + i * (DIE + 2);
    y = DICE_Y;
}

void cubeAt(uint8_t owner, int &x, int &y) {
    x = mx(BAR_X + CHIP / 2 - CUBE / 2, CUBE);
    y = owner == bg::WHITE ? FIELD_B - CUBE - 1 : owner == bg::RED ? FIELD_T + 1 : CY - CUBE / 2;
}

// ---------------------------------------------------------------------------
// The board, a row at a time: each row's wood, felt and tray in one pass of
// word stores, then the points' spans over the felt. (Drawn as boxes, one
// over another, the board wrote most of its pixels twice; and the points'
// taper was a division for every point on every row.)
// ---------------------------------------------------------------------------
struct Run { int16_t a, b; };                   // screen x [a, b)

static Run worldRun(int x, int w) {
    int a = mx(x, w);
    Run r = {(int16_t)sx(a), (int16_t)sx(a + w)};
    return r;
}

// Colour c over [a, b) of a framebuffer row, clipped: ragged nibble ends,
// bytes up to a word boundary, then words.
RAMFUNC(tblrun) static void run(uint8_t *row, int a, int b, uint8_t c) {
    if (a < 0) a = 0;
    if (b > GFX_W) b = GFX_W;
    if (a >= b) return;
    uint8_t cc = (uint8_t)(c * 0x11);
    if (a & 1) { row[a >> 1] = (uint8_t)((row[a >> 1] & 0x0F) | (cc & 0xF0)); a++; }
    if (b & 1) { b--; row[b >> 1] = (uint8_t)((row[b >> 1] & 0xF0) | (cc & 0x0F)); }
    uint8_t *p = row + (a >> 1), *e = row + (b >> 1);
    while (p < e && ((uintptr_t)p & 3)) *p++ = cc;
    uint32_t w = cc * 0x01010101u;
    for (; p + 4 <= e; p += 4) *(uint32_t *)p = w;
    while (p < e) *p++ = cc;
}

struct Layout {
    Run wood, inlay, felt[2], tray, pts[12];
    int16_t yTop, yInT, yF, yPT, yPB, yF2, yInB, yBot, yTrayT, yTrayB, pw;
};

RAMFUNC(tblrows) static void boardRows(const Layout &L, int y0, int y1) {
    static const uint8_t FAR[2] = {FELT_DK, FELT_LT}, NEAR[2] = {FELT_LT, FELT_DK};
    if (y0 < L.yTop) y0 = L.yTop;
    if (y1 > L.yBot) y1 = L.yBot;
    for (int y = y0; y < y1; y++) {
        uint8_t *row = gfx_fb + y * GFX_FB_STRIDE;
        run(row, L.wood.a, L.wood.b, WOOD);
        if (y < L.yInT || y >= L.yInB) continue;                 // the frame
        if (y < L.yF || y >= L.yF2) { run(row, L.inlay.a, L.inlay.b, GOLD); continue; }
        run(row, L.felt[0].a, L.felt[0].b, FELT);
        run(row, L.felt[1].a, L.felt[1].b, FELT);
        if (y < L.yTrayT || y >= L.yTrayB) run(row, L.tray.a, L.tray.b, INK);
        // The points: White's 13..24 down from the far side, 12..1 up from the near.
        const uint8_t *col;
        int d, h;
        if (y < L.yPT) { d = y - L.yF; h = L.yPT - L.yF; col = FAR; }
        else if (y >= L.yPB) { d = L.yF2 - 1 - y; h = L.yF2 - L.yPB; col = NEAR; }
        else continue;
        int inset = (L.pw * d) / (2 * h);
        for (int i = 0; i < 12; i++) run(row, L.pts[i].a + inset, L.pts[i].b - inset, col[i & 1]);
    }
}

void drawBoard(int y0, int y1) {
    Layout L;
    L.wood = worldRun(0, GFX_W);
    L.inlay = worldRun(1, 117);
    L.felt[0] = worldRun(LEFT_X, 6 * PW);
    L.felt[1] = worldRun(RIGHT_X, 6 * PW);
    L.tray = worldRun(TRAY_X, CHIP);
    for (int i = 0; i < 12; i++) L.pts[i] = worldRun((i < 6 ? LEFT_X : RIGHT_X - 6 * PW) + i * PW, PW);
    L.pw = (int16_t)zoomed(PW);
    L.yTop = (int16_t)sy(TOP); L.yBot = (int16_t)sy(BOT);
    L.yInT = (int16_t)sy(FIELD_T - 1); L.yInB = (int16_t)sy(FIELD_B + 1);
    L.yF = (int16_t)sy(FIELD_T); L.yF2 = (int16_t)sy(FIELD_B);
    L.yPT = (int16_t)sy(FIELD_T + PH); L.yPB = (int16_t)sy(FIELD_B - PH);
    L.yTrayT = (int16_t)sy(FIELD_T + SLOT_H); L.yTrayB = (int16_t)sy(FIELD_B - SLOT_H);
    boardRows(L, y0 < 0 ? 0 : y0, y1 > GFX_H ? GFX_H : y1);
}

static void box(int x, int y, int w, int h, uint8_t c) {
    int a = sx(x), t = sy(y);
    gfx_fillRect(a, t, sx(x + w) - a, sy(y + h) - t, c);
}

void tint(uint8_t side, uint8_t p, uint8_t c, bool solid) {
    if (p == bg::OFF) {
        int y = side == bg::WHITE ? FIELD_B - SLOT_H : FIELD_T, tx = mx(TRAY_X, CHIP);
        int a = sx(tx), t = sy(y), w = sx(tx + CHIP) - a, h = sy(y + SLOT_H) - t;
        if (solid) gfx_fillRect(a, t, w, h, c);
        else dither(a, t, w, h, c, 0);
        return;
    }
    uint8_t wp = whites(side, p);
    bool far = wp > 12;
    int x = sx(pointX(wp)), w = zoomed(PW);
    int y0 = far ? sy(FIELD_T) : sy(FIELD_B) - 1;
    int h = far ? sy(FIELD_T + PH) - y0 : y0 + 1 - sy(FIELD_B - PH);
    for (int d = 0; d < h; d++) {
        int inset = (w * d) / (2 * h), y = far ? y0 + d : y0 - d;
        if (solid) gfx_hline(x + inset, y, w - 2 * inset, c);
        else dither(x + inset, y, w - 2 * inset, 1, c, 0);
    }
}

// ---------------------------------------------------------------------------
// Checkers, dice and the cube
// ---------------------------------------------------------------------------
// Raised by `lift`: the art's scale, and how far its top-left moves up and
// left to stay centred over where it was.
static int lifted(uint8_t lift, int size, int &back) {
    int s = zscale(), s2 = s + s * lift / 16;
    back = ((size * s2) >> 8) - ((size * s) >> 8);
    return s2;
}

static void shadow(int px, int py, int size, int lift) {
    if (!lift) return;
    int s = zoomed(size) - 2;
    fillRound(px + 1 + zoomed(1), py + 1 + zoomed(1), s, s, (uint8_t)(s / 2), INK);    // it stays on the felt
}

void drawChecker(int x, int y, const uint8_t *remap, uint8_t lift) {
    int px = sx(x), py = sy(y);
    // Close up, the bigger drawing of it (at half the scale).
    bool big = zoom >= 8;
    const uint8_t *art = big ? CHECKER_BIG : CHECKER;
    if (!lift) { sprite4(art, px, py, remap, zscale() >> big); return; }
    int back, s = lifted(lift, CHIP, back);
    shadow(px, py, CHIP, lift);
    sprite4(art, px - back / 2, py - back / 2 - zoomed(lift) / 2, remap, s >> big);
}

void drawOff(uint8_t side, uint8_t k) {
    int x, y;
    checkerAt(side, bg::OFF, k, 0, x, y);
    box(x, y, CHIP, 2, CHECKER_REMAP[side][WHITE]);
    box(x, y + 2, CHIP, 1, CHECKER_REMAP[side][SILVER]);
}

void drawDie(int x, int y, uint8_t face, const uint8_t *remap, uint8_t turn, uint8_t lift) {
    const uint8_t *art = DICE + DIE_AT[face - 1];
    int back, s = lifted(lift, DIE, back);
    int px = sx(x) - back / 2, py = sy(y) - back / 2 - zoomed(lift) / 2;
    shadow(sx(x), sy(y) + zoomed(1), DIE, lift);
    if (!turn) { sprite4(art, px, py, remap, s); return; }
    int half = (DIE * s) >> 9;
    spriteRot(art, DIE / 2, DIE / 2, px + half, py + half, turn, s, remap);
}

// The doubling cube: an ivory block with its value in ink, raised by lift.
// A pixel taller than the camera makes it (odd, like the figures' 11 or 5
// rows), and wider too for the 3x5 figures' odd widths, so the value sits
// dead centre.
void drawCube(int x, int y, uint16_t value, uint8_t face, uint8_t lift) {
    int s = zoomed(CUBE) + 1, w = zoom >= 8 ? s - 1 : s, px = sx(x), py = sy(y) - zoomed(lift) / 2;
    shadow(px, sy(y), CUBE, lift);
    fillRound(px, py, w, s, 2, face);
    roundRect(px, py, w, s, 2, INK);
    char num[4];
    fmtInt(num, value);
    if (zoom >= 8) fontText(px + w / 2 - fontWidth(num, 0) / 2, py + s / 2 - 5, num, INK, 0);
    else text35(px + w / 2 - text35Width(num) / 2, py + s / 2 - 2, num, INK);
}

}  // namespace table
