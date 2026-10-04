// Where the tiles of the line lie on the felt.
//
// The felt is a grid of units, 42 by 26 (3 px each seen whole, 6 close
// up); a tile is 4 x 2 of them (a double set across its arm 2 x 4). The
// first tile is in the middle. Each arm then walks outward: straight on
// while there is room, else round a corner - clockwise by choice, so the
// arms turn about the middle like a pinwheel and keep out of each other's
// way. A tile once down never moves.
//
// No graphics here (the host tests lay out thousands of games).
#pragma once
#include <stdint.h>
#include "Dominoes.h"

namespace layout {

constexpr int UW = 42, UH = 26;

struct Placed {
    uint8_t x, y;       // its top-left unit
    uint8_t v;          // upright << 7 | the pips of its left (upright: top) half << 3 | the other's
    bool upright() const { return v >> 7; }
    uint8_t first() const { return (v >> 3) & 7; }
    uint8_t second() const { return v & 7; }
    uint8_t w() const { return upright() ? 2 : 4; }
    uint8_t h() const { return upright() ? 4 : 2; }
};

extern Placed at[dom::TILES];       // by the order played
extern uint8_t n;

void reset();
// Where the tile would go on the arm (with nothing down: the middle). False
// if there was no room and it had to be laid over others.
bool plan(uint8_t tile, uint8_t arm, Placed &p);
bool add(uint8_t tile, uint8_t arm);
void rebuild(const dom::Round &r, uint8_t count);
// An arm's open end: the point (in units) where its middle line leaves its
// last tile, the way it heads (0 E, 1 S, 2 W, 3 N), the pips open there, and
// how many tiles are on it.
uint8_t endOf(uint8_t arm, int &x, int &y, uint8_t &dir, uint8_t &value);

}  // namespace layout
