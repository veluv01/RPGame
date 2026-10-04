// The craps table: bets, money, the point and the dice.
//
// Pure logic: no graphics, no sound, host-testable (tools/tests). The play
// screen calls add()/take() as the player moves chips and throwDice() when
// the dice leave the shooter's hand. throwDice() settles everything at once
// - every bet's fate goes into res[] - and the presenter then shows it at
// its own pace (chips swept, paid, moved). Because the money has already
// moved, saving or quitting in the middle of the show is always consistent.
#pragma once
#include <stdint.h>

// Every spot that can hold chips. COME4..COME10 and CODDS4..CODDS10 are the
// come bets the dealer has moved into the number boxes, and their odds.
enum Bet : uint8_t {
    PASS, DONT, PASS_ODDS, DONT_ODDS, COME, FIELD,
    PLACE4, PLACE5, PLACE6, PLACE8, PLACE9, PLACE10,
    HARD4, HARD6, HARD8, HARD10,
    ANY7, ANYCRAPS, YO,
    COME4, COME5, COME6, COME8, COME9, COME10,
    CODDS4, CODDS5, CODDS6, CODDS8, CODDS9, CODDS10,
    BET_COUNT
};

// The six box numbers, in layout order; box(n) is the index of n (or -1).
extern const uint8_t BOX_NUM[6];
int8_t box(uint8_t n);

enum : uint8_t { TABLE_CLASSIC = 0, TABLE_BEGINNER = 1 };
enum : uint8_t { ODDS_345 = 0, ODDS_2X = 1, ODDS_10X = 2 };
enum : uint8_t { GOAL_1000 = 0, GOAL_5000, GOAL_ENDLESS };

// All-zero is the default for everything: the game object lives in .bss.
// The options menu indexes this struct as bytes, in this order.
struct Options {
    uint8_t table;      // TABLE_CLASSIC / TABLE_BEGINNER
    uint8_t odds;       // ODDS_345 / ODDS_2X / ODDS_10X
    uint8_t goal;       // GOAL_*
    uint8_t speed;      // 0 normal, 1 fast
    uint8_t theme;      // felt colour (pal::Theme)
    uint8_t sound;      // 0 on, 1 off
    uint8_t pad[2];
};

struct Stats {
    uint32_t rolls;
    uint16_t pointsMade, sevenOuts, longestHand, hardways;
    int32_t  bestPurse, biggestWin;
    uint16_t banksBroken, timesBroke;
};

// Why a chip or a roll was refused (the stickman says so).
enum Deny : uint8_t {
    D_OK,
    D_CLOSED,           // not on this table (Beginner)
    D_COMEOUT,          // line bets go down on the come-out only
    D_COME_CLOSED,      // the Come opens once there is a point
    D_NEED_POINT,       // odds wait for a point
    D_NEED_LINE,        // odds need the line bet under them
    D_MAX,              // table max on this spot
    D_MAX_ODDS,         // odds at their limit
    D_NO_CHIPS,         // purse empty
    D_CONTRACT,         // pass / come points stay up
    D_EMPTY,            // nothing there to take
    D_ROLL_LINE,        // the shooter must bet the line
    DENY_COUNT
};

// What a roll did to one spot.
enum ResKind : uint8_t {
    R_NONE,             // not decided (or off)
    R_LOSE,             // swept to the dealer
    R_WIN,              // win paid, the bet stays up
    R_WIN_HOME,         // win paid, the bet comes home too
    R_RETURN,           // the bet comes home unpaid (odds off on the come-out)
    R_MOVE,             // a come bet travels to its number (to)
};

struct Res {
    uint8_t  kind;
    uint8_t  to;        // R_MOVE: destination spot
    uint16_t stake;     // the bet before the roll
    int32_t  win;       // R_WIN / R_WIN_HOME: winnings
};

// Roll history kinds (the board on the wall colours them).
enum : uint8_t { H_PLAIN, H_WINNER, H_CRAPS, H_SEVEN_OUT, H_POINT };

class Craps {
public:
    static const uint16_t TABLE_MAX = 500;
    static const int32_t  START_PURSE = 500;

    Options  opt;
    Stats    stats;
    int32_t  purse;                 // set by newGame()
    uint16_t bet[BET_COUNT];
    uint8_t  point;                 // 0 = off (come-out)
    uint8_t  d1, d2;                // the last roll
    uint8_t  prevPoint;             // the point the last roll was thrown against
    uint16_t handRolls;             // rolls by this shooter
    uint8_t  handPoints;            // points this shooter has made (hot hand)
    uint8_t  hist[6], histKind[6];  // last rolls, newest first: d1 << 4 | d2
    Res      res[BET_COUNT];        // the last roll's decisions
    int32_t  rollWon, rollLost;     // money the last roll paid / took

    void newGame();                 // $500 and an empty table; options and stats stay
    void seed(uint32_t s);          // a fixed sequence (debug R): timing is no longer mixed in
    void mix(uint32_t entropy);     // stir timing into the dice (boot, every throw)
    void force(uint8_t a, uint8_t b);   // debug: the next roll (queued, up to 8)

    bool    onTable(uint8_t b) const;           // the spot exists on this table
    bool    working(uint8_t b) const;           // decided by the next roll
    uint16_t limit(uint8_t b) const;            // the most this spot may hold now
    Deny    canAdd(uint8_t b) const;
    Deny    add(uint8_t b, uint16_t chip, uint16_t *added = nullptr);
    Deny    canTake(uint8_t b) const;
    uint16_t take(uint8_t b, uint16_t chip);    // one chip's worth (or what's left)
    uint16_t takeDown(uint8_t b);               // the whole bet (and odds that rest on it)
    Deny    canRoll() const;
    void    throwDice();                        // roll and settle everything
    void    settle(uint8_t a, uint8_t b);       // settle a given roll (tests)

    int32_t tableTotal() const;
    bool    classicBetsUp() const;              // anything a Beginner table lacks
    bool    broke() const { return purse + tableTotal() <= 0; }
    int32_t goal() const;
    bool    reachedGoal() const;
    void    refundTable();                      // everything back to the purse
    uint8_t sum() const { return (uint8_t)(d1 + d2); }

    // Payouts, rounded down to the dollar like a dealer with no change.
    static int32_t oddsWin(uint8_t n, int32_t a);
    static int32_t layWin(uint8_t n, int32_t a);
    static int32_t placeWin(uint8_t n, int32_t a);
    int32_t winFor(uint8_t b, int32_t a) const;  // what a hit pays on spot b (for the hint)
    uint8_t oddsMultiple(uint8_t n) const;

private:
    uint32_t rng;
    uint8_t  forced[8], nForced;
    bool     reseeded;
    uint32_t rand32();
    uint8_t  die();
    void     decide(uint8_t a, uint8_t b);
};
