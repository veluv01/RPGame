// Drawing the table (Table.h): the felt, the camera, and the tiles at
// every size, close up from SRAM (tileFast).
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed
#include <string.h>
#include <RPGame.h>
#include "Table.h"
#include "Colours.h"

namespace table {

uint8_t zoom = 5;
int16_t camX = CX, camY = CY;

// ---------------------------------------------------------------------------
// Sets of tiles. Every tile is drawn in two palette slots, so a set is a
// palette swap: a light set has its face in BONE and its pips in SLATE
// (bevels white and silver); a dark set has its face in SLATE, white pips,
// and BONE free for its lit bevel, its shade one of the casino colours.
// ---------------------------------------------------------------------------
struct Set { uint16_t bone, slate; uint8_t dark, hi, lo; };
static const Set SETS[SET_COUNT] = {
    {0xEEE, 0x445, 0, SILVER, NAVY},    // white (its dark style: the title's black tiles)
    {0x667, 0x334, 1, BONE, INK},       // black
    {0xFEB, 0x742, 0, SILVER, NAVY},    // ivory, brown pips
    {0xF88, 0xC12, 1, BONE, WINE},      // red
    {0x8BF, 0x25C, 1, BONE, NAVY},      // blue
    {0x6DB, 0x097, 1, BONE, NAVY},      // jade
    {0xC9E, 0x72B, 1, BONE, NAVY},      // grape
    {0xFBD, 0x936, 0, SILVER, NAVY},    // pink, raspberry pips
};
static bool setDark;
static uint8_t darkHi = SILVER, darkLo = NAVY;

void useSet(uint8_t i) {
    const Set &s = SETS[i < SET_COUNT ? i : 0];
    pal::setFx(BONE, s.bone);
    pal::setFx(SLATE, s.slate);
    setDark = s.dark;
    darkHi = s.hi; darkLo = s.lo;
}

uint8_t tileFace() { return setDark ? SLATE : BONE; }
uint8_t tileDim() { return setDark ? darkLo : SILVER; }

void setCamera(int x, int y) {
    // Half the view, in world units: 64 across, 40 down at the plain size.
    int hx = 320 / zoom, hy = 202 / zoom;
    if (x < hx) x = hx;
    if (x > 128 - hx) x = 128 - hx;
    if (y < TOP + hy) y = TOP + hy;
    if (y > BOT - hy) y = BOT - hy;
    camX = (int16_t)x; camY = (int16_t)y;
}

void drawFelt() {
    gfx_fillRect(0, TOP, 128, BOT - TOP, FELT);
    // A line printed round the playing area, and a second inside it.
    int x = sx(X0 + 2), y = sy(Y0 + 2), w = sx(X0 + layout::UW * UNIT - 2) - x, h = sy(Y0 + layout::UH * UNIT - 2) - y;
    roundRect(x, y, w, h, 3, FELT_LT);
    int g = zoomed(2);
    roundRect(x + g, y + g, w - 2 * g, h - 2 * g, 2, FELT_DK);
    // The lamp over the table: the felt falls into shadow towards the rails
    // (the top rail's own shadow the deepest).
    gfx_hline(0, TOP, 128, INK);
    gfx_hline(0, TOP + 1, 128, FELT_DK);
    dither(0, TOP + 2, 128, 3, FELT_DK, 0);
    dither(0, BOT - 3, 128, 3, FELT_DK, 1);
    gfx_fillRect(0, TOP + 2, 2, BOT - TOP - 2, FELT_DK);
    gfx_fillRect(126, TOP + 2, 2, BOT - TOP - 2, FELT_DK);
}

// ---------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------
// Pips on a half, lying down: bit row * 3 + column of a 3 x 3 grid (a six's
// rows run along the tile).
static const uint16_t PIPMASK[7] = {0x000, 0x010, 0x101, 0x111, 0x145, 0x155, 0x1C7};

// Pip k of the half hf (0, 1) showing v: its top-left within the tile, or
// false if there is none there. (The small drawings: the whole table, a
// hand shown at the round's end.)
static bool pipAt(uint8_t v, uint8_t hf, uint8_t k, bool upright, uint8_t p, int &tx, int &ty) {
    if (!((PIPMASK[v] >> k) & 1)) return false;
    int i = (k % 3) * (p + 1), j = (k / 3) * (p + 1), off = 1 + hf * (3 * p + 3);
    if (upright) { tx = 1 + j; ty = off + i; }
    else { tx = off + i; ty = 1 + j; }
    return true;
}

// A close-up tile, 13 x 25 (upright) or 25 x 13, and `depth` rows of its
// near edge below: the colour of its pixel (r, c), CLEAR for none (15 is a
// colour here: FX_B, the face of a tile about to go off). Lit from the
// top left: the face (BONE) is bevelled, white along its top and left,
// silver along its bottom and right; the bar across the middle is a groove,
// its far side in shade and its near side lit; each pip is a 2 x 2 dot
// with a soft shadow off its lower right corner, inside a 9 x 9 field that
// leaves a pixel of the face all round. A dark tile has white pips, its
// bevels the set's (darkHi, darkLo).
static const uint8_t CLEAR = 0xFF;

RAMFUNC(tilepx) static uint8_t tilePx(int r, int c, bool up, int depth, uint16_t ma, uint16_t mb, uint8_t face,
                                     uint8_t edge, bool dark) {
    const int w = up ? 13 : 25, h = up ? 25 : 13, last = h + depth - 1;
    uint8_t hi = dark ? darkHi : WHITE, lo = dark ? darkLo : SILVER;
    if ((c == 0 || c == w - 1) && (r == 0 || r == last)) return CLEAR;
    if (c == 0 || c == w - 1 || r == 0 || r == h - 1 || r == last) return edge;
    if (r >= h) return lo;
    if (r == 1 || c == 1) return hi;
    if (r == h - 2 || c == w - 2) return lo;
    int along = up ? r : c;
    if (along == 12) return lo;
    if (along == 13) return hi;
    bool second = along > 12;
    int la = along - (second ? 14 : 2), lc = (up ? c : r) - 2;
    if (la < 0 || la > 8 || lc < 0 || lc > 8) return face;
    if (!(((second ? mb : ma) >> ((lc / 3) * 3 + la / 3)) & 1)) return face;
    int sa = la % 3, sc = lc % 3, sx = up ? sc : sa, sy = up ? sa : sc;
    if (sx < 2 && sy < 2) return dark ? WHITE : SLATE;
    // The shadow: the two pixels off its lower right corner (not the corner
    // itself, which would run diagonal pips together).
    return (sx == 2 && sy == 1) || (sx == 1 && sy == 2) ? lo : face;
}

// The kind of row r is: rows of one kind are alike (so each is worked out once).
static int rowKind(int r, bool up, int h, int last) {
    if (r == 0 || r == last) return 0;
    if (r == h - 1) return 1;
    if (r >= h) return 2;
    if (r == 1) return 3;
    if (r == h - 2) return 4;
    if (!up) return 8 + ((r - 2) / 3) * 2 + ((r - 2) % 3 == 2);
    if (r == 11) return 5;
    if (r == 12) return 6;
    if (r == 13) return 7;
    int half = r >= 14, ly = r - (half ? 14 : 2);
    return 14 + half * 6 + (ly / 3) * 2 + (ly % 3 == 2);
}

// A close-up tile straight into the framebuffer, from SRAM: each kind of
// row worked out once into a row of colours, then copied two pixels a byte.
// (Built of rectangles it was some twenty calls a tile, most of the line's
// drawing time.)
RAMFUNC(tilefast) static void tileFast(int x, int y, uint8_t a, uint8_t b, bool upright, uint8_t face, uint8_t edge,
                                       uint8_t depth, bool dark) {
    const int w = upright ? 13 : 25, h = upright ? 25 : 13, last = h + depth - 1;
    uint8_t row[25];
    int key = -1;
    uint16_t ma = PIPMASK[a], mb = PIPMASK[b];
    for (int r = 0; r <= last; r++) {
        int yy = y + r;
        if ((unsigned)yy >= GFX_H) continue;
        int k = rowKind(r, upright, h, last);
        if (k != key) {
            key = k;
            for (int c = 0; c < w; c++) row[c] = tilePx(r, c, upright, depth, ma, mb, face, edge, dark);
        }
        // Two pixels a byte where both are there, else one nibble.
        uint8_t *fb = gfx_fb + yy * GFX_FB_STRIDE;
        int c = x < 0 ? -x : 0, end = x + w > GFX_W ? GFX_W - x : w;
        for (; c < end; c++) {
            int xx = x + c;
            uint8_t lo = row[c];
            uint8_t &q = fb[xx >> 1];
            if (!(xx & 1) && c + 1 < end && lo != CLEAR && row[c + 1] != CLEAR) {
                q = (uint8_t)(lo | (row[c + 1] << 4));
                c++;
                continue;
            }
            if (lo == CLEAR) continue;
            q = (xx & 1) ? (uint8_t)((q & 0x0F) | (lo << 4)) : (uint8_t)((q & 0xF0) | lo);
        }
    }
}

void drawTile(int x, int y, uint8_t a, uint8_t b, bool upright, uint8_t p, uint8_t zm, uint8_t face, uint8_t edge,
              uint8_t depth) {
    bool dark = setDark || face == SLATE;
    if (p == 3 && zm == 5) { tileFast(x, y, a, b, upright, face, edge, depth, dark); return; }
    // The small drawings: plain, but in the same colours.
    uint8_t lo = dark ? darkLo : SILVER, pip = dark ? WHITE : SLATE;
    int S = 3 * p + 4, L = 6 * p + 7, D = 3 * p + 3;
    int w = upright ? S : L, h = upright ? L : S;
#define TX(i) (x + (i) * zm / 5)
#define TY(j) (y + (j) * zm / 5)
    int W = TX(w) - x, H = TY(h) - y;
    fillRound(x, y, W, H + depth, 1, edge);
    if (depth > 1) gfx_fillRect(x + 1, y + H, W - 2, depth - 1, lo);
    gfx_fillRect(TX(1), TY(1), TX(w - 1) - TX(1), TY(h - 1) - TY(1), face);
    if (upright) gfx_fillRect(TX(1), TY(D), TX(w - 1) - TX(1), TY(D + 1) - TY(D), lo);
    else gfx_fillRect(TX(D), TY(1), TX(D + 1) - TX(D), TY(h - 1) - TY(1), lo);
    for (uint8_t hf = 0; hf < 2; hf++) {
        uint8_t v = hf ? b : a;
        for (uint8_t k = 0; k < 9; k++) {
            int tx, ty;
            if (!pipAt(v, hf, k, upright, p, tx, ty)) continue;
            int px = TX(tx), py = TY(ty);
            gfx_fillRect(px, py, TX(tx + p) - px, TY(ty + p) - py, pip);
        }
    }
#undef TX
#undef TY
}

void drawBack(int x, int y, int w, int h) {
    if (w == 13) { tileFast(x, y, 0, 0, true, tileFace(), INK, 0, setDark); return; }     // face down: blank
    fillRound(x, y, w, h, 1, INK);
    gfx_fillRect(x + 1, y + 1, w - 2, h - 2, SLATE);
    gfx_hline(x + 1, y + 1, w - 2, setDark ? BONE : SILVER);    // lit along its top
    gfx_hline(x + 1, y + h - 2, w - 2, setDark ? darkLo : NAVY);
}

void placedBox(const layout::Placed &p, int &x, int &y, int &w, int &h) {
    x = X0 + p.x * UNIT; y = Y0 + p.y * UNIT;
    w = p.w() * UNIT + 1; h = p.h() * UNIT + 1;
}

bool onScreen(const layout::Placed &p) {
    int x, y, w, h;
    placedBox(p, x, y, w, h);
    int a = sx(x), t = sy(y);
    return a < 128 && t < BOT && sx(x + w) + 4 > 0 && sy(y + h) + 6 > TOP - 4;
}

void drawPlaced(const layout::Placed &p, uint8_t face, uint8_t edge) {
    int x, y, w, h;
    placedBox(p, x, y, w, h);
    // Close up, the bigger drawing of it (a unit is six pixels there).
    if (zoom == 10) drawTile(sx(x), sy(y), p.first(), p.second(), p.upright(), 3, 5, face, edge, 3);
    else drawTile(sx(x), sy(y), p.first(), p.second(), p.upright(), 1, zoom, face, edge, (uint8_t)(zoom * 3 / 10));
}

void drawShadow(const layout::Placed &p) {
    int x, y, w, h;
    placedBox(p, x, y, w, h);
    // Cast down and to the right by the lamp, as deep as the tile is thick.
    int d = zoom * 3 / 10, a = sx(x) + zoomed(1), t = sy(y) + zoomed(1) + d;
    gfx_fillRect(a, t, sx(x + w) - sx(x), sy(y + h) - sy(y), FELT_DK);
}

void drawLift(int cx, int cy, int w, int h, int height) {
    // The higher it is, the further the shadow falls and the smaller and
    // softer it is.
    if (height > 40) return;
    int off = 2 + height / 4;
    w -= height / 3; h -= height / 4;
    if (w < 3 || h < 3) return;
    if (height < 6) gfx_fillRect(cx - w / 2 + off, cy - h / 2 + off, w, h, FELT_DK);
    else dither(cx - w / 2 + off, cy - h / 2 + off, w, h, FELT_DK, 0);
}

void drawScorch(const layout::Placed &p) {
    int x, y, w, h;
    placedBox(p, x, y, w, h);
    int a = sx(x + 1), t = sy(y + 1);
    dither(a, t, sx(x + w - 1) - a, sy(y + h - 1) - t, FELT_DK, (uint8_t)(p.x + p.y));
}

void drawGhost(const layout::Placed &p, uint8_t c, bool solid) {
    int x, y, w, h;
    placedBox(p, x, y, w, h);
    int a = sx(x), t = sy(y), ww = sx(x + w) - a, hh = sy(y + h) - t;
    if (solid) gfx_fillRect(a + 1, t + 1, ww - 2, hh - 2, c);
    else dither(a + 1, t + 1, ww - 2, hh - 2, c, 0);
    roundRect(a, t, ww, hh, 1, c);
}

void drawTag(uint8_t arm, bool lit) {
    static const int8_t DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};
    int ux, uy;
    uint8_t dir, v;
    layout::endOf(arm, ux, uy, dir, v);
    char s[2] = {(char)('0' + v), 0};
    int x = sx(X0 + ux * UNIT), y = sy(Y0 + uy * UNIT);
    if (zoom != 10) {
        x += DX[dir] * 5; y += DY[dir] * 6;
        fillRound(x - 3, y - 4, 7, 9, 1, lit ? FX_B : NAVY);
        text35(x - 1, y - 2, s, lit ? INK : GOLD);
        return;
    }
    // Close up: an end out of view keeps its plate at the edge of the felt,
    // on the side it lies.
    x += DX[dir] * 8; y += DY[dir] * 10;
    if (x < 6) x = 6;
    if (x > 120) x = 120;
    if (y < TOP + 8) y = TOP + 8;
    if (y > BOT - 10) y = BOT - 10;
    // 12 x 16 round the 6 x 10 figure: two pixels clear all round inside the
    // border.
    fillRound(x - 4, y - 6, 12, 16, 2, INK);                // its shadow
    fillRound(x - 5, y - 7, 12, 16, 2, lit ? FX_B : NAVY);
    roundRect(x - 5, y - 7, 12, 16, 2, lit ? INK : GOLD);
    text35x2(x - 2, y - 4, s, lit ? INK : GOLD);
}

