// The rules of both machines, with no graphics in sight (the host tests link
// this file alone).
//
//   LUCKY 7         three reels, one line across the middle; two lucky charms
//                   (clover, horseshoe) on it spin the bonus wheel.
//   SWEET           three reels of three rows, five lines (the rows and the two
//                   diagonals), a lollipop wild, and a sugar rush: every win in
//                   a row raises the multiplier on the next (x1, x2, x3, x5).
//   DRAGON FORTUNE  five reels of three rows, 25 fixed lines paying left to
//                   right, an expanding wild, free games, and Hold and Spin
//                   with four jackpot meters.
//
// Every step is settled in full the moment it is asked for - spin() draws
// the stops, pays the purse and arms any feature; respin() does the same for
// one Hold and Spin respin - and leaves what happened in `res` / `held` for
// the presenter to replay. The screen never decides anything.
#pragma once
#include <stdint.h>

enum Machine : uint8_t { M_CLASSIC, M_FORTUNE, M_SWEET, M_COUNT };

// (In the order of the sprite sheet, tools/art/symbols.png.)
enum : uint8_t {
    C_CHERRY, C_LEMON, C_BELL, C_BAR, C_SEVEN, C_CHEST, C_PLUM, C_GRAPE, C_WATERMELON, C_BANANA,
    C_CLOVER, C_HORSESHOE, C_DIAMOND, C_APPLE, C_ORANGE, C_COUNT
};
enum : uint8_t {
    F_JADE, F_FAN, F_LANTERN, F_ENVELOPE, F_KOI, F_INGOT, F_CAT,
    F_DRAGON,           // wild: fills its reel, stands for anything but a gong or a coin
    F_GONG,             // scatter: three anywhere start the free games
    F_COIN,             // six on screen start Hold and Spin
    F_COUNT
};

enum : uint8_t {
    S_GUMDROP, S_CANDY, S_CHOCOLATE, S_DONUT, S_CUPCAKE, S_ICECREAM, S_BEAR,
    S_LOLLY,            // wild: stands for any sweet; three of them pay the most
    S_COUNT
};
constexpr uint8_t SWEET_LINES = 5, RUSH_STEPS = 4;

// What a coin carries: a multiple of the bet, or one of the meters.
enum : uint8_t { K_NONE, K_X1, K_X2, K_X3, K_X5, K_MINI, K_MINOR, K_MAJOR, K_COUNT };
enum : uint8_t { J_MINI, J_MINOR, J_MAJOR, J_GRAND, J_COUNT };

enum : uint8_t { GOAL_1000, GOAL_5000, GOAL_ENDLESS };

constexpr uint8_t ROWS = 3, LINES = 25, CELLS = 15, WHEEL_SEGS = 12;
constexpr int32_t START_PURSE = 500;
constexpr uint8_t FREE_GAMES = 8, FREE_MAX = 40, HOLD_COINS = 6, HOLD_RESPINS = 3;

struct Options {
    uint8_t goal;       // GOAL_*
    uint8_t speed;      // 0 normal, 1 fast
    uint8_t sound;      // 0 on, 1 off
    uint8_t music;      // 0 on, 1 off
    uint8_t pad[4];
};

struct Stats {
    uint32_t spins;
    int32_t  bestPurse, biggestWin;
    uint16_t freeGames, holdSpins, jackpots, banksBroken, timesBroke;
};

// One settled spin.
struct Result {
    uint8_t stop[5];            // strip index of each reel's top row
    uint8_t grid[5][ROWS];      // what landed (before any wild expands)
    uint8_t coin[5][ROWS];      // K_* for each F_COIN cell
    uint8_t wild;               // bit per reel: a dragon fills it
    uint8_t lineCount[LINES];   // 0, or how many reels the line matched
    uint8_t lineSym[LINES];
    uint8_t nLines;             // lines that paid
    uint8_t scatters, coins;
    bool    freeTrigger, holdTrigger, wasFree;
    uint8_t wheel;              // LUCKY 7: the bonus wheel's segment + 1 (0 = no bonus)
    uint8_t mult;               // SWEET: the sugar-rush multiplier this spin paid at
    int32_t linePay, scatterPay, wheelPay, total;   // dollars
};

class Slots {
public:
    Options opt;
    Stats   stats;
    int32_t purse;
    uint8_t machine;
    uint8_t betIdx[M_COUNT];
    uint32_t pot[2];            // what MAJOR and GRAND have grown by (bet units)
    uint8_t rush;               // SWEET: wins in a row so far (0..3), the next spin's multiplier step

    // Features (Fortune).
    uint8_t  freeLeft, freePlayed;
    bool     holding;
    uint8_t  respins;
    uint8_t  held[CELLS];       // K_* per cell (reel * 3 + row)
    uint16_t heldNew;           // cells that landed on the last respin
    int32_t  holdPay;           // paid when the feature ended
    uint8_t  holdJackpot;       // highest J_* it paid, + 1 (0 = none)
    int32_t  lastWin;           // everything won since the last paid spin

    Result res;

    void newGame();             // a fresh purse; options, stats and pots stay
    void seed(uint32_t s);
    void mix(uint32_t entropy); // stir timing in (ignored once seed() was called)

    uint8_t  reels() const { return machine == M_FORTUNE ? 5 : 3; }
    uint16_t bet() const;
    static uint16_t betOf(uint8_t machine, uint8_t idx);
    bool changeBet(int d);      // false at either end, or beyond the purse
    void fitBet();              // lower the bet to what the purse covers
    bool inFeature() const { return freeLeft || holding; }
    bool canSpin() const { return freeLeft || purse >= bet(); }

    void spin();                // a paid spin, or the next free game
    bool respin();              // one Hold and Spin respin; false = it just ended and paid

    int32_t meter(uint8_t j) const;         // J_* in dollars at the current bet
    int32_t coinValue(uint8_t k) const;     // K_* in dollars at the current bet
    int32_t linePay(uint8_t line) const;    // what res.lineCount[line] paid
    static uint8_t lineRow(uint8_t line, uint8_t reel);
    uint8_t stripLen() const;
    uint8_t stripSym(uint8_t reel, int idx) const;      // idx wraps
    static int32_t classicPay(uint8_t a, uint8_t b, uint8_t c);     // x bet
    static uint8_t fortunePay(uint8_t sym, uint8_t count);          // x bet / 5
    static uint8_t wheelPrize(uint8_t seg);                         // x bet
    static uint8_t sweetPay(uint8_t sym);                           // three on a line, x bet / 5
    static uint8_t rushMult(uint8_t step);                          // x1, x2, x3, x5

    int32_t goal() const;       // 0 = endless
    bool reachedGoal() const { return goal() && !inFeature() && purse >= goal(); }
    bool broke() const { return !inFeature() && purse < 1; }

    // Debug and tests.
    void force(const uint8_t *stops) { for (uint8_t i = 0; i < 5; i++) forced[i] = stops[i]; isForced = true; }
    void forceFeature(uint8_t k) { feature = k; }    // 1 free games, 2 hold, 3 dragon, 4 five cats, 5 LUCKY 7's wheel
    uint32_t rand32();
    uint32_t below(uint32_t n);

private:
    uint32_t rng;
    bool reseeded, isForced;
    uint8_t forced[5], feature;
    void evalClassic();
    void evalFortune();
    void evalSweet();
    uint8_t drawCoin(bool inHold);
    void endHold();
};
