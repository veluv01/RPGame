// The craps table's rules (Craps.h): the dice, placing and taking chips,
// and settling every bet when the dice are thrown.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <string.h>
#include "Craps.h"

const uint8_t BOX_NUM[6] = {4, 5, 6, 8, 9, 10};

int8_t box(uint8_t n) {
    for (int8_t i = 0; i < 6; i++) if (BOX_NUM[i] == n) return i;
    return -1;
}

static bool isCraps(uint8_t s) { return s == 2 || s == 3 || s == 12; }
static uint8_t hardIndex(uint8_t n) { return (uint8_t)((n - 4) / 2); }   // 4 6 8 10 -> 0..3

// ---------------------------------------------------------------------------
// Dice
// ---------------------------------------------------------------------------
void Craps::seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; reseeded = true; }

void Craps::mix(uint32_t e) {
    if (reseeded) return;                       // scripts replay the same rolls
    rng ^= e * 2654435761u;
    rand32();
}

uint32_t Craps::rand32() {
    if (!rng) rng = 0x9E3779B9u;
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

// 1..6 from the top of a 32-bit draw: one multiply-high, bias ~1e-9.
uint8_t Craps::die() { return (uint8_t)(1 + (uint32_t)(((uint64_t)rand32() * 6u) >> 32)); }

void Craps::force(uint8_t a, uint8_t b) {
    if (nForced < 8 && a >= 1 && a <= 6 && b >= 1 && b <= 6) forced[nForced++] = (uint8_t)(a << 4 | b);
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------
void Craps::newGame() {
    purse = START_PURSE;
    memset(bet, 0, sizeof bet);
    memset(res, 0, sizeof res);
    memset(hist, 0, sizeof hist);
    memset(histKind, 0, sizeof histKind);
    point = prevPoint = 0;
    d1 = d2 = 0;
    handRolls = 0; handPoints = 0;
    rollWon = rollLost = 0;
}

int32_t Craps::tableTotal() const {
    int32_t t = 0;
    for (uint8_t i = 0; i < BET_COUNT; i++) t += bet[i];
    return t;
}

int32_t Craps::goal() const {
    static const int32_t G[3] = {1000, 5000, 0};
    return G[opt.goal < 3 ? opt.goal : 0];
}

bool Craps::reachedGoal() const { return goal() && purse + tableTotal() >= goal(); }

void Craps::refundTable() {
    purse += tableTotal();
    memset(bet, 0, sizeof bet);
}

bool Craps::onTable(uint8_t b) const {
    if (opt.table != TABLE_BEGINNER) return true;
    return b == PASS || b == DONT || b == PASS_ODDS || b == DONT_ODDS || b == FIELD ||
           b == PLACE6 || b == PLACE8;
}

bool Craps::classicBetsUp() const {
    for (uint8_t b = 0; b < BET_COUNT; b++) {
        if (!bet[b]) continue;
        bool beginner = b == PASS || b == DONT || b == PASS_ODDS || b == DONT_ODDS || b == FIELD ||
                        b == PLACE6 || b == PLACE8;
        if (!beginner) return true;
    }
    return false;
}

// Place bets, hardways and come odds are "off" on the come-out: they stay
// on the layout but that roll neither pays nor takes them.
bool Craps::working(uint8_t b) const {
    if (point) return true;
    return !((b >= PLACE4 && b <= HARD10) || b >= CODDS4);
}

uint8_t Craps::oddsMultiple(uint8_t n) const {
    if (opt.odds == ODDS_2X) return 2;
    if (opt.odds == ODDS_10X) return 10;
    return (n == 4 || n == 10) ? 3 : (n == 5 || n == 9) ? 4 : 5;     // 3-4-5X
}

int32_t Craps::oddsWin(uint8_t n, int32_t a) {
    return (n == 4 || n == 10) ? a * 2 : (n == 5 || n == 9) ? a * 3 / 2 : a * 6 / 5;
}
int32_t Craps::layWin(uint8_t n, int32_t a) {
    return (n == 4 || n == 10) ? a / 2 : (n == 5 || n == 9) ? a * 2 / 3 : a * 5 / 6;
}
int32_t Craps::placeWin(uint8_t n, int32_t a) {
    return (n == 4 || n == 10) ? a * 9 / 5 : (n == 5 || n == 9) ? a * 7 / 5 : a * 7 / 6;
}

// Odds may be k times the flat bet (k from the ODDS option); a lay may be as
// big as wins k times the flat bet.
static uint16_t cap16(int32_t v) { return (uint16_t)(v > 65535 ? 65535 : v < 0 ? 0 : v); }

uint16_t Craps::limit(uint8_t b) const {
    if (b == PASS_ODDS) return point ? cap16((int32_t)bet[PASS] * oddsMultiple(point)) : 0;
    if (b == DONT_ODDS) {
        if (!point) return 0;
        int32_t w = (int32_t)bet[DONT] * oddsMultiple(point);           // the most it may win
        uint8_t n = point;
        return cap16((n == 4 || n == 10) ? w * 2 : (n == 5 || n == 9) ? w * 3 / 2 : w * 6 / 5);
    }
    if (b >= CODDS4) {
        uint8_t i = (uint8_t)(b - CODDS4);
        return cap16((int32_t)bet[COME4 + i] * oddsMultiple(BOX_NUM[i]));
    }
    if (b >= COME4) return 0;                                           // the dealer's moves only
    return TABLE_MAX;
}

static bool isOdds(uint8_t b) { return b == PASS_ODDS || b == DONT_ODDS || b >= CODDS4; }

Deny Craps::canAdd(uint8_t b) const {
    if (b >= BET_COUNT || !onTable(b) || (b >= COME4 && b < CODDS4)) return D_CLOSED;
    if ((b == PASS || b == DONT) && point) return D_COMEOUT;
    if (b == COME && !point) return D_COME_CLOSED;
    if (b == PASS_ODDS || b == DONT_ODDS) {
        if (!point) return D_NEED_POINT;
        if (!bet[b == PASS_ODDS ? PASS : DONT]) return D_NEED_LINE;
    }
    if (b >= CODDS4 && !bet[COME4 + (b - CODDS4)]) return D_NEED_LINE;
    if (bet[b] >= limit(b)) return isOdds(b) ? D_MAX_ODDS : D_MAX;
    if (purse <= 0) return D_NO_CHIPS;
    return D_OK;
}

// A chip that doesn't fit is changed into what does: $25 on odds with room
// for $12 puts $12 down.
Deny Craps::add(uint8_t b, uint16_t chip, uint16_t *added) {
    if (added) *added = 0;
    Deny d = canAdd(b);
    if (d != D_OK) return d;
    int32_t a = chip, room = limit(b) - bet[b];
    if (a > room) a = room;
    if (a > purse) a = purse;
    bet[b] = (uint16_t)(bet[b] + a);
    purse -= a;
    if (added) *added = (uint16_t)a;
    return D_OK;
}

Deny Craps::canTake(uint8_t b) const {
    if (b >= BET_COUNT || !bet[b]) return D_EMPTY;
    if ((b == PASS && point) || (b >= COME4 && b < CODDS4)) return D_CONTRACT;
    return D_OK;
}

// After a line bet shrinks, its odds may be over their limit: the excess
// comes back.
static void clampOdds(Craps &c) {
    static const uint8_t ODDS[2] = {PASS_ODDS, DONT_ODDS};
    for (uint8_t k = 0; k < 2; k++) {
        uint8_t b = ODDS[k];
        uint16_t l = c.limit(b);
        if (c.bet[b] > l) { c.purse += c.bet[b] - l; c.bet[b] = l; }
    }
}

uint16_t Craps::take(uint8_t b, uint16_t chip) {
    if (canTake(b) != D_OK) return 0;
    uint16_t a = chip < bet[b] ? chip : bet[b];
    bet[b] = (uint16_t)(bet[b] - a);
    purse += a;
    clampOdds(*this);
    return a;
}

uint16_t Craps::takeDown(uint8_t b) {
    if (canTake(b) != D_OK) return 0;
    return take(b, bet[b]);
}

Deny Craps::canRoll() const {
    if (point || bet[PASS] || bet[DONT]) return D_OK;
    for (uint8_t i = 0; i < 6; i++) if (bet[COME4 + i]) return D_OK;
    return D_ROLL_LINE;
}

// ---------------------------------------------------------------------------
// The roll
// ---------------------------------------------------------------------------
void Craps::throwDice() {
    uint8_t a, b;
    if (nForced) {
        a = (uint8_t)(forced[0] >> 4); b = (uint8_t)(forced[0] & 15);
        memmove(forced, forced + 1, --nForced);
    } else {
        a = die(); b = die();
    }
    settle(a, b);
}

// Every spot's fate for roll (a, b), decided from the table as it stands.
void Craps::decide(uint8_t a, uint8_t b) {
    uint8_t s = (uint8_t)(a + b);
    bool hard = a == b;
    for (uint8_t i = 0; i < BET_COUNT; i++) { res[i].kind = R_NONE; res[i].to = 0; res[i].stake = bet[i]; res[i].win = 0; }
    auto lose = [&](uint8_t i) { if (bet[i]) res[i].kind = R_LOSE; };
    auto win = [&](uint8_t i, int32_t w, bool home) {
        if (bet[i]) { res[i].kind = home ? R_WIN_HOME : R_WIN; res[i].win = w; }
    };
    auto back = [&](uint8_t i) { if (bet[i]) res[i].kind = R_RETURN; };

    // One-roll bets.
    if (s == 2) win(FIELD, bet[FIELD] * 2, false);
    else if (s == 12) win(FIELD, bet[FIELD] * 3, false);
    else if (s == 3 || s == 4 || s == 9 || s == 10 || s == 11) win(FIELD, bet[FIELD], false);
    else lose(FIELD);
    if (s == 7) win(ANY7, bet[ANY7] * 4, false); else lose(ANY7);
    if (isCraps(s)) win(ANYCRAPS, bet[ANYCRAPS] * 7, false); else lose(ANYCRAPS);
    if (s == 11) win(YO, bet[YO] * 15, false); else lose(YO);

    // Place bets and hardways (off on the come-out).
    if (point) {
        for (uint8_t i = 0; i < 6; i++) {
            uint8_t n = BOX_NUM[i], p = (uint8_t)(PLACE4 + i);
            if (s == n) win(p, placeWin(n, bet[p]), false);
            else if (s == 7) lose(p);
        }
        for (uint8_t n = 4; n <= 10; n += 2) {
            uint8_t h = (uint8_t)(HARD4 + hardIndex(n));
            if (s == n && hard) win(h, bet[h] * (n == 4 || n == 10 ? 7 : 9), false);
            else if (s == n || s == 7) lose(h);
        }
    }

    // Come points: the flat always works, its odds only with a point on.
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t n = BOX_NUM[i], c = (uint8_t)(COME4 + i), o = (uint8_t)(CODDS4 + i);
        if (s == n) {
            win(c, bet[c], true);
            if (point) win(o, oddsWin(n, bet[o]), true); else back(o);
        } else if (s == 7) {
            lose(c);
            if (point) lose(o); else back(o);
        }
    }

    // The Come: a bet waiting for its own come-out.
    if (bet[COME]) {
        if (s == 7 || s == 11) win(COME, bet[COME], true);
        else if (isCraps(s)) lose(COME);
        else { res[COME].kind = R_MOVE; res[COME].to = (uint8_t)(COME4 + box(s)); }
    }

    // The line.
    if (!point) {
        if (s == 7 || s == 11) { win(PASS, bet[PASS], false); lose(DONT); }
        else if (s == 2 || s == 3) { lose(PASS); win(DONT, bet[DONT], false); }
        else if (s == 12) lose(PASS);                    // the Don't is barred: a push
    } else if (s == point) {
        win(PASS, bet[PASS], false);
        win(PASS_ODDS, oddsWin(point, bet[PASS_ODDS]), true);
        lose(DONT); lose(DONT_ODDS);
    } else if (s == 7) {
        lose(PASS); lose(PASS_ODDS);
        win(DONT, bet[DONT], false);
        win(DONT_ODDS, layWin(point, bet[DONT_ODDS]), true);
    }
}

void Craps::settle(uint8_t a, uint8_t b) {
    d1 = a; d2 = b;
    uint8_t s = (uint8_t)(a + b);
    prevPoint = point;
    decide(a, b);

    // Money: losers and winners first, then come bets travel (a come point
    // that just won has gone home, so its box is free).
    rollWon = rollLost = 0;
    for (uint8_t i = 0; i < BET_COUNT; i++) {
        Res &r = res[i];
        switch (r.kind) {
            case R_LOSE: rollLost += bet[i]; bet[i] = 0; break;
            case R_WIN: purse += r.win; rollWon += r.win; break;
            case R_WIN_HOME: purse += r.win + bet[i]; rollWon += r.win; bet[i] = 0; break;
            case R_RETURN: purse += bet[i]; bet[i] = 0; break;
            default: break;
        }
        if (r.kind == R_WIN && i >= HARD4 && i <= HARD10) stats.hardways++;
    }
    for (uint8_t i = 0; i < BET_COUNT; i++)
        if (res[i].kind == R_MOVE) { bet[res[i].to] = (uint16_t)(bet[res[i].to] + bet[i]); bet[i] = 0; }

    // The point and the shooter.
    uint8_t kind = H_PLAIN;
    handRolls++;
    stats.rolls++;
    if (!point) {
        if (s == 7 || s == 11) kind = H_WINNER;
        else if (isCraps(s)) kind = H_CRAPS;
        else { point = s; kind = H_POINT; }
    } else if (s == point) {
        point = 0; handPoints++; stats.pointsMade++; kind = H_WINNER;
    } else if (s == 7) {
        point = 0; kind = H_SEVEN_OUT; stats.sevenOuts++;
        if (handRolls > stats.longestHand) stats.longestHand = handRolls;
        handRolls = 0; handPoints = 0;
    }
    for (uint8_t i = 5; i > 0; i--) { hist[i] = hist[i - 1]; histKind[i] = histKind[i - 1]; }
    hist[0] = (uint8_t)(a << 4 | b);
    histKind[0] = kind;

    if (rollWon > stats.biggestWin) stats.biggestWin = rollWon;
    int32_t worth = purse + tableTotal();
    if (worth > stats.bestPurse) stats.bestPurse = worth;
}

int32_t Craps::winFor(uint8_t b, int32_t a) const {
    if (b >= PLACE4 && b <= PLACE10) return placeWin(BOX_NUM[b - PLACE4], a);
    if (b >= HARD4 && b <= HARD10) return a * (b == HARD4 || b == HARD10 ? 7 : 9);
    if (b >= CODDS4) return oddsWin(BOX_NUM[b - CODDS4], a);
    switch (b) {
        case PASS_ODDS: return point ? oddsWin(point, a) : 0;
        case DONT_ODDS: return point ? layWin(point, a) : 0;
        case ANY7: return a * 4;
        case ANYCRAPS: return a * 7;
        case YO: return a * 15;
        default: return a;                       // even money (the field's 2 and 12 pay more)
    }
}
