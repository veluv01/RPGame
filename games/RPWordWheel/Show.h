// The show: an episode's flow, the turns at the wheel, the money.
//
// Pure logic: no graphics, no sound, host-testable (CHBlackjack's Round
// pattern). Every logic tick the play screen calls update() with the
// buttons; the show advances when its own pacing timer and the
// presentation (fxBusy) allow, and reports what happened through a small
// event queue that the presenter turns into motion and sound.
//
// An episode: a $1,000 toss-up, round 1, round 2 (the mystery round), a
// $2,000 toss-up, round 3 (ends on a final spin), then the leader plays
// the bonus round. Quick Play is a toss-up, round 3 and the bonus.
//
// Money: what a contestant earns in a round (cash) is only kept by the one
// who solves it, with a $1,000 house minimum; it then joins their total.
// Toss-up and bonus money go straight to the total.
//
// The wheel: the rules draw the stop first, the presenter then spins the
// wheel so that it lands there (Spin.h). 72 stops = 24 wedges of three
// peg slots (Wedges.h).
#pragma once
#include <stdint.h>
#include "Puzzle.h"
#include "Wedges.h"

enum : uint8_t { P_HUMAN = 0, P_ACE, P_DOT, P_BUZZ, P_KINDS };      // who stands at a podium
enum : uint8_t { GAME_FULL = 0, GAME_QUICK };
enum : uint8_t { PACE_FUN = 0, PACE_QUICK };
enum : uint8_t { CLOCK_TV = 0, CLOCK_RELAXED };
enum : uint8_t { SEC_ROUND = 0, SEC_TOSS, SEC_BONUS, SEC_COUNT };   // puzzle sections of the bank

// All-zero is the default for everything below: the game object lives in
// .bss, and with no initialisers there is no constructor code to run.
struct Options {
    uint8_t game;               // GAME_FULL or GAME_QUICK
    uint8_t pace;               // PACE_FUN or PACE_QUICK
    uint8_t clock;              // CLOCK_TV or CLOCK_RELAXED (solve and bonus clocks doubled)
    uint8_t sound;              // 0 lead, 1 arpeggio, 2 off
    uint8_t host;               // 0 classic, 1 night
    uint8_t kind[3];            // the podiums, as last set up (0 0 0 = never: You, Dot, Ace)
};

struct Stats {
    uint32_t career;            // everything the human podiums took home
    uint32_t bestEpisode, bestRound;
    uint16_t episodes, wins, bonusWins, solved, tossups, bankrupts, topWedge, pad;
};

enum class Phase : uint8_t {
    Intro, Load,
    TossReveal,
    TurnMenu, Charge, Spinning, PickLetter, MysteryChoice, FinalMenu,
    Entry, Confirm,
    RoundEnd,
    BonusGiven, BonusReveal, BonusThink,
    EpisodeEnd, Quit,
};

enum class Ev : uint8_t {
    NeedPuzzle,    // a = section: the screen fetches one and calls supply()
    PuzzleUp,      // a = step kind (StepKind)
    TossPanel,     // a = cell turned
    Buzz,          // a = player
    TurnTo,        // a = player
    Cursor,        // menu or picker moved
    Deny,          // a = Deny reason
    Intent,        // a = player (CPU), b = Act: what they say they will do
    SpinStart,     // a = player (3 = the host), b = stop, c = power; amount = 1 on the bonus wheel
    Landed,        // a = stop, b = wedge kind, amount = value
    Bankrupt,      // a = player, amount = what they lost
    LoseTurn,      // a = player
    Called,        // a = letter, b = count, c = player, amount = money gained
    NoLetter,      // a = letter, b = 1 if it had been called already, c = player
    Token,         // a = player, b = wedge kind (WILD/PRIZE), c = 1 gained, 0 used
    Mystery,       // a = player, b = 0 kept, 1 flipped to $10,000, 2 flipped to bankrupt
    Type,          // a = cell, b = letter (0 = erased): solve entry
    SolveTry,      // a = player: "I'd like to solve"
    SolveRight,    // a = player
    SolveWrong,    // a = player, b = 1 if the clock ran out
    RoundWon,      // a = player (3 = nobody), b = 1 if the house minimum applied, c = step kind, amount
    FinalBell,
    FinalValue,    // amount = what a consonant is worth
    Clock,         // a = seconds left
    Envelope,      // a = slot
    Picked,        // a = letter, b = picks so far: bonus round choices
    Bonus,         // a = 1 won, amount = the envelope
    Say,           // a = Line, b = Face, c = letter, amount = number
    GameOver,      // a = winner
};

