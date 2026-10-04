// The rules (Game.h): a phase machine stepped once a tick. It queues
// events for the stage and waits while the stage is busy showing them.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <string.h>
#include "config.h"
#include "Game.h"
#include "Cpu.h"

namespace game {

using namespace board;

State st;
Auction auc;

static uint8_t ph;                          // Phase
static uint8_t afterSettle, afterAuction;   // where a payment and an auction hand back to
static uint8_t steps, rolled;               // the move to make; the dice just thrown
static bool special;                        // sent by a card to the nearest railroad or utility: its rent rule
static uint8_t cardDeck, cardIdx;           // the card on show
static State mark;                          // the game as the turn began

static const uint8_t DEAL2 = 4;

// Payments a player owes, settled in order (a card can ask three players).
struct Due { uint8_t from, to, reason; int16_t amount; };
static Due due[4];
static uint8_t nDue;

static Event q[16];
static uint8_t qHead, qCount;

static void push(uint8_t type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, int amount = 0) {
    if (qCount == 16) { qHead = (uint8_t)((qHead + 1) & 15); qCount--; }    // never stall: drop the oldest
    Event &e = q[(qHead + qCount++) & 15];
    e.type = type; e.a = a; e.b = b; e.c = c; e.amount = (int16_t)amount;
}

bool peekEvent(Event &e) {
    if (!qCount) return false;
    e = q[qHead];
    return true;
}

bool popEvent(Event &e) {
    if (!peekEvent(e)) return false;
    qHead = (uint8_t)((qHead + 1) & 15);
    qCount--;
    return true;
}

// The game's own random stream (presentation has fx::rnd): the same seed and
// the same presses play the same game.
static uint32_t rnd() {
    uint32_t x = st.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return st.rng = x;
}

static void shuffle(uint8_t *a, uint8_t n) {
    for (uint8_t i = (uint8_t)(n - 1); i > 0; i--) {
        uint8_t j = (uint8_t)(rnd() % (uint32_t)(i + 1)), t = a[i];
        a[i] = a[j]; a[j] = t;
    }
}

#if CHGAME_DEBUG || defined(CHTEST)
static uint8_t forced[8], nForced;
void forceDice(uint8_t d1, uint8_t d2) {
    if (nForced + 2 <= 8) { forced[nForced++] = d1; forced[nForced++] = d2; }
}
static uint8_t die() {
    if (nForced) {
        uint8_t d = forced[0];
        memmove(forced, forced + 1, --nForced);
        return d;
    }
    return (uint8_t)(1 + rnd() % 6);
}
#else
static uint8_t die() { return (uint8_t)(1 + rnd() % 6); }
#endif

// ---------------------------------------------------------------------------
// Reading the board
// ---------------------------------------------------------------------------
Phase phase() { return (Phase)ph; }
uint8_t lastRoll() { return rolled; }
bool humanToAct() { return isHuman(st.cur) && (ph == P_PREROLL || ph == P_OFFER) && !qCount; }

// How many of t's group its owner holds, and the group's size.
static uint8_t held(uint8_t t, uint8_t &n) {
    uint8_t tiles[4], have = 0, o = owner(t);
    n = groupTiles(group(t), tiles);
    for (uint8_t i = 0; i < n; i++) have += owner(tiles[i]) == o;
    return have;
}

// How many of a group's tiles p holds; and the fewest and the most houses
// on those.
static uint8_t holds(uint8_t p, uint8_t g, uint8_t &n, uint8_t &lo, uint8_t &hi) {
    uint8_t tiles[4], have = 0;
    n = groupTiles(g, tiles);
    lo = 5; hi = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (owner(tiles[i]) != p) continue;
        uint8_t l = level(tiles[i]);
        have++;
        if (l < lo) lo = l;
        if (l > hi) hi = l;
    }
    return have;
}

bool ownsGroup(uint8_t p, uint8_t g) {
    uint8_t n, lo, hi;
    return holds(p, g, n, lo, hi) == n;
}

