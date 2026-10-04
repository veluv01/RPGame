// Checkers: the rules and the CPU. Squares are 0..63 (a1 = 0, h8 = 63) as
// the board is drawn; pieces stand on the dark squares only (a1 is dark),
// White at the bottom, moving up, and moving first.
//
// The game moves a step at a time: a slide, or one jump. A jump that can be
// followed by another leaves the turn with the same piece (chainSq()) until
// it has no more to take, so a multiple jump is several steps - played, shown
// and searched one hop at a time.
//
// While think() runs, the board is mid-search: the poll hook (and everything
// it calls: drawing, the stage, debug commands) must not call anything here
// except rootFrom() and nodes().
#pragma once
#include <stdint.h>

namespace eng {

enum : uint8_t { EMPTY = 0, MAN, KING };
constexpr uint8_t BLACK = 8;                    // colour bit of a piece code
constexpr uint8_t TYPE = 7;

// House rules (a bit each). American checkers is R_FORCED alone.
enum : uint8_t {
    R_FORCED = 1,       // a jump must be taken when there is one
    R_FLYING = 2,       // kings slide any distance, and land anywhere beyond what they take
    R_BACKJUMP = 4,     // men jump backwards too (they still only step forwards)
    R_ALL = 7,
};

struct Step { uint8_t from, to, cap; };         // squares; cap: the piece jumped, 0xFF none
constexpr uint8_t NONE = 0xFF;

enum Status : uint8_t {
    NORMAL,
    LOST_NO_PIECES,     // the side to move has nothing left
    LOST_BLOCKED,       // ... or nothing that can move
    DRAW_40,            // 40 moves each without a jump or a man moving
    DRAW_REPETITION,    // the same position a third time
};

// A position the game can come back to without replaying from the start
// (long games, saved games). 16 bytes. Bits are dark squares, a1 c1 e1 g1
// b2 ... h8.
struct Snap {
    uint32_t white, black, kings;
    uint8_t blackToMove, noProgress;
    uint16_t ply;
};

void newGame(uint8_t rules);
uint8_t rules();
void snapshot(Snap &s);                         // between moves only
void restore(const Snap &s, uint8_t rules);     // the repetition count restarts

// The steps that may be played now, in a fixed order (a saved game is the
// indices into it). max: room in out.
uint8_t steps(Step *out, uint8_t max);
uint8_t stepsFrom(uint8_t from, Step *out, uint8_t max);     // one piece's
uint8_t stepCount();
int16_t indexOf(uint8_t from, uint8_t to);      // -1 if not legal
bool stepAt(uint8_t index, Step &out);          // the step with that index
// Play the step with that index: true once the move is complete (the turn
// has passed), false if the piece has another jump to make.
bool play(uint8_t index);
uint8_t chainSq();                              // the piece in the middle of a multiple jump, NONE if none
bool canJump();                                 // the side to move has a jump
Status status();                                // between moves
uint8_t pieceAt(uint8_t square);                // EMPTY or type | BLACK
uint8_t count(bool black);                      // pieces on the board
bool blackToMove();
uint16_t ply();                                 // moves played

// The CPU: node budget, and how far below the best a step may score and
// still be picked.
struct Level { uint32_t nodes; int16_t margin; };
int16_t think(const Level &lv);                 // blocking; calls the poll hook. A step index, -1: none/aborted
int16_t benchThink(const Level &lv);            // no poll hook (speed tests)
void abort();                                   // from the poll hook
bool aborted();
int16_t lastScore();                            // side to move's view
uint32_t nodes();
uint8_t rootFrom();                             // root piece being searched (hook-safe), NONE if none

#if defined(CHSIM) || defined(CHTEST)
// 32 characters, a1 c1 e1 g1 b2 ... h8: . w W b B (capitals are kings).
void setup(const char *cells, bool blackToMove, uint8_t rules);
#endif

void seed(uint32_t s);                          // the AI's own random stream
uint8_t rand8();

// Called every POLL_NODES nodes during think() (null = nothing).
constexpr uint8_t POLL_NODES = 32;
extern void (*pollHook)();
bool thinking();

}  // namespace eng
