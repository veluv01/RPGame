// One game at a table: whose turn, the toss or the bidding before it, the
// cursor, the dealer's thinking, the blitz clock. Pure (no graphics): it
// reports what happened as events, and waits while the stage says it is
// still showing the last ones.
#pragma once
#include "Rules.h"
#include "Cpu.h"

// The RPGame library's button masks (rpgame/Input.h), repeated so the host
// tests build this without the library.
enum : uint8_t { K_A = 1, K_B = 2, K_UP = 4, K_DOWN = 8, K_LEFT = 16, K_RIGHT = 32 };

enum class Phase : uint8_t { Intro, Toss, Bid, BidShow, Human, Think, Reach, Settle, Between, Over };

enum Ev : uint8_t {
    EV_START,       // a fresh board
    EV_TOSS,        // a = who the coin gave the move to
    EV_BID,         // a = who won the bid
    EV_MOVE,        // the cursor moved
    EV_DENY,        // not there
    EV_ARG,         // the size or bid changed
    EV_REACH,       // a = the cell the dealer goes for
    EV_PLACE,       // a = cell, b = side
    EV_GONE,        // a = the cell that vanished
    EV_SMALL,       // a = the small board decided
    EV_BOARD,       // BLITZ: a = that board's result
    EV_TIMEOUT,     // BLITZ: the shot clock played for you
    EV_BUMP,        // DARK: a = the hidden mark walked into
    EV_BOOM,        // MINES: a = the cell that blew
    EV_OVER,        // a = result
};
struct Event { uint8_t type, a, b; };

struct Options { uint8_t dealer, goal, sound, look, unused[4]; };

struct Stats {
    uint16_t played[MODE_COUNT], won[MODE_COUNT];
    int32_t bestPurse, biggestWin;
    uint16_t bestStreak, cats, gamesWon, gamesBroke;
};

// What a run carries from table to table (and into the save).
struct Casino {
    int32_t purse;
    Options opt;
    Stats stats;
    uint8_t mode, ante, streak;     // ante: index into ANTES
};
constexpr int32_t START_PURSE = 100;
constexpr uint8_t ANTE_COUNT = 6;
extern const uint8_t ANTES[ANTE_COUNT];
extern const int16_t GOALS[3];      // 0 = endless

constexpr uint16_t BLITZ_TICKS = 60 * 60;
constexpr uint8_t SHOT_TICKS = 180;
constexpr uint8_t MINE_COUNT = 4;

struct Match {
    Board b;
    Mode mode;
    uint8_t level;
    Phase phase;
    uint16_t t;                     // ticks in this phase
    uint8_t cur;                    // the cursor's cell
    uint8_t size;                   // GOBBLE: the size in hand
    cpu::Move pend;                 // the dealer's choice, on its way
    bool thought;
    uint8_t forced;                 // tests: the dealer's next cell (NONE: he thinks for himself)
    uint8_t bidP, bidC;
    uint8_t first;                  // who opens the next board
    uint16_t clock;                 // BLITZ: ticks left
    uint8_t shot;                   // BLITZ: ticks left for this move
    uint8_t wins, boards;           // BLITZ
    uint8_t result;                 // the game's Result
    bool two;                       // two players, turn about on the one handheld
    bool iso = false;                       // the board is seen in iso: the D-pad goes by screen direction
    uint8_t score[2] = {};               // two players: games won (start() leaves it alone)
    uint32_t rng = 1;
    Event ev[6];
    uint8_t nEv;

    void seed(uint32_t s) { rng = s ? s : 1; }
    void start(Mode m, uint8_t lvl, bool twoPlayers = false);
    void update(uint8_t pressed, uint8_t rep, bool busy);
    void place(uint8_t cell, uint8_t arg);
    // What comes back to the purse (the ante was paid at the start).
    int32_t payout(int32_t ante, uint8_t streak) const;

private:
    void emit(uint8_t type, uint8_t a = 0, uint8_t b2 = 0);
    void to(Phase p) { phase = p; t = 0; }
    void nextTurn();
    void goTurn();
    void finish();
    void human(uint8_t pressed, uint8_t rep);
    void nudge(uint8_t rep);
    void nudgeIso(uint8_t rep);
};
