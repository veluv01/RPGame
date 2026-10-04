// Yacht Dice: five dice, three rolls a turn, thirteen boxes to fill.
//
// Pure logic: no graphics, no sound, host-testable (tools/tests). The play
// screen calls roll() when the dice leave the hand and score() when a box is
// picked. Both settle everything at once - the dice values, the box, the
// bonuses, the money - and the presenter then shows it at its own pace.
// Because the money has already moved, saving or quitting in the middle of
// the show is always consistent.
#pragma once
#include <stdint.h>

enum Cat : uint8_t {
    ONES, TWOS, THREES, FOURS, FIVES, SIXES,        // the upper section: 63+ earns 35
    KIND3, KIND4, FULL, SMALL, LARGE, YACHT, CHANCE,
    CAT_COUNT
};

enum Mode : uint8_t { M_SOLO, M_CPU, M_PARTY2, M_PARTY3, M_PARTY4, MODE_COUNT };

// All-zero is the default for everything: the game object lives in .bss.
// The options menu indexes this struct as bytes, in this order.
struct Options {
    uint8_t ante;       // $5 / $25 / $100
    uint8_t speed;      // 0 normal, 1 fast
    uint8_t theme;      // felt colour (pal::Theme)
    uint8_t sound;      // 0 on, 1 off
    uint8_t mode;       // the title menu's last pick (Mode)
    uint8_t pad[3];
};

struct Stats {
    uint16_t games, best, yachts, bonuses;      // solo and vs games; best score
    uint16_t cpuWins, cpuLosses, timesBroke, pad;
    int32_t  bestPurse, biggestWin;
};

struct Card {
    uint8_t  score[CAT_COUNT];
    uint16_t filled;                // bit per box
    uint8_t  extra;                 // bonus yachts (100 each)

    bool open(uint8_t c) const { return !(filled >> c & 1); }
    uint16_t upper() const;         // the six upper boxes
    bool bonus() const { return upper() >= 63; }
    uint16_t total() const;         // boxes + 35 upper bonus + 100 a bonus yacht
};

// What a score() did.
enum : uint8_t {
    EV_YACHT = 1,                   // 50 in the yacht box
    EV_EXTRA = 2,                   // a bonus yacht: +100
    EV_UPPER = 4,                   // this box took the upper section to 63: +35
    EV_ZERO = 8,                    // a box scratched
    EV_OVER = 16,                   // the last box of the game
};

class Yacht {
public:
    static const int32_t START_PURSE = 500;
    static const uint8_t TURNS = CAT_COUNT;

    Options  opt;
    Stats    stats;
    int32_t  purse;
    // The game in progress.
    uint8_t  mode, players, cur;    // cur: whose turn
    uint8_t  round;                 // 0..12
    uint8_t  rollsLeft;             // 3 before the first roll of a turn
    uint8_t  dice[5];
    uint8_t  held;                  // bit i: die i sits the next roll out
    bool     over;
    uint16_t ante;                  // staked on this game (0: party)
    Card     card[4];
    // The last score().
    uint8_t  lastCat, lastPts, lastPlayer;
    int32_t  lastPay;               // an instant bonus paid by it
    int32_t  endPay;                // game over: what came back (ante included)

    void newPurse();                // $500; options and stats stay
    void newGame(uint8_t mode);     // antes up
    void seed(uint32_t s);          // a fixed sequence (debug R): timing is no longer mixed in
    void mix(uint32_t entropy);     // stir timing into the dice (boot, every throw)
    void force(const uint8_t *v);   // debug: the next roll's five dice (queued, up to 4)

    bool rolled() const { return rollsLeft < 3; }
    bool canRoll() const { return !over && rollsLeft > 0 && (held != 31 || !rolled()); }
    void roll();                    // every die not held
    bool cpuTurn() const { return mode == M_CPU && cur == 1; }
    bool staked() const { return mode <= M_CPU; }
    static uint16_t anteFor(uint8_t opt);       // $5 / $25 / $100
    bool broke() const { return purse < 5; }

    // Scoring. The joker rule: a yacht rolled with the yacht box filled must
    // go in its own upper box if that is open, else in any open lower box
    // (where it counts as a full house and both straights), else anywhere.
    static uint8_t pointsFor(const Card &c, uint8_t cat, const uint8_t *d);
    static bool    legalFor(const Card &c, uint8_t cat, const uint8_t *d);
    static bool    isYacht(const uint8_t *d);
    uint8_t points(uint8_t cat) const { return pointsFor(card[cur], cat, dice); }
    bool    legal(uint8_t cat) const { return rolled() && !over && legalFor(card[cur], cat, dice); }
    uint8_t score(uint8_t cat);     // fill the box, pay, pass the dice; returns EV_* bits

    // Solo: what a final score pays, in antes (0 = the ante is lost).
    static uint8_t tier(uint16_t total);
    static const uint8_t TIERS = 5;
    static const uint16_t TIER_AT[TIERS];
    static const uint8_t TIER_PAYS[TIERS];
    // Paid the moment they happen: a yacht (or a bonus yacht) pays the ante
    // again, the upper bonus a fifth of it.
    static const uint8_t UPPER_SHARE = 5;
    uint8_t winner() const;         // the highest total (first of equals), or 0xFF: vs CPU tie

    // The best name for five dice (a banner when they stop), or CAT_COUNT.
    static uint8_t combo(const uint8_t *d);

private:
    uint32_t rng;
    uint32_t forced[4];
    uint8_t  nForced;
    bool     reseeded;
    uint32_t rand32();
    uint8_t  die();
    void     finish();
};

// The house's player: one roll of lookahead over the 32 ways to hold, each
// outcome valued by its best box against that box's par.
namespace ai {

struct Think { uint8_t mask, best; int32_t bestEv; };
void begin(Think &t);
// A few holds per call (spread over frames); true once t.best is the mask to keep.
bool step(const Yacht &g, Think &t);
uint8_t bestCat(const Card &c, const uint8_t *dice);

}  // namespace ai
