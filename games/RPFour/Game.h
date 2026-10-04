// A game of four in a row: whose turn it is, the CPU's thinking, and what
// the dealer makes of each move.
//
// No drawing here. The game reports what happened as events (a disc
// dropped, a line said, the game over) and the stage shows them; while the
// stage is busy the CPU waits its turn, so it never plays over its own
// words or a disc still falling.
//
// Against the CPU you are RED and the dealer GOLD. His lines come from what
// is true on the board - the cells either side could win in right now,
// before and after each move - and from what his search found, so what he
// says is about the position in front of you.
#pragma once
#include <stdint.h>
#include "Rules.h"
#include "Ai.h"
#include "Taunt.h"

namespace game {

enum Mode : uint8_t { VS_CPU, TWO_PLAYER };
constexpr uint8_t LEVELS = ai::LEVELS;
constexpr uint8_t YOU = c4::RED, DEALER = c4::GOLD;

struct Setup {
    uint8_t mode, level;
    uint8_t first;              // the side that moves first
    uint8_t quick;              // shorter pauses
    uint32_t seed;
};

enum EventType : uint8_t {
    EV_NONE,
    EV_START,
    EV_TURN,                    // a: the side, b: 1 if a person plays it
    EV_DROP,                    // a: the side, b: column, c: row
    EV_SAY,                     // a: the dealer's face, b: 1 if the game waits for him; the words are in said
    EV_OVER,                    // a: the winner (c4::NOBODY: a draw); four[], and his last word in said
};
struct Event { uint8_t type, a, b, c; };

extern c4::Board board;
extern Setup setup;
extern uint8_t winner;              // once over
extern uint8_t four[4];             // the winning cells (col * 7 + row)
extern char said[taunt::LINE_MAX];  // the line of the last EV_SAY / EV_OVER
extern uint8_t lastKind;            // ... and its kind (the tests and the debug protocol)

void start(const Setup &s);
void update(bool stageBusy);        // once per tick
bool popEvent(Event &e);

bool active();                      // a game is being played
uint8_t side();                     // to move
bool humanToMove();
bool cpuThinking();
uint8_t thinkColumn();              // the column the CPU is weighing
bool drop(uint8_t col);             // the person to move plays; false: the column is full
void resign();                      // the person to move gives up

// A game in progress, for the save page.
struct Record {
    c4::Board board;
    Setup setup;
    uint8_t turn, flags;
};
void save(Record &r);
bool load(const Record &r);
// Debug and tests: a position from a string of columns ("4453..." as the
// moves were played, 1..7), `first` moving first.
bool startPosition(const Setup &s, const char *moves);

}  // namespace game
