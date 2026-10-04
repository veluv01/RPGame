// The board on screen: an isometric ring of 40 tiles round a felt centre
// (CHChess's renderer, a size up and hollowed out), zoomable, bigger than
// the screen at any zoom - the camera follows the play.
//
// Iso world space is the 2:1 projection itself, in pixels at the current
// zoom, with the board's far corner at (0, 0). The board is an N x N lattice
// of cells (u runs down-right on screen, v down-left); lattice point (u, v)
// sits at world ((u - v) * hw, (u + v) * hh). The ring is R cells deep: a
// tile is one cell along its side and R into the board, a corner R x R.
// Tiles go clockwise from GO, the near corner: up the near-left side to Jail
// (the left corner), along the far left to Free Parking (the far corner),
// down the far right to Go To Jail (the right corner), and home along the
// near right.
#pragma once
#include <stdint.h>

namespace iso {

constexpr int CX = 64, CY = 69;             // screen point the camera looks at (below the HUD)
constexpr int N = 13, R = 2;                // cells along a side; the ring's depth

// Current zoom: tileH is half a cell's height, 5 (20x10 cells, the art's own
// size) up to 10 (40x20, the art doubled) - the camera zooms through the
// steps between. The board is drawn at every step; the art only ever as
// drawn or doubled (it changes over halfway), never stretched in between.
extern uint8_t tileH;
inline int hw() { return 2 * tileH; }       // half a cell's width
inline int hh() { return tileH; }           // half a cell's height
inline int slab() { return (3 * tileH + 2) / 5; }   // board thickness
inline int zoomed(int px) { return px * tileH / 5; }        // a length on the board
inline int zscale() { return tileH < 8 ? 256 : 512; }       // art scale, Q8 (256 = 1:1)
inline int sized(int px) { return tileH < 8 ? px : 2 * px; }    // a length on a sprite

// Camera: the world point shown at (CX, CY).
struct Cam { int x, y; };
extern Cam cam;
inline int toScreenX(int wx) { return wx - cam.x + CX; }
inline int toScreenY(int wy) { return wy - cam.y + CY; }

// Which side of the board a tile is on (0 near left, 1 far left, 2 far
// right, 3 near right; a corner counts with the side it starts).
inline uint8_t side(uint8_t tile) { return (uint8_t)(tile / 10); }
inline bool corner(uint8_t tile) { return tile % 10 == 0; }

// A point on a tile, in eighths of a cell: `along` the way the tokens travel
// (0..8; a corner 0..16), `out` from the board's inside edge (0) to its rim
// (16). World position.
void place(uint8_t tile, int along, int out, int &x, int &y);
void worldOf(uint8_t tile, int &x, int &y);  // its centre

// Look-dev switches (fixed once the look is picked).
extern uint8_t lightTile, darkTile;         // the ring's two tile tones
extern bool solidGroups;                    // pink and orange as WINE and SKIN, not dithered

void drawTable();                           // the carpet
void drawBoard();                           // slab, tiles, colour bands, decals, the felt centre
// Half of a tile's pixels (checkerboard) in c, or all of them (solid).
void tileTint(uint8_t tile, uint8_t c, bool solid = false);
void rim(uint8_t tile, uint8_t c);          // a strip of colour along its outside edge: the owner's

// A special tile's decal (span4) and the remap it is drawn through;
// nullptr for a deed's tile... but for the railroads and utilities.
const uint8_t *decalOf(uint8_t tile, const uint8_t *&remap);

// A street group's colour: c, and c2 if it is dithered (else the same).
void groupColour(uint8_t group, uint8_t &c, uint8_t &c2);

}  // namespace iso
