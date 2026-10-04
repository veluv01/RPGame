// The betting layout's spots: every place a chip can go, and the action
// bar's buttons, which the glove visits too. Pure (no graphics), shared by
// the rules, the navigation and the renderer. docs/design/layout.md has the
// pixel layout this encodes.
//
// Ids:
//   0..143   the lattice, id = v*24 + u, over half-cells of the number grid:
//            u 0..23 left to right (odd u = a column's centre, even u = the
//            line left of column u/2 + 1; u = 0 is the line beside the zero),
//            v 0..5 bottom to top (odd v = a row's centre, even v = a line;
//            v = 0 is the grid's bottom edge). Odd/odd = a straight number;
//            one even = a split; both even = a corner; v = 0 = a street
//            (odd u) or a six line (even u); u = 0 = the bets with the zero.
//   144      0             145  00 (American only)   146  0/00 (American only)
//   147..149 the columns (2 TO 1), bottom row's first
//   150..152 the dozens
//   153..158 LOW EVEN RED BLACK ODD HIGH
//   159..165 the bar: CLR $1 $5 $10 $25 $100 SPIN (not bets)
#pragma once
#include <stdint.h>

namespace spots {

constexpr uint8_t ZERO = 144, DZERO = 145, ZERO_DZERO = 146;
constexpr uint8_t COLUMN = 147, DOZEN = 150, BET_LOW = 153, BET_EVEN = 154, BET_RED = 155,
                  BET_BLACK = 156, BET_ODD = 157, BET_HIGH = 158;   // (LOW and HIGH are Arduino macros)
constexpr uint8_t CLR = 159, CHIP0 = 160, SPIN = 165;
constexpr uint8_t NBET = 159;               // bet ids are 0..158
constexpr uint8_t NSPOT = 166;              // with the bar
constexpr uint8_t NONE = 0xFF;

enum Kind : uint8_t {
    STRAIGHT, SPLIT, STREET, TRIO, CORNER, FIRST_FOUR, TOP_LINE, SIX_LINE,
    COLUMN_BET, DOZEN_BET, EVEN_MONEY, BAR,
};

bool valid(uint8_t id, bool us);            // exists on this wheel's layout
Kind kind(uint8_t id, bool us);
inline bool isBet(uint8_t id) { return id < NBET; }
bool inside(uint8_t id);                    // inside bets (limit $100) vs outside ($250)

// Where it is on screen. (ax, ay) is the chip's anchor (a stack stands on
// it, the glove's fingertip points 4 px above it); (x0, y0)-(x1, y1) the
// inside of its cell, inclusive, or the anchor itself for a lattice point
// (a line or a corner).
struct Geo { uint8_t ax, ay, x0, y0, x1, y1; };
void geo(uint8_t id, bool us, Geo &g);     // (an out-parameter: less code than a 6-byte return)
inline Geo geo(uint8_t id, bool us) { Geo g; geo(id, us, g); return g; }

bool covers(uint8_t id, uint8_t number, bool us);   // the bet wins on this number
uint8_t count(uint8_t id, bool us);                 // how many numbers it covers
inline uint8_t payout(uint8_t id, bool us) { return (uint8_t)(36 / count(id, us) - 1); }  // to 1
uint8_t straightId(uint8_t number);                 // the straight-up spot of a number

// The plate's words: the bet's name ("SPLIT", "1st DOZEN", "RED") and its
// numbers ("17/20", "13-18", "1-12"; empty for even money).
const char *kindName(uint8_t id, bool us);
char *numbers(char *p, uint8_t id, bool us);

}  // namespace spots