bool majority(uint8_t p, uint8_t g) {
    uint8_t n, lo, hi;
    return 2 * holds(p, g, n, lo, hi) > n;
}

int rent(uint8_t t, uint8_t roll) {
    uint8_t n, have = held(t, n);
    switch (type(t)) {
        case STREET: {
            int r = RENT[TILE[t].aux][level(t)];
            return !level(t) && have == n ? 2 * r : r;       // a full set doubles the bare rent
        }
        case RAIL: return (25 << (have - 1)) * (special ? 2 : 1);
        default:   return (have == n || special ? 10 : 4) * roll;
    }
}

int netWorth(uint8_t p) {
    int w = (int)st.pl[p].cash;
    for (uint8_t t = 0; t < TILES; t++)
        if (isDeed(t) && owner(t) == p) w += price(t) + level(t) * houseCost(t);
    return w;
}

// ---------------------------------------------------------------------------
// Money
// ---------------------------------------------------------------------------
static void pay(uint8_t from, uint8_t to, int amount, uint8_t reason) {
    if (amount <= 0) return;
    if (from != BANK) st.pl[from].cash -= amount;
    if (to != BANK) st.pl[to].cash += amount;
    else if (reason == R_TAX || reason == R_CARD || reason == R_JAIL) st.pot = (uint16_t)(st.pot + amount);
    push(EV_PAY, from, to, reason, amount);
}

static void owe(uint8_t from, uint8_t to, int amount, uint8_t reason) {
    if (amount <= 0 || nDue == 4) return;
    Due &d = due[nDue++];
    d.from = from; d.to = to; d.reason = reason; d.amount = (int16_t)amount;
}

static void settleThen(uint8_t next) {
    afterSettle = next;
    ph = P_SETTLE;
}

static void finish(uint8_t why) {
    uint8_t w = NOBODY;
    int best = 0;
    for (uint8_t p = 0; p < st.players; p++) {
        if (st.pl[p].flags & F_BUST) continue;
        int v = netWorth(p);
        // Ties: more cash, then the lower seat.
        if (w == NOBODY || v > best || (v == best && st.pl[p].cash > st.pl[w].cash)) { w = p; best = v; }
    }
    st.over = (uint8_t)(why + 1);
    st.winner = w;
    nDue = 0;
    ph = P_OVER;
    push(EV_OVER, w, why);
}

static void sellOne(uint8_t t) {
    st.deed[t] = (uint8_t)(st.deed[t] - 8);
    push(EV_SELL, t, level(t), owner(t));
    pay(BANK, owner(t), houseCost(t) / 2, R_SELL);
}

// ---------------------------------------------------------------------------
// The auction
// ---------------------------------------------------------------------------
static int nextBid() { return auc.leader == NOBODY ? 0 : auc.price + auc.step; }

// Who could answer the bid that stands? Each CPU that could takes a moment
// over it: quick while the lot is cheap to it, slower as it nears what it
// would pay, longest at its last. False: nobody could.
static bool answers() {
    bool open = false;
    for (uint8_t o = 0; o < st.players; o++) {
        if (o == auc.leader || o == auc.seller || nextBid() > st.pl[o].cash) continue;
        if (isHuman(o)) { open = true; continue; }
        if (nextBid() > auc.limit[o]) continue;
        open = true;
        auc.wait[o] = (uint8_t)(2 * nextBid() < auc.limit[o] ? 6 + rnd() % 10 : 14 + rnd() % 40);
        if (nextBid() + auc.step > auc.limit[o]) auc.wait[o] = (uint8_t)(auc.wait[o] + 30);
    }
    return open;
}

static void startAuction(uint8_t t, uint8_t seller, uint8_t next) {
    memset(&auc, 0, sizeof auc);
    auc.tile = t;
    auc.seller = seller;
    auc.leader = NOBODY;
    // A tenth of the list price a bid, to the nearest $10.
    auc.step = (uint8_t)((price(t) + 50) / 100 * 10);
    if (auc.step < 10) auc.step = 10;
    // A player's deed never goes for nothing: the bank opens at half its
    // price, and keeps it if nobody goes higher.
    if (seller != BANK) { auc.leader = BANK; auc.price = (int16_t)(price(t) / 2); }
    for (uint8_t p = 0; p < st.players; p++)
        if (isCpu(p) && p != seller) auc.limit[p] = (int16_t)cpu::valuation(p, t);
    auc.timer = answers() ? AUCTION_OPEN : AUCTION_LAST;
    afterAuction = next;
    ph = P_AUCTION;
    push(EV_AUCTION, t, seller);
}

