// A match of dominoes: rounds to a target score; in each round the deal,
// the first tile set, whose turn, drawing and passing, the CPU, and the
// events the presentation turns into motion. No graphics or sound here, so
// the host tests can play thousands of matches (tools/tests).
//
// The flow is CHChess's and CHBlackjack's: the match reports events and
// waits while the stage is busy showing them, so the rules never outrun the
// show.
#pragma once
#include <stdint.h>
#include "config.h"
#include "Dominoes.h"
#include "Ai.h"

namespace match {

enum Mode : uint8_t { VS_CPU, TWO_PLAYER };
constexpr uint8_t LEVELS = ai::LEVELS;

enum Ev : uint8_t {
    EV_START,       // a round began (a = 0: the deal follows) or was restored (a = 1): redraw from round
    EV_DEAL,        // seven tiles each
    EV_TURN,        // a = side, b = 1 if a human plays it, c = 1 if it is only to set the first tile
    EV_PLAY,        // a = side, b = tile, c = arm, d = points scored
    EV_DRAW,        // a = side, b = tile, from the boneyard
    EV_PASS,        // a = side
    EV_ROUND,       // a = winner (2: nobody), b = dom::Reason, c = points
};
struct Event { uint8_t type, a, b, c, d; };

struct Setup {
    uint8_t mode;       // Mode
    uint8_t level;      // VS_CPU: 0..LEVELS-1 (the human is side 0, the CPU side 1)
    uint8_t game;       // dom::Game
    uint8_t target;     // the score that wins the match, in fives (20: 100)
    uint32_t seed;
};

// State the stage and the screens read.
extern dom::Round round;            // tiles played so far included
extern Setup setup;
extern uint16_t score[2];
extern uint8_t winner, reason, points;  // the last round's end
extern uint8_t rounds;              // rounds finished

void start(const Setup &s);         // a new match
void nextRound();                   // after a round, when the match goes on
void update(bool stageBusy);        // once per logic tick
bool active();                      // a round is on
bool between();                     // a round is over, the match goes on
bool matchOver();
bool isHuman(uint8_t side);
bool humanToPlay();                 // waiting for a tile
bool humanToDraw();                 // ... with nothing to play: for a draw from the boneyard
inline uint16_t target() { return (uint16_t)(setup.target * 5); }

// The human's turn.
uint8_t options(uint8_t tile, uint8_t *arms);   // the arms it may go on (0: none), alike ones merged
bool play(uint8_t tile, uint8_t arm);
void draw();
bool hint(ai::Move &m);             // what the SHARK would play

bool popEvent(Event &e);
bool peekEvent(Event &e);

// Saved games: the round as it stands, the score and the generators.
struct Record {
    dom::Round round;
    Setup setup;
    uint16_t score[2];
    uint32_t rng[2];
    uint8_t leader, rounds, pad[2];
};
void save(Record &r);
bool load(const Record &r);

// Scripts and tests (the simulator, the host tests, a device debug build).
#if defined(CHSIM) || defined(CHTEST) || (defined(CHGAME_DEBUG) && CHGAME_DEBUG)
#define MATCH_SCRIPTED 1
// The next deal: "66 65 31 ..": the first seven to side 0, the next seven to
// side 1, then the boneyard in the order it will be drawn; the tiles not
// named follow, shuffled.
void stackDeal(const char *tiles);
void setScore(uint16_t a, uint16_t b);
void endRound(uint8_t winner);      // as if the side had just played its last tile
#endif

}  // namespace match
