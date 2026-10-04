// The rules of backgammon, and nothing else: the board, a checker's step,
// what a roll allows, and every play it allows. No graphics, sound or
// timing, so the host tests and the trainer compile this file as it is
// (tools/tests/test_backgammon.cpp, tools/train/).
//
// Each side counts the points its own way: it moves from its 24 point down
// to its 1 point, its home board is 1..6, the bar is 25 and "off" is 0. My
// point p is the other side's point 25 - p. Every rule is then written once,
// for "the side to move".
#pragma once
#include <stdint.h>

namespace bg {

constexpr uint8_t OFF = 0, BAR = 25;
constexpr uint8_t WHITE = 0, RED = 1;          // WHITE's home is bottom right on screen
constexpr uint8_t CHECKERS = 15;

struct alignas(4) Board {   // (word-aligned: the network compares boards a word at a time)
    uint8_t n[2][26];       // [side][0 off, 1..24 points, 25 bar]
    uint8_t far[2];         // checkers outside the home board (7..25): none = may bear off
};

void reset(Board &b);                           // the starting position
void recount(Board &b);                         // far[] from n[] (after setting a position up by hand)
bool valid(const Board &b);                     // 15 a side, no point shared, far[] right

// One checker, one die. `from` is 1..25; the checker lands on from - die, or
// bears off when that is <= 0.
bool canStep(const Board &b, uint8_t side, uint8_t from, uint8_t die);
bool doStep(Board &b, uint8_t side, uint8_t from, uint8_t die);          // true: it hit a blot
void undoStep(Board &b, uint8_t side, uint8_t from, uint8_t die, bool hit);
inline uint8_t landing(uint8_t from, uint8_t die) { return from > die ? (uint8_t)(from - die) : OFF; }

// The dice still to play this turn: two different, or up to four the same.
struct Dice {
    uint8_t d[4];
    uint8_t n;
};
inline Dice roll2(uint8_t a, uint8_t b) {
    Dice r = {{a, b, a, a}, (uint8_t)(a == b ? 4 : 2)};
    return r;
}

// The most dice the side can play from here (the rules require that many).
uint8_t maxPlayable(Board &b, uint8_t side, const Dice &dice);

// A turn being played a step at a time (the human's, and the CPU's once it
// has chosen): what may still be played, and taking steps back.
struct Turn {
    Dice dice;              // still to play
    uint8_t need;           // dice that must still be played
    uint8_t forced;         // when just one of two different dice can be played: which (the higher if either can), else 0
    uint8_t steps;          // played so far
    uint8_t from[4], die[4], hit[4];
    uint8_t needWas[4];     // need before each step (the last checker off ends a turn early)
};
void beginTurn(Turn &t, Board &b, uint8_t side, uint8_t d1, uint8_t d2);
// May this step be played now? (Legal, and it leaves the rest playable.)
bool stepAllowed(Turn &t, Board &b, uint8_t side, uint8_t from, uint8_t die);
bool playStep(Turn &t, Board &b, uint8_t side, uint8_t from, uint8_t die);   // true: it hit
bool takeBack(Turn &t, Board &b, uint8_t side);                             // false: nothing to take back
inline bool turnDone(const Turn &t) { return t.need == 0; }

// Where the checker on `from` may be set down: single dice, then the dice
// added up through open points. Each target is a landing point (OFF = the
// tray) with the dice that take it there, in order.
struct Target {
    uint8_t to;
    uint8_t n;              // dice used
    uint8_t die[4];
    uint8_t hits;           // bit k: step k hits
};
uint8_t targets(Turn &t, Board &b, uint8_t side, uint8_t from, Target *out);   // <= 4

// Every play a roll allows, one final position each, without storing any:
// begin() then next() until it returns false. After each true, the board
// holds the position after the play and steps()/from()/die() describe it;
// after false the board is as it was. A roll with no legal move gives one
// play of no steps (the pass).
struct Plays {
    uint8_t side, need, depth, pass;
    uint8_t a, b;           // the dice, a >= b
    bool dbl, yielded, done;
    uint8_t from[4], die[4], hit[4];
};
void begin(Plays &g, Board &b, uint8_t side, uint8_t d1, uint8_t d2);
bool next(Plays &g, Board &b);

// The end of a game: 0 not over, else 1 single, 2 gammon, 3 backgammon for
// `winner`.
uint8_t result(const Board &b, uint8_t &winner);
uint16_t pips(const Board &b, uint8_t side);
bool contact(const Board &b);                   // can the sides still block or hit each other?

// Dice: PCG32, one stream per use (the dice, the CPU's own noise) so that
// nothing the CPU or the presentation does can change a roll.
struct Rng {
    uint64_t state;
    void seed(uint32_t s, uint32_t stream);
    uint32_t next();
    uint8_t die();          // 1..6, exactly uniform
};

}  // namespace bg
