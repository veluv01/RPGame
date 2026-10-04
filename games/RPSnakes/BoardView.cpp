// The board on screen (BoardView.h): squares, ladders and snakes, all
// drawn as spans by a few RAMFUNCs, at any zoom.
#pragma GCC optimize("Os")   // cold code: size over speed (the spans are RAMFUNCs)
#include <string.h>
#include <RPGame.h>
#include "BoardView.h"
#include "Layout.h"
#include "Fx.h"
#include "src/assets/Assets.h"

namespace board {

using namespace layout;

uint8_t zoom = 5;
int16_t camX = CX, camY = CY;
uint8_t light = FELT, dark = FELT_DK;
bool numbers = true;

void setCamera(int x, int y, int below) {
    // Half the view, in world units: 64 across, 59 down at the plain size.
    int hx = 320 / zoom, hy = 295 / zoom;
    if (x < hx) x = hx;
    if (x > 128 - hx) x = 128 - hx;
    if (y > 128 - hy + below * 5 / zoom) y = 128 - hy + below * 5 / zoom;
    if (y < TOP + hy) y = TOP + hy;
    camX = (int16_t)x; camY = (int16_t)y;
}

void centre(uint8_t n, int &x, int &y) {
    x = X0 + col(n) * CELL + CELL / 2;
    y = Y0 + (9 - row(n)) * CELL + CELL / 2;
}

// ---------------------------------------------------------------------------
// Spans. Everything on the board is built from these (from SRAM: code in
// flash runs with 3 wait states).
// ---------------------------------------------------------------------------

// Colour c over [a, b) of a framebuffer row, clipped: ragged nibble ends,
// bytes up to a word boundary, then words. (CHBackgammon's.)
RAMFUNC(brdrun) static void run(uint8_t *row, int a, int b, uint8_t c) {
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

// The rows of the screen from y0 on, each a copy of one of the patterns:
// which[y] picks it.
RAMFUNC(brdrows) static void rows(const uint32_t *pat, const uint8_t *which, int y0) {
    for (int y = y0; y < GFX_H; y++) {
        const uint32_t *s = pat + which[y] * (GFX_FB_STRIDE / 4);
        uint32_t *d = (uint32_t *)(gfx_fb + y * GFX_FB_STRIDE);
        for (int i = 0; i < GFX_FB_STRIDE / 4; i++) d[i] = s[i];
    }
}

// A line w wide from (x0, y0) to (x1, y1), a span to each row it crosses.
RAMFUNC(brdseg) static void seg(int x0, int y0, int x1, int y1, int w, uint8_t c) {
    if (y0 > y1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y1 < 0 || y0 >= GFX_H) return;
    int dy = y1 - y0;
    if (!dy) {
        if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
        run(gfx_fb + y0 * GFX_FB_STRIDE, x0, x1 + w, c);
        return;
    }
    int32_t x = x0 << 8, step = ((x1 - x0) << 8) / dy;
    for (int y = y0; y <= y1; y++, x += step) {
        int a = x >> 8, b = y < y1 ? (x + step) >> 8 : a;
        if (a > b) { int t = a; a = b; b = t; }
        if ((unsigned)y < GFX_H) run(gfx_fb + y * GFX_FB_STRIDE, a, b + w, c);
    }
}

// Round blobs of odd sizes 1..11: each one's row widths, down to the middle.
static const uint8_t DISC[] = {1, 1, 3, 3, 5, 5, 3, 5, 7, 7, 5, 7, 9, 9, 9, 5, 9, 9, 11, 11, 11};
static const uint8_t DISC_AT[] = {0, 1, 3, 6, 10, 15};

RAMFUNC(brddisc) static void disc(int cx, int cy, int d, uint8_t c) {
    if (cx < -6 || cx > GFX_W + 5 || cy < TOP - 6 || cy > GFX_H + 5) return;
    int h = d >> 1;
    const uint8_t *w = DISC + DISC_AT[h];
    for (int j = 0; j < d; j++) {
        int y = cy - h + j, ww = w[j <= h ? j : d - 1 - j];
        if ((unsigned)y < GFX_H) run(gfx_fb + y * GFX_FB_STRIDE, cx - (ww >> 1), cx - (ww >> 1) + ww, c);
    }
}

static void line(int x0, int y0, int x1, int y1, int w, uint8_t c) {
    int dx = x1 - x0, dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx > dy) for (int i = 0; i < w; i++) seg(x0, y0 + i, x1, y1 + i, 1, c);   // shallow: its width is rows
    else seg(x0, y0, x1, y1, w, c);
}

static int isqrt(int32_t v) {
    int r = 0;
    for (int b = 1 << 11; b; b >>= 1)
        if ((int32_t)(r + b) * (r + b) <= v) r += b;
    return r;
}

// The direction of (x, y) in 256ths of a turn, 0 along +x, 64 along +y
// (near enough: straight lines between the octants).
static uint8_t heading(int x, int y) {
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, a;
    if (!ax && !ay) return 0;
    a = ax >= ay ? ay * 32 / ax : 64 - ax * 32 / ay;
    if (x < 0) a = 128 - a;
    if (y < 0) a = -a;
    return (uint8_t)a;
}

// ---------------------------------------------------------------------------
// The board
// ---------------------------------------------------------------------------
static uint32_t pat[5][GFX_FB_STRIDE / 4];       // carpet, wood, inlay, and the two rows of squares
static uint8_t which[GFX_H];

void drawBoard() {
    enum { CARPET, WOODEN, INLAY, ROW_A, ROW_B };
    const int G = 10 * CELL;
    uint8_t *p0 = (uint8_t *)pat[0];
    memset(p0, FELT_DK * 0x11, GFX_FB_STRIDE);
    for (int i = 1; i < 5; i++) {
        uint8_t *p = (uint8_t *)pat[i];
        memcpy(p, pat[i == ROW_B ? INLAY : i - 1], GFX_FB_STRIDE);
        if (i == WOODEN) run(p, sx(X0 - 3), sx(X0 + G + 3), WOOD);
        else if (i == INLAY) run(p, sx(X0 - 1), sx(X0 + G + 1), GOLD);
        else for (int c = 0; c < 10; c++) run(p, sx(X0 + c * CELL), sx(X0 + (c + 1) * CELL), (c + i) & 1 ? dark : light);
    }
    // Which pattern each row of the screen shows.
    int yW0 = sy(Y0 - 3), yG0 = sy(Y0 - 1), yG1 = sy(Y0 + G + 1), yW1 = sy(Y0 + G + 3);
    int r = 0, next = sy(Y0 + CELL);
    for (int y = TOP; y < GFX_H; y++) {
        uint8_t k;
        if (y < yW0 || y >= yW1) k = CARPET;
        else if (y < yG0 || y >= yG1) k = WOODEN;
        else {
            while (y >= next && r < 10) next = sy(Y0 + (++r + 1) * CELL);
            k = y < sy(Y0) || r >= 10 ? INLAY : r & 1 ? ROW_B : ROW_A;
        }
        which[y] = k;
    }
    rows(pat[0], which, TOP);

    // Home: square 100, in the pulsing gold, under its crown.
    int x = sx(X0), y = sy(Y0), s = zoomed(CELL);
    gfx_fillRect(x, y, sx(X0 + CELL) - x, sy(Y0 + CELL) - y, WINE);
    sprite4(CROWN, x + (s - sized(CROWN[0])) / 2, y + (s - sized(CROWN[1])) / 2, RM_ID, zscale());

    // The numbers, printed in the other tone: close up, in each square's
    // corner; at the plain size they fill it.
    if (!big() && (!numbers || zoom != 5)) return;
    for (uint8_t n = 1; n < LAST; n++) {
        int wx = X0 + col(n) * CELL, wy = Y0 + (9 - row(n)) * CELL;
        x = sx(wx); y = sy(wy);
        if (x < -24 || x > GFX_W || y < TOP - 24 || y > GFX_H) continue;
        char buf[4];
        fmtInt(buf, n);
        bool lt = (col(n) + 9 - row(n)) & 1;
        uint8_t c = lt ? dark : light;
        if (big()) text35(x + 2, y + 2, buf, c);
        else text35(x + (CELL + 1 - text35Width(buf)) / 2, y + 3, buf, c);
    }
}

void mark(uint8_t n, uint8_t c) {
    int wx = X0 + col(n) * CELL, wy = Y0 + (9 - row(n)) * CELL;
    int x = sx(wx), y = sy(wy), w = sx(wx + CELL) - x, h = sy(wy + CELL) - y;
    gfx_rect(x, y, w, h, c);
    if (big()) gfx_rect(x + 1, y + 1, w - 2, h - 2, c);
}

// ---------------------------------------------------------------------------
// Ladders: two rails and a rung every four world pixels.
// ---------------------------------------------------------------------------
void drawLadder(uint8_t i, int lit) {
    int fx, fy, tx, ty;
    centre(LINK[i].from, fx, fy);
    centre(LINK[i].to, tx, ty);
    int len = isqrt((int32_t)(tx - fx) * (tx - fx) + (ty - fy) * (ty - fy));
    int m = len / 4;
    fx = sx(fx); fy = sy(fy); tx = sx(tx); ty = sy(ty);
    int dx = tx - fx, dy = ty - fy, L = isqrt((int32_t)dx * dx + dy * dy);
    if (!L) return;
    int half = big() ? 5 : 2, w = big() ? 2 : 1;
    int px = -dy * half / L, py = dx * half / L;      // across the ladder, to a rail
    // Their shadow on the board first, then the rungs, then the rails over
    // the rungs' ends.
    for (int side = -1; side <= 1; side += 2)
        line(fx + side * px + w, fy + side * py + w, tx + side * px + w, ty + side * py + w, w, INK);
    for (int j = 1; j < m; j++) {
        int x = fx + dx * j / m, y = fy + dy * j / m;
        line(x - px, y - py, x + px, y + py, w, j * 256 <= lit * m ? FX_B : big() ? GOLD : WOOD);
    }
    // (At the plain size the rails are a pixel wide: gold, to be seen.)
    for (int side = -1; side <= 1; side += 2)
        line(fx + side * px, fy + side * py, tx + side * px, ty + side * py, w, big() ? WOOD : GOLD);
}

// ---------------------------------------------------------------------------
// Snakes: a chain of round beads along the line from head to tail, pushed
// sideways by a sine that the pose's phase moves along the body. The ends
// stay put.
// ---------------------------------------------------------------------------
struct Curve {
    int32_t hx, hy, dx, dy;         // Q4: the head, and from there to the tail
    int px, py;                     // the unit normal, Q8
    int n, amp;
    uint8_t phase;

