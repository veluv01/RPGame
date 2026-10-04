// A game of Snakes and Ladders: whose turn, the dice, the move, ladders,
// snakes, bumps, and the events the presentation turns into motion. No
// graphics or sound here, so the host tests can play thousands of games
// (tools/tests/test_rules.cpp).
//
// The flow is CHBoardwalk's (and CHChess's Match): the game reports events
// and waits while the stage is busy showing them, so the rules never outrun
// the show.
//
// CLASSIC: one die. A 6 rolls again. 100 needs the exact roll: go past it
// and you bounce back. Squares are shared.
// ARCADE: two dice, and you choose which one to move by. Doubles move and
// roll again. Land on a rival and it is bumped down a row.
#pragma once
#include <stdint.h>
#include "Layout.h"

namespace game {

enum : uint8_t { SEATS = 4, NOBODY = 0xFF };
// State::kind: OFF, HUMAN, or CPU + its level (0..LEVELS-1).
enum Kind : uint8_t { OFF, HUMAN, CPU };
constexpr uint8_t LEVELS = 3;
enum Mode : uint8_t { CLASSIC, ARCADE };
constexpr uint8_t MAX_AGAIN = 2;        // extra rolls in one turn

// Everything a saved game holds.
struct State {
    uint32_t rng;
    uint8_t pos[SEATS];                 // 1..100
    uint8_t kind[SEATS];
    uint8_t players, cur, mode;
    uint8_t streak;                     // extra rolls taken this turn
    uint8_t over, winner;
    uint16_t turn;                      // turns begun, from 1
    uint8_t ladders[SEATS], snakes[SEATS], bumps[SEATS];   // climbed, slid down, rivals bumped
};
extern State st;

inline bool isCpu(uint8_t p)    { return st.kind[p] >= CPU; }
inline bool isHuman(uint8_t p)  { return st.kind[p] == HUMAN; }

enum Phase : uint8_t {
    P_OFF,
    P_TURN,         // a turn begins
    P_ROLL,         // the player at the turn rolls
    P_PICK,         // ARCADE: two dice down, one to choose
    P_MOVE,         // the token goes
    P_ENDTURN,
    P_OVER,
};

enum Ev : uint8_t {
    EV_START,       // a game began or was restored: redraw from the state
    EV_TURN,        // a = player, b = 1 if a human, c = extra rolls so far this turn
    EV_DICE,        // a, b = the dice (b = 0: one die), c = 1 if it earns another roll
    EV_PICK,        // a = player, b = which die (0, 1): a CPU's choice, shown before it moves
    EV_MOVE,        // a = player, b = from, c = the square reached, amount = steps (past 100: there and back)
    EV_LADDER,      // a = player, b = foot, c = top
    EV_SNAKE,       // a = player, b = head, c = tail
    EV_BUMP,        // a = the one bumped, b = from, c = to, amount = by whom
    EV_OVER,        // a = winner
};
struct Event {
    uint8_t type, a, b, c;
    int16_t amount;
};

struct Setup {
    uint8_t kind[SEATS];    // Kind per seat; the seats in use close up
    uint8_t mode;
    uint32_t seed;
};

void start(const Setup &s);
void update(bool stageBusy);        // once per logic tick
Phase phase();
inline bool active() { return phase() != P_OFF && phase() != P_OVER; }
bool humanToAct();                  // waiting for the player at the turn (a human)

bool roll();                        // P_ROLL
bool pick(uint8_t which);           // P_PICK: move by die 0 or 1
uint8_t die(uint8_t i);             // the dice last thrown

bool popEvent(Event &e);
bool peekEvent(Event &e);

// Saved games hold the state as the turn began: continuing replays the turn.
const State &checkpoint();
bool restore(const State &s);

// Debug and tests: the next dice (up to 8 queued).
void forceDice(uint8_t d1, uint8_t d2);

}  // namespace game
