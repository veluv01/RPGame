// The bingo hall: the rules, the money and the flow of a round.
//
// Pure logic: no graphics, no sound, host-testable (CHBlackjack's Round
// pattern). Every logic tick the play screen calls update() with the
// buttons; the caller draws a ball on its own clock, and what happened is
// reported through a small event queue that the presenter turns into motion
// and sound.
//
// 75-ball bingo: 5x5 cards, B 1-15, I 16-30, N 31-45, G 46-60, O 61-75, the
// centre free. A number only counts once it is daubed, and each card is
// daubed on its own: the player has to be on that card and press A. A line
// (row, column or diagonal) of daubed cells wins the pot.
//
// The hall: the rivals' cards are dealt at the start of the round and
// reduced to the one fact that matters, the call on which the first of them
// completes a line. The player has until the call after that one.
//
// Money: the cards are paid for when the round starts. The pot is 90% of
// what the whole hall paid; 5% of the player's buy-in feeds the jackpot,
// which pays for a bingo within JACKPOT_CALLS calls.
#pragma once
#include <stdint.h>

// All-zero is the default for everything below: the game object lives in
// .bss, and with no initialisers there is no constructor code to run.
struct Options {
    uint8_t stakes;             // $5, $10, $25 a card
    uint8_t hall;               // 0 busy (20 rival cards), 1 packed (40), 2 quiet (8)
    uint8_t speed;              // 0 normal, 1 fast, 2 slow
    uint8_t sound;              // 0 on, 1 off
    uint8_t dealer;             // 0 classic, 1 night
    uint8_t dauber;             // the daubs' colour (cards::DAUB)
    uint8_t unused[2];
};

struct Stats {
    uint32_t rounds, wins, cards;
    int32_t  bestPurse, biggestWin;
    uint16_t jackpots, bestStreak, fastest, gamesBroke;   // fastest: fewest calls to a bingo (0 = none yet)
};

enum class Phase : uint8_t { Welcome, Buy, Calling, Won, Lost, GameLost, Quit };

enum class Ev : uint8_t {
    BuyOpen,       // the buy-in is open: a = cards chosen
    BuyPick,       // a = cards chosen
    Start,         // a = cards, amount = what they cost
    Resume,        // a round picked up from a save
    Call,          // a = number, b = how many of the player's cards hold it
    Focus,         // a = card, b = 1 moved right, 0 left
    Daub,          // a = card, b = cells, c = 1 fast, amount = the cells (bit r*5+c)
    Miss,          // A with nothing to daub (a = 0) or B with no power-up (a = 1)
    Power,         // a = the power-up granted
    PowerUse,      // a = power-up, b = card, amount = the cell daubed (WILD)
    Bingo,         // a = card, b = line, c = WinFlag bits, amount = the pot paid
    Rival,         // a = the table that called it, b = 1 the player had an undaubed line
    GameOver,      // broke
    Deny,          // cannot afford that
};

enum Power : uint8_t { P_NONE, P_WILD, P_FREEZE, P_DOUBLE, POWER_KINDS = 3 };
enum WinFlag : uint8_t { WIN_RARE = 1, WIN_JACKPOT = 2, WIN_DOUBLE = 4 };

struct Event {
    Ev      type;
    uint8_t a, b, c;
    int32_t amount;
};

constexpr uint8_t MAX_CARDS = 9;
constexpr int32_t START_PURSE = 500;
constexpr int32_t JACKPOT_SEED = 250;
constexpr uint8_t JACKPOT_CALLS = 10;     // a bingo this early takes the jackpot
constexpr uint8_t POWER_FULL = 12;
constexpr uint8_t FAST_TICKS = 60;        // a daub this soon after its call is "fast"
constexpr uint8_t RARE_ONE_IN = 10;       // "IT'S A BINGO!"
constexpr uint8_t NLINES = 12;
extern const uint32_t LINES[NLINES];      // rows, columns, the two diagonals (bit r*5+c)
extern const uint8_t PRICES[3];
extern const uint8_t RIVALS[3];

