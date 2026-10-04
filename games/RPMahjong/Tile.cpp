#pragma GCC optimize("Os")
// The tile blitters (Tile.h): 1x, close up and any size from SRAM (RAMFUNC);
// turned, from flash (the title).
#include <RPGfx.h>
#include "Tile.h"
#include <rpgame/RamFunc.h>  // the pile is up to 144 of these a frame: from SRAM

namespace tile {

static int clipY0 = 0, clipY1 = GFX_H;

void setClip(int y0, int y1) {
    clipY0 = y0;
    clipY1 = y1;
    gfx_setClip(0, y0, GFX_W, y1 - y0);
}

// Byte k of a row (bx + k) gets v, in the nibbles m says.
static inline __attribute__((always_inline)) void put(uint8_t *p, int bx, int k, uint8_t v, uint8_t m) {
    if (!m || (unsigned)(bx + k) >= (unsigned)GFX_FB_STRIDE) return;
    p[k] = m == 0xFF ? v : (uint8_t)((p[k] & ~m) | (v & m));
}

// 1x at an even x. The body is two copies of the tile's outline, moved
// (2, 2) in the backing colour and (1, 1) in the side colour, under the face:
//
//   row  0   E E E E E E E E . .
//   row  1   E f f f f f f f S .
//   ...      E f f f f f f f S B
//   row 12   . S S S S S S S S B
//   row 13   . . B B B B B B B B
RAMFUNC(tile1) static void draw1(const uint8_t *cell, int x, int y, const uint8_t *lut, const Style &s) {
    uint8_t tab[16];
    if (cell)
        for (uint8_t n = 0; n < 16; n++) tab[n] = (uint8_t)(lut[n & 3] | (lut[n >> 2] << 4));
    bool body = s.side != NONE;
    uint8_t E = (uint8_t)(s.edge * 0x11), S = (uint8_t)(s.side * 0x11), B = (uint8_t)(s.back * 0x11);
    uint8_t SB = (uint8_t)((S & 0x0F) | (B & 0xF0));
    int bx = x >> 1;
    for (int r = 0; r < H + 2; r++) {
        int yy = y + r;
        if (yy < clipY0 || yy >= clipY1) continue;
        if (r >= H && !body) break;
        uint8_t *p = gfx_fb + yy * GFX_FB_STRIDE + bx;
        if (r < H) {
            if (cell) {
                if (!r) {
                    for (int k = 0; k < 4; k++) put(p, bx, k, E, 0xFF);
                } else {
                    uint8_t a = cell[2 * r], b = cell[2 * r + 1];
                    put(p, bx, 0, (uint8_t)((tab[a & 15] & 0xF0) | (E & 0x0F)), 0xFF);
                    put(p, bx, 1, tab[a >> 4], 0xFF);
                    put(p, bx, 2, tab[b & 15], 0xFF);
                    put(p, bx, 3, tab[b >> 4], 0xFF);
                }
            }
            if (body && r) put(p, bx, 4, SB, r == 1 ? 0x0F : 0xFF);
        } else if (r == H) {
            put(p, bx, 0, S, 0xF0);
            for (int k = 1; k < 4; k++) put(p, bx, k, S, 0xFF);
            put(p, bx, 4, SB, 0xFF);
        } else {
            for (int k = 1; k < 5; k++) put(p, bx, k, B, 0xFF);
        }
    }
}

// Close up (w 16) at an even x: a 16x24 face (big) a pixel a pixel, or the
// small one doubled - a source pixel a byte, a source row two rows. The
// body's bands are 2 px: bytes 8 (side) and 9 (backing) right of the face,
// rows 24-27 below it.
RAMFUNC(tile2) static void draw2(const uint8_t *cell, const uint8_t *big, int x, int y, const uint8_t *lut,
                                 const Style &s) {
    uint8_t pair[4], tab[16];
    for (int k = 0; k < 4; k++) pair[k] = (uint8_t)(lut[k] * 0x11);
    if (big)
        for (uint8_t n = 0; n < 16; n++) tab[n] = (uint8_t)(lut[n & 3] | (lut[n >> 2] << 4));
    uint8_t E = (uint8_t)(lut[4] * 0x11), S = (uint8_t)(s.side * 0x11), B = (uint8_t)(s.back * 0x11);
    bool body = s.side != NONE, face = cell || big;
    int bx = x >> 1;
    uint8_t row[10];
    int loaded = -1;
    for (int r = 0; r < 2 * H + 4; r++) {
        int yy = y + r;
        if (r >= 2 * H && !body) break;
        if (yy < clipY0 || yy >= clipY1) continue;
        if (r < 2 * H && face) {
            if (big) {
                const uint8_t *src = big + 4 * r;
                for (int k = 0; k < 8; k++) row[k] = tab[(src[k >> 1] >> ((k & 1) * 4)) & 15];
                if (!r) for (int k = 0; k < 8; k++) row[k] = E;
                else row[0] = (uint8_t)((row[0] & 0xF0) | (E & 0x0F));
            } else if ((r >> 1) != loaded) {
                // The source row, once for its two screen rows (or for the
                // one the clip leaves).
                int sr = loaded = r >> 1;
                uint16_t bits = (uint16_t)(cell[2 * sr] | (cell[2 * sr + 1] << 8));
                for (int k = 0; k < 8; k++, bits >>= 2) row[k] = (sr && k) ? pair[bits & 3] : E;
            }
        }
        uint8_t *p = gfx_fb + yy * GFX_FB_STRIDE + bx;
        int k0 = 0, k1 = 8;
        if (r < 2 * H) {
            if (!face) k0 = 8;
            if (body && r >= 2) { row[8] = S; row[9] = B; k1 = r >= 4 ? 10 : 9; }
        } else if (r < 2 * H + 2) {
            for (int k = 1; k < 9; k++) row[k] = S;
            row[9] = B;
            k0 = 1; k1 = 10;
        } else {
            for (int k = 2; k < 10; k++) row[k] = B;
            k0 = 2; k1 = 10;
        }
        for (int k = k0; k < k1; k++) put(p, bx, k, row[k], 0xFF);
    }
}

// Any size, any x (the frames of a zoom): a pixel at a time, the body's
// bands included. src is sw x sw*3/2 (an 8x12 or a 16x24 cell).
RAMFUNC(tilen) static void drawN(const uint8_t *src, int sw, int x, int y, const uint8_t *lut, const Style &s, int w) {
    int h = w * 3 / 2, t = w >= 16 ? 2 : 1, stride = sw >> 2;
    bool body = s.side != NONE;
    int cols = body ? w + 2 * t : w, rows = body ? h + 2 * t : h;
    uint8_t sc[2 * W];
    for (int i = 0; i < w; i++) sc[i] = (uint8_t)(i * sw / w);
    for (int j = 0; j < rows; j++) {
        int yy = y + j;
        if (yy < clipY0 || yy >= clipY1) continue;
        int sr = j < h ? j * sw / w : 0;
        const uint8_t *srow = src ? src + sr * stride : nullptr;
        uint8_t *row = gfx_fb + yy * GFX_FB_STRIDE;
        for (int i = 0; i < cols; i++) {
            int xx = x + i;
            if ((unsigned)xx >= (unsigned)GFX_W) continue;
            uint8_t c;
            if (i < w && j < h) {
                if (!src) continue;
                int k = sc[i];
                c = (!sr || !k) ? lut[4] : lut[(srow[k >> 2] >> ((k & 3) * 2)) & 3];
            } else if (i >= t && i < w + t && j >= t && j < h + t) c = s.side;
            else if (i >= 2 * t && j >= 2 * t) c = s.back;
            else continue;
            uint8_t &q = row[xx >> 1];
            q = (xx & 1) ? (uint8_t)((q & 0x0F) | (c << 4)) : (uint8_t)((q & 0xF0) | c);
        }
    }
}

void drawSpun(const Face &f, const Style &s, int cx, int cy, int cs, int sn, int xs, bool big) {
    const uint8_t *cell = big ? f.big : f.cell;
    uint8_t inks = big ? f.bigInks : f.inks;
    uint8_t lut[4] = {s.face, s.shade, (uint8_t)(inks & 15), (uint8_t)(inks >> 4)};
    int w = big ? 2 * W : W, h = big ? 2 * H : H, t = big ? 2 : 1, stride = w >> 2;
    int ox = w / 2 + t, oy = h / 2 + t;                  // the middle of face and body
    if (xs < 16) xs = 16;
    int32_t ix = (65536 + xs / 2) / xs;                  // 1/xs, Q8
    // Just the box the turned tile covers: half its size, turned.
    int hw = ((w / 2 + t + 1) * xs) >> 8, hh = h / 2 + t + 1;
    int acs = cs < 0 ? -cs : cs, asn = sn < 0 ? -sn : sn;
    int rx = (hw * acs + hh * asn) / 256 + 1, ry = (hw * asn + hh * acs) / 256 + 1;
    for (int dy = -ry; dy <= ry; dy++) {
        int yy = cy + dy;
        if (yy < clipY0 || yy >= clipY1) continue;
        uint8_t *row = gfx_fb + yy * GFX_FB_STRIDE;
        // Screen offset -> the tile's own axes: (dx, dy) turned back by the angle.
        int32_t U = -rx * cs + dy * sn + 128, V = rx * sn + dy * cs + 128;
        for (int dx = -rx; dx <= rx; dx++, U += cs, V -= sn) {
            int xx = cx + dx;
            if ((unsigned)xx >= (unsigned)GFX_W) continue;
            int x = (int)((U * ix) >> 16) + ox, y = (int)(V >> 8) + oy;
            uint8_t c;
            if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) {
                if (!cell) continue;
                c = (!x || !y) ? s.edge : lut[(cell[y * stride + (x >> 2)] >> ((x & 3) * 2)) & 3];
            } else if (x >= t && x < w + t && y >= t && y < h + t) c = s.side;
            else if (x >= 2 * t && x < w + 2 * t && y >= 2 * t && y < h + 2 * t) c = s.back;
            else continue;
            uint8_t &q = row[xx >> 1];
            q = (xx & 1) ? (uint8_t)((q & 0x0F) | (c << 4)) : (uint8_t)((q & 0xF0) | c);
        }
    }
}

void draw(const Face &f, int x, int y, const Style &s, int w) {
    bool big = f.big && w > W;
    uint8_t inks = big ? f.bigInks : f.inks;
    uint8_t lut[5] = {s.face, s.shade, (uint8_t)(inks & 15), (uint8_t)(inks >> 4), s.edge};
    if (!(x & 1) && w == W) draw1(f.cell, x, y, lut, s);
    else if (!(x & 1) && w == 2 * W) draw2(big ? nullptr : f.cell, big ? f.big : nullptr, x, y, lut, s);
    else if (big) drawN(f.big, 2 * W, x, y, lut, s, w);
    else drawN(f.cell, W, x, y, lut, s, w);
}

}  // namespace tile
