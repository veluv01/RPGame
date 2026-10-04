// The roulette table: the rules, the money and the flow of a spin.
//
// Pure logic: no graphics, no sound, host-testable (CHBlackjack's Round
// pattern). Every logic tick the play screen calls update() with the
// buttons; the table advances when its own pacing timer and the
// presentation (fxBusy) allow, and reports what happened through a small
// event queue that the presenter turns into motion and sound.
//
// Money: a chip leaves the purse the moment it goes down, and comes back
// when it is taken up. A winning bet stays up and its winnings go to the
// purse; a losing one goes to the house. After the spin the losing bets
// are placed again (from the purse) if they can all be afforded, so the
// layout carries over from spin to spin, the way the chips on a real table
// do; otherwise they stay off.
#pragma once
#include <stdint.h>
#include "Spots.h"

enum : uint8_t { WHEEL_EURO = 0, WHEEL_AMERICAN };
enum : uint8_t { GOAL_1000 = 0, GOAL_5000, GOAL_ENDLESS };
enum : uint8_t { PACE_FUN = 0, PACE_QUICK };

// All-zero is the default for everything below: the game object lives in
// .bss, and with no initialisers there is no constructor code to run.
struct Options {
    uint8_t wheel;              // WHEEL_EURO or WHEEL_AMERICAN (takes effect at the next betting)
    uint8_t goal;               // GOAL_1000, GOAL_5000, GOAL_ENDLESS
    uint8_t pace;               // PACE_FUN or PACE_QUICK
    uint8_t sound;              // 0 lead, 1 arpeggio, 2 off
    uint8_t dealer;             // 0 classic, 1 night
    uint8_t unused[3];
};

struct Stats {
    uint32_t spins, spinsWon, wagered;
    int32_t  bestPurse, biggestWin;     // biggest net win of one spin
    uint16_t gamesWon, gamesBroke, straightHits, pad;
    uint16_t hits[38];                  // per number (37 = 00): hot and cold
};

enum class Phase : uint8_t {
    Welcome, Betting, NoMoreBets, Spin, Result, Settle, EndOfSpin, GameWon, GameLost, Quit,
};

enum class Ev : uint8_t {
    BetAdd,        // a = spot, b = chip (0..4), amount = value (from the glove)
    BetRemove,     // a = spot, amount = value (back to the purse)
    Clear,         // amount = everything on the layout, back to the purse
    ClearArmed,    // CLR pressed once: amount = what a second press clears
    ChipSel,       // a = chip
    Cursor,        // a = spot: the glove moved
    Deny,          // a = Deny reason, b = spot
    NoMoreBets,    // a = the number that will come up (the ball is solved for it)
    Spin,          // a = number: launch the ball
    Result,        // a = number: it has landed
    Settle,        // a = number, amount = winnings (stakes stay up); the presenter walks bet[]
    Rebet,         // amount = the losing bets placed again (0: they stay off)
    Say,           // a = Line, b = Face, c = number (for L_RESULT)
    GameOver,      // a = 1 won the game, 0 broke
};

enum Deny : uint8_t { D_SPOT_MAX, D_TABLE_MAX, D_NO_CASH, D_NO_BET, D_NOTHING };
enum Face : uint8_t { F_NORMAL, F_ANGRY, F_RAISED, F_SMILE, F_SURPRISED };
enum Line : uint8_t {
    L_WELCOME, L_GOOD_LUCK, L_PLACE, L_NO_MORE, L_RESULT, L_WINNER, L_BIG_WIN, L_HOUSE,
    LINE_COUNT
};

struct Event {
    Ev      type;
    uint8_t a, b, c;
    int32_t amount;
};

constexpr uint8_t CHIP_COUNT = 5;
extern const uint8_t CHIP_VALUES[CHIP_COUNT];       // $1 $5 $10 $25 $100
constexpr int32_t START_PURSE = 500;
constexpr uint8_t INSIDE_MAX = 100, OUTSIDE_MAX = 250;   // per spot
constexpr int32_t TABLE_MAX = 1000;
constexpr uint8_t CLR_ARM_FRAMES = 90;

class Roulette {
public:
    Options opt;
    Stats   stats;
    int32_t purse;                      // set by newGame()
    uint8_t bet[spots::NBET];           // dollars on each spot
    uint8_t history[8], nHist;          // the tote board: newest first
    uint8_t cursor;                     // the spot under the glove
    uint8_t gx, gy;                     // the glove (nav::Glove)
    uint8_t chip;                       // active chip, 0..4
    uint8_t number;                     // this spin's number (from NoMoreBets on)
    uint8_t clrArm;                     // frames left to press CLR again
    bool    us;                         // the wheel in play (opt.wheel, latched at Betting)
    Phase   phase;

    void    newGame();                  // $500, empty layout, welcome
    void    resume();                   // keep the purse and the layout (after quitNow(): placed again if the purse covers it)
    void    seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; }
    // pressed = buttons pressed this tick; repeat = press or auto-repeat
    // (RPGame::repeat). START is the screen's (pause), never passed here.
    void    update(uint8_t pressed, uint8_t repeat, bool fxBusy);

    bool    popEvent(Event &e);
    int32_t onTable() const;            // sum of bet[]
    int32_t goal() const;
    uint8_t spotMax(uint8_t spot) const;
    // A line for the croupier (up to 4 lines of 12 characters, '\n' between).
    const char *lineText(uint8_t line, uint8_t number, char *buf) const;

    // SAVE & QUIT: while betting the layout goes back to the purse (and is
    // kept as the next game's layout); after NO MORE BETS the spin is
    // settled as it was going to be first. Leaves the table in Quit.
    void    quitNow();
    // The layout to keep in the save (spot, amount) and to put back on
    // CONTINUE (placed again if the purse covers it).
    uint8_t saveLayout(uint8_t *pairs, uint8_t max) const;   // returns the pair count
    void    loadLayout(const uint8_t *pairs, uint8_t n);

    // Test/debug hooks.
    void    force(uint8_t number);      // queue the next spins' numbers (up to 8)
    void    place(uint8_t spot, uint8_t amount);   // as if with the glove (limits apply)
    uint8_t dropped() const { return qDropped; }   // events lost to a full queue

private:
    uint32_t rng;
    uint16_t wait;
    uint8_t step;
    uint8_t forced[8], nForced;
    Event   q[16];
    uint8_t qHead, qLen, qDropped;
    uint8_t hold;                       // betting opens once the rebet chips have landed

    uint32_t rand32();
    uint8_t nextNumber();
    void    emit(Ev t, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, int32_t amount = 0);
    void    say(uint8_t line, uint8_t face, uint8_t c = 0) { emit(Ev::Say, line, face, c); }
    void    go(Phase p);
    void    pace(uint16_t frames);      // halved at QUICK pace
    void    betInput(uint8_t pressed, uint8_t repeat);
    bool    addChip(uint8_t spot, uint8_t value, bool fromGlove);
    void    settle();
    void    clearLayout();
};
