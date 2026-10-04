// Four in a row: seven columns, six rows, discs dropped from the top.
//
// Each side's discs are a 64-bit set, seven bits a column (bit col * 7 + row,
// row 0 at the bottom; the seventh bit of a column stays clear, so shifts by
// 1, 6, 7 and 8 walk the four directions without wrapping into a
// neighbour). Everything here is and, or, add and constant shifts - nothing
// the chip has to call libgcc for.
#pragma once
#include <stdint.h>

namespace c4 {

constexpr uint8_t COLS = 7, ROWS = 6, CELLS = 42;
enum Side : uint8_t { RED = 0, GOLD = 1, NOBODY = 2 };

typedef uint64_t Bits;
constexpr Bits COLUMN = 0x3F;                                 // a column's six cells (shift by col * 7)
constexpr Bits everyColumn(Bits v) { return v | v << 7 | v << 14 | v << 21 | v << 28 | v << 35 | v << 42; }
constexpr Bits BOARD = everyColumn(COLUMN);                   // every cell
constexpr Bits BOTTOM = everyColumn(1);                       // row 0 of every column

// (A 32-bit shift and a move: a 64-bit shift by a variable is a call into
// libgcc, in flash, and this is in the search's inner loop.)
inline Bits cellBit(uint8_t i) {
    uint32_t b = 1u << (i & 31);
    return i < 32 ? (Bits)b : (Bits)b << 32;
}
inline Bits cell(uint8_t col, uint8_t row) { return cellBit((uint8_t)(col * 7 + row)); }

struct Board {
    Bits side[2];
    uint8_t h[COLS];            // discs in each column
    uint8_t n;                  // ... and on the board
};

void reset(Board &b);
inline bool canPlay(const Board &b, uint8_t col) { return col < COLS && b.h[col] < ROWS; }
inline uint8_t play(Board &b, uint8_t side, uint8_t col) {  // returns the row it came to rest in
    uint8_t row = b.h[col]++;
    b.side[side] |= cell(col, row);
    b.n++;
    return row;
}
inline void undo(Board &b, uint8_t side, uint8_t col) {
    b.side[side] &= ~cell(col, --b.h[col]);
    b.n--;
}
inline bool full(const Board &b) { return b.n == CELLS; }

bool hasFour(Bits p);
// The empty cells (anywhere on the board) that would complete a four for p.
Bits winning(Bits p, Bits occupied);
// The cell a disc dropped in each column would come to rest in.
inline Bits playable(const Board &b) { return ((b.side[0] | b.side[1]) + BOTTOM) & BOARD; }
// The side's immediate wins: cells it could drop into right now for a four.
inline Bits immediate(const Board &b, uint8_t side) { return winning(b.side[side], b.side[0] | b.side[1]) & playable(b); }
uint8_t count(Bits x);
uint8_t columnOf(Bits cells);                                // of the lowest cell in it
// A four of p's through (col, row), as cell indices (col * 7 + row): false if none.
bool fourThrough(Bits p, uint8_t col, uint8_t row, uint8_t cells[4]);

// A small generator (the CPU's judgement, the taunts): not the display's.
struct Rng {
    uint32_t s;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    uint8_t below(uint8_t n) { return (uint8_t)((next() >> 8) % n); }
};

}  // namespace c4
