// What the dealer has to say. He is courtesy itself, and something of a
// coach: he praises a good move, points out what a move missed or gave
// away, congratulates you when you win and encourages you when you lose.
//
// The game decides when (Game.cpp knows what is true on the board: who
// can win, who just missed it, what the search saw coming); this picks the
// words. Lines are at most three rows of twelve characters, in capitals and
// the 3x5 font's punctuation, a newline between rows; '#' is replaced by a
// number. rpgame check checks every line fits the bubble.
#pragma once
#include <stdint.h>
#include "Rules.h"

namespace taunt {

enum Kind : uint8_t {
    // Against the dealer.
    START,          // a new game
    WIN_NOW,        // he is about to drop the fourth disc (and is sorry about it)
    GIFT,           // ... into a win your last disc opened for him
    UNBLOCKED,      // ... that you could have blocked
    FORCED,         // the search has found a win in # of his moves
    MUST_BLOCK,     // you threatened; he blocks
    MISSED,         // you had a win and played elsewhere
    BLOCKED,        // you blocked his
    DOUBLE,         // you have two ways to win: he cannot stop both
    LOSING,         // the search has found he is lost
    TRAP,           // he has two ways to win
    THREAT,         // he has one
    OPEN_CENTRE, OPEN_EDGE,
    AHEAD, BEHIND, BANTER,
    IDLE,           // you are taking your time
    DRAWISH,        // the board is filling up with nothing in it
    HE_LOST, HE_WON, DRAWN,
    // Two players: he watches.
    P2_START, P2_MISSED, P2_DOUBLE, P2_BLOCK, P2_THREAT, P2_BANTER, P2_WON,
    KINDS
};

// The dealer's face for a line (render/Table.h's expressions).
enum Face : uint8_t { NORMAL, ANGRY, RAISED, BLINK, SMILE, SURPRISED, TALK };

constexpr uint8_t LINE_MAX = 40;        // a line and its terminator

void reset();                            // a new game: every line may be said again
// A line of the kind into out (one not used since the kind last ran out);
// returns the face that goes with it.
uint8_t pick(uint8_t kind, c4::Rng &rng, char *out, uint8_t number = 0);
// For the checks: line i of a kind (false: no such line).
bool line(uint8_t kind, uint8_t i, char *out);

}  // namespace taunt
