// The CPU players (Ai.h): Monte Carlo equity, then a bet, call or fold
// shaped by the table's Level row below; and Five Card Draw's discards.
#pragma GCC optimize("Os")
#include <string.h>
#include "Ai.h"
#include "Cards.h"
#include "Hand.h"
#include "Variants.h"

namespace ai {

// How each table's CPUs play, in Q8 (256 = 1).
//   noise    random error added to the equity (few deals are noisy anyway)
//   slack    calls this much worse than the price (a calling station)
//   betAt    bets with this much more than a fair share of the pot...
//   raiseAt  ...and raises with this much
//   bluff    bets or raises with nothing, this often (fewer opponents only)
//   slow     checks or calls a monster, this often
//   fear     equity lost per bet or raise from the others this street
//   pos      equity gained acting last
//   floor    on the first street, folds anything below this share (the
//            price looks cheap there, but the betting to come is not)
struct Level { uint16_t samples; uint8_t noise, slack; uint16_t betAt, raiseAt, floor; uint8_t bluff, slow, fear, pos; };
static const Level LEVEL[LEVELS] = {
    {32, 30, 34, 400, 540, 150, 6, 70, 0, 0},       // ROOKIE: calls too much, rarely raises
    {128, 13, 8, 330, 390, 265, 20, 26, 18, 5},     // PRO: plays the odds
    {256, 8, 0, 300, 345, 285, 31, 38, 22, 10},     // SHARK: positional, bluffs, reads you
};

uint16_t samplesFor(uint8_t level, uint8_t game) {
    uint16_t n = LEVEL[level].samples;
    return game == OMAHA ? (uint16_t)(n / 4) : n;
}

// ---------------------------------------------------------------------------
// Equity by Monte Carlo
// ---------------------------------------------------------------------------
static View w;                       // what it is weighing
static uint8_t rest[52], nRest;      // the cards it can't see
static uint16_t done, target;
static uint32_t won;                 // Q8 shares of the deals played
static uint32_t rs;

static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

// Deals k unseen cards into out (a partial shuffle of rest).
static void take(uint8_t *out, uint8_t k, uint8_t &used) {
    for (uint8_t i = 0; i < k; i++) {
        uint8_t j = (uint8_t)(used + rnd() % (uint32_t)(nRest - used));
        uint8_t t = rest[used]; rest[used] = rest[j]; rest[j] = t;
        out[i] = rest[used++];
    }
}

static uint32_t best(const uint8_t *cards, uint8_t n, const uint8_t *board, uint32_t beat) {
    if (w.game == OMAHA) return hand::omaha(cards, board, 5, beat);
    uint8_t all[7];
    memcpy(all, cards, n);
    if (w.game == HOLDEM) { memcpy(all + 2, board, 5); n = 7; }
    return hand::eval(all, n);
}

// Five Card Draw: throw and replace as the CPUs do.
static void drawOut(uint8_t *five, uint8_t &used) {
    uint8_t m = discards(five, PRO);
    for (uint8_t i = 0; i < 5; i++) if ((m >> i) & 1) take(five + i, 1, used);
}

void begin(const View &v, uint32_t seed, uint16_t samples) {
    w = v;
    rs = seed | 1;
    done = 0; won = 0; target = samples ? samples : 1;
    // Everything it can't see: not its own, not the board, not a stud up-card,
    // not a card it threw away.
    uint8_t seen[52];
    memset(seen, 0, sizeof seen);
    for (uint8_t i = 0; i < v.nOwn; i++) seen[v.own[i]] = 1;
    for (uint8_t i = 0; i < v.nBoard; i++) seen[v.board[i]] = 1;
    for (uint8_t i = 0; i < v.nDead; i++) seen[v.dead[i]] = 1;
    for (uint8_t o = 0; o < v.nOpp; o++)
        for (uint8_t i = 0; i < v.nOppUp[o]; i++) seen[v.oppUp[o][i]] = 1;
    nRest = 0;
    for (uint8_t c = 0; c < 52; c++) if (!seen[c]) rest[nRest++] = c;
}

static void sample() {
    const Variant &var = VARIANTS[w.game];
    uint8_t used = 0, board[5], mine[7];
    memcpy(board, w.board, w.nBoard);
    memcpy(mine, w.own, w.nOwn);
    uint8_t hole = var.hole;
    bool community = w.game == HOLDEM || w.game == OMAHA;
    if (community) take(board + w.nBoard, (uint8_t)(5 - w.nBoard), used);
    else take(mine + w.nOwn, (uint8_t)(hole - w.nOwn), used);      // stud: the streets to come
    if (w.game == DRAW && w.preDraw) drawOut(mine, used);
    uint32_t me = best(mine, hole, board, 0xFFFFFFFFu);
    uint8_t ties = 0;
    for (uint8_t o = 0; o < w.nOpp; o++) {
        uint8_t theirs[7];
        uint8_t k = w.game == STUD ? w.nOppUp[o] : 0;
        memcpy(theirs, w.oppUp[o], k);
        take(theirs + k, (uint8_t)(hole - k), used);
        if (w.game == DRAW && w.preDraw) drawOut(theirs, used);
        uint32_t s = best(theirs, hole, board, me);
        if (s > me) return;                          // lost this deal
        if (s == me) ties++;
    }
    won += 256 / (ties + 1);
}

bool step(uint8_t k) {
    while (k-- && done < target) { sample(); done++; }
    return done >= target;
}

uint16_t equity() { return done ? (uint16_t)(won / done) : 0; }

// ---------------------------------------------------------------------------
// The decision
// ---------------------------------------------------------------------------
static int32_t roundTo(int32_t v, int32_t step) { return step > 1 ? (v + step / 2) / step * step : v; }

Choice decide(const View &v, uint8_t level, int8_t quirk, const Read &rd) {
    const Level &L = LEVEL[level];
    int eq = equity();
    eq += (int)(rnd() % (2u * L.noise + 1)) - L.noise + quirk;
    if (v.lastToAct) eq += L.pos;
    // Bets against it are a warning - less so from a player SHARK has seen
    // bluff or raise a lot.
    int fear = L.fear * v.raisesAgainst;
    if (level == SHARK && v.youRaised && rd.acts > 8) {
        int aggr = (int)(rd.raises * 256u / rd.acts) + rd.bluffs * 64;
        if (aggr > 256) aggr = 256;
        fear = fear * (320 - aggr) / 320;
    }
    eq -= fear;
    if (eq < 0) eq = 0;
    if (eq > 256) eq = 256;
    int rel = eq * (v.nOpp + 1);                       // 256 = a fair share
    uint32_t roll = rnd() & 255;
    Choice c = {M_CALL, 0};
    bool raise = false;
    if (v.toCall == 0) {
        if (rel >= L.betAt) raise = !(rel >= 2 * L.betAt && roll < L.slow);
        else if (v.nOpp <= 2 && roll < L.bluff) raise = true;
    } else {
        int need = (int)(v.toCall * 256 / (v.pot + v.toCall));
        if (eq + L.slack < need || (v.street == 0 && rel < L.floor)) {
            // SHARK sometimes re-bluffs one opponent instead of folding.
            if (level == SHARK && v.nOpp == 1 && v.canRaise && roll < L.bluff / 3) raise = true;
            else { c.move = M_FOLD; return c; }
        } else if (rel >= L.raiseAt) {
            raise = !(roll < L.slow && rel >= 2 * L.raiseAt);
        }
    }
    if (!raise || !v.canRaise) return c;
    c.move = M_RAISE;
    if (v.limit == FIXED_LIMIT || level == ROOKIE) { c.to = v.minTo; return c; }
    // Half the pot to the whole pot, more the stronger it is.
    int margin = rel - L.betAt;
    int frac = 128 + (margin > 0 ? margin / 3 : 0);
    if (frac > 256) frac = 256;
    int32_t after = v.pot + v.toCall;
    int32_t to = roundTo(v.bet + v.toCall + after * frac / 256, v.bb);
    if (to < v.minTo) to = v.minTo;
    // Most of the stack in anyway: all of it.
    if ((to - v.bet) * 5 > v.stack * 3) to = v.maxTo;
    if (to > v.maxTo) to = v.maxTo;
    c.to = to;
    return c;
}

// ---------------------------------------------------------------------------
// Five Card Draw
// ---------------------------------------------------------------------------
uint8_t discards(const uint8_t *f, uint8_t level) {
    uint32_t s = hand::eval(f, 5);
    uint8_t cat = hand::cat(s), cnt[13] = {0}, sc[4] = {0};
    for (uint8_t i = 0; i < 5; i++) { cnt[rankOf(f[i])]++; sc[suitOf(f[i])]++; }
    uint8_t keep = 0;                                  // mask of cards to keep
    if (cat >= hand::STRAIGHT) return 0;               // stand pat
    if (cat >= hand::PAIR) {
        // Keep the pairs and trips (two pair: both pairs).
        for (uint8_t i = 0; i < 5; i++) if (cnt[rankOf(f[i])] >= 2) keep |= (uint8_t)(1 << i);
        // A low pair breaks for four to a flush.
        if (cat == hand::PAIR && hand::topRank(s) < RJ)
            for (uint8_t su = 0; su < 4; su++)
                if (sc[su] == 4) { keep = 0; for (uint8_t i = 0; i < 5; i++) if (suitOf(f[i]) == su) keep |= (uint8_t)(1 << i); }
        return (uint8_t)(~keep & 31);
    }
    for (uint8_t su = 0; su < 4; su++)
        if (sc[su] == 4) {
            for (uint8_t i = 0; i < 5; i++) if (suitOf(f[i]) != su) return (uint8_t)(1 << i);
        }
    // Four in a row, open at both ends: throw the odd card.
    uint32_t m = 0;
    for (uint8_t i = 0; i < 5; i++) m |= 1u << rankOf(f[i]);
    for (uint8_t lo = 0; lo + 3 < 12; lo++) {
        uint32_t run = 15u << lo;
        if ((m & run) == run)
            for (uint8_t i = 0; i < 5; i++) if (!((run >> rankOf(f[i])) & 1)) return (uint8_t)(1 << i);
    }
    // Nothing: keep an ace (ROOKIE keeps a kicker with it), else the two highest.
    uint8_t order[5] = {0, 1, 2, 3, 4};
    for (uint8_t i = 0; i < 5; i++)
        for (uint8_t j = (uint8_t)(i + 1); j < 5; j++)
            if (f[order[j]] > f[order[i]]) { uint8_t t = order[i]; order[i] = order[j]; order[j] = t; }
    keep = (uint8_t)(1 << order[0]);
    if (rankOf(f[order[0]]) != RA || level == ROOKIE) keep |= (uint8_t)(1 << order[1]);
    return (uint8_t)(~keep & 31);
}

}  // namespace ai
