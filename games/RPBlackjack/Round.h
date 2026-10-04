// The blackjack table: rules, money and the flow of a round.
//
// Derived from Press-Play-On-Tape/Blackjack (Apache-2.0),
// PlayGameState_Play.cpp / _Utils.cpp / _Buttons.cpp. Modified 2026 for
// RPGame by bateske: logic separated from rendering, the ViewState flow kept;
// six-deck shoe, casino rules with a Classic (PPOT) preset, and PPOT's
// money bugs fixed (see NOTICE).
//
// Pure logic: no graphics, no sound, host-testable. Every frame the play
// screen calls update() with the buttons; the round advances when its own
// pacing timer and the presentation layer (fxBusy) allow, and reports what
// happened through a small event queue the presenter turns into animation.
#pragma once
#include <stdint.h>

// Card = 0..51. suit = c / 13 (hearts, diamonds, spades, clubs),
// rank = c % 13 (0 = ace .. 9 = ten, 10 J, 11 Q, 12 K) - PPOT's encoding.
static inline uint8_t cardRank(uint8_t c)  { return (uint8_t)(c % 13); }
static inline uint8_t cardSuit(uint8_t c)  { return (uint8_t)(c / 13); }
static inline uint8_t cardValue(uint8_t c) { uint8_t r = cardRank(c); return r >= 9 ? 10 : (uint8_t)(r + 1); }
static inline bool    cardRed(uint8_t c)   { return c < 26; }

enum : uint8_t { RULES_CASINO = 0, RULES_CLASSIC = 1 };
enum : uint8_t { GOAL_1000 = 0, GOAL_5000, GOAL_ENDLESS };

// All-zero is the default for everything below: the game object lives in
// .bss, and with no initialisers there is no constructor code to run.
struct Options {
    uint8_t rules;              // RULES_CASINO or RULES_CLASSIC
    uint8_t goal;               // GOAL_1000, GOAL_5000, GOAL_ENDLESS
    uint8_t speed;          // 0 normal, 1 fast
    uint8_t sound;          // 0 lead, 1 arpeggio, 2 off
    uint8_t theme;          // felt colour
    uint8_t fourColour;     // four-colour deck
    uint8_t totals;         // 0: show hand-total badges, 1: hide them (purists)
    uint8_t dealer;         // dealer look
};

struct Stats {
    uint32_t hands, won, lost, pushed, blackjacks;
    int32_t  bestPurse, biggestWin;
    uint16_t gamesWon, gamesBroke;
};

struct Hand {
    uint8_t  cards[12];
    uint8_t  count;
    uint16_t bet;
    bool     doubled, bust, stood, fromSplit;
    uint8_t  hard() const;              // aces as 1
    uint8_t  best() const;              // aces as 11 where that helps
    bool     soft() const { return best() != hard(); }
    bool     natural() const { return count == 2 && !fromSplit && best() == 21; }
    void     clear();
};

enum class Phase : uint8_t {
    StartHand, Shuffle, InitBet, InitDeal, OfferInsurance, Peeking, PeekResult,
    PlayHand, SplitCards, DoubleUp, Bust, RevealHole, PlayDealerHand,
    CheckForWins, OverallWinOrLose, EndOfGame, GameWon, GameLost, Quit,
};

enum class Bar : uint8_t { None, Bet, Insurance, Play, End };

// Bet bar slots; play bar slots.
enum : uint8_t { B_1, B_5, B_10, B_25, B_DEAL, B_CLEAR, BET_SLOTS };
enum : uint8_t { P_HIT, P_STAND, P_DOUBLE, P_SPLIT, PLAY_SLOTS };
enum : uint8_t { I_YES, I_NO };
enum : uint8_t { E_CONTINUE, E_QUIT };

