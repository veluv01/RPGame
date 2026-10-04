// Where each tile of the line goes (Layout.h): the arms walk outward over
// the grid of units, turning corners where the felt runs out.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed
#include <string.h>
#include "Layout.h"

namespace layout {

using namespace dom;

Placed at[TILES];
uint8_t n;

// An arm's open end: the point where its middle line leaves its last tile,
// the way it is heading (0 E, 1 S, 2 W, 3 N), half that tile's width across,
// and which tile that is.
struct Cursor { int8_t x, y; uint8_t dir, half, last; };
static Cursor cur[ARMS];
static uint8_t endv[ARMS], lens[ARMS];
static uint8_t occ[UH][(UW + 7) / 8];

static const int8_t DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};
static const uint8_t ARM_DIR[ARMS] = {0, 2, 3, 1};

struct Rect { int8_t x0, y0, x1, y1; };

// From p, L units the way d heads and hw either side of the line.
static Rect along(int x, int y, uint8_t d, int L, int hw) {
    Rect r;
    switch (d) {
        case 0:  r = {(int8_t)x, (int8_t)(y - hw), (int8_t)(x + L), (int8_t)(y + hw)}; break;
        case 1:  r = {(int8_t)(x - hw), (int8_t)y, (int8_t)(x + hw), (int8_t)(y + L)}; break;
        case 2:  r = {(int8_t)(x - L), (int8_t)(y - hw), (int8_t)x, (int8_t)(y + hw)}; break;
        default: r = {(int8_t)(x - hw), (int8_t)(y - L), (int8_t)(x + hw), (int8_t)y}; break;
    }
    return r;
}

static bool inside(const Rect &r) { return r.x0 >= 0 && r.y0 >= 0 && r.x1 <= UW && r.y1 <= UH; }

static bool taken(Rect r) {
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > UW) r.x1 = UW;
    if (r.y1 > UH) r.y1 = UH;
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            if ((occ[y][x >> 3] >> (x & 7)) & 1) return true;
    return false;
}

static void mark(const Rect &r, bool on) {
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++) {
            if ((unsigned)x >= (unsigned)UW || (unsigned)y >= (unsigned)UH) continue;
            if (on) occ[y][x >> 3] |= (uint8_t)(1 << (x & 7));
            else occ[y][x >> 3] &= (uint8_t)~(1 << (x & 7));
        }
}

static Rect rectOf(const Placed &p) {
    Rect r = {(int8_t)p.x, (int8_t)p.y, (int8_t)(p.x + p.w()), (int8_t)(p.y + p.h())};
    return r;
}

// Room for a tile lying over r, laid the way d heads from the tile it joins:
// r itself clear, and a unit clear ahead of it and beside it - but for its
// first unit, where it meets its neighbours at the join. tight: r alone
// (with nowhere better to go, tiles may touch).
static bool room(const Rect &r, uint8_t d, bool tight) {
    if (!inside(r) || taken(r)) return false;
    if (tight) return true;
    Rect g = {(int8_t)(r.x0 - 1), (int8_t)(r.y0 - 1), (int8_t)(r.x1 + 1), (int8_t)(r.y1 + 1)};
    switch (d) {
        case 0:  g.x0 = (int8_t)(r.x0 + 1); break;
        case 1:  g.y0 = (int8_t)(r.y0 + 1); break;
        case 2:  g.x1 = (int8_t)(r.x1 - 1); break;
        default: g.y1 = (int8_t)(r.y1 - 1); break;
    }
    return !taken(g);
}

void reset() {
    n = 0;
    memset(occ, 0, sizeof occ);
}

// A way to lay the next tile of an arm: where, heading which way after it.
struct Try { Rect r; Cursor c; };

static Try straight(const Cursor &c, int L, int hw) {
    Try t;
    t.r = along(c.x, c.y, c.dir, L, hw);
    t.c = c;
    t.c.x = (int8_t)(c.x + L * DX[c.dir]); t.c.y = (int8_t)(c.y + L * DY[c.dir]);
    t.c.half = (uint8_t)hw;
    return t;
}

// Round a corner to head d2: past the end of the last tile (tucked: against
// the side of its last half instead).
static Try corner(const Cursor &c, uint8_t d2, bool tucked) {
    int d = c.dir, back = tucked ? -1 : 1, out = tucked ? c.half : -1;
    int x = c.x + back * DX[d] + out * DX[d2], y = c.y + back * DY[d] + out * DY[d2];
    Try t;
    t.r = along(x, y, d2, 4, 1);
    t.c.x = (int8_t)(x + 4 * DX[d2]); t.c.y = (int8_t)(y + 4 * DY[d2]);
    t.c.dir = d2; t.c.half = 1; t.c.last = c.last;
    return t;
}

