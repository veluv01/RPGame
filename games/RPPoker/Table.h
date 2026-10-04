// The poker table: four seats (you in seat 0, three CPUs), the deck, the
// betting, the pots and the flow of a hand, for every variant.
//
// Pure logic: no graphics, no sound, host-testable (tools/tests). Every
// frame the play screen calls update() with the buttons; the hand advances
// when its pacing timer and the presentation (fxBusy) allow, and reports
// what happened through an event queue the stage (Stage.cpp) turns into
// motion.
//
// Seats go clockwise 0 (you, at the bottom), 1 (left), 2 (top), 3 (right).
#pragma once
#include <stdint.h>
#include "Cards.h"
#include "Variants.h"
#include "Ai.h"

constexpr uint8_t SEATS = 4;
constexpr uint8_t YOU = 0;

// All-zero is the default for everything below: the table lives in .bss,
// and with no initialisers there is no constructor code to run.
struct Options {
    uint8_t sound;          // 0 on, 1 off
    uint8_t felt;           // felt colour (pal::Theme)
    uint8_t deck;           // 0 two-colour, 1 four-colour
    uint8_t pace;           // 0 fun, 1 quick
    uint8_t goal;           // GOAL_10K, GOAL_50K, GOAL_ENDLESS
    uint8_t hints;          // 0: show your best hand, 1: hide it
    uint8_t game, level;    // last table chosen in the lobby
};
enum : uint8_t { GOAL_10K, GOAL_50K, GOAL_ENDLESS };

struct GameStats {
    uint32_t hands, won;    // hands dealt to you / hands you won a pot in
    int32_t  net;           // winnings minus losses
    int32_t  biggest;       // biggest pot you won
    uint8_t  best;          // best hand you showed down (Cat + 1; 0 = none)
    uint8_t  pad[3];
};
struct Stats {
    GameStats g[GAMES];
    int32_t   bestPurse;
    uint16_t  banks, broke; // goals reached, times gone broke
};

enum SeatState : uint8_t { S_OUT, S_LIVE, S_FOLDED, S_ALLIN };
enum Act : uint8_t {
    A_NONE, A_FOLD, A_CHECK, A_CALL, A_BET, A_RAISE, A_ALLIN,
    A_SB, A_BB, A_ANTE, A_BRINGIN, A_COMPLETE, A_DRAW,
};

struct Seat {
    int32_t stack;          // in front of the player
    int32_t bet;            // this street
    int32_t put;            // this hand (side pots)
    uint8_t cards[7], n;
    uint8_t up;             // bit i: card i face up (stud; every card once shown)
    uint8_t state;          // SeatState
    uint8_t last;           // last action (Act), for the plate
    uint8_t colour;         // CPU avatar colour index; you: 0xFF
    uint8_t acted, mayRaise;
    uint8_t drew;           // Five Card Draw: cards drawn, 0xFF = not yet
    int8_t  quirk;          // a CPU's personality: a nudge to its judgement
};

struct Pot { int32_t amount; uint8_t eligible; };   // eligible: seat mask

enum class Phase : uint8_t {
    Idle, HandStart, BringIn, Betting, Think, Human, EndStreet,
    Draw, DrawThink, DrawHuman, RunOut, Showdown, Award, HandOver, Rebuy,
    Leave, Won, Broke,
};
enum class Bar : uint8_t { None, Bet, Draw, Next, Rebuy };
enum : uint8_t { B_FOLD, B_CALL, B_RAISE, B_MAX, BET_SLOTS };
enum : uint8_t { N_NEXT, N_LEAVE };

enum class Ev : uint8_t {
    Shuffle,
    Button,     // a = seat
    Join,       // a = seat, b = colour, amount = stack (a new CPU sits down)
    Bust,       // a = seat (out of chips)
    Post,       // a = seat, b = Act (SB, BB, ANTE, BRINGIN), amount
    Deal,       // a = seat (4 = board, 5 = burn), b = index, c = card, amount: 1 = face up
    Street,     // a = street
    Turn,       // a = seat to act
    Action,     // a = seat, b = Act, amount = the seat's bet now (or 0)
    Return,     // a = seat, amount: an uncalled bet back
    Collect,    // amount = pot after the bets are swept in
    Discard,    // a = seat, b = mask of cards thrown, c = how many
    Reveal,     // a = seat: hand turned face up
    Win,        // a = seat, b = pot index, c = category (0xFF: everyone folded), amount
    HandEnd,
    Rebuy,      // amount
    Cursor, Deny,
};
struct Event {
    Ev      type;
    uint8_t a, b, c;
    int32_t amount;
};

