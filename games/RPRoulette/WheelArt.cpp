#pragma GCC optimize("Os")   // cold code: size over speed (the ring loop runs from SRAM)
#include <RPGame.h>
#include "WheelArt.h"
#include "src/assets/WheelMap.h"

namespace wheelart {

// ---------------------------------------------------------------------------
// The rotor: one quadrant's angle map (tools/wheel.py), one byte a pixel,
// band << 6 | q, mirrored into four quadrants through four 256-byte colour
// tables built each frame for the rotor's angle. A pixel costs a map read
// and a table read, and the frets and dividers come out of the table.
// ---------------------------------------------------------------------------

// Pocket colours by wheel index: European 0 green, odd red, even black;
// American 0 and 19 (the 00) green, odd black, even red. 0 green, 1 red, 2 black.
static uint8_t cls(uint8_t p, uint8_t n) {
    if (!p || (n == 38 && p == 19)) return 0;
    return (uint8_t)(((p & 1) == (n == 37)) ? 1 : 2);
}

// The 1 KB table in the chunk scratch: blocks right-down, left-down,
// left-up, right-up; in each, 64 entries per band (floor, number ring,
// mark, gold). Colours are sampled at step centres, so a fret is always
// exactly 2 steps wide.
static void buildLut(uint8_t *lut, uint32_t rho, uint8_t n, uint8_t hiP, uint8_t hiC) {
    static const uint8_t RING_C[3] = {FELT, RED, INK}, FLOOR_C[3] = {FELT_DK, WINE, INK};
    const uint32_t T = (uint32_t)n << 16, step = (uint32_t)n << 8;
    const uint32_t FR = (uint32_t)n << 8, DV = (uint32_t)n << 7, MK = (uint32_t)n << 7;
    uint32_t u = ((uint32_t)n << 7) + T - (rho % T);    // centre of step 0, in the rotor's frame
    if (u >= T) u -= T;
    for (int s = 0; s < 256; s++) {
        uint8_t p = (uint8_t)(u >> 16);
        uint32_t f = u & 0xFFFF;
        uint8_t k = cls(p, n);
        uint8_t ring = RING_C[k], flo = FLOOR_C[k];
        if (p == hiP) ring = flo = hiC;
        uint8_t c0 = (f < FR || f >= 0x10000 - FR) ? SILVER : flo;
        uint8_t c1 = (f < DV || f >= 0x10000 - DV) ? GOLD : ring;
        uint8_t c2 = (f - (0x8000 - MK) < 2 * MK) ? WHITE : c1;
        int q = s & 63;
        if (s & 64) q = 63 - q;                          // LD: 127-s, RU: 255-s
        uint8_t *d = lut + ((s >> 6) << 8);
        d[q] = c0; d[64 + q] = c1; d[128 + q] = c2; d[192 + q] = GOLD;
        u += step;
        if (u >= T) u -= T;
    }
}

// The ring, written straight into the framebuffer in [x0, x1) x [y0, y1)
// (x0, x1 even; cx even, so pixel pairs line up). From SRAM, no calls.
RAMFUNC(wheelring) static void ring(int cx, int cy, const uint8_t *lut, int x0, int x1, int y0, int y1) {
    const uint8_t *m = WHEEL_MAP;
    for (int j = 0; j < WHEEL_ROWS; j++) {
        int a = WHEEL_SPAN[j][0], b = WHEEL_SPAN[j][1];
        const uint8_t *rm = m - a;                       // rm[i], i in [a, b)
        m += b - a;
        for (int up = 0; up < 2; up++) {
            if (up && !j) break;
            int y = up ? cy - j : cy + j;
            if (y < y0 || y >= y1) continue;
            uint8_t *fb = gfx_fb + y * GFX_FB_STRIDE;
            const uint8_t *L = lut + (up ? 3 : 0) * 256;   // right half: RU / RD
            int i = a > x0 - cx ? a : x0 - cx, e = b < x1 - cx ? b : x1 - cx;
            if (i < e) {
                uint8_t *p = fb + ((cx + i) >> 1);
                if (i & 1) { *p = (uint8_t)((*p & 0x0F) | (L[rm[i]] << 4)); p++; i++; }   // odd x: high nibble
                for (; i + 1 < e; i += 2) *p++ = (uint8_t)(L[rm[i]] | (L[rm[i + 1]] << 4));
                if (i < e) *p = (uint8_t)((*p & 0xF0) | L[rm[i]]);
            }
            L = lut + (up ? 2 : 1) * 256;                   // left half: LU / LD
            i = a ? a : 1;
            if (i < cx - x1 + 1) i = cx - x1 + 1;
            e = b < cx - x0 + 1 ? b : cx - x0 + 1;
            if (i < e) {
                uint8_t *p = fb + ((cx - i) >> 1);
                if (!(i & 1)) { *p = (uint8_t)((*p & 0xF0) | L[rm[i]]); p--; i++; }       // even x: low nibble
                for (; i + 1 < e; i += 2) *p-- = (uint8_t)(L[rm[i + 1]] | (L[rm[i]] << 4));
                if (i < e) *p = (uint8_t)((*p & 0x0F) | (L[rm[i]] << 4));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The bowl, cone and turret
// ---------------------------------------------------------------------------
// Outermost first: centre offset (dx, dy), radius R (ry = R/2), fill, edge.
struct Layer { int8_t dx, dy; uint8_t R, fill, edge; };
static const Layer LAYERS[7] = {
    {2, 5, 60, FELT_DK, 0xFF},  // shadow
    {0, 3, 60, WINE, INK},      // side face
    {0, -4, 60, WOOD, GOLD},    // rim top
    {0, -4, 56, INK, 0xFF},     // bowl wall
    {0, -3, 54, NAVY, 0xFF},    // ball track
    {0, -2, 50, WOOD, 0xFF},    // apron (the deflectors' ring)
    {0, -1, 46, INK, 0xFF},     // the gap under the rotor
};

static const int R_DEFL = 48, CONE_R = 26, INLAY_R = 18, ARM_LEN = 12;

// Deflector k (0..7) at (2k+1)/16 of a turn on the apron; the near ones a
// row lower, onto the strip of apron the rotor leaves visible.
static void deflector(int cx, int cy, int k, bool lit) {
    int a = (2 * k + 1) * 16, s = fx::isin(a);
    int x = cx + ((R_DEFL * fx::isin(a + 64) + 128) >> 8);
    int y = cy - 2 + (((R_DEFL / 2) * s + 128) >> 8) + (s > 0);
    uint8_t c = lit ? WHITE : SILVER;
    gfx_hline(x - 1, y, 3, c);
    if (!(k & 1)) { gfx_pixel(x, y - 1, c); gfx_pixel(x, y + 1, c); }   // a plus, then a bar
    gfx_pixel(x, y, WHITE);
}

static void turret(int cx, int cy, uint8_t a8) {
    gfx_fillEllipse(cx, cy - 2, 8, 4, GOLD);
    gfx_ellipse(cx, cy - 2, 8, 4, WINE);
    gfx_fillRect(cx - 2, cy - 9, 5, 7, GOLD);
    gfx_vline(cx - 1, cy - 9, 7, FX_B);                 // highlight
    gfx_vline(cx + 2, cy - 9, 7, WOOD);                 // shade
    int hub = cy - 9;
    // The four arms, far ones first, each with a shadow line under it.
    int ex[4], ey[4], c[4], s[4];
    uint8_t order[4] = {0, 1, 2, 3};
    for (int k = 0; k < 4; k++) {
        int a = a8 + 64 * k;
        c[k] = fx::isin(a + 64); s[k] = fx::isin(a);
        ex[k] = cx + ((ARM_LEN * c[k] + 128) >> 8);
        ey[k] = hub + (((ARM_LEN / 2) * s[k] + 128) >> 8);
    }
    for (int i = 1; i < 4; i++)
        for (int j = i; j > 0 && s[order[j - 1]] > s[order[j]]; j--) { uint8_t o = order[j]; order[j] = order[j - 1]; order[j - 1] = o; }
    for (int i = 0; i < 4; i++) {
        int k = order[i];
        gfx_line(cx, hub + 1, ex[k], ey[k] + 1, INK);
        gfx_line(cx, hub, ex[k], ey[k], GOLD);
        gfx_fillRect(c[k] >= 0 ? ex[k] : ex[k] - 1, s[k] >= 0 ? ey[k] : ey[k] - 1, 2, 2, SILVER);
    }
    gfx_fillEllipse(cx, hub, 3, 1, FX_B);               // the cap shimmers for free
    gfx_pixel(cx, cy - 12, WHITE);                      // finial
    gfx_pixel(cx, cy - 11, SILVER);
}

// ---------------------------------------------------------------------------
// The ball
// ---------------------------------------------------------------------------
// 4x4, WHITE with SILVER lower right, in a 1 px INK rim (without it the ball
// is lost among the WHITE marks and SILVER frets). Rows of 6, 15 = clear.
static const uint8_t BALL[6][6] = {
    {15, 15, INK, INK, 15, 15},
    {15, INK, WHITE, WHITE, INK, 15},
    {INK, WHITE, WHITE, WHITE, SILVER, INK},
    {INK, WHITE, WHITE, SILVER, SILVER, INK},
    {15, INK, SILVER, SILVER, INK, 15},
    {15, 15, INK, INK, 15, 15},
};

// Section 3.4: screen (x, y) of the ball, its shadow row ys, and sin.
static void project(int cx, int cy, uint32_t angle, int r, int z, uint8_t n, bool snap,
                    int &x, int &y, int &ys, int &sn) {
    uint32_t a16 = (angle % ((uint32_t)n << 16)) / n;   // turn-Q16
    if (snap) a16 = (a16 & ~255u) + 128;                 // settled: ride the step grid
    int a8 = (int)(a16 >> 8), f = (int)(a16 & 255);
    int s = fx::isin(a8) + (((fx::isin(a8 + 1) - fx::isin(a8)) * f) >> 8);
    int c = fx::isin(a8 + 64) + (((fx::isin(a8 + 65) - fx::isin(a8 + 64)) * f) >> 8);
    int o = r >= 52 << 8 ? -768 : r > 46 << 8 ? -256 - (r - (46 << 8)) * 2 / 6 : 0;
    x = cx + ((r * c / 256 + 128) >> 8);
    ys = cy + (((r * s >> 9) + o + 128) >> 8);
    y = ys - ((z * 7) >> 11);
    sn = s;
}

void ballXY(int cx, int cy, const BallView &b, uint8_t n, int &x, int &y) {
    int ys, s;
    project(cx, cy, b.angle, b.r, b.z, n, b.snap, x, y, ys, s);
}

void ballRows(int cx, int cy, const BallView &b, uint8_t n, int &lo, int &hi) {
    int x, y, ys, s;
    project(cx, cy, b.angle, b.r, b.z, n, b.snap, x, y, ys, s);
    lo = y - 3; hi = (ys > y ? ys : y) + 2;
    for (int k = 1; k <= 3; k++) {                      // the trail
        project(cx, cy, (uint32_t)(b.angle - (k * b.w) / 4), b.r, b.z, n, false, x, y, ys, s);
        if (y - 1 < lo) lo = y - 1;
        if (y + 1 > hi) hi = y + 1;
    }
}

static void drawBall(int cx, int cy, const BallView &b, uint8_t n, int side) {
    int x, y, ys, s;
    project(cx, cy, b.angle, b.r, b.z, n, b.snap, x, y, ys, s);
    if ((s < 0) != (side < 0)) return;                  // side -1: far half, +1: near half
    int32_t w = b.w < 0 ? -b.w : b.w;
    if (w > 1311 * (int32_t)n) {                        // faster than 1.2 rev/s: three ghosts behind
        static const uint8_t GW[3] = {2, 2, 1}, GH[3] = {2, 1, 1}, GC[3] = {WHITE, SILVER, SILVER};
        for (int k = 1; k <= 3; k++) {
            int gx, gy, gys, gs;
            project(cx, cy, (uint32_t)(b.angle - (k * b.w) / 4), b.r, b.z, n, false, gx, gy, gys, gs);
            gfx_fillRect(gx - 1, gy - 1, GW[k - 1], GH[k - 1], GC[k - 1]);
        }
    }
    if (b.z > 0) gfx_hline(x - 2, ys, 3, INK);          // the hop's shadow on the surface
    for (int j = 0; j < 6; j++)
        for (int i = 0; i < 6; i++)
            if (BALL[j][i] != 15) gfx_pixel(x - 3 + i, y - 3 + j, BALL[j][i]);
}

// ---------------------------------------------------------------------------
void draw(int cx, int cy, uint32_t rho, uint8_t n, uint8_t hi, uint8_t hiColour,
          int x0, int x1, int y0, int y1, int by0, int by1, bool felt, const BallView *ball, uint8_t flash) {
    if (by0 < y0) by0 = y0;
    if (by1 > y1) by1 = y1;
    if (by0 < by1) {
        gfx_setClip(x0, by0, x1 - x0, by1 - by0);
        if (felt) {                                     // CHBlackjack's felt: darker at the edges
            gfx_fillRect(0, by0, 128, by1 - by0, FELT);
            int sx = cx - 64;                           // the scene's left edge (it slides in the whip)
            if (sx >= x0 && sx + 3 <= x1) dither(sx, by0, 3, by1 - by0, FELT_DK, 0);   // dither() ignores the clip
            if (sx + 125 >= x0 && sx + 128 <= x1) dither(sx + 125, by0, 3, by1 - by0, FELT_DK, 1);
            gfx_hline(0, y0, 128, FELT_DK);
        }
        for (const Layer &l : LAYERS) {
            gfx_fillEllipse(cx + l.dx, cy + l.dy, l.R, l.R / 2, l.fill);
            if (l.edge != 0xFF) gfx_ellipse(cx + l.dx, cy + l.dy, l.R, l.R / 2, l.edge);
        }
    }
    gfx_setClip(x0, y0, x1 - x0, y1 - y0);
    uint8_t *lut = gfx_chunkScratch();
    buildLut(lut, rho, n, hi, hiColour);
    int rx0 = x0 < 0 ? 0 : x0, rx1 = x1 > GFX_W ? GFX_W : x1;
    ring(cx, cy, lut, rx0 & ~1, (rx1 + 1) & ~1, y0, y1);
    for (int k = 0; k < 8; k++) deflector(cx, cy, k, (flash >> k) & 1);
    gfx_fillEllipse(cx, cy, CONE_R, CONE_R / 2, WOOD);
    gfx_ellipse(cx, cy, CONE_R, CONE_R / 2, GOLD);     // the inner lip
    gfx_ellipse(cx, cy - 1, INLAY_R, INLAY_R / 2, WINE);
    if (ball) drawBall(cx, cy, *ball, n, -1);          // far side: behind the turret
    uint32_t a16 = (rho % ((uint32_t)n << 16)) / n;
    turret(cx, cy, (uint8_t)(a16 >> 8));
    if (ball) drawBall(cx, cy, *ball, n, 1);
    gfx_resetClip();
}

}  // namespace wheelart
