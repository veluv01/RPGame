#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed
// The CPU contestants (Cpu.h): each persona's numbers, then its choices.
#include "Cpu.h"
#include "Show.h"

namespace cpu {

using namespace pz;

enum : uint8_t { V_RARELY, V_SMART, V_HOUND };
enum : uint8_t { M_NEVER, M_BEHIND, M_ALWAYS };

struct Persona {
    uint8_t informed;       // % of picks that are the best letter really on the board
    uint8_t solveAt;        // % of the board turned before solving
    uint8_t wrong;          // % of solves that come out wrong
    uint8_t repeat;         // % of consonant calls that are a letter already called
    uint8_t vowel, flip;
    uint8_t buzz;           // toss-up: % turned before buzzing (plus 0..19)
    uint8_t tossWrong;
    uint8_t bonus;          // bonus round: added to the % turned for the chance to win
};

static const Persona PERSONA[3] = {
    {64, 68, 3, 0, V_SMART, M_BEHIND, 45, 5, 25},       // ACE
    {54, 74, 6, 0, V_HOUND, M_NEVER, 55, 10, 10},       // DOT
    {44, 82, 15, 3, V_RARELY, M_ALWAYS, 70, 25, 0},     // BUZZ
};

// The share of the board a contestant wants turned before solving: lower
// once the final spin's bell has rung (a turn may be the last).
static int nerve(const Show &s, const Persona &m) {
    return m.solveAt + s.noise[s.cur] - (s.finalMode ? 15 : 0);
}

static const Persona &me(const Show &s, uint8_t p) {
    uint8_t k = s.kind[p];
    return PERSONA[k >= P_ACE && k <= P_BUZZ ? k - P_ACE : 1];
}

void newPuzzle(Show &s) {
    for (uint8_t p = 0; p < 3; p++) {
        s.noise[p] = (int8_t)((int)(s.rand32() % 21) - 10);
        s.buzzAt[p] = (uint8_t)(me(s, p).buzz + s.rand32() % 20);
    }
}

uint8_t turn(Show &s) {
    const Persona &m = me(s, s.cur);
    uint8_t pct = s.puzzle.shownPct();
    uint32_t hid = s.puzzle.hiddenMask();
    bool cons = (hid & ~VOWELS) != 0, vow = (hid & VOWELS) != 0;
    if (pct >= nerve(s, m)) return A_SOLVE;
    if (cons && s.wildUsable() && (m.flip != M_BEHIND || s.value >= 700)) return A_WILD;
    if (vow && s.cash[s.cur] >= VOWEL_COST) {
        if (!cons) return A_VOWEL;
        if (m.vowel == V_HOUND) return A_VOWEL;
        if (m.vowel == V_SMART ? (pct < 60 && s.cash[s.cur] >= 500 && s.chance(50)) : s.chance(10))
            return A_VOWEL;
    }
    return cons ? A_SPIN : A_SOLVE;
}

// The unturned letter of `among` with the most panels (0: none there).
static char best(const Show &s, uint32_t among) {
    char b = 0;
    uint8_t bn = 0;
    for (char l = 'A'; l <= 'Z'; l++) {
        if (!(among & maskOf(l))) continue;
        uint8_t n = s.puzzle.count(l);
        if (n > bn) { bn = n; b = l; }
    }
    return b;
}

// The first letter of a frequency-ordered list that is in `ok`.
static char first(const char *order, uint32_t ok) {
    for (; *order; order++)
        if (ok & maskOf(*order)) return *order;
    return 0;
}

static const char CONSONANTS[] = "TNSRHLDCMPGBFYWKVXZJQ";
static const char BONUS_CONS[] = "CDMHPGBFYWKVXZJQ";
static const char VOWEL_ORDER[] = "EAOIU";

char letter(Show &s) {
    const Persona &m = me(s, s.cur);
    uint32_t ok = s.pickAllowed();
    bool bonus = s.stepKind() == SK_BONUS;
    char l = 0;
    if (!bonus && s.pickMode == PM_CONSONANT && s.chance(m.repeat))
        l = first(CONSONANTS, s.puzzle.used & ~VOWELS);        // oops
    if (!l && s.chance(m.informed)) {
        // Money first: a consonant if any is there to find.
        l = best(s, ok & ~VOWELS);
        if (!l) l = best(s, ok);
    }
    if (!l) l = first(bonus ? BONUS_CONS : CONSONANTS, ok);
    if (!l) l = first(VOWEL_ORDER, ok);
    return l ? l : 'E';
}

bool solveRight(Show &s) {
    const Persona &m = me(s, s.cur);
    uint8_t pct = s.puzzle.shownPct();
    if (!s.puzzle.nHidden) return true;
    // Forced to guess early (nothing else to do): the less showing, the worse.
    if (pct < nerve(s, m)) return s.chance(pct < 20 ? 20 : pct);
    return !s.chance(m.wrong);
}

bool flip(Show &s) {
    const Persona &m = me(s, s.cur);
    if (m.flip == M_BEHIND) {
        uint8_t lead = s.leader();
        return lead != s.cur && s.total[lead] + s.cash[lead] > s.total[s.cur] + s.cash[s.cur];
    }
    return m.flip == M_ALWAYS;
}

bool finalSolve(Show &s) {
    const Persona &m = me(s, s.cur);
    return s.puzzle.shownPct() >= nerve(s, m);
}

bool tossRight(Show &s, uint8_t p) { return !s.chance(me(s, p).tossWrong); }

bool bonusWin(Show &s) {
    int pct = (int)s.puzzle.shownPct() - 35 + me(s, s.cur).bonus;
    if (pct < 5) pct = 5;
    if (pct > 95) pct = 95;
    return s.chance((uint8_t)pct);
}

}  // namespace cpu
