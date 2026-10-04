// A mahjong tile: an 8x12 face (its top and left edge, then 7x11 of art)
// standing on a body that shows as two bands, right of and below it - the
// tile's thickness, then its backing - as real tiles have.
//
// Faces are stored at 2 bits a pixel: the face, the emboss (the art's
// shadow, a pixel down and right of it, worked out by tools/assets.py) and
// two inks; the edge is drawn round them. They are drawn through a Style,
// so the same art is a tile, a white flash or a gold shimmer.
//
// At 1x a tile is drawn at an even x: a framebuffer byte is two pixels, so
// a row is a few byte stores. Scaled (the close-up view), w is the tile's
// width, 8..16 px; 16 is the fast case (the 16x24 face if there is one,
// else the small one doubled).
#pragma once
#include <stdint.h>

namespace tile {

constexpr int W = 8, H = 12;        // the face; a tile next to it starts W right or H down
constexpr uint8_t NONE = 0xFF;

struct Style {
    uint8_t face, shade, edge;      // shade: the emboss (= face for none)
    uint8_t side, back;             // the bands; side NONE: no body (a tile held up)
};

// Rows drawn: [y0, y1). Also sets RPGfx's clip to them.
void setClip(int y0, int y1);

// A face: its 8x12 cell (24 bytes, tools/assets.py pack_cell) and inks (low
// nibble, high nibble), and optionally a 16x24 one (96 bytes) for close up.
// No cell: the body only - all that shows of a tile with another squarely
// on it.
struct Face {
    const uint8_t *cell;
    uint8_t inks;
    const uint8_t *big;
    uint8_t bigInks;
};

void draw(const Face &f, int x, int y, const Style &s, int w = W);

// A tile turned in the plane (cs, sn: the angle's cosine and sine, Q8) and
// squeezed sideways (xs, Q8, 256 = full width: a flip seen edge-on is
// small), centred on (cx, cy). big: the 16x24 face (Face::big), else the
// small one. A pixel at a time, from flash: for the title's falling tiles.
void drawSpun(const Face &f, const Style &s, int cx, int cy, int cs, int sn, int xs, bool big);

}  // namespace tile