bool canBid(uint8_t p) {
    return ph == P_AUCTION && auc.started && p < st.players && p != auc.seller && p != auc.leader &&
           nextBid() <= st.pl[p].cash && nextBid() <= 30000;
}

bool bid(uint8_t p) {
    if (!canBid(p)) return false;
    auc.price = (int16_t)nextBid();
    auc.leader = p;
    // With nobody left who could answer, the gavel comes down early.
    auc.timer = answers() ? AUCTION_WINDOW : AUCTION_LAST;
    push(EV_BID, p, 0, 0, auc.price);
    return true;
}

static void closeAuction() {
    uint8_t w = auc.leader;
    // The bank's own lot, and nobody bid: it goes free to the next seat
    // after the lander.
    if (w == NOBODY) w = (uint8_t)((st.cur + 1) % st.players);
    st.deed[auc.tile] = w;                              // (the bank, if its opening bid stood)
    push(EV_SOLD, w, auc.tile, auc.seller, auc.price);
    pay(w, auc.seller, auc.price, R_AUCTION);
    ph = afterAuction;
}

static void auctionTick(bool stageBusy) {
    if (!auc.started) {
        if (stageBusy || qCount) return;        // the stage opens the lot first
        auc.started = 1;
    }
    for (uint8_t p = 0; p < st.players; p++) {
        if (!isCpu(p) || p == auc.seller || p == auc.leader) continue;
        if (auc.wait[p]) { auc.wait[p]--; continue; }
        int limit = auc.limit[p] < st.pl[p].cash ? auc.limit[p] : (int)st.pl[p].cash;
        if (nextBid() <= limit && bid(p)) break;            // one bid a tick
    }
    if (--auc.timer == 0) closeAuction();
}

// ---------------------------------------------------------------------------
// Moving and landing
// ---------------------------------------------------------------------------
static void goJail() {
    Player &me = st.pl[st.cur];
    push(EV_JAIL, st.cur, me.pos);
    me.pos = JAIL_TILE;
    me.jail = 1;
    st.doubles = 0;
    ph = P_ENDTURN;
}

// Passing GO pays $200; landing on it with the dice, payday: $400.
static void moveBy(int n, bool dice = false) {
    Player &me = st.pl[st.cur];
    uint8_t from = me.pos;
    me.pos = (uint8_t)((from + n + TILES) % TILES);
    push(EV_MOVE, st.cur, from, me.pos, n);
    if (n > 0 && me.pos < from) pay(BANK, st.cur, dice && !me.pos ? 400 : 200, R_GO);
    ph = P_RESOLVE;
}

static void moveTo(uint8_t t) { moveBy((t - st.pl[st.cur].pos + TILES) % TILES); }

static uint8_t nearest(uint8_t kind) {
    uint8_t t = st.pl[st.cur].pos;
    do t = (uint8_t)((t + 1) % TILES); while (type(t) != kind);
    return t;
}

static uint8_t draw(uint8_t d) {
    for (;;) {
        uint8_t c = st.deck[d][st.top[d]];
        st.top[d] = (uint8_t)((st.top[d] + 1) % DECK);
        // The get-out-of-jail card is out of the deck while someone holds it.
        bool out = false;
        if (CARD[d][c].kind == GOOJF)
            for (uint8_t p = 0; p < st.players; p++) out |= (st.pl[p].flags & (F_CARD << d)) != 0;
        if (!out) return c;
    }
}