class Table {
public:
    Options opt;
    Stats   stats;
    int32_t purse;          // money off the table (wealth = purse + your stack)

    uint8_t game, level;
    Seat    seats[SEATS];
    uint8_t board[5], nBoard;
    uint8_t button, actor, street, aggressor;
    int32_t pot;            // swept in from finished streets
    int32_t curBet, lastRaise;
    uint8_t raises;         // bets and raises this street (fixed-limit cap)
    Pot     pots[SEATS];
    uint8_t nPots;
    Phase   phase;
    Bar     bar;
    uint8_t sel;            // bar slot
    int32_t raiseTo;        // your raise, being chosen
    uint8_t glove, drawMask;   // Five Card Draw: the glove (0..4 cards, 5 the button), cards marked
    uint8_t shownDown;      // seats whose hands have been turned up
    bool    demo;           // attract mode: the CPU plays your seat
    bool    wantSave;       // a hand boundary worth saving at (the screen saves, then clears it)
    int32_t handStart;      // your stack when the hand began (stats)

    const Variant &v() const { return VARIANTS[game]; }
    int32_t unit() const { return UNIT[level]; }
    int32_t bb() const { return 2 * unit(); }
    int32_t betSize() const;                // fixed limit: this street's bet
    int32_t wealth() const { return purse + seats[YOU].stack; }
    int32_t goal() const;
    int32_t rebuyAmount() const { return buyIn < purse ? buyIn : purse; }

    void newPurse();                        // $500, stats kept
    // Sit down with buyIn taken from the purse; fresh CPUs; first hand dealt.
    void sitDown(uint8_t game, uint8_t level, int32_t buyIn, uint32_t seed);
    void leave();                           // your stack back into the purse
    void update(uint8_t pressed, uint8_t repeat, bool fxBusy, bool mayThink);
    bool popEvent(Event &e);

    // Rules queries (also for the bar and the AI).
    bool    live(uint8_t s) const { return seats[s].state == S_LIVE || seats[s].state == S_ALLIN; }
    uint8_t nLive() const;
    uint8_t nCanAct() const;
    int32_t potTotal() const;               // the pot plus every bet on the table
    int32_t toCall(uint8_t s) const;
    int32_t minTo(uint8_t s) const;         // smallest bet/raise, as the seat's total bet
    int32_t maxTo(uint8_t s) const;
    bool    canRaise(uint8_t s) const;
    bool    slotEnabled(uint8_t slot) const;
    uint8_t slots() const;                  // bar slots in use
    uint8_t drawLimit() const;              // Five Card Draw: how many you may throw
    uint32_t score(uint8_t s) const;        // best hand with what is out now
    uint32_t showing(uint8_t s) const;      // stud: the face-up cards' hand
    uint8_t bringInSeat() const;

    // Test and debug hooks.
    void seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; }
    void stackDeck(const uint8_t *cards, uint8_t n);   // the next cards dealt, in order
    void act(uint8_t s, uint8_t a, int32_t to);        // apply an action (no checks)
    ai::Read read;          // what SHARK has seen you do

private:
    uint8_t deck[52], deckPos;
    uint8_t stacked[24], nStacked, stackPos;
    uint32_t rng;
    uint16_t wait;
    uint8_t step;
    uint8_t thinkT, accel;
    bool    aiDone;
    uint8_t handsSinceSave;
    int32_t youWon;         // won this hand
    int32_t buyIn;          // what you sat down with (a rebuy is the same)
    uint8_t streetRaises[SEATS];
    uint32_t scores[SEATS];
    Event   q[32];
    uint8_t qHead, qLen;
    ai::View view;

    uint32_t rand32();
    uint8_t draw();
    void    shuffle();
    void    emit(Ev t, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, int32_t amount = 0);
    void    go(Phase p, uint16_t frames = 0);
    void    pace(uint16_t frames);
    void    seatCpu(uint8_t s);
    void    startHand();
    void    post(uint8_t s, uint8_t a, int32_t amount, bool asBet);
    void    startStreet();
    void    dealStreet();
    uint8_t firstToAct() const;
    void    nextActor(uint8_t from);
    void    endStreet();
    void    afterStreet();
    void    revealAll();
    void    afterAction();
    bool    bettingNeeded() const;
    int32_t fullTo() const;
    void    computePots();
    void    showdown();
    void    award();
    void    endHand();
    void    winUncontested();
    void    replaceCards(uint8_t s, uint8_t mask);
    void    makeView(uint8_t s);
    void    cpuAct();
    void    humanInput(uint8_t pressed, uint8_t repeat);
    void    drawInput(uint8_t pressed, uint8_t repeat);
    void    barInput(uint8_t pressed);
    void    fixSel();
};
