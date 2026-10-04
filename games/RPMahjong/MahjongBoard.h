// Mahjong solitaire: the pile, the rules, the deal and the chips. No
// graphics and no sound - the stage shows what happens here, and the host
// tests (tools/tests) run it as it is.
//
// Tiles sit on a grid of half tiles: a tile at (x2, y2, z) covers x2..x2+1
// and y2..y2+1 on layer z, so neighbours can be offset by half a tile. A
// tile is free when nothing lies on any part of it and nothing touches its
// left side, or nothing its right.
#pragma once
#include <stdint.h>

namespace board {

constexpr uint8_t MAX_TILES = 144, NONE = 0xFF;
constexpr uint8_t FACES = 42;        // 27 suit tiles, 4 winds, 3 dragons, 4 flowers, 4 seasons
constexpr uint8_t FLOWER = 34, SEASON = 38;
constexpr uint8_t GROUPS = 36;       // any flower matches any flower, and so the seasons
constexpr uint8_t LAYOUTS = 4;
constexpr uint8_t MAX_SHUFFLES = 4;
constexpr uint8_t HIST = MAX_TILES / 2 + MAX_SHUFFLES;

// Chips.
constexpr uint8_t  PAIR_PAYS = 10;          // $ a pair, times the streak; flowers and seasons double
constexpr uint8_t  MAX_STREAK = 5;
constexpr uint16_t STREAK_FRAMES = 300;     // match again within this to raise the streak
constexpr uint8_t  HINT_COST = 25, SHUFFLE_COST = 100;
constexpr uint16_t CLEAR_BONUS = 500, PAR_SECS = 900;   // + $1 a second under par

struct Pos { uint8_t x2, y2, z; };

extern Pos pos[MAX_TILES];           // in drawing order (back to front)
extern uint8_t face[MAX_TILES];
extern uint8_t count;                // tiles in this layout
extern uint8_t left;                 // still on the table
extern uint8_t layout;
extern int32_t chips;
extern uint8_t streak;               // the last pair's multiplier (0: none yet)
extern uint16_t streakT;             // frames left to keep it going
extern uint32_t ticks;               // frames played
extern uint16_t bonus;               // paid for clearing the table (0 until then)
extern uint8_t mark;                 // the cursor's tile: the stage's, kept here to be saved with the game

inline uint8_t group(uint8_t f) { return f < FLOWER ? f : (f < SEASON ? FLOWER : FLOWER + 1); }
// Top-left of a tile in pile pixels: 8x12 tiles, each layer 2 px up and left.
inline int px(uint8_t i) { return pos[i].x2 * 4 - pos[i].z * 2; }
inline int py(uint8_t i) { return pos[i].y2 * 6 - pos[i].z * 2; }

void load(uint8_t layoutIndex);      // the layout's tiles, all present, faces not dealt

// Dealing: every deal can be cleared (it is built by taking a full table
// apart at random, two free tiles at a time). begin, then step until it
// returns true; the result is the same however the steps are split up.
void dealBegin(uint32_t seed);
bool dealStep(uint8_t budget);
bool dealing();
// Deals the tiles left on the table again (their faces only). false: no
// shuffle left, or nothing to shuffle. Step as a deal, then shuffleFailed()
// says whether these tiles cannot be dealt so that they clear.
bool shuffleBegin();
bool shuffleFailed();

bool present(uint8_t i);
bool isFree(uint8_t i);
uint8_t freeList(uint8_t *list);     // up to MAX_FREE
constexpr uint8_t MAX_FREE = 64;
uint8_t pairs();                     // matching pairs among the free tiles
bool hint(uint8_t &a, uint8_t &b);   // one of them (no charge: see spend)
bool canMatch(uint8_t a, uint8_t b);
bool match(uint8_t a, uint8_t b);    // takes the pair and pays for it
uint8_t lastPay();                   // what the last match paid, in $
bool canUndo();
bool undo(uint8_t &a, uint8_t &b);   // puts the last pair back, and takes its chips
bool cleared();
bool stuck();                        // tiles left, none to take
uint8_t shufflesLeft();
void spend(uint16_t cost);           // never below $0
void tick();                         // a frame of play: the clock and the streak
uint16_t secs();

// A game in progress, for the save: the deal's seed and what was taken.
struct Record {
    uint32_t seed;
    int32_t  chips;
    uint16_t secs;
    uint8_t  layout, n, streak, mark;
    uint8_t  ab[HIST][2];            // a == NONE: a shuffle
    uint8_t  pay[HIST / 2];          // a nibble each: $10s
};
void save(Record &r);
bool restore(const Record &r);       // false: not a game this build can replay

#if defined(CHSIM) || defined(CHTEST)
extern uint8_t dealOrder[MAX_TILES]; // the last deal's own way of clearing it, pair by pair
#endif
#ifdef CHTEST
extern const uint8_t *testLayout;    // tests: a layout of their own, instead of LAYOUT[]
#endif

}  // namespace board