static void resolve() {
    uint8_t p = st.cur, t = st.pl[p].pos;
    push(EV_LAND, p, t, 0, type(t) == PARKING ? st.pot : 0);
    ph = P_ENDTURN;
    switch (type(t)) {
        case PARKING:                                   // the jackpot
            pay(BANK, p, st.pot, R_JACKPOT);
            st.pot = 0;
            break;
        case STREET: case RAIL: case UTIL: {
            uint8_t o = owner(t);
            if (o == BANK) {
                if (st.pl[p].cash >= price(t)) { ph = P_OFFER; push(EV_OFFER, p, t, 0, price(t)); }
                else startAuction(t, BANK, P_ENDTURN);
            } else if (o != p) {
                owe(p, o, rent(t, rolled), R_RENT);
                settleThen(P_ENDTURN);
            }
            break;
        }
        case TAX:
            owe(p, BANK, TILE[t].aux * 10, R_TAX);
            settleThen(P_ENDTURN);
            break;
        case GOTOJAIL: goJail(); break;
        case CHANCE: case CHEST:
            cardDeck = type(t) == CHEST;
            cardIdx = draw(cardDeck);
            push(EV_CARD, cardDeck, cardIdx, p);
            ph = P_CARD;
            break;
    }
    special = false;
}

static void applyCard() {
    uint8_t p = st.cur;
    const Card &c = CARD[cardDeck][cardIdx];
    int amount = c.arg * 5;
    ph = P_ENDTURN;
    switch (c.kind) {
        case MOVE_TO:   moveTo(c.arg); break;
        case BACK3:     moveBy(-3); break;
        case NEAR_RAIL: special = true; moveTo(nearest(RAIL)); break;
        case NEAR_UTIL: special = true; moveTo(nearest(UTIL)); break;
        case COLLECT:   pay(BANK, p, amount, R_CARD); break;
        case PAY:       owe(p, BANK, amount, R_CARD); break;
        case PAY_EACH:
            for (uint8_t o = 0; o < st.players; o++) if (o != p) owe(p, o, amount, R_CARD);
            break;
        case COLLECT_EACH:
            for (uint8_t o = 0; o < st.players; o++) if (o != p) owe(o, p, amount, R_CARD);
            break;
        case REPAIRS: {
            int houses = 0, hotels = 0;
            for (uint8_t t = 0; t < TILES; t++) {
                if (owner(t) != p || !level(t)) continue;
                if (level(t) == 5) hotels++; else houses += level(t);
            }
            owe(p, BANK, c.arg ? houses * 40 + hotels * 115 : houses * 25 + hotels * 100, R_CARD);
            break;
        }
        case GO_JAIL:   goJail(); break;
        case GOOJF:     st.pl[p].flags |= (uint8_t)(F_CARD << cardDeck); break;
    }
    if (nDue) settleThen(P_ENDTURN);
}

// ---------------------------------------------------------------------------
// Debts: pay if the cash is there; else a house goes back at half price,
// then deeds go to auction (strays first, cheapest first), and with nothing
// left to sell the player is bankrupt.
// ---------------------------------------------------------------------------
static void settle() {
    if (!nDue) { ph = afterSettle; return; }
    Due d = due[0];
    Player &p = st.pl[d.from];
    if (p.cash >= d.amount) {
        pay(d.from, d.to, d.amount, d.reason);
        memmove(due, due + 1, --nDue * sizeof(Due));
        return;
    }
    int house = -1, deed = -1, deedScore = 0;
    for (uint8_t t = 0; t < TILES; t++) {
        if (!isDeed(t) || owner(t) != d.from) continue;
        if (level(t) && (house < 0 || level(t) > level((uint8_t)house))) house = t;
        int s = price(t) + (majority(d.from, group(t)) ? 1000 : 0);
        if (deed < 0 || s < deedScore) { deed = t; deedScore = s; }
    }
    if (house >= 0) { sellOne((uint8_t)house); return; }
    if (deed >= 0) { startAuction((uint8_t)deed, d.from, P_SETTLE); return; }
    pay(d.from, d.to, (int)p.cash, d.reason);
    p.flags |= F_BUST;
    push(EV_BANKRUPT, d.from);
    finish(BY_BANKRUPTCY);
}

