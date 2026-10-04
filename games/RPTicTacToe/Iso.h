// The isometric tables: the 3x3 and 5x5 boards as a wooden slab on the
// carpet, under a spotlight, with gold-inlaid lines between felt pads and
// the pieces standing on them (CHChess's 2:1 view). The flat top-down
// drawing in Stage is the strategy map.
//
// Lattice point (u, v) - u along a row, v down the rows of the flat map -
// sits at screen (x0 + (u - v) * hw, y0 + (u + v) * hh), so the map's top-left
// cell is the far corner and its top row runs down the upper-right edge.
#pragma once
#include <stdint.h>
#include "Rules.h"

namespace iso {

struct View {
    int16_t x0, y0;                 // the board's far corner on screen
    uint8_t n;                      // cells a side
    uint8_t hh;                     // half a tile's height (a tile is 4hh x 2hh)
    bool big;                       // the 3x3 size of art
};

bool fits(const Board &b);          // a table drawn this way
View view(const Board &b, int lift = 0);
void cellPos(const View &v, uint8_t cell, int &cx, int &cy);   // the pad's centre

void drawRoom(uint8_t base, uint8_t pool);      // the carpet (or felt) under its spotlight
void drawBoard(const View &v);                  // slab, inlay, pads
// A pad's outline, dashed in c/c2 (marching with phase), inset from its edge.
void padBorder(const View &v, uint8_t cell, uint8_t inset, uint8_t c, uint8_t c2, uint8_t phase);
void padFill(const View &v, uint8_t cell, uint8_t inset, uint8_t c);

// A piece's art (tools/pieces.py): sym 1 = X, 2 = O; GOBBLE chips by level.
const uint8_t *art(bool big, uint8_t sym);
const uint8_t *chipArt(bool big, uint8_t level);
// Draw art with its base centre at (x, y) (lift px up), and its shadow on
// the felt beneath (shadow = false: none, it's in the air over its own).
void stand(const uint8_t *art, int x, int y, int lift, const uint8_t *remap, bool shadow = true, bool mirror = false);
// The 3x3 X or O turned about its upright: step 0..3 = 0, 45, 90, 135
// degrees (the last is the 45 degree art mirrored). Other art is returned as is.
const uint8_t *spin(const uint8_t *art, uint8_t step, bool &mirror);
void shadow(const uint8_t *art, int x, int y, int lift);   // just its shadow
int height(const uint8_t *art);                            // rows above its base

}  // namespace iso
