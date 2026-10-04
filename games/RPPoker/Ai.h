// The CPU players.
//
// A CPU judges its hand by playing the rest of it out many times at random
// (Monte Carlo): unknown cards dealt to the opponents still in, the board or
// the stud streets completed, and in Five Card Draw everyone's draw played
// out the way the CPUs draw. The share of those deals it wins is its equity,
// which it weighs against the price of a call. The work is spread over
// frames (step()), so the table keeps moving while a CPU thinks.
//
// The CPU sees only a View: the cards on the table and its own. The tests
// check that the other players' hidden cards never change a decision.
#pragma once
#include <stdint.h>

namespace ai {

struct View {
    uint8_t game, street, seat;
    uint8_t own[7], nOwn;           // its cards so far (stud: down and up)
    uint8_t board[5], nBoard;
    uint8_t dead[8], nDead;         // cards it has seen that nobody holds (its discards)
    uint8_t nOpp;                   // opponents still in
    uint8_t oppUp[3][4], nOppUp[3]; // stud: each opponent's face-up cards
    uint8_t preDraw;                // Five Card Draw: the draw is still to come
    uint8_t canRaise, lastToAct;
    uint8_t raisesAgainst;          // bets and raises by others this street
    uint8_t youRaised;              // ...one of them yours (SHARK reads you)
    int32_t pot;                    // everything in the middle, bets included
    int32_t toCall, minTo, maxTo, bet, stack, bb;
    uint8_t limit;
};

struct Read { uint16_t acts, raises, bluffs; };      // what SHARK has seen you do

enum Move : uint8_t { M_FOLD, M_CALL, M_RAISE };     // call = check when nothing to call
struct Choice { uint8_t move; int32_t to; };

uint16_t samplesFor(uint8_t level, uint8_t game);
void begin(const View &v, uint32_t seed, uint16_t samples);
bool step(uint8_t k);                // k more deals; true once all are done
uint16_t equity();                   // Q8: 256 = wins every deal
Choice decide(const View &v, uint8_t level, int8_t quirk, const Read &rd);

// Five Card Draw: which cards to throw (bit i = card i): never more than
// three, or four keeping an ace.
uint8_t discards(const uint8_t *five, uint8_t level);

}  // namespace ai