static bool lay(uint8_t tile, uint8_t arm, Placed &p, Cursor *next) {
    bool dbl = isDouble(tile);
    if (!n) {
        p.x = dbl ? UW / 2 - 1 : UW / 2 - 2;
        p.y = dbl ? UH / 2 - 2 : UH / 2 - 1;
        p.v = (uint8_t)((dbl << 7) | (lo(tile) << 3) | hi(tile));
        return true;
    }
    const Cursor &c = cur[arm];
    uint8_t in = endv[arm], outv = lo(tile) == in ? hi(tile) : lo(tile);
    // The tile it joins is no obstacle.
    Rect joined = rectOf(at[c.last]);
    mark(joined, false);
    Try best = straight(c, dbl ? 2 : 4, dbl ? 2 : 1);
    // (Never tucked against the first tile when it is a double: its ends are arms.)
    bool ok = false, mayTuck = c.last || !at[0].upright();
    for (uint8_t tight = 0; tight < 2 && !ok; tight++) {
        ok = room(best.r, c.dir, tight);
        if (!ok && dbl) {                                   // a double end-on
            Try t = straight(c, 4, 1);
            if ((ok = room(t.r, c.dir, tight))) best = t;
        }
        for (uint8_t k = 0; k < 4 && !ok; k++) {
            uint8_t d2 = (uint8_t)((c.dir + (k < 2 ? 1 : 3)) & 3);
            if ((k & 1) && !mayTuck) continue;
            Try t = corner(c, d2, k & 1);
            if ((ok = room(t.r, d2, tight))) best = t;
        }
    }
    mark(joined, true);
    if (!ok) {
        // No room anywhere: straight on regardless, kept on the felt.
        int sx = best.r.x1 > UW ? UW - best.r.x1 : best.r.x0 < 0 ? -best.r.x0 : 0;
        int sy = best.r.y1 > UH ? UH - best.r.y1 : best.r.y0 < 0 ? -best.r.y0 : 0;
        best.r.x0 = (int8_t)(best.r.x0 + sx); best.r.x1 = (int8_t)(best.r.x1 + sx);
        best.r.y0 = (int8_t)(best.r.y0 + sy); best.r.y1 = (int8_t)(best.r.y1 + sy);
        best.c.x = (int8_t)(best.c.x + sx); best.c.y = (int8_t)(best.c.y + sy);
    }
    bool up = best.r.y1 - best.r.y0 > best.r.x1 - best.r.x0;
    // The half that matched is nearest the tile it joins.
    bool inFirst = best.c.dir < 2;
    p.x = (uint8_t)best.r.x0; p.y = (uint8_t)best.r.y0;
    p.v = (uint8_t)((up << 7) | ((inFirst ? in : outv) << 3) | (inFirst ? outv : in));
    if (next) *next = best.c;
    return ok;
}

bool plan(uint8_t tile, uint8_t arm, Placed &p) { return lay(tile, arm, p, nullptr); }

bool add(uint8_t tile, uint8_t arm) {
    Placed p;
    Cursor next;
    bool ok = lay(tile, arm, p, &next);
    if (!n) {
        bool dbl = p.upright();
        int cx = UW / 2, cy = UH / 2;
        for (uint8_t a = 0; a < ARMS; a++) {
            uint8_t d = ARM_DIR[a];
            int reach = (a < N) == dbl ? 1 : 2;             // from the middle to that side of the tile
            cur[a].x = (int8_t)(cx + reach * DX[d]); cur[a].y = (int8_t)(cy + reach * DY[d]);
            cur[a].dir = d; cur[a].last = 0;
            cur[a].half = a < N && dbl ? 2 : 1;
            endv[a] = a == W ? lo(tile) : hi(tile);
            lens[a] = 0;
        }
    } else {
        endv[arm] = lo(tile) == endv[arm] ? hi(tile) : lo(tile);
        cur[arm] = next;
        cur[arm].last = n;
        lens[arm]++;
    }
    at[n++] = p;
    mark(rectOf(p), true);
    return ok;
}

uint8_t endOf(uint8_t arm, int &x, int &y, uint8_t &dir, uint8_t &value) {
    x = cur[arm].x; y = cur[arm].y;
    dir = cur[arm].dir;
    value = endv[arm];
    return lens[arm];
}

void rebuild(const Round &r, uint8_t count) {
    reset();
    for (uint8_t i = 0; i < count; i++) add(r.play[i] & 31, r.play[i] >> 5);
}

}  // namespace layout
