// The rules of every table. One board type covers them all: up to 99 cells
// in a w x h grid, a line length k, and a few rule flags. Pure: no
// graphics, no clock; host-tested (tools/tests).
#pragma once
#include <stdint.h>

enum Mode : uint8_t {
    M_CLASSIC, M_BLITZ, M_MISERE, M_ALLX, M_VANISH, M_GOBBLE, M_WILD, M_DARK, M_COIN, M_AUCTION,
    M_BIG5, M_WRAP, M_MINES, M_DROP, M_ULTIMATE, M_99, MODE_COUNT
};

enum : uint16_t {
    F_MISERE = 1,       // completing a line loses
    F_VANISH = 2,       // three marks a side; the oldest goes when a fourth lands
    F_GOBBLE = 4,       // three sizes, two of each; a bigger piece covers a smaller
    F_WILD = 8,         // either symbol each move; whoever completes a line wins
    F_COIN = 16,        // a coin toss before every move decides who makes it
    F_AUCTION = 32,     // both sides bid chips for every move
    F_ULTIMATE = 64,    // nine 3x3 boards; a cell sends the opponent to that board
    F_BLITZ = 128,      // boards against the clock
    F_SAME = 256,       // both sides play X
    F_GRAVITY = 512,    // marks drop to the bottom of their column
    F_WRAP = 1024,      // lines run off one edge and on at the other
    F_DARK = 2048,      // the dealer's marks are hidden until bumped into
    F_MINES = 4096,     // hidden mines: a mark put on one is lost, and the cell with it
};

struct ModeDef { uint8_t w, h, k; uint16_t flags; uint8_t payNum, payDen; };
extern const ModeDef MODES[MODE_COUNT];

struct Dir { int8_t dx, dy; };
extern const Dir DIRS[4];
extern const uint8_t LINES3[8][3];      // the eight lines of a 3x3

enum Result : uint8_t { R_NONE, R_P0, R_P1, R_DRAW };   // P0 the player, P1 the dealer

constexpr uint8_t NONE = 0xFF;
constexpr uint8_t AUCTION_CHIPS = 8;

struct Board {
    // A cell holds a symbol (1 = X, 2 = O) in two bits per size level; only
    // GOBBLE uses levels 1 and 2. The piece showing is the highest level.
    uint8_t cell[99];
    uint16_t flags;
    uint16_t seen;                  // DARK: the dealer's marks found so far, a bit a cell
    uint32_t mines;                 // MINES: a bit a cell (laid by Match::start); a blown cell holds 3
    uint8_t boom;                   // MINES: the last move found one
    uint8_t w, h, k, n;
    uint8_t turn;                   // side to move: 0 the player, 1 the dealer
    uint8_t result;                 // Result
    uint8_t left;                   // empty cells
    uint8_t last;                   // the last cell played
    uint8_t gone;                   // VANISH: the cell that move emptied
    uint8_t run;                    // the longest line through the last cell
    uint8_t win[5];                 // the winning line's cells, end to end (win[0] NONE: no line)
    uint8_t q[2][3], qn[2];         // VANISH: each side's marks, oldest first
    uint8_t stock[2][3];            // GOBBLE: pieces left of each size
    uint8_t small[9];               // ULTIMATE: 0 open, 1/2 won, 3 dead
    uint8_t smallWon;               // ULTIMATE: the small board that move decided
    uint8_t must;                   // ULTIMATE: the board to play in (NONE = any)
    uint8_t chips[2], tieTo;        // AUCTION
};

inline uint8_t levelOf(uint8_t c) { return c >> 4 ? 2 : (c >> 2 ? 1 : 0); }
inline uint8_t topOf(uint8_t c) { return (uint8_t)(c >> (2 * levelOf(c))); }
inline uint8_t smallOf(uint8_t cell) { return (uint8_t)(cell / 27 * 3 + cell % 9 / 3); }
inline uint8_t smallCentre(uint8_t s) { return (uint8_t)((s / 3 * 3 + 1) * 9 + s % 3 * 3 + 1); }

namespace rules {

void start(Board &b, Mode m);
// arg: GOBBLE the size (0..2), WILD the symbol (0 = X, 1 = O), else 0.
bool legal(const Board &b, uint8_t cell, uint8_t arg);
bool anyLegal(const Board &b);
void play(Board &b, uint8_t cell, uint8_t arg);     // must be legal
uint8_t drop(const Board &b, uint8_t cell);         // GRAVITY: where a mark let go in that column lands (NONE: full)

}  // namespace rules