    void set(int ahx, int ahy, int atx, int aty, int beads, int amp16, uint8_t ph) {
        hx = ahx; hy = ahy; dx = atx - ahx; dy = aty - ahy;
        int L = isqrt(dx * dx + dy * dy);
        if (!L) L = 1;
        px = -dy * 256 / L; py = dx * 256 / L;
        n = beads < 2 ? 2 : beads;
        amp = amp16; phase = ph;
    }
    // Bead k of n, Q4.
    void at(int k, int &x, int &y) const {
        int env = k < n - k ? k : n - k;
        if (env > 4) env = 4;
        int off = ((amp * fx::isin(k * 18 + phase)) >> 8) * env / 4;
        x = hx + dx * k / n + ((px * off) >> 8);
        y = hy + dy * k / n + ((py * off) >> 8);
    }
};

void chain(int hx, int hy, int tx, int ty, bool large, int beads, int amp16, uint8_t phase, uint8_t c1, uint8_t c2,
           const Pose &p, uint32_t frame) {
    Curve cv;
    cv.set(hx, hy, tx, ty, beads, amp16, phase);
    int n = cv.n, base = large ? 7 : 3, meal = p.bulge < 0 ? -9 : p.bulge * n >> 8;
    // Where the beads are, once: each is drawn twice.
    const int MAXB = 40;
    int16_t bx[MAXB + 1], by[MAXB + 1];
    if (n > MAXB) n = cv.n = MAXB;
    for (int k = 0; k <= n; k++) {
        int x, y;
        cv.at(k, x, y);
        bx[k] = (int16_t)((x + 8) >> 4); by[k] = (int16_t)((y + 8) >> 4);
    }
    // The outline (large) or its shadow on the board (small), then the body, tail first.
    for (int pass = 0; pass < 2; pass++)
        for (int k = n; k >= 1; k--) {
            int d = base, t = k - (n - 2);
            if (t > 0) d -= 2 * t;
            if (d < 1) d = 1;
            bool fed = k >= meal - 1 && k <= meal + 1;
            if (fed) d += 2;
            if (!pass) disc(bx[k] + !large, by[k] + !large, large ? d + 2 : d, INK);
            else disc(bx[k], by[k], d, fed ? p.bulgeColour : (k >> (large ? 1 : 2)) & 1 ? c2 : c1);    // bands, 8 px either way
        }
    // The head, turned the way the neck runs.
    int x0 = bx[0], y0 = by[0];
    uint8_t a = heading(x0 - bx[2], y0 - by[2]);
    if (x0 < -20 || x0 > GFX_W + 20 || y0 < TOP - 20 || y0 > GFX_H + 20) return;
    uint8_t rm[16];
    memcpy(rm, RM_ID, 16);
    rm[FELT_LT] = c1; rm[FELT] = c2;
    int s = large ? 2 : 1;
    if (!p.open && !((frame >> 4) & 3)) {
        // Its tongue, flicking.
        int ux = fx::isin(a + 64), uy = fx::isin(a);
        for (int i = 5; i <= 6; i++) gfx_fillRect(x0 + ((ux * i * s) >> 8), y0 + ((uy * i * s) >> 8), s, s, RED);
    }
    spriteRot(p.open ? SNAKE_HEAD_OPEN : SNAKE_HEAD, 4, 5, x0, y0, (uint8_t)(a + 64), s * 256, rm);
}

// Each snake's two colours.
static const uint8_t SKIN_OF[4][2] = {{FELT_LT, GOLD}, {CYAN, BLUE}, {WHITE, RED}, {RED, GOLD}};

static void ends(uint8_t i, int &hx, int &hy, int &tx, int &ty, int &beads) {
    const Link &l = LINK[LADDERS + i];
    centre(l.from, hx, hy);
    centre(l.to, tx, ty);
    beads = isqrt((int32_t)(tx - hx) * (tx - hx) + (ty - hy) * (ty - hy)) / 2;
}

// World, Q4 -> screen, Q4.
static int sxq(int w) { return ((w - camX * 16 + 5120) * zoom) / 5 - 1024 * zoom + CX * 16; }
static int syq(int w) { return ((w - camY * 16 + 5120) * zoom) / 5 - 1024 * zoom + CY * 16; }

void drawSnake(uint8_t i, const Pose &p, uint32_t frame) {
    int hx, hy, tx, ty, beads;
    ends(i, hx, hy, tx, ty, beads);
    chain(sxq(hx << 4), syq(hy << 4), sxq(tx << 4), syq(ty << 4), big(), beads, p.amp * zoom / 5,
          (uint8_t)(p.phase + i * 37), SKIN_OF[i & 3][0], SKIN_OF[i & 3][1], p, frame);
}

}  // namespace board
