#pragma GCC optimize("Os")   // cold code: size over speed
// The roulette table's rules and flow (Roulette.h). The phase machine,
// pacing and event queue follow CHBlackjack's Round.
#include <string.h>
#include "Roulette.h"
#include "Nav.h"
#include "Wheel.h"
#include <rpgame/Input.h>   // the button masks only, so the rules build on the PC for the host tests

using namespace spots;

const uint8_t CHIP_VALUES[CHIP_COUNT] = {1, 5, 10, 25, 100};

static_assert(INSIDE_MAX <= 255 && OUTSIDE_MAX <= 255, "bet[] is uint8_t");
static_assert(UP_BUTTON == 4 && DOWN_BUTTON == 8 && LEFT_BUTTON == 16 && RIGHT_BUTTON == 32,
              "betInput() decodes the D-pad by bit position");

// Frames at 60 fps (halved at QUICK pace).
enum : uint16_t { T_WELCOME = 30, T_NO_MORE = 40, T_RESULT = 30, T_EVENT = 2 };
enum : uint8_t { DPAD = UP_BUTTON | DOWN_BUTTON | LEFT_BUTTON | RIGHT_BUTTON };

// ---------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------
uint32_t Roulette::rand32() {
    if (!rng) rng = 0x9E3779B9u;
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

uint8_t Roulette::nextNumber() {
    uint8_t p = wheel::pockets(us);
    while (nForced) {
        uint8_t n = forced[0];
        memmove(forced, forced + 1, --nForced);
        if (n < p) return n;                     // 00 forced on a European wheel: skipped
    }
    return (uint8_t)(rand32() % p);
}

void Roulette::force(uint8_t n) {
    if (nForced < sizeof forced) forced[nForced++] = n;
}

void Roulette::emit(Ev t, uint8_t a, uint8_t b, uint8_t c, int32_t amount) {
    if (qLen >= 16) { qDropped++; return; }
    Event &e = q[(qHead + qLen) & 15];
    e.type = t; e.a = a; e.b = b; e.c = c; e.amount = amount;
    qLen++;
}

bool Roulette::popEvent(Event &e) {
    if (!qLen) return false;
    e = q[qHead];
    qHead = (uint8_t)((qHead + 1) & 15);
    qLen--;
    return true;
}

void Roulette::pace(uint16_t frames) { wait = opt.pace == PACE_QUICK ? (uint16_t)(frames >> 1) : frames; }

int32_t Roulette::goal() const {
    if (opt.goal == GOAL_ENDLESS) return 0x7FFFFFFF;
    return opt.goal == GOAL_5000 ? 5000 : 1000;
}

int32_t Roulette::onTable() const {
    int32_t t = 0;
    for (uint8_t s = 0; s < NBET; s++) t += bet[s];
    return t;
}

uint8_t Roulette::spotMax(uint8_t spot) const {
    return isBet(spot) ? (inside(spot) ? INSIDE_MAX : OUTSIDE_MAX) : 0;
}

// The stakes on the spots that win (or lose) on this spin's number.
static int32_t stakes(const Roulette &r, bool won) {
    int32_t t = 0;
    for (uint8_t s = 0; s < NBET; s++)
        if (covers(s, r.number, r.us) == won) t += r.bet[s];
    return t;
}

static void putGlove(Roulette &r, uint8_t spot) {
    nav::Glove g = nav::at(spot, r.us);
    r.cursor = spot; r.gx = g.x; r.gy = g.y;
}

// Entering Betting latches the wheel: a change made in the options while
// the last spin ran refunds the whole layout first.
void Roulette::go(Phase p) {
    phase = p;
    step = 0;
    if (p == Phase::Betting && us != (opt.wheel != 0)) {
        clearLayout();
        us = !us;
        putGlove(*this, valid(cursor, us) ? cursor : ZERO);   // (the 0 moves too)
    }
}

// Everything on the layout back to the purse.
void Roulette::clearLayout() {
    int32_t t = onTable();
    clrArm = 0;
    if (!t) return;
    purse += t;
    memset(bet, 0, sizeof bet);
    emit(Ev::Clear, 0, 0, 0, t);
}

// ---------------------------------------------------------------------------
// Games
// ---------------------------------------------------------------------------
void Roulette::newGame() {
    purse = START_PURSE;
    if (stats.bestPurse < START_PURSE) stats.bestPurse = START_PURSE;
    memset(bet, 0, sizeof bet);
    nHist = 0;
    chip = 1;
    clrArm = 0;
    us = opt.wheel != 0;
    putGlove(*this, straightId(17));
    qHead = qLen = 0;
    go(Phase::Welcome);
    say(L_WELCOME, F_SMILE);
    pace(T_WELCOME);
}

void Roulette::resume() {
    // After SAVE & QUIT (CONTINUE in the same session) bet[] is only the
    // saved layout: quitNow() put its money back in the purse. It goes down
    // again if the purse covers it, as loadLayout() would place it.
    if (phase == Phase::Quit) {
        purse -= onTable();
        if (purse < 0) clearLayout();            // can't cover it: it stays off
    }
    clrArm = 0;
    go(Phase::Betting);                          // latch the wheel (refunding a stale layout)
    qHead = qLen = 0;                            // the presenter resets from this state, not from events
    putGlove(*this, onTable() ? SPIN : straightId(17));
    go(Phase::Welcome);
    say(L_GOOD_LUCK, F_NORMAL);
    pace(T_WELCOME);
}

// ---------------------------------------------------------------------------
// Betting
// ---------------------------------------------------------------------------
bool Roulette::addChip(uint8_t spot, uint8_t value, bool fromGlove) {
    uint8_t why = D_SPOT_MAX;
    if (bet[spot] + value > spotMax(spot)) {
    } else if (onTable() + value > TABLE_MAX) why = D_TABLE_MAX;
    else if (value > purse) why = D_NO_CASH;
    else {
        bet[spot] = (uint8_t)(bet[spot] + value);
        purse -= value;
        emit(Ev::BetAdd, spot, fromGlove ? chip : 0xFF, 0, value);
        return true;
    }
    emit(Ev::Deny, why, spot);
    return false;
}

void Roulette::place(uint8_t spot, uint8_t amount) {
    if (isBet(spot) && valid(spot, us) && amount) addChip(spot, amount, false);
}

void Roulette::betInput(uint8_t pressed, uint8_t repeat) {
    // The glove: a tap if a direction was pressed this tick, else a run.
    uint8_t held = (uint8_t)(pressed | repeat);
    int8_t dx = (int8_t)(((held >> 5) & 1) - ((held >> 4) & 1));
    int8_t dy = (int8_t)(((held >> 3) & 1) - ((held >> 2) & 1));
    nav::Glove g = {cursor, gx, gy};
    if (nav::step(g, dx, dy, (pressed & DPAD) != 0, us)) {
        cursor = g.spot; gx = g.x; gy = g.y;
        clrArm = 0;
        emit(Ev::Cursor, cursor);
    }
    uint8_t c = cursor, k = chip;
    int32_t t = onTable();
    if (repeat & A_BUTTON) {
        if (isBet(c)) addChip(c, CHIP_VALUES[chip], true);
        else if (pressed & A_BUTTON) {
            if (c > CLR && c < SPIN) {           // a chip button
                k = (uint8_t)(c - CHIP0);
                if (CHIP_VALUES[k] > purse) { emit(Ev::Deny, D_NO_CASH, c); k = chip; }
            } else if (!t) emit(Ev::Deny, c == SPIN ? D_NO_BET : D_NOTHING, c);
            else if (c == SPIN) {
                number = nextNumber();           // the number is decided here
                emit(Ev::NoMoreBets, number);
                say(L_NO_MORE, F_RAISED);
                go(Phase::NoMoreBets);
                pace(T_NO_MORE);
                return;
            } else if (clrArm) clearLayout();    // CLR, the second press
            else {
                clrArm = CLR_ARM_FRAMES;
                emit(Ev::ClearArmed, 0, 0, 0, t);
            }
        }
    }
    if (repeat & B_BUTTON) {
        if (clrArm) clrArm = 0;                  // B disarms CLR
        else if (isBet(c) && bet[c]) {
            uint8_t v = CHIP_VALUES[chip];
            if (v > bet[c]) v = bet[c];
            bet[c] = (uint8_t)(bet[c] - v);
            purse += v;
            emit(Ev::BetRemove, c, 0, 0, v);
        } else emit(Ev::Deny, D_NOTHING, c);
    }
    // SELECT: the next chip the purse can afford. And a chip that has
    // become unaffordable drops to the largest one that is.
    if (pressed & SELECT_BUTTON)
        do k = (uint8_t)((k + 1) % CHIP_COUNT); while (k != chip && CHIP_VALUES[k] > purse);
    while (k && CHIP_VALUES[k] > purse) k--;
    if (k != chip && CHIP_VALUES[k] <= purse) {
        chip = k;
        emit(Ev::ChipSel, k);
    }
}

// ---------------------------------------------------------------------------
// The spin
// ---------------------------------------------------------------------------
static void record(Roulette &r) {
    memmove(r.history + 1, r.history, sizeof r.history - 1);
    r.history[0] = r.number;
    if (r.nHist < sizeof r.history) r.nHist++;
    r.stats.spins++;
    r.stats.hits[r.number]++;
    if (r.bet[straightId(r.number)]) r.stats.straightHits++;
}

// Winners are paid (their stakes stay up); losers stay on bet[] until
// EndOfSpin, for the presenter to sweep.
void Roulette::settle() {
    int32_t win = 0, staked = onTable(), lost = stakes(*this, false);
    for (uint8_t s = 0; s < NBET; s++)
        if (bet[s] && covers(s, number, us)) win += (int32_t)bet[s] * payout(s, us);
    purse += win;
    stats.wagered += (uint32_t)staked;
    int32_t net = win - lost;
    if (net > 0) stats.spinsWon++;
    if (net > stats.biggestWin) stats.biggestWin = net;
    emit(Ev::Settle, number, 0, 0, win);
    // The croupier: a big win is any straight-up hit, or 17 times the stake.
    uint8_t line = L_HOUSE, face = F_SMILE;
    if (net > 0) line = L_WINNER, face = F_ANGRY;
    if (bet[straightId(number)] || net >= 17 * staked) line = L_BIG_WIN, face = F_SURPRISED;
    say(line, face);
}

void Roulette::update(uint8_t pressed, uint8_t repeat, bool fxBusy) {
    if (wait) { wait--; return; }
    Phase p = phase;

    if (p == Phase::Betting) {
        if (hold) { if (fxBusy) return; hold = 0; }
        if (us != (opt.wheel != 0)) go(Phase::Betting);   // the wheel changed in the options: now
        if (clrArm) clrArm--;
        betInput(pressed, repeat);
        return;
    }

    if (p == Phase::EndOfSpin) {
        // The losing bets go down again if the purse covers them all. A
        // wheel change (made in the options during the spin) takes the
        // layout home instead: the winners' stakes (Clear), then no rebet.
        int32_t lost = stakes(*this, false);
        bool change = us != (opt.wheel != 0);
        if (lost > purse || change) {
            for (uint8_t s = 0; s < NBET; s++)
                if (!covers(s, number, us)) bet[s] = 0;
            lost = 0;
            if (change) clearLayout();
        }
        purse -= lost;
        emit(Ev::Rebet, 0, 0, 0, lost);
        int32_t bank = purse + onTable();
        if (bank > stats.bestPurse) stats.bestPurse = bank;
        if (bank >= goal() || bank < 1) {
            bool won = bank > 0;
            if (won) stats.gamesWon++;
            else stats.gamesBroke++;
            emit(Ev::GameOver, won);
            go(won ? Phase::GameWon : Phase::GameLost);
        } else {
            say(L_PLACE, F_NORMAL);
            if (onTable()) putGlove(*this, SPIN);
            go(Phase::Betting);
            hold = 1;                            // no input until the rebet chips have landed
        }
        return;
    }
    if (p > Phase::EndOfSpin) return;            // the end: the screen takes over

    // Welcome, NoMoreBets, Spin, Result, Settle: each does its work on its
    // first tick, then moves on to the next phase.
    if (!step) {
        step = 1;
        if (p == Phase::Spin) {
            emit(Ev::Spin, number);
            pace(T_EVENT);                       // let the presenter see it before fxBusy counts
            return;
        }
        if (p == Phase::Result) {
            record(*this);
            emit(Ev::Result, number);
            say(L_RESULT, F_NORMAL, number);
            pace(T_RESULT);
            return;
        }
        if (p == Phase::Settle) {
            settle();
            pace(T_EVENT);
            return;
        }
    }
    if (fxBusy && (p == Phase::Spin || p == Phase::Settle)) return;   // the ball, the payout
    go((Phase)((uint8_t)p + 1));
}

// ---------------------------------------------------------------------------
// Save & quit
// ---------------------------------------------------------------------------
void Roulette::quitNow() {
    Phase p = phase;
    if (p == Phase::Quit) return;
    int32_t back = onTable();
    if (p >= Phase::NoMoreBets && p <= Phase::EndOfSpin) {
        // The number is drawn: finish the spin as it was going to go, then
        // the winners' stakes come home. (Phase and step say what is done.)
        uint8_t at = (uint8_t)((uint8_t)p * 2 + (step ? 1 : 0));
        if (at <= 2 * (uint8_t)Phase::Result) record(*this);
        if (at <= 2 * (uint8_t)Phase::Settle) settle();
        back = stakes(*this, true);
    }
    purse += back;                               // bet[] stays: the layout to save
    if (purse > stats.bestPurse) stats.bestPurse = purse;
    qLen = 0;                                    // the table is leaving: no presenter
    go(Phase::Quit);
}

uint8_t Roulette::saveLayout(uint8_t *pairs, uint8_t max) const {
    uint8_t n = 0;
    for (uint8_t s = 0; s < NBET && n < max; s++)
        if (bet[s]) { *pairs++ = s; *pairs++ = bet[s]; n++; }
    return n;
}

// Onto an empty table (CONTINUE): all of it, or none of it if the purse
// can't cover it. Spots this wheel lacks, and amounts over a limit, are
// left out.
void Roulette::loadLayout(const uint8_t *pairs, uint8_t n) {
    if (onTable()) return;
    us = opt.wheel != 0;
    while (n--) {
        unsigned s = *pairs++, a = *pairs++;
        if (isBet((uint8_t)s) && valid((uint8_t)s, us) && bet[s] + a <= spotMax((uint8_t)s)) {
            bet[s] = (uint8_t)(bet[s] + a);
            purse -= a;
        }
    }
    int32_t t = onTable();
    if (purse < 0 || t > TABLE_MAX) {            // not affordable: back it all goes
        purse += t;
        memset(bet, 0, sizeof bet);
    } else if (t) {
        emit(Ev::Rebet, 0, 0, 0, t);
        putGlove(*this, SPIN);
    }
}

// ---------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------
// The lines in Line order (L_RESULT's is built), then the colours in
// wheel::Colour order.
static const char LINES[] =
    "WELCOME TO\nTHE WHEEL!\0GOOD LUCK!\0PLACE YOUR\nBETS\0NO MORE\nBETS!\0"
    "\0WINNER!\0INCREDIBLE!\0THE HOUSE\nTHANKS YOU\0"
    "GREEN\0RED\0BLACK";
static_assert(L_HOUSE == 7 && LINE_COUNT == 8 && wheel::BLACK_NUM == 2, "LINES order");

const char *Roulette::lineText(uint8_t line, uint8_t n, char *buf) const {
    if (line >= LINE_COUNT) return "";
    char *p = buf;
    if (line == L_RESULT) {                      // "17 BLACK", "0 GREEN", "00 GREEN"
        p = wheel::name(buf, n);
        *p++ = ' ';
        line = (uint8_t)(LINE_COUNT + wheel::colour(n));
    }
    const char *s = LINES;
    while (line--) while (*s++) {}
    if (p == buf) return s;
    while ((*p++ = *s++)) {}
    return buf;
}