// ---------------------------------------------------------------------------
// The player at the turn
// ---------------------------------------------------------------------------
bool roll() {
    if (ph != P_PREROLL) return false;
    Player &me = st.pl[st.cur];
    uint8_t d1 = die(), d2 = die();
    bool dbl = d1 == d2;
    rolled = steps = (uint8_t)(d1 + d2);
    push(EV_DICE, d1, d2, dbl);
    ph = P_MOVE;
    if (me.jail) {
        st.doubles = 0;                                 // out on doubles: no second roll
        if (dbl) {
            me.jail = 0;
            push(EV_JAILOUT, st.cur, 0);
        } else if (me.jail >= 3) {                      // the third miss: pay, and move
            me.jail = 0;
            push(EV_JAILOUT, st.cur, 1);
            owe(st.cur, BANK, 50, R_JAIL);
            settleThen(P_MOVE);
        } else {
            me.jail++;
            ph = P_ENDTURN;
        }
        return true;
    }
    if (!dbl) st.doubles = 0;
    else if (++st.doubles == 3) goJail();               // three doubles running
    return true;
}

bool payJail() {
    Player &me = st.pl[st.cur];
    if (ph != P_PREROLL || !me.jail || me.cash < 50) return false;
    me.jail = 0;
    pay(st.cur, BANK, 50, R_JAIL);
    push(EV_JAILOUT, st.cur, 1);
    return true;
}

bool useJailCard() {
    Player &me = st.pl[st.cur];
    if (ph != P_PREROLL || !me.jail || !(me.flags & (F_CARD | F_CARD << 1))) return false;
    me.flags &= (uint8_t) ~(me.flags & F_CARD ? F_CARD : F_CARD << 1);
    me.jail = 0;
    push(EV_JAILOUT, st.cur, 2);
    return true;
}

// With most of a colour group you can build on what you hold of it, evenly.
bool canBuild(uint8_t t) {
    uint8_t n, lo, hi;
    if (ph != P_PREROLL || type(t) != STREET || owner(t) != st.cur) return false;
    return 2 * holds(st.cur, group(t), n, lo, hi) > n && level(t) < 5 && level(t) == lo &&
           st.pl[st.cur].cash >= houseCost(t);
}

bool build(uint8_t t) {
    if (!canBuild(t)) return false;
    pay(st.cur, BANK, houseCost(t), R_BUILD);
    st.deed[t] = (uint8_t)(st.deed[t] + 8);
    push(EV_BUILD, t, level(t), st.cur);
    return true;
}

bool canSell(uint8_t t) {
    uint8_t n, lo, hi;
    if (ph != P_PREROLL || type(t) != STREET || owner(t) != st.cur || !level(t)) return false;
    holds(st.cur, group(t), n, lo, hi);
    return level(t) == hi;
}

bool sell(uint8_t t) {
    if (!canSell(t)) return false;
    sellOne(t);
    return true;
}

bool canOffer(uint8_t t) {
    uint8_t n, lo, hi;
    if (ph != P_PREROLL || !isDeed(t) || owner(t) != st.cur) return false;
    holds(st.cur, group(t), n, lo, hi);
    return !hi;                                         // no buildings on what you hold of its group
}

bool offer(uint8_t t) {
    if (!canOffer(t)) return false;
    startAuction(t, st.cur, P_PREROLL);
    return true;
}

bool buy() {
    uint8_t p = st.cur, t = st.pl[p].pos;
    if (ph != P_OFFER || st.pl[p].cash < price(t)) return false;
    st.deed[t] = p;
    push(EV_BUY, p, t);
    pay(p, BANK, price(t), R_BUY);
    ph = P_ENDTURN;
    return true;
}

bool decline() {
    if (ph != P_OFFER) return false;
    startAuction(st.pl[st.cur].pos, BANK, P_ENDTURN);
    return true;
}

