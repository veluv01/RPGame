// The isometric 3x3 and 5x5 tables (Iso.h): the room, the slab, the
// felt pads and their inlay, and the pieces standing on them.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
#include <RPGame.h>
#include "Iso.h"
#include "src/assets/Assets.h"

namespace iso {

static const int ROOM_Y0 = 12, ROOM_Y1 = 116;          // between the bars

__attribute__((noinline)) bool fits(const Board &b) {
    return b.w == b.h && (b.n == 9 || b.n == 25) && !(b.flags & (F_ULTIMATE | F_GRAVITY));
}

__attribute__((noinline)) View view(const Board &b, int lift) {
    View v;
    v.n = (uint8_t)b.w;
    v.big = b.n == 9;
    v.hh = v.big ? 10 : 6;                              // 120 px across either way
    v.x0 = 64;
    v.y0 = (int16_t)(38 - lift);
    return v;
}

static inline int hw(const View &v) { return 2 * v.hh; }

__attribute__((noinline)) void cellPos(const View &v, uint8_t cell, int &cx, int &cy) {
    int u = cell % v.n, w = cell / v.n;
    cx = v.x0 + (u - w) * hw(v);
    cy = v.y0 + (u + w + 1) * v.hh;
}

// ---------------------------------------------------------------------------
// Diamonds: every pixel whose centre lies inside, so neighbours meet without
// gaps and every edge is a clean 2:1 staircase (CHChess's iso tiles).
// ---------------------------------------------------------------------------
static inline int halfWidth(int k, int rows) { return k < (rows >> 1) ? 2 * k + 1 : 2 * (rows - 1 - k) + 1; }

static void diamond(int cx, int top, int rows, uint8_t c) {
    for (int k = 0; k < rows; k++) {
        int w = halfWidth(k, rows);
        gfx_hline(cx - w, top + k, 2 * w, c);
    }
}

// A 2:1 line, 2 px a row, from (x, y) down `rows` rows going right (dir 1)
// or left (-1).
static void slope(int x, int y, int rows, int dir, uint8_t c) {
    for (int k = 0; k < rows; k++) gfx_hline(dir > 0 ? x + 2 * k : x - 2 * k - 1, y + k, 2, c);
}

void drawRoom(uint8_t base, uint8_t pool) {
    gfx_fillRect(0, ROOM_Y0, 128, ROOM_Y1 - ROOM_Y0, base);
    // The spotlight's pool on the carpet: an ellipse of dithered blue. Its
    // half widths are worked out once.
    static uint8_t half[ROOM_Y1 - ROOM_Y0];
    static bool ready;
    if (!ready) {
        ready = true;
        for (int y = ROOM_Y0; y < ROOM_Y1; y++) {
            int dy = y - 70, w2 = 64 * 64 - dy * dy * 64 * 64 / (50 * 50), w = 0;
            while ((w + 1) * (w + 1) <= w2) w++;
            half[y - ROOM_Y0] = (uint8_t)w;
        }
    }
    for (int y = ROOM_Y0; y < ROOM_Y1; y++) {
        int w = half[y - ROOM_Y0];
        if (w) dither(64 - w, y, 2 * w, 1, pool, 0);
    }
}

// The slab's two near faces, row by row (CHChess's leftFace/rightFace): the
// face's top follows the board's edge staircase exactly.
static void faces(const View &v, int depth, int shift, uint8_t lc, uint8_t rc, bool trim) {
    int span = v.n * hw(v), rows = v.n * v.hh;
    int xl = v.x0 - span, yl = v.y0 + rows;             // left corner
    int xb = v.x0, yb = v.y0 + 2 * rows;                // near corner
    for (int y = yl; y < yb + depth; y++) {
        int b = xl + 2 * (y - yl), a = xl + 2 * (y - depth - yl) + 1;
        if (a < xl) a = xl;
        if (b > xb - 1) b = xb - 1;
        if (a <= b) gfx_hline(a + shift, y, b - a + 1, lc);
        // The right face: from the near corner up to the right corner.
        int a2 = xb + 2 * (yb - y) - 1, b2 = xb + 2 * (yb - y + depth) - 2;
        if (a2 < xb) a2 = xb;
        if (b2 > xb + span - 1) b2 = xb + span - 1;
        if (a2 <= b2) gfx_hline(a2 + shift, y, b2 - a2 + 1, rc);
        if (trim && y < yb) {                           // gold along the top edge
            if (a <= b) gfx_hline(b - 1, y, 2, GOLD);
            if (a2 <= b2 && y > yl - rows) gfx_hline(a2, y, 2, GOLD);
        }
    }
}

void drawBoard(const View &v) {
    int rows = 2 * v.n * v.hh, depth = v.big ? 7 : 6;
    // Its shadow on the carpet, then the slab: walnut on the left, the
    // right face in shadow, and a darker band of wood grain along each.
    faces(v, depth + 4, 3, INK, INK, false);
    faces(v, depth, 0, WOOD, WINE, true);
    faces(v, 2, 0, WINE, INK, false);
    // The top: wood, the felt pads sunk into it, gold inlaid between.
    diamond(v.x0, v.y0, rows, WOOD);
    int inset = v.big ? 2 : 1;
    for (uint8_t c = 0; c < v.n * v.n; c++) padFill(v, c, inset, FELT);
    // Each pad's far edges in shadow (the wood rim stands over it), its near
    // edges catching the light.
    for (uint8_t c = 0; c < v.n * v.n; c++) {
        int cx, cy;
        cellPos(v, c, cx, cy);
        int top = cy - v.hh + inset, h = 2 * (v.hh - inset);
        for (int k = 0; k < h; k++) {
            int w = halfWidth(k, h);
            if (k < h / 2) {
                gfx_hline(cx - w, top + k, 2, FELT_DK);
                gfx_hline(cx + w - 2, top + k, 2, FELT_DK);
            } else if (k >= h / 2 + 1 && v.big) gfx_hline(cx + w - 2, top + k, 2, FELT_LT);
        }
    }
    // The inlay: every lattice line, gold.
    int H = hw(v);
    for (int i = 0; i <= v.n; i++) {
        slope(v.x0 - i * H, v.y0 + i * v.hh, v.n * v.hh, 1, GOLD);
        slope(v.x0 + i * H, v.y0 + i * v.hh, v.n * v.hh, -1, GOLD);
    }
}

void padFill(const View &v, uint8_t cell, uint8_t inset, uint8_t c) {
    int cx, cy;
    cellPos(v, cell, cx, cy);
    int h = 2 * (v.hh - inset);
    for (int k = 0; k < h; k++) {
        int w = halfWidth(k, h);
        gfx_hline(cx - w, cy - v.hh + inset + k, 2 * w, c);
    }
}

void padBorder(const View &v, uint8_t cell, uint8_t inset, uint8_t c, uint8_t c2, uint8_t phase) {
    int cx, cy;
    cellPos(v, cell, cx, cy);
    int h = 2 * (v.hh - inset), top = cy - v.hh + inset;
    for (int k = 0; k < h; k++) {
        int w = halfWidth(k, h);
        uint8_t cr = (((k + phase) >> 1) & 1) ? c2 : c;
        uint8_t cl = (((2 * h - 1 - k + phase) >> 1) & 1) ? c2 : c;
        if (w <= 2) { gfx_hline(cx - w, top + k, 2 * w, cr); continue; }
        gfx_hline(cx + w - 2, top + k, 2, cr);
        gfx_hline(cx - w, top + k, 2, cl);
    }
}

// ---------------------------------------------------------------------------
// Pieces
// ---------------------------------------------------------------------------
const uint8_t *art(bool big, uint8_t sym) {
    if (sym == 1) return big ? X_L : X_S;
    return big ? O_L : O_S;
}

const uint8_t *chipArt(bool big, uint8_t level) {
    static const uint8_t *const L[3] = {CHIP0_L, CHIP1_L, CHIP2_L}, *const S[3] = {CHIP0_S, CHIP1_S, CHIP2_S};
    return (big ? L : S)[level > 2 ? 2 : level];
}

// The base centre follows the row spans (tools/assets.py appends it).
__attribute__((noinline)) static const uint8_t *anchor(const uint8_t *a) {
    const uint8_t *p = a + 2;
    for (uint8_t r = 0; r < a[1]; r++) p += 1 + p[0];
    return p;
}

int height(const uint8_t *a) { return anchor(a)[1]; }

// Smaller and fainter the higher the piece is.
void shadow(const uint8_t *a, int x, int y, int lift) {
    int rx = a[0] * 3 / 8 - lift / 6, ry = rx / 2;
    if (lift > 8) dither(x + 1 - rx, y - ry / 2, 2 * rx, ry + 1, FELT_DK, 0);
    else gfx_fillEllipse(x + 1, y, rx, ry > 1 ? ry : 1, FELT_DK);
}

void stand(const uint8_t *a, int x, int y, int lift, const uint8_t *remap, bool withShadow, bool mirror) {
    const uint8_t *p = anchor(a);
    if (withShadow) shadow(a, x, y, lift);
    sprite4(a, x - (mirror ? a[0] - 1 - p[0] : p[0]), y - p[1] - lift, remap, 256, mirror ? SPR_FLIP_H : 0);
}

const uint8_t *spin(const uint8_t *a, uint8_t step, bool &mirror) {
    static const uint8_t *const XS[3] = {X_L, X_L1, X_L2}, *const OS[3] = {O_L, O_L1, O_L2};
    const uint8_t *const *t = a == X_L ? XS : (a == O_L ? OS : nullptr);
    mirror = step == 3;
    if (!t) return a;
    return t[mirror ? 1 : step];
}

}  // namespace iso