enum Deny : uint8_t { D_NO_CONSONANTS, D_NO_VOWELS, D_NO_CASH };
enum Act : uint8_t { A_SPIN, A_VOWEL, A_SOLVE, A_WILD, A_KEEP, A_FLIP, A_PASS };
// The host's expressions, numbered as Stage.h's Expr.
enum Face : uint8_t { F_NORMAL = 0, F_RAISED = 2, F_SMILE = 4, F_SURPRISED = 5 };
enum PickMode : uint8_t { PM_CONSONANT, PM_VOWEL, PM_ANY };
enum EntryCtx : uint8_t { CTX_ROUND, CTX_TOSS, CTX_BONUS };
enum StepKind : uint8_t { SK_TOSS, SK_ROUND, SK_BONUS, SK_END };
enum Line : uint8_t {
    L_WELCOME, L_TOSSUP, L_NOBODY, L_ROUND, L_COUNT, L_NONE, L_REPEAT, L_BANKRUPT, L_LOSE,
    L_FREE, L_ONLY_VOWELS, L_NO_VOWELS, L_SOLVED, L_WRONG, L_TIME, L_MINIMUM, L_FINAL,
    L_FINAL_VALUE, L_MYSTERY, L_BIG, L_WILD, L_PRIZE, L_BONUS, L_GIVEN, L_PICK, L_CLOCK,
    L_BONUS_WIN, L_BONUS_LOSE, L_GOODNIGHT,
    LINE_COUNT
};

struct Event {
    Ev      type;
    uint8_t a, b, c;
    int32_t amount;
};

constexpr int32_t VOWEL_COST = 250, HOUSE_MIN = 1000, PRIZE_VALUE = 3000;
constexpr uint8_t FINAL_SPINS = 6;              // round 3: the bell rings at the turn change after this many spins
constexpr uint8_t NOBODY = 3, HOST = 3;

class Show {
public:
    Options  opt;
    Stats    stats;
    Puzzle   puzzle;
    uint8_t  kind[3];                   // P_HUMAN or a CPU persona
    int32_t  cash[3], total[3];         // this round's money, and what is banked
    bool     wild[3], prize[3];         // tokens held
    uint8_t  cur;                       // whose turn (toss-up: who buzzed; bonus: who plays)
    uint8_t  starter;                   // who starts the next round
    uint8_t  stepIdx;                   // where the episode is (saved)
    uint8_t  stepNow, roundNow, layoutNow;      // ... as a StepKind, a round number, a wheel layout
    Phase    phase;

    uint8_t  menuSel;                   // TurnMenu: an Act; MysteryChoice and FinalMenu: 0 or 1
    uint8_t  pickMode, pickCur;         // the letter picker: mode, and the letter under the cursor (0..25)
    uint8_t  power;                     // the spin being charged, 0..255
    uint8_t  stop;                      // where the wheel stops (from SpinStart on)
    uint16_t value;                     // what a consonant is worth this turn
    uint16_t clock;                     // ticks left on the solve clock
    uint16_t think;                     // ticks left on the bonus round's clock
    char     guess[pz::CELLS];          // solve entry: what has been typed on each panel
    uint8_t  blank;                     // solve entry: the panel being typed
    uint8_t  entryCtx;
    bool     finalMode;                 // round 3 after the bell
    uint8_t  turns;                     // spins taken this round
    uint8_t  locked;                    // toss-up: bit per player who buzzed wrong
    uint8_t  picks[5], nPicks, needPicks;   // bonus round: the letters chosen
    uint32_t bonusPrize;
    uint8_t  winner;                    // who won the last round, toss-up or episode (NOBODY)
    bool     minimum;                   // ... with the house minimum
    int32_t  wonAmount;
    int8_t   noise[3];                  // CPU: this puzzle's nerve (Cpu.cpp)
    uint8_t  buzzAt[3];                 // CPU: toss-up buzz point, % turned

