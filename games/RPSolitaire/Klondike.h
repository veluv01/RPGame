// Klondike, by the rules and the scoring of the Windows classic. No graphics
// or sound in here: the host tests (tools/tests) build this file alone.
//
// Cards: 0..51 = suit * 13 + rank.
//   rank 0..12 = A, 2, ... 10, J, Q, K
//   suit 0..3  = clubs, diamonds, spades, hearts (odd suits are red)
//
// Piles: the stock, the waste, four foundations (one per suit, in suit
// order) and seven tableau columns. The stock and the waste share one
// 24-card array - the waste grows from its front, the stock sits at its
// back - so the whole game is 190 bytes: small enough to keep a copy for
// undo, and to save in a flash page.
#pragma once
#include <stdint.h>

constexpr uint8_t NO_CARD = 0xFF;
static inline uint8_t rankOf(uint8_t c) { return (uint8_t)(c % 13); }
static inline uint8_t suitOf(uint8_t c) { return (uint8_t)(c / 13); }
static inline bool    redCard(uint8_t c) { return suitOf(c) & 1; }
static inline uint8_t makeCard(uint8_t rank, uint8_t suit) { return (uint8_t)(suit * 13 + rank); }

enum : uint8_t { CLUBS = 0, DIAMONDS, SPADES, HEARTS };
enum : uint8_t { RA = 0, RT = 9, RJ, RQ, RK };

enum Pile : uint8_t { STOCK = 0, WASTE = 1, FOUND = 2, TAB = 6, PILES = 13, NO_PILE = 0xFF };
enum Scoring : uint8_t { STANDARD = 0, VEGAS, NO_SCORE };

// One byte each, in the order of the options screen (which indexes them).
struct Options {
    uint8_t draw;        // 0: draw three, 1: draw one
    uint8_t scoring;     // Scoring
    uint8_t timed;       // 0: timed game, 1: not
    uint8_t sound;       // 0: on
    uint8_t deck;        // 0: two-colour, 1: four-colour
    uint8_t back;        // the card back (the deck screen)
    uint8_t unused[2];
};

struct Stats {
    uint32_t played, won;
    int32_t  bank;       // Vegas: what the house owes you, over every game
    int32_t  bestScore;  // Standard
    uint16_t bestTime;   // seconds, 0 = none yet
    uint16_t streak, bestStreak, unused;
};

struct Klondike {
    uint8_t deck[24];            // waste: [0, nWaste), top last; stock: [24 - nStock, 24), top first
    uint8_t tab[7][19];          // bottom card first; the first down[i] are face down
    uint8_t nStock, nWaste, fan; // fan: how many of the waste's top cards are spread out (draw three)
    uint8_t nTab[7], down[7];
    uint8_t found[4];            // cards on each suit's foundation
    uint8_t drawOne, scoring, timed;
    uint8_t passes;              // times the waste has been turned over
    uint8_t live;                // a game is in progress (dealt, not won)
    uint8_t started;             // the first move has been made: the clock runs
    uint8_t sub;                 // frames into the current second
    uint8_t unused;
    uint16_t secs, moves;
    int32_t score;               // points, or dollars in Vegas (from -52)

    void deal(uint32_t seed, const Options &o);
    // The stock: returns how many cards went to the waste, 0xFF if the waste
    // was turned over instead, 0 if nothing can be done.
    uint8_t draw();
    bool canRecycle() const;
    // n cards from the top of src onto dst. Any foundation stands for the
    // card's own; dest() says which pile the cards really land on.
    bool canMove(uint8_t src, uint8_t n, uint8_t dst) const;
    uint8_t dest(uint8_t src, uint8_t dst) const;
    bool move(uint8_t src, uint8_t n, uint8_t dst);      // true: a tableau card turned up
    uint8_t count(uint8_t pile) const;
    uint8_t card(uint8_t pile, uint8_t idx) const;       // idx 0 = bottom
    uint8_t top(uint8_t pile) const { uint8_t n = count(pile); return n ? card(pile, (uint8_t)(n - 1)) : NO_CARD; }
    uint8_t faceUp(uint8_t col) const { return (uint8_t)(nTab[col] - down[col]); }
    bool won() const { return found[0] + found[1] + found[2] + found[3] == 52; }
    bool autoReady() const;       // everything is face up and the stock is done: it plays itself out
    uint8_t autoSource() const;   // a pile whose top card can go up, lowest rank first
    void tick();                  // once a frame while playing
    int32_t bonus() const;        // the time bonus of a won, timed Standard game
    void nearWin(uint8_t left);   // debug and tests: all but `left` cards already up
};