void tileImage(uint8_t *buf, uint8_t a, uint8_t b, bool dark) {
    uint16_t ma = PIPMASK[a], mb = PIPMASK[b];
    for (int y = 0; y < TILE_IH; y++)
        for (int x = 0; x < TILE_IW; x++) {
            uint8_t c = tilePx(y, x, true, 0, ma, mb, dark ? SLATE : BONE, INK, dark);
            if (c == CLEAR) c = 15;                         // (rotRaw's clear)
            uint8_t &q = buf[y * ((TILE_IW + 1) >> 1) + (x >> 1)];
            q = (x & 1) ? (uint8_t)((q & 0x0F) | (c << 4)) : (uint8_t)((q & 0xF0) | c);
        }
}

void spinImage(const uint8_t *img, int px, int py, uint8_t angle, int scale, const uint8_t *remap) {
    memcpy(gfx_chunkScratch(), img, TILE_IMG);
    rotRaw(TILE_IW, TILE_IH, TILE_IW / 2, TILE_IH / 2, px, py, angle, scale, remap);
}

void spinTile(uint8_t a, uint8_t b, bool upright, int px, int py, uint8_t angle, int scale, const uint8_t *remap) {
    // Built upright; lying down is a quarter turn more.
    tileImage(gfx_chunkScratch(), a, b, setDark);
    rotRaw(TILE_IW, TILE_IH, TILE_IW / 2, TILE_IH / 2, px, py, (uint8_t)(angle + (upright ? 0 : 192)), scale, remap);
}

}  // namespace table
