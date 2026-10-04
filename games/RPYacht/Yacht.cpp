// The rules (Yacht.h): the dice, the thirteen boxes and their bonuses, the
// money, and the house player (ai::), which looks one roll ahead.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <string.h>
#include "Yacht.h"

// ---------------------------------------------------------------------------
// Dice (CHCraps's generator)
// ---------------------------------------------------------------------------
void Yacht::seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; reseeded = true; }

void Yacht::mix(uint32_t e) {
    if (reseeded) return;                       // scripts replay the same rolls
    rng ^= e * 2654435761u;
    rand32();
}

uint32_t Yacht::rand32() {
    if (!rng) rng = 0x9E3779B9u;
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

// 1..6 from the top of a 32-bit draw: one multiply-high, bias ~1e-9.
uint8_t Yacht::die() { return (uint8_t)(1 + (uint32_t)(((uint64_t)rand32() * 6u) >> 32)); }

void Yacht::force(const uint8_t *v) {
    if (nForced >= 4) return;
    uint32_t w = 0;
    for (uint8_t i = 0; i < 5; i++) {
        if (v[i] < 1 || v[i] > 6) return;
        w |= (uint32_t)v[i] << (3 * i);
    }
    forced[nForced++] = w;
}

void Yacht::roll() {
    if (!canRoll()) return;
    uint32_t f = 0;
    if (nForced) {
        f = forced[0];
        for (uint8_t i = 1; i < nForced; i++) forced[i - 1] = forced[i];
        nForced--;
    }
    if (!rolled()) held = 0;
    for (uint8_t i = 0; i < 5; i++) {
        if (held >> i & 1) continue;
        dice[i] = f ? (uint8_t)(f >> (3 * i) & 7) : die();
    }
    rollsLeft--;
}

// ---------------------------------------------------------------------------
// The card
// ---------------------------------------------------------------------------
uint16_t Card::upper() const {
    uint16_t s = 0;
    for (uint8_t c = ONES; c <= SIXES; c++) s += score[c];
    return s;
}

uint16_t Card::total() const {
    uint16_t s = (uint16_t)(extra * 100 + (bonus() ? 35 : 0));
    for (uint8_t c = 0; c < CAT_COUNT; c++) s += score[c];
    return s;
}

bool Yacht::isYacht(const uint8_t *d) {
    return d[0] == d[1] && d[1] == d[2] && d[2] == d[3] && d[3] == d[4];
}

// Every box's points for these dice at once (the house player values
// thousands of rolls a turn).
static void pointsAll(const Card &c, const uint8_t *d, uint8_t *out) {
    uint8_t cnt[7] = {0, 0, 0, 0, 0, 0, 0}, sum = 0, most = 0, run = 0, best = 0;
    bool pair = false;
    for (uint8_t i = 0; i < 5; i++) { cnt[d[i]]++; sum = (uint8_t)(sum + d[i]); }
    for (uint8_t f = 1; f <= 6; f++) {
        out[f - 1] = (uint8_t)(cnt[f] * f);
        if (cnt[f] > most) most = cnt[f];
        if (cnt[f] == 2) pair = true;
        run = cnt[f] ? (uint8_t)(run + 1) : 0;
        if (run > best) best = run;
    }
    bool joker = most == 5 && !c.open(YACHT);
    out[KIND3] = most >= 3 ? sum : 0;
    out[KIND4] = most >= 4 ? sum : 0;
    out[FULL] = ((most == 3 && pair) || joker) ? 25 : 0;
    out[SMALL] = (best >= 4 || joker) ? 30 : 0;
    out[LARGE] = (best == 5 || joker) ? 40 : 0;
    out[YACHT] = most == 5 ? 50 : 0;
    out[CHANCE] = sum;
}

uint8_t Yacht::pointsFor(const Card &c, uint8_t cat, const uint8_t *d) {
    uint8_t p[CAT_COUNT];
    pointsAll(c, d, p);
    return p[cat];
}

bool Yacht::legalFor(const Card &c, uint8_t cat, const uint8_t *d) {
    if (!c.open(cat)) return false;
    if (!isYacht(d) || c.open(YACHT)) return true;
    uint8_t own = (uint8_t)(d[0] - 1);
    if (c.open(own)) return cat == own;
    if ((c.filled & 0x1FC0) != 0x1FC0) return cat >= KIND3;
    return true;
}

uint8_t Yacht::combo(const uint8_t *d) {
    static const uint8_t ORDER[6] = {YACHT, LARGE, KIND4, FULL, SMALL, KIND3};
    Card none;
    none.filled = 0;
    uint8_t p[CAT_COUNT];
    pointsAll(none, d, p);
    for (uint8_t i = 0; i < 6; i++) if (p[ORDER[i]]) return ORDER[i];
    return CAT_COUNT;
}

// ---------------------------------------------------------------------------
// The game
// ---------------------------------------------------------------------------
// Calibrated against the house player's own solo scores (tools/tests/
// test_yacht.cpp prints the distribution and the return).
const uint16_t Yacht::TIER_AT[TIERS] = {260, 300, 350, 400, 500};
const uint8_t Yacht::TIER_PAYS[TIERS] = {1, 2, 3, 5, 10};

uint8_t Yacht::tier(uint16_t total) {
    uint8_t pays = 0;
    for (uint8_t i = 0; i < TIERS; i++) if (total >= TIER_AT[i]) pays = TIER_PAYS[i];
    return pays;
}

uint16_t Yacht::anteFor(uint8_t o) { return o == 0 ? 5 : (o == 1 ? 25 : 100); }

void Yacht::newPurse() { purse = START_PURSE; }

void Yacht::newGame(uint8_t m) {
    mode = m;
    players = m == M_SOLO ? 1 : (m == M_CPU ? 2 : (uint8_t)(m - M_PARTY2 + 2));
    cur = 0; round = 0; rollsLeft = 3; held = 0; over = false;
    memset(card, 0, sizeof card);
    for (uint8_t i = 0; i < 5; i++) dice[i] = (uint8_t)(i + 1);
    lastPay = endPay = 0;
    ante = 0;
    if (staked()) {
        if (broke()) newPurse();
        ante = anteFor(opt.ante);
        while (ante > purse) ante = ante > 25 ? 25 : 5;     // what the purse can cover
        purse -= ante;
    }
}

uint8_t Yacht::winner() const {
    uint8_t w = 0;
    for (uint8_t p = 1; p < players; p++) if (card[p].total() > card[w].total()) w = p;
    if (mode == M_CPU && card[0].total() == card[1].total()) return 0xFF;
    return w;
}

void Yacht::finish() {
    over = true;
    if (!staked()) return;
    uint16_t mine = card[0].total();
    stats.games++;
    if (mine > stats.best) stats.best = mine;
    if (mode == M_SOLO) endPay = (int32_t)ante * tier(mine);
    else {
        uint8_t w = winner();
        endPay = w == 0 ? 2 * (int32_t)ante : (w == 0xFF ? ante : 0);
        if (w == 0) stats.cpuWins++;
        else if (w == 1) stats.cpuLosses++;
    }
    purse += endPay;
    if (endPay - ante > stats.biggestWin) stats.biggestWin = endPay - ante;
    if (purse > stats.bestPurse) stats.bestPurse = purse;
    if (broke()) stats.timesBroke++;
}

uint8_t Yacht::score(uint8_t cat) {
    if (!legal(cat)) return 0;
    Card &c = card[cur];
    uint8_t ev = 0, pts = points(cat);
    bool hadBonus = c.bonus(), mine = staked() && cur == 0;
    if (isYacht(dice) && !c.open(YACHT) && c.score[YACHT]) { c.extra++; ev |= EV_EXTRA; }
    c.score[cat] = pts;
    c.filled |= (uint16_t)(1u << cat);
    if (cat == YACHT && pts) ev |= EV_YACHT;
    if (!hadBonus && c.bonus()) ev |= EV_UPPER;
    if (!pts) ev |= EV_ZERO;
    lastCat = cat; lastPts = pts; lastPlayer = cur;
    lastPay = 0;
    if (mine) {
        if (ev & (EV_YACHT | EV_EXTRA)) { lastPay += ante; stats.yachts++; }
        if (ev & EV_UPPER) { lastPay += ante / UPPER_SHARE; stats.bonuses++; }
        purse += lastPay;
    }
    rollsLeft = 3; held = 0;
    if (++cur >= players) { cur = 0; round++; }
    if (round >= TURNS) { finish(); ev |= EV_OVER; }
    return ev;
}

// ---------------------------------------------------------------------------
// The house player
// ---------------------------------------------------------------------------
namespace ai {

// What each box is worth on average to a good player, in tenths: a roll is
// valued by how far its best box beats that.
static const uint16_t PAR[CAT_COUNT] = {21, 53, 86, 122, 157, 192, 217, 131, 226, 295, 327, 169, 220};
static const int UPPER_WEIGHT = 15;     // tenths a point over / under three of a kind (the 63)

static int32_t best(const Card &c, const uint8_t *d, uint8_t *which) {
    uint8_t p[CAT_COUNT];
    pointsAll(c, d, p);
    int32_t bv = -100000;
    for (uint8_t cat = 0; cat < CAT_COUNT; cat++) {
        if (!Yacht::legalFor(c, cat, d)) continue;
        int32_t v = 10 * p[cat] - PAR[cat];
        if (cat <= SIXES && !c.bonus()) v += UPPER_WEIGHT * (p[cat] - 3 * (cat + 1));
        if (v > bv) { bv = v; if (which) *which = cat; }
    }
    if (Yacht::isYacht(d) && !c.open(YACHT) && c.score[YACHT]) bv += 1000;
    return bv;
}

uint8_t bestCat(const Card &c, const uint8_t *dice) {
    uint8_t cat = 0;
    best(c, dice, &cat);
    return cat;
}

void begin(Think &t) { t.mask = 32; t.best = 31; t.bestEv = -0x7FFFFFFF; }

// Hold `mask`, roll the rest: every distinct outcome once (sorted faces),
// weighted by how many ways it comes up, scaled to 6^5 so holds compare.
static int32_t expect(const Card &c, const uint8_t *dice, uint8_t mask) {
    static const uint8_t FACT[6] = {1, 1, 2, 6, 24, 120};
    static const uint16_t POW6[6] = {7776, 1296, 216, 36, 6, 1};
    uint8_t d[5], at[5], k = 0, v[5];
    for (uint8_t i = 0; i < 5; i++) {
        d[i] = dice[i];
        if (!(mask >> i & 1)) { at[k] = i; v[k++] = 1; }
    }
    int32_t sum = 0;
    for (;;) {
        uint8_t ways = FACT[k], run = 1;
        for (uint8_t i = 0; i < k; i++) {
            d[at[i]] = v[i];
            if (i + 1 < k && v[i + 1] == v[i]) run++;
            else { ways = (uint8_t)(ways / FACT[run]); run = 1; }
        }
        sum += ways * best(c, d, nullptr);
        int8_t p = (int8_t)(k - 1);
        while (p >= 0 && v[p] == 6) p--;
        if (p < 0) break;
        v[p]++;
        for (uint8_t i = (uint8_t)(p + 1); i < k; i++) v[i] = v[p];
    }
    return sum * POW6[k];
}

bool step(const Yacht &g, Think &t) {
    for (uint8_t n = 0; n < 2 && t.mask; n++) {
        t.mask--;
        int32_t ev = expect(g.card[g.cur], g.dice, t.mask);
        if (ev > t.bestEv) { t.bestEv = ev; t.best = t.mask; }      // ties: the fuller hand
    }
    return t.mask == 0;
}

}  // namespace ai
