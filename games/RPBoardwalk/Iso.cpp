// The isometric board (Iso.h): tiles as pixel-exact diamonds, colour
// bands, decals, the slab, the carpet and the name on the felt.
#pragma GCC optimize("Os")   // cold code: size over speed (the span fills are RPGfx's, in SRAM)
#include <string.h>
#include <RPGame.h>
#include "Iso.h"
#include "Tiles.h"
#include "src/assets/Assets.h"

namespace iso {

Cam cam = {0, 110};
uint8_t tileH = 5;
uint8_t lightTile = WHITE, darkTile = SILVER;
bool solidGroups;

// A tile's lattice box: its far cell (u, v) and its size in cells.
static void box(uint8_t t, int &u, int &v, int &du, int &dv) {
    int k = t % 10, far = k ? R + k - 1 : 0, near = N - R - k;      // along its side, from either end
    du = dv = R;
    switch (t / 10) {
        case 0:  u = near; v = N - R; if (k) du = 1; break;
        case 1:  u = 0; v = near; if (k) dv = 1; break;
        case 2:  u = far; v = 0; if (k) du = 1; break;
        default: u = N - R; v = far; if (k) dv = 1; break;
    }
}

void place(uint8_t t, int along, int out, int &x, int &y) {
    int u, v, du, dv, a, b;
    box(t, u, v, du, dv);
    switch (side(t)) {
        case 0:  a = (u + du) * 8 - along; b = v * 8 + out; break;
        case 1:  b = (v + dv) * 8 - along; a = (u + du) * 8 - out; break;
        case 2:  a = u * 8 + along; b = (v + dv) * 8 - out; break;
        default: b = v * 8 + along; a = u * 8 + out; break;
    }
    x = (a - b) * hw() / 8;
    y = (a + b) * hh() / 8;
}

void worldOf(uint8_t t, int &x, int &y) { place(t, corner(t) ? 8 : 4, 8, x, y); }

// ---------------------------------------------------------------------------
// Cells: rows 2, 6, 10, ... 2 wide - every pixel whose centre lies inside
// the diamond, so neighbours meet without gaps or overlaps and every edge is
// a clean 2:1 staircase.
// ---------------------------------------------------------------------------
static inline int halfWidth(int k, int th) { return k < (th >> 1) ? 2 * k + 1 : 2 * (th - 1 - k) + 1; }   // th > 0

// A diamond `cells` cells a side with its top corner at screen (x0, y0);
// with a trim colour, a darker edge and a line `in` px inside it too.
static void diamond(int x0, int y0, int cells, uint8_t c, int in = 0, uint8_t trim = 0) {
    int rows = cells * 2 * hh();
    for (int k = 0; k < rows; k++) {
        int y = y0 + k;
        if ((unsigned)y >= GFX_H) continue;
        int w = halfWidth(k, rows);
        if (!in) { gfx_hline(x0 - w, y, 2 * w, c); continue; }
        gfx_hline(x0 - w, y, 2 * w, FELT_DK);               // the edge: 2 px left showing each end
        gfx_hline(x0 - w + 2, y, 2 * w - 4, c);
        if (k < in || k >= rows - in) continue;
        w -= 2 * in;
        if (w <= 2) { gfx_hline(x0 - w, y, 2 * w, trim); continue; }
        gfx_hline(x0 - w, y, 2, trim);
        gfx_hline(x0 + w - 2, y, 2, trim);
    }
}

// A cell's top corner on screen. False if the cell is off it.
static bool cellAt(int u, int v, int &cx, int &top) {
    cx = (u - v) * hw() - cam.x + CX;
    top = (u + v) * hh() - cam.y + CY;
    return cx + hw() > 0 && cx - hw() < GFX_W && top + 2 * hh() > 0 && top < GFX_H;
}

// A strip d px across along one edge of a cell (0 upper right, 1 lower
// right, 2 lower left, 3 upper left): the diamond's rows cut off parallel
// to that edge. d = 0: the whole cell. With c2 != c, dithered. ruled: an
// ink line along the strip's inside edge.
static void strip(int u, int v, uint8_t edge, int d, uint8_t c, uint8_t c2, bool ruled = false) {
    int cx, top, th = 2 * hh();
    if (!cellAt(u, v, cx, top)) return;
    for (int k = 0; k < th; k++) {
        int y = top + k;
        if ((unsigned)y >= GFX_H) continue;
        int w = halfWidth(k, th), a = cx - w, b = cx + w;
        if (d) {
            int up = 2 * k + 1, dn = 2 * (th - 1 - k) + 1;  // how far the upper and the lower edges reach on this row
            int e = (edge == 0 || edge == 3 ? up : dn) - d;
            int rule = edge < 2 ? cx + e - 2 : cx - e;      // just inside the strip's inner edge...
            if (edge < 2) { if (cx + e > a) a = cx + e; }
            else          { if (cx - e < b) b = cx - e; }
            if (ruled && rule >= cx - w && rule + 2 <= cx + w) gfx_hline(rule, y, 2, INK);     // ... and inside this cell
            if (a >= b) continue;
        }
        if (c2 != 0xFF) gfx_hline(a, y, b - a, c);
        if (c2 != c) dither(a, y, b - a, 1, c2 == 0xFF ? c : c2, 0);
    }
}

// The cell of a tile on the board's inside edge (inner), or on its rim.
static void edgeCell(uint8_t t, bool inner, int &u, int &v) {
    int du, dv;
    box(t, u, v, du, dv);
    uint8_t s = side(t);
    if (s == 0 || s == 2) { if (inner == (s == 2)) v += dv - 1; }
    else                  { if (inner == (s == 1)) u += du - 1; }
}

// All of a tile's cells: solid, or (c2 = 0xFF) the checkerboard of pixels
// where x + y is even.
static void cells(uint8_t t, uint8_t c, uint8_t c2) {
    int u, v, du, dv;
    box(t, u, v, du, dv);
    for (int i = 0; i < du; i++)
        for (int j = 0; j < dv; j++) strip(u + i, v + j, 0, 0, c, c2);
}

void tileTint(uint8_t t, uint8_t c, bool solid) { cells(t, c, solid ? c : 0xFF); }

void rim(uint8_t t, uint8_t c) {
    int u, v;
    edgeCell(t, false, u, v);
    strip(u, v, (uint8_t)((side(t) + 2) & 3), zoomed(4), c, c);
}

// ---------------------------------------------------------------------------
// Colour bands and decals
// ---------------------------------------------------------------------------
void groupColour(uint8_t g, uint8_t &c, uint8_t &c2) {
    static const uint8_t SOLID[8] = {WOOD, CYAN, WINE, SKIN, RED, GOLD, FELT_LT, BLUE};
    c = c2 = SOLID[g];
    if (solidGroups) return;
    if (g == 2) { c = RED; c2 = WHITE; }        // pink
    if (g == 3) { c = RED; c2 = GOLD; }         // orange
}

static const uint8_t RM_GOTO[16] = {0, 1, 2, 3, 4, RED, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};     // the cell (SILVER), in red
static const uint8_t RM_CHEST[16] = {0, 1, 2, 3, 4, 5, 6, 7, BLUE, 9, 10, 11, 12, 13, 14, 15};

// A sprite centred on a world point, at scale s.
static void centred(const uint8_t *spr, int x, int y, const uint8_t *remap, int s) {
    sprite4(spr, toScreenX(x) - ((spr[0] * s) >> 9), toScreenY(y) - ((spr[1] * s) >> 9), remap, s);
}

const uint8_t *decalOf(uint8_t t, const uint8_t *&remap) {
    using namespace board;
    remap = RM_ID;
    switch (type(t)) {
        case STREET:  return nullptr;
        case GO:      return ICON_GO;
        case RAIL:    return ICON_RAIL;
        case UTIL:    return t < 20 ? ICON_BULB : ICON_WATER;
        case CHANCE:  return ICON_CHANCE;
        case CHEST:   return ICON_CHEST;
        case TAX:     return ICON_TAX;
        case PARKING: return ICON_PARKING;
        case GOTOJAIL: remap = RM_GOTO;
        default:      return ICON_JAIL;
    }
}

// ---------------------------------------------------------------------------
// Table and board
// ---------------------------------------------------------------------------
// The carpet, and the stacks of chips standing about on it (lattice
// positions, in eighths of a cell).
void drawTable() {
    static const int8_t AT[6][2] = {{36, 122}, {122, 36}, {74, 124}, {124, 74}, {-18, 40}, {40, -18}};
    static const uint8_t COLOUR[6] = {RED, BLUE, FELT_LT, INK, GOLD, RED};
    gfx_clear(NAVY);
    uint8_t rm[16];
    memcpy(rm, RM_ID, 16);
    for (int i = 0; i < 6; i++) {
        rm[RED] = COLOUR[i];
        centred(CHIPS, (AT[i][0] - AT[i][1]) * hw() / 8, (AT[i][0] + AT[i][1]) * hh() / 8, rm, zscale());
    }
}

// The name across the felt, on a plaque: the title's lettering (LOGO, a
// bit a pixel), a filled run at a time. It lies along the board's long
// diagonal, so on screen it is level. It is painted on the board, not
// standing on it, so it grows with the board through every step of a zoom.
static void wordmark() {
    int cx = toScreenX(0), cy = toScreenY(N * hh());
    int pw = zoomed(LOGO_W + 10), ph = zoomed(LOGO_H + 6);
    if (cy + ph < 10 || cy - ph >= GFX_H) return;
    fillRound(cx - pw / 2, cy - ph / 2, pw, ph, 2, NAVY);
    roundRect(cx - pw / 2, cy - ph / 2, pw, ph, 2, GOLD);
    int x0 = cx - zoomed(LOGO_W) / 2, y0 = cy - zoomed(LOGO_H) / 2;
    const uint8_t *row = LOGO;
    for (int j = 0; j < LOGO_H; j++, row += (LOGO_W + 7) / 8) {
        int y = y0 + zoomed(j), h = zoomed(j + 1) - zoomed(j);
        if (y + h <= 10 || y >= GFX_H) continue;
        for (int i = 0; i < LOGO_W; i++) {
            int a = i;
            while (i < LOGO_W && (row[i >> 3] & (0x80 >> (i & 7)))) i++;
            if (i > a) gfx_fillRect(x0 + zoomed(a), y, zoomed(i) - zoomed(a), h, GOLD);
        }
    }
}

// The two slab faces below the near edges, row by row: the face's top
// boundary is exactly the tiles' edge staircase. left: the edge from the
// left corner (xl, yl) down to the near corner; right: from the near corner
// up to the right corner.
static void leftFace(int xl, int yl, int depth, int shift, uint8_t c, uint8_t trim) {
    int xr = xl + N * hw();                      // x of the near corner
    for (int y = yl; y < yl + N * hh() + depth; y++) {
        if ((unsigned)y >= GFX_H) continue;
        int b = xl + 2 * (y - yl);               // last face pixel on this row
        int a = xl + 2 * (y - depth - yl) + 1;
        if (a < xl) a = xl;
        if (b > xr - 1) b = xr - 1;
        a += shift; b += shift;
        if (a > b) continue;
        gfx_hline(a, y, b - a + 1, c);
        if (trim != 0xFF && y < yl + N * hh()) gfx_hline(b - 1, y, 2, trim);
    }
}

static void rightFace(int xb, int yb, int depth, int shift, uint8_t c, uint8_t trim) {
    int xr = xb + N * hw();
    int yr = yb - N * hh();
    for (int y = yr; y < yb + depth; y++) {
        if ((unsigned)y >= GFX_H) continue;
        int a = xb + 2 * (yb - y) - 1;           // first face pixel on this row
        int b = xb + 2 * (yb - y + depth) - 2;
        if (a < xb) a = xb;
        if (b > xr - 1) b = xr - 1;
        a += shift; b += shift;
        if (a > b) continue;
        gfx_hline(a, y, b - a + 1, c);
        if (trim != 0xFF && y >= yr + 1 && y <= yb) gfx_hline(a, y, 2, trim);
    }
}

void drawBoard() {
    using namespace board;
    // Corners in screen space: left (Jail), near (GO), far (Free Parking).
    int xl = -N * hw() - cam.x + CX, yl = N * hh() - cam.y + CY;
    int xb = -cam.x + CX,            yb = 2 * N * hh() - cam.y + CY;
    int yf = -cam.y + CY;
    int s = slab(), z = (tileH + 2) / 5;

    // Contact shadow on the carpet, then the slab.
    leftFace(xl, yl, s + 2 + z, 1 + z, INK, 0xFF);
    rightFace(xb, yb, s + 2 + z, 1 + z, INK, 0xFF);
    leftFace(xl, yl, s, 0, WINE, GOLD);
    rightFace(xb, yb, s, 0, INK, GOLD);

    // The ring in its lighter tone, the felt in the middle with its gold
    // inlay, then every other tile in the darker one: neighbours differ.
    diamond(xb, yf, N, lightTile);
    diamond(xb, yf + 2 * R * hh(), N - 2 * R, FELT, zoomed(3), GOLD);
    wordmark();
    // The two decks on the felt either side of it, each under its sign.
    for (int d = 0; d < 2; d++) {
        int x = (d ? 2 : -2) * hw(), y = (d ? N + 5 : N - 5) * hh();
        centred(CARD_DECK, x, y, d ? RM_CHEST : RM_ID, zscale());
        centred(d ? ICON_CHEST : ICON_CHANCE, x, y - sized(9), RM_ID, zscale());
    }

    for (uint8_t t = 0; t < TILES; t++) {
        if (t & 1) cells(t, darkTile, darkTile);
        const uint8_t *rm, *icon = decalOf(t, rm);
        int x, y;
        if (icon) {
            // Flat, centred on the tile.
            worldOf(t, x, y);
            centred(icon, x, y, rm, zscale());
        } else {
            // A street's colour band, along its inside edge.
            uint8_t c, c2;
            groupColour(group(t), c, c2);
            edgeCell(t, true, x, y);
            strip(x, y, side(t), zoomed(8), c, c2, true);
        }
    }
}

}  // namespace iso
