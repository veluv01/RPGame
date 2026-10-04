// A game: the board, the racks, the bag, the scores and whose turn it is.
// No graphics and no input: the screens ask it to play, swap or pass, and
// read `last` to show what happened.
#pragma once
#include <stdint.h>
#include "Words.h"

namespace game {

enum Mode : uint8_t { VS_CPU, TWO_PLAYER };
constexpr uint8_t LEVELS = 3;
constexpr uint8_t SCORELESS_END = 6;        // turns in a row without a score end the game

struct Setup {
    uint8_t mode, level;
    uint32_t seed;
};

struct Play {
    uint8_t n;
    wd::Placement p[wd::RACK];
};

enum Kind : uint8_t { NOTHING, PLAYED, SWAPPED, PASSED };
struct Last {
    uint8_t kind, side, n;
    uint8_t cell[wd::RACK];
    int16_t score;
    wd::Span main;
};

extern Setup setup;
extern uint8_t board[wd::CELLS];
extern uint8_t rack[2][wd::RACK];           // 0: an empty slot, else a tile 1..27
extern int16_t score[2];
extern uint8_t bagLeft;
extern uint8_t turn;                        // whose turn: 0 (you, or player 1) or 1
extern bool over;
extern int16_t rackPenalty[2];              // at the end: what each rack cost (or won) its side
extern Last last;

void start(const Setup &s);
inline bool cpuTurn() { return !over && setup.mode == VS_CPU && turn == 1; }
uint8_t tilesIn(uint8_t side);

// Are all the words a checked play makes in the dictionary? If not, `bad`
// is the first that is not. Touches the card: after gfx_wait() only.
bool wordsOk(const Play &pl, const wd::Result &r, bool coreOnly, wd::Span &bad);

// The moves. play() takes a play wd::check() passed, whose tiles are in the
// mover's rack.
void play(const Play &pl, const wd::Result &r);
inline bool canSwap() { return bagLeft >= wd::RACK; }
void swap(uint8_t slots);                   // bit i: rack slot i goes back in the bag
void pass();

uint8_t winner();                           // 0, 1, or 2 for a tie

// A game in progress, packed for the save page.
struct Record {
    uint8_t cells[142];                     // 225 squares, 5 bits each (and a byte to spill into)
    uint8_t blank[2];                       // squares holding a blank (0xFF: none)
    uint8_t rack[2][wd::RACK];
    int16_t score[2];
    uint32_t rng;
    uint8_t turn, scoreless, mode, level;
};
void save(Record &r);
bool load(const Record &r);

}  // namespace game
