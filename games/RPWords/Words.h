// The rules of the board: where tiles may go and what a play scores.
// Nothing here knows about the dictionary, the screen or whose turn it is.
//
// A square is 0 (empty) or a letter 1..26, with BLANK set when the tile is
// a blank standing for that letter (it scores nothing). Squares are numbered
// row * 15 + column.
#pragma once
#include <stdint.h>

namespace wd {

constexpr uint8_t SIZE = 15, CELLS = 225, CENTRE = 112, RACK = 7;
constexpr uint8_t BLANK = 0x80, LETTER = 0x1F;
constexpr uint8_t BLANK_TILE = 27;              // a blank in a rack or the bag
constexpr uint8_t BINGO = 50;                   // all seven tiles in one play

extern const uint8_t VALUE[28];                 // by tile 1..27 (the blank: 0)
extern const uint8_t COUNT[28];                 // tiles of each in the bag: 100 in all

enum Premium : uint8_t { PLAIN, DL, TL, DW, TW };
uint8_t premium(uint8_t cell);

struct Placement { uint8_t cell, tile; };       // tile: a letter, | BLANK
struct Span { uint8_t start, len, step; };      // step 1: across, 15: down

enum Error : uint8_t {
    OK,
    E_TAKEN,        // on a square that is not empty
    E_LINE,         // not all in one row or column
    E_GAP,          // an empty square between them
    E_CENTRE,       // the first play must cover the centre square
    E_ALONE,        // touches nothing (or one tile making no word)
};

struct Result {
    uint8_t nWords;                 // word[0] is the main word
    Span word[8];
    int16_t score;
};

// Judge the tiles p[0..n) as one play on board b. The board is written to
// while it works and left as it was.
uint8_t check(uint8_t *b, const Placement *p, uint8_t n, Result &r);

// The letters (1..26) of a span, with the play's tiles in place.
void letters(const uint8_t *b, const Placement *p, uint8_t n, const Span &s, uint8_t *out);

inline uint8_t tileOf(uint8_t placed) { return (placed & BLANK) ? BLANK_TILE : placed; }

}  // namespace wd