// ---------------------------------------------------------------------------
// The CPU at the turn: one thing a step, each announced first so the stage's
// glove can press the button a player would.
// ---------------------------------------------------------------------------
static void cpuPreroll() {
    uint8_t p = st.cur;
    if (st.pl[p].jail) {
        uint8_t how = cpu::jailChoice(p);
        if (how) {
            push(EV_PICK, how == 2 ? ACT_CARD : ACT_PAY);
            if (how == 2) useJailCard(); else payJail();
            return;
        }
    }
    int t = cpu::pickBuild(p);
    if (t >= 0) { push(EV_PICK, ACT_BUILD, (uint8_t)t); build((uint8_t)t); return; }
    push(EV_PICK, ACT_ROLL);
    roll();
}

static void cpuOffer() {
    bool want = cpu::wantsBuy(st.cur, st.pl[st.cur].pos);
    push(EV_PICK, want ? ACT_BUY : ACT_AUCTION);
    if (want) buy(); else decline();
}

// ---------------------------------------------------------------------------
// Turns
// ---------------------------------------------------------------------------
static void beginTurn() {
    mark = st;
    push(EV_TURN, st.cur, isHuman(st.cur), 0);
    ph = P_PREROLL;
}

static void endTurn() {
    if (st.doubles && !st.pl[st.cur].jail) {            // doubles: again
        push(EV_TURN, st.cur, isHuman(st.cur), st.doubles);
        ph = P_PREROLL;
        return;
    }
    st.doubles = 0;
    st.cur = (uint8_t)((st.cur + 1) % st.players);
    if (!st.cur) {
        if (st.roundCap && st.round >= st.roundCap) { finish(BY_CLOSING); return; }
        if (++st.round == st.roundCap) push(EV_LASTROUND);
    }
    ph = P_TURN;
}

void start(const Setup &s) {
    memset(&st, 0, sizeof st);
    st.rng = s.seed ? s.seed : 0x9E3779B9u;
    for (uint8_t k = 0; k < SEATS; k++) {
        if (!s.kind[k]) continue;
        Player &p = st.pl[st.players++];
        p.cash = 1500;
        p.kind = s.kind[k];
    }
    st.roundCap = s.roundCap;
    st.round = 1;
    memset(st.deed, BANK, sizeof st.deed);
    for (uint8_t d = 0; d < 2; d++) {
        for (uint8_t i = 0; i < DECK; i++) st.deck[d][i] = i;
        shuffle(st.deck[d], DECK);
    }
    qHead = qCount = nDue = 0;
    special = false;
    push(EV_START, 1);
    // The deal: DEAL2 deeds each for two players, two each for more.
    uint8_t deeds[28], n = 0;
    for (uint8_t t = 0; t < TILES; t++) if (isDeed(t)) deeds[n++] = t;
    shuffle(deeds, n);
    const uint8_t deedCount = n;
    n = 0;
    for (uint8_t i = s.deal ? s.deal : st.players == 2 ? DEAL2 : 2; i; i--)
        for (uint8_t p = 0; p < st.players && n < deedCount; p++) {
            st.deed[deeds[n]] = p;
            push(EV_DEAL, p, deeds[n++]);
        }
    ph = P_TURN;
}

void update(bool stageBusy) {
    if (ph == P_AUCTION) { auctionTick(stageBusy); return; }
    if (stageBusy || qCount) return;
    switch (ph) {
        case P_TURN:    beginTurn(); break;
        case P_PREROLL: if (isCpu(st.cur)) cpuPreroll(); break;
        case P_MOVE:    moveBy(steps, true); break;
        case P_RESOLVE: resolve(); break;
        case P_OFFER:   if (isCpu(st.cur)) cpuOffer(); break;
        case P_CARD:    applyCard(); break;
        case P_SETTLE:  settle(); break;
        case P_ENDTURN: endTurn(); break;
    }
}

const State &checkpoint() { return mark; }

bool restore(const State &s) {
    if (s.players < 2 || s.players > SEATS || s.cur >= s.players || s.over) return false;
    st = s;
    qHead = qCount = nDue = 0;
    special = false;
    push(EV_START);
    ph = P_TURN;
    return true;
}

}  // namespace game