inline uint8_t letterOf(uint8_t n) { return (uint8_t)((n - 1) / 15); }   // 0..4 = B I N G O
char *callName(char *p, uint8_t n);       // "B-12"; returns the end (not terminated)

// A round in progress, as the save keeps it: the cards, the draw and the
// hall all come back from the seed.
struct RoundSave {
    uint32_t seed;
    uint32_t daub[MAX_CARDS];
    uint8_t  nCards, nCalled, focus, meter, power, doubleCard, price, rivals;
};

class Bingo {
public:
    Options  opt;
    Stats    stats;
    int32_t  purse;                     // set by newGame()
    int32_t  jackpot;                   // carries over from game to game
    int32_t  pot;
    uint8_t  cards[MAX_CARDS][25];      // numbers, cell r*5+c; the centre is 0
    uint32_t daub[MAX_CARDS];           // daubed cells (the centre always)
    uint32_t pend[MAX_CARDS];           // called, not yet daubed
    uint8_t  balls[75];                 // this round's draw
    uint8_t  nCalled, nCards, focus;
    uint8_t  buyN;                      // cards chosen at the buy-in
    uint8_t  price, rivals;             // this round's (latched from the options)
    uint8_t  hallBall, hallTable;       // the call a rival wins on, and who
    uint8_t  meter, power, doubleCard;  // doubleCard: 0xFF = none
    uint16_t streak;
    uint8_t  winCard, winLine;
    Phase    phase;

    void    newGame();                  // $500, the buy-in
    void    resume();                   // after a load: the round in progress, or the buy-in
    void    seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; }
    // pressed = buttons pressed this tick; repeat = press or auto-repeat
    // (RPGame::repeat). START is the screen's (pause), never passed here.
    void    update(uint8_t pressed, uint8_t repeat, bool fxBusy);

    bool    popEvent(Event &e);
    bool    called(uint8_t n) const { return (calledBits[(n - 1) >> 5] >> ((n - 1) & 31)) & 1; }
    uint8_t priceNow() const;           // the stakes, lowered to what the purse can buy
    uint8_t maxCards() const;
    int32_t potFor(uint8_t n) const;    // the pot if n cards are bought now
    uint16_t interval() const;          // ticks between calls
    bool    frozen() const { return phase == Phase::Calling && nCalled && timer > interval(); }   // FREEZE holds the caller
    bool    inPlay() const { return phase == Phase::Calling || phase == Phase::Won || phase == Phase::Lost; }
    bool    hasRound() const { return phase == Phase::Calling || (phase == Phase::Quit && roundOpen); }

    void    quitNow();                  // SAVE & QUIT: leaves the table in Quit
    void    saveRound(RoundSave &s) const;
    void    loadRound(const RoundSave &s);

    // Test/debug hooks.
    void    start(uint8_t n);           // buy n cards and start the round now
    void    force(uint8_t number);      // this number is the next one called
    void    forceRare(uint8_t v) { rare = v; }  // 0 by chance, 1 always, 2 never
    void    grant(uint8_t p) { power = p; }
    void    daubAll();                  // daub every pending cell on every card
    void    forceWin() { daub[focus] |= LINES[2]; checkWin(focus); }   // the middle row, now
    uint8_t dropped() const { return qDropped; }   // events lost to a full queue

private:
    uint32_t rng, roundSeed;
    uint32_t calledBits[3];
    uint16_t timer, callAge;
    uint8_t  wait, nForced;
    uint8_t  rare;
    bool     roundOpen;                 // a round to pick up on resume()
    Event    q[16];
    uint8_t  qHead, qLen, qDropped;

    uint32_t rand32();
    void    emit(Ev t, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, int32_t amount = 0);
    void    dealCard(uint8_t *card);
    void    setupRound(uint8_t n, uint8_t cardPrice, uint8_t nRivals);
    void    openBuy();
    void    callBall();
    void    moveFocus(int8_t d);
    void    daubFocus();
    void    usePower();
    void    checkWin(uint8_t card);
    void    rivalWins();
};