enum class Ev : uint8_t {
    Deal,          // a = seat (0 dealer, 1 hand 0, 2 hand 1), b = index, c = card, amount bit0 = face down, bit1 = sideways
    Reveal,        // dealer turns the hole card
    Shuffle,       // shoe reshuffled (amount = decks)
    BetAdd,        // a = seat, b = chip index, amount = value
    BetRemove,     // a = seat, amount = value (back to the player)
    Insure,        // amount = insurance
    Split,         // second card of hand 0 moves to hand 1
    Double,        // a = seat, amount = extra bet
    Bust,          // a = seat
    Natural,       // a = seat (player blackjack)
    Hand21,        // a = seat (21, not natural)
    PeekStart,
    PeekEnd,       // a = 1 if dealer has blackjack
    Settle,        // a = seat, b = Result, amount = returned to purse (stake + winnings)
    Insurance,     // b = 1 paid / 0 lost, amount = returned
    DealerBust,
    Say,           // a = Line, b = Face
    Cursor,        // selection moved
    Deny,          // pressed something unavailable
    GameOver,      // a = 1 won the game, 0 broke
};

enum Result : uint8_t { R_LOSE, R_PUSH, R_WIN, R_BLACKJACK };
enum Face : uint8_t { F_NORMAL, F_ANGRY, F_RAISED, F_SMILE, F_SURPRISED };
enum Line : uint8_t {
    L_WELCOME, L_PLACE_BET, L_INSURANCE, L_PEEK, L_DEALER_BJ, L_NO_BJ, L_INS_LOST, L_INS_PAYS,
    L_PLAYER_BJ, L_YOU_WIN, L_PUSH, L_BUST, L_DEALER_WINS, L_DEALER_BUSTS, L_SHUFFLE,
    L_DOUBLE, L_SPLIT, L_BOTH_BJ, L_GOOD_LUCK, L_TWENTY_ONE,
    LINE_COUNT
};

struct Event {
    Ev      type;
    uint8_t a, b, c;
    int16_t amount;
};

class Round {
public:
    Options opt;
    Stats   stats;
    int32_t purse;                      // set by newGame()

    Hand    dealer, hands[2];
    uint8_t nHands, active;
    uint16_t initBet, lastBet, insurance, insureAmt;
    Phase   phase;
    Bar     bar;
    uint8_t sel;
    bool    holeShown;
    bool    dealerPeeked;

    void    newGame();                  // $500, fresh shoe
    void    resume();                   // keep the purse, fresh shoe and table
    void    resetShoe() { shoeLen = 0; }
    void    seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; }
    // pressed = buttons pressed this frame; repeat = press-or-autorepeat.
    void    update(uint8_t pressed, uint8_t repeat, bool fxBusy);

    bool    popEvent(Event &e);
    bool    slotEnabled(uint8_t slot) const;
    uint8_t slotCount() const;
    uint16_t insuranceMax() const;
    int32_t goal() const;
    uint8_t shoeLeftPercent() const;    // for the cut-card gauge
    uint8_t decks() const { return opt.rules == RULES_CASINO ? 6 : 1; }
    const char *lineText(uint8_t line) const;

    // Test/debug hooks.
    void    stackDeck(const uint8_t *cards, uint8_t n);   // next cards dealt, in order

private:
    uint8_t shoe[6 * 52];
    uint16_t shoeLen, shoePos, cutAt;
    uint8_t stacked[16], nStacked;
    uint32_t rng;
    uint16_t wait;
    uint8_t step;
    Event   q[16];
    uint8_t qHead, qLen;

    uint32_t rand32();
    void    shuffle();
    uint8_t draw();
    void    deal(uint8_t seat, bool faceDown = false, bool sideways = false);
    void    emit(Ev t, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, int16_t amount = 0);
    void    say(uint8_t line, uint8_t face) { emit(Ev::Say, line, face); }
    void    go(Phase p, Bar b = Bar::None, uint8_t s = 0);
    void    pace(uint16_t frames);
    bool    dealerShouldHit() const;
    void    nextHand();
    void    settleHand(uint8_t h);
    void    moveSel(int8_t dir);
    void    fixSel();
    void    betInput(uint8_t pressed, uint8_t repeat);
    void    playInput(uint8_t pressed);
    Hand   &cur() { return hands[active]; }
};
