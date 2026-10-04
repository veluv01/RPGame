// A match of backgammon: games to a number of points (one: a single game),
// the doubling cube and the Crawford rule; in each game the opening roll,
// whose turn, the dice, a turn played a checker at a time (and taken back),
// the CPU, and the events the presentation turns into motion. No graphics
// or sound here, so the host tests can play thousands of games
// (tools/tests/test_match.h).
//
// The flow is CHChess's and CHBlackjack's: the match reports events and
// waits while the stage is busy showing them, so the rules never outrun the
// show. The CPU thinks a slice per tick (Ai.h) while frames go on.
#pragma once
#include <stdint.h>
#include "config.h"
#include "Rules.h"
#include "Ai.h"

namespace match {

enum Mode : uint8_t { VS_CPU, TWO_PLAYER };
constexpr uint8_t LEVELS = ai::LEVELS;
constexpr uint8_t CENTRE = 2;       // the cube's owner while nobody owns it

// Points in events are in the moving side's own numbering (bg::BAR, bg::OFF).
enum Ev : uint8_t {
    EV_START,       // a game began or was restored: redraw from board, the score and the cube
    EV_OPENING,     // the opening roll: a = White's die, b = Red's (equal: thrown again)
    EV_TURN,        // a = side, b = 1 if a human plays it, c = 1 if its dice are already on the table
    EV_ROLL,        // a, b = the dice, c = how many must be played (0: no move), d = side
    EV_THINK,       // the CPU starts weighing its plays
    EV_STEP,        // a = from, b = to, c = flags, d = the die
    EV_UNDO,        // a step taken back: a = from, b = to (as it was played), c = flags, d = the die
    EV_PICKUP,      // a = side: its dice are picked up, the turn is over
    EV_DOUBLE,      // a = side offering, b = the value offered (log2)
    EV_TAKE,        // a = side taking: the cube is theirs, at the value offered
    EV_BEAVER,      // a = side doubled: it takes and redoubles at once, b = the value now (log2)
    EV_RACCOON,     // a = side that doubled: it redoubles the beaver, b = the value now (log2)
    EV_OVER,        // a = winner, b = 1 single / 2 gammon / 3 backgammon, c = Reason, d = points won
};
enum : uint8_t {
    F_HIT = 1,      // the step hit a blot (taken back: the blot returns from the bar)
    F_MORE = 2,     // another step of the same move follows (the checker travels on)
};
enum Reason : uint8_t { BY_PLAY, BY_RESIGNATION, BY_PASS };
struct Event { uint8_t type, a, b, c, d; };

struct Setup {
    uint8_t mode;       // Mode
    uint8_t level;      // VS_CPU: 0..LEVELS-1 (the human is White, the CPU Red)
    uint8_t length;     // points to win the match: 1 (a single game, no cube), 3, 5, 7...
    uint32_t seed;      // the dice (and the CPU's own generator)
};

// State the stage and the screens read.
extern bg::Board board;             // the position, steps played so far included
extern Setup setup;
extern uint8_t winner, how, reason; // the last game's end
extern uint8_t points;              // ... and what it was worth
extern uint8_t score[2];            // the match: points each
extern uint8_t cube;                // the cube: log2 of its value (0: 1)
extern uint8_t cubeOwner;           // bg::WHITE, bg::RED or CENTRE
extern bool crawford;               // this is the Crawford game: no doubling
extern uint16_t turns;              // turns completed this game
extern bool autoPlay;               // the dice are thrown when there is no cube to decide on, forced plays made
extern bool beavers;                // a doubled side may beaver, and the doubler then raccoon

void start(const Setup &s);         // a new match
void nextGame();                    // after a game, when the match goes on
void update(bool stageBusy);        // once per logic tick
bool active();                      // a game is on
bool between();                     // a game is over, the match goes on
bool matchOver();
uint8_t side();                     // whose turn
uint8_t die(uint8_t i);             // its roll (0, 1), as thrown
const bg::Board &turnStart();       // the position as its turn began
bool isHuman(uint8_t side);
bool humanToRoll();                 // waiting for the dice to be thrown (the opening roll too)
bool humanToMove();                 // waiting for a checker to be moved
bool humanToConfirm();              // the dice are played: pick them up, or take a move back
bool humanToAnswer();               // a human has been doubled (take, pass or beaver) or beavered (take or raccoon)
bool beavered();                    // ... beavered: take() accepts it, raccoon() redoubles
bool canBeaver();                   // the doubled side may beaver (the doubler raccoon)
bool cpuThinking();
inline uint16_t cubeValue() { return (uint16_t)(1u << cube); }
bool cubeLive();                    // a match with a cube: longer than one point
bool postCrawford();                // the Crawford game has been played

// The human's turn.
void roll();
bool canDouble();                   // the side to roll may double now
void offerDouble();
void take();
void pass();
void beaver();                      // doubled: take and redouble at once, keeping the cube
void raccoon();                     // beavered: redouble again, and the cube is the doubler's
uint8_t targetsFrom(uint8_t from, bg::Target *out);     // where the checker on `from` may go (<= 4)
bool play(uint8_t from, const bg::Target &t);           // set it down there
bool canTakeBack();
bool takeBack();                    // the last checker set down goes back
void confirm();                     // pick the dice up: the turn ends
void resign();                      // the side to move (vs the CPU: the human) gives the game up, at the cube's value
// Steps played this turn (the coach reads the play back).
uint8_t steps(uint8_t *from, uint8_t *die, uint8_t *hit);

bool popEvent(Event &e);
bool peekEvent(Event &e);

// Saved games: the match, the position as the turn began, its roll, and the
// dice generators (so reloading can never change a roll).
struct Record {
    uint8_t pts[26];                // [i]: White's count | Red's << 4, each in its own numbering
    uint8_t state;                  // the opening roll to throw, a turn to roll, or a roll in hand
    uint8_t vars[sizeof(Setup) + 27];   // the match (see Match.cpp)
};
void save(Record &r);
bool load(const Record &r);

// Scripts and tests (the simulator, the host tests, a device debug build):
// a position (see Match.cpp), dice to come up next, and the match's score.
#if defined(CHSIM) || defined(CHTEST) || (defined(CHGAME_DEBUG) && CHGAME_DEBUG)
#define MATCH_SCRIPTED 1
bool startPosition(const Setup &s, const char *spec, uint8_t sideToRoll);
void stackDice(const char *digits);         // "6431..": the next rolls, a die a digit
void setScore(uint8_t length, uint8_t white, uint8_t red, uint8_t cubeLog, uint8_t owner, bool crawfordGame);
#endif

}  // namespace match
