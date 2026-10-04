// A game of checkers: whose turn, the CPU, the rules' verdicts, undo, and
// the events the presentation turns into motion. No graphics or sound here,
// so the host tests can play thousands of games (tools/tests/test_checkers.cpp).
//
// The flow mirrors CHBlackjack's Round: the match reports events and waits
// while the stage is busy showing them, so the rules never outrun the show.
// A move is played a hop at a time (a multiple jump is several EV_HOPs, the
// turn staying with the piece between them). The CPU's search is blocking;
// while it runs, the engine's poll hook keeps frames coming (Frame.h) and
// update() is not re-entered.
#pragma once
#include <stdint.h>
#include "Engine.h"

namespace match {

enum Mode : uint8_t { VS_CPU, TWO_PLAYER };

enum Result : uint8_t { PLAYING, WHITE_WINS, BLACK_WINS, DRAW_40, DRAW_REPETITION };
enum Reason : uint8_t { BY_CAPTURE, BY_BLOCK, BY_RESIGNATION, BY_RULE };

enum Ev : uint8_t {
    EV_START,          // a game began (or was restored/undone): redraw from board[]
    EV_TURN,           // a = side to move (0 white, 1 black), b = 1 if a human moves, c = 1 if it must jump
    EV_THINK,          // the CPU starts thinking
    EV_PICK,           // the CPU chose: a = from, b = to, c = 1 if it carries on a multiple jump, captured = 1 for a jump
    EV_HOP,            // a = from, b = to, piece, captured (code), capSq, hop, flags
    EV_OVER,           // a = Result, b = Reason
};
enum : uint8_t {
    H_LAST = 1,        // the move ends with this hop
    H_CROWN = 2,       // ... which makes a king
    H_FINAL = 4,       // ... and wins the game
};
struct Event {
    uint8_t type, a, b, c;
    uint8_t piece, captured, capSq;
    uint8_t hop;       // jumps in this move so far, this one included (0: a slide)
    uint8_t flags;
};

struct Setup {
    uint8_t mode;       // Mode
    uint8_t humanBlack; // VS_CPU: the human plays Black (the CPU opens)
    uint8_t level;      // VS_CPU: 0..LEVELS-1
    uint8_t rules;      // eng::R_* bits
    uint32_t seed;      // the CPU's random stream
};

constexpr uint8_t LEVELS = 3;
extern const eng::Level LEVEL[LEVELS];

// State the stage and the screens read.
extern uint8_t board[64];           // the position (piece codes), hop by hop
extern Setup setup;
extern uint8_t lastFrom, lastTo;    // the last move, 0xFF: none
extern Result result;
extern Reason reason;
extern uint16_t plies;              // moves played

void start(const Setup &s);
#if defined(CHSIM) || defined(CHTEST)
// Scripted positions: 32 cells (see eng::setup).
void startAt(const Setup &s, const char *cells, bool blackToMove);
#endif
void update(bool stageBusy);        // once per logic tick
bool active();                      // a game is on (not over)
bool blackToMove();
bool humanToMove();                 // waiting for input
bool cpuThinking();
uint8_t chainSq();                  // the piece that must jump on, 0xFF: none
bool mustJump();                    // the side to move has to take

// Human moves, a hop at a time.
uint8_t movesFrom(uint8_t from, uint8_t *to, uint8_t *capture);   // destinations (<= 16)
bool play(uint8_t from, uint8_t to);                              // false if illegal
bool canUndo();
bool undo();                        // vs CPU: back to your previous move; 2P: one move
void resign();
void abortThink();                  // from the think hook: the next update() drops the search

bool popEvent(Event &e);
bool peekEvent(Event &e);

// Saved games: a snapshot plus the steps since, replayed on load.
struct Record {
    eng::Snap base;                 // 16 B
    uint8_t baseIsStart;
    uint8_t mode, humanBlack, level, rules;
    uint8_t n;                      // steps in m[]
    uint8_t m[180];                 // each an index into the steps legal where it was played
};
void save(Record &r);
bool load(const Record &r);

}  // namespace match