    void    newEpisode();               // kind[] and opt set by the caller
    void    resume();                   // CONTINUE: the saved step starts afresh
    void    seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; }
    uint32_t rngState() const { return rng; }
    // pressed = buttons pressed this tick; repeat = press or auto-repeat;
    // held = down now. START is the screen's (pause), never passed here.
    void    update(uint8_t pressed, uint8_t repeat, uint8_t held, bool fxBusy);
    // The answer to Ev::NeedPuzzle: lines separated by '\n' (Puzzle::set).
    void    supply(const char *text, const char *category);

    bool    popEvent(Event &e);
    uint8_t dropped() const { return qDropped; }

    // What the screen needs to draw the state.
    StepKind stepKind() const { return (StepKind)stepNow; }
    uint8_t  roundNo() const { return roundNow; }       // 1..3 as announced
    uint8_t  layout() const { return layoutNow; }       // wheel layout in play, 0..2
    wedge::Wedge wedgeAt(uint8_t index) const;      // with tokens taken and the mystery spent
    bool     human(uint8_t p) const { return kind[p] == P_HUMAN; }
    uint8_t  humans() const;
    const char *name(uint8_t p, char *buf) const;   // "YOU", "P2", "DOT" (buf: 4 bytes)
    uint8_t  leader() const;
    uint32_t pickAllowed() const;       // letters the picker's cursor may rest on
    bool     wildUsable() const { return wildValue != 0 && wild[cur]; }
    uint8_t  menuCount() const { return wildUsable() ? 4 : 3; }
    uint16_t tossWorth() const { return tossValue; }
    bool     onFreePlay() const { return freePlay; }
    int32_t  keepWorth() const { return (int32_t)wedge::MYSTERY_VALUE * hitCount; }   // MysteryChoice
    // A line for the host (up to 4 lines of 18 characters, '\n' between).
    const char *lineText(uint8_t line, uint8_t letter, int32_t number, char *buf) const;

    // The rules' generator, for Cpu.cpp (so CPU play is deterministic).
    uint32_t rand32();
    bool     chance(uint8_t pct) { return rand32() % 100 < pct; }

    // Test/debug hooks.
    void    forceStop(uint8_t stop);    // queue the next spins' stops (up to 8)
    void    actSpin(uint8_t pow) { doSpin(pow); }
    void    actCall(char letter) { call(letter); }
    void    actSolve(bool right) { solveResult(right, false); }

private:
    uint32_t rng;
    uint16_t wait;
    uint8_t  step;
    uint8_t  forced[8], nForced;
    Event    q[16];
    uint8_t  qHead, qLen, qDropped;
    uint8_t  taken;                     // bits: wild wedge taken, prize taken, mystery spent
    uint8_t  spinBy;                    // who spins: a player, HOST (final spin)
    bool     flat;                      // value is paid once, not per letter
    uint8_t  pending;                   // wedge kind whose token or choice waits on the letter
    uint8_t  hitCount;                  // mystery: how many letters the pending choice is worth
    uint16_t wildValue;                 // the wedge a wild card would replay (0: not now)
    uint16_t finalValue;
    uint16_t tossTimer, tossValue;
    uint8_t  cpuAct;
    char     cpuLetter;
    bool     cpuWin;
    bool     freePlay;

    void    emit(Ev t, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, int32_t amount = 0);
    void    say(uint8_t line, uint8_t face, uint8_t letter = 0, int32_t number = 0);
    void    go(Phase p) { phase = p; step = 0; }
    void    pace(uint16_t frames);
    uint16_t seconds(uint8_t s) const;  // ticks, doubled on the relaxed clock
    uint8_t nextStop();

    void    syncStep();
    void    beginStep();
    void    nextStep();
    void    turnStart();
    void    nextTurn();
    void    startFinal();
    void    doSpin(uint8_t pow);
    void    land();
    void    bankrupt(uint8_t p);
    void    call(char letter);
    void    afterHit(bool consonant);
    void    announceLeft(uint32_t before);
    void    roundWon(uint8_t p);
    void    tossWon(uint8_t p);
    void    tossNobody();
    void    buzz(uint8_t p);
    void    startEntry(uint8_t ctx);
    void    entryInput(uint8_t pressed, uint8_t repeat);
    bool    entryRight() const;
    void    solveResult(bool right, bool timeout);
    void    bonusDone(bool won);
    void    menuInput(uint8_t pressed, uint8_t repeat);
    void    pickerInput(uint8_t repeat);
    void    pickerSettle();
    void    choose(uint8_t act);
    void    mystery(bool flip);
};
