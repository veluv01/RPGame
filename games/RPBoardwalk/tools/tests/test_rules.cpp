// Host tests for the rules (Tiles, Game, Cpu): each rule on its own, then thousands
// of seeded games with every seat a CPU or a "human" pressing buttons at
// random, checking the books balance and every game ends.
//
//   rpgame test            tests + the game-length table
//   rpgame test quick      fewer fuzzed games
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include "../../Game.h"
#include "../../Cpu.h"

using namespace game;
using namespace board;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); failures++; } } while (0)

// ---------------------------------------------------------------------------
// Driving a game without a stage
// ---------------------------------------------------------------------------
static std::vector<Event> evlog;
static long long book[SEATS];           // each player's cash by the PAY events alone

static void drain() {
    Event e;
    while (popEvent(e)) {
        evlog.push_back(e);
        if (e.type == EV_START) for (int p = 0; p < SEATS; p++) book[p] = st.pl[p].cash;
        if (e.type == EV_PAY) {
            if (e.a != BANK) book[e.a] -= e.amount;
            if (e.b != BANK) book[e.b] += e.amount;
        }
    }
}

static const Event *last(uint8_t type) {
    for (size_t i = evlog.size(); i--;) if (evlog[i].type == type) return &evlog[i];
    return nullptr;
}
static int count(uint8_t type) {
    int n = 0;
    for (auto &e : evlog) n += e.type == type;
    return n;
}

// Run until the game wants a human, an auction is on, or it is over.
static void run(int maxTicks = 100000) {
    for (int i = 0; i < maxTicks; i++) {
        drain();
        if (phase() == P_OVER || phase() == P_AUCTION || humanToAct()) return;
        update(false);
    }
    CHECK(!"run: stalled");
}

// Let an auction run out with no human taps.
static void runAuction() {
    for (int i = 0; i < 100000 && phase() == P_AUCTION; i++) { update(false); drain(); }
    run();
}

// A two-human game with nothing dealt: a clean board to set up on.
static void fresh(uint8_t players = 2, uint8_t kind = HUMAN, uint8_t cap = 0) {
    Setup s = {{kind, kind, (uint8_t)(players > 2 ? kind : OFF), (uint8_t)(players > 3 ? kind : OFF)}, cap, 12345, 0};
    start(s);
    evlog.clear();
    run();
    memset(st.deed, BANK, sizeof st.deed);
    evlog.clear();
}

// After a test has put cash in a pocket by hand.
static void cashIs(uint8_t p, int cash) { book[p] = st.pl[p].cash = cash; }

static void give(uint8_t t, uint8_t p, uint8_t lvl = 0) { st.deed[t] = (uint8_t)(p | lvl << 3); }

// The player at the turn rolls d1 + d2 and everything automatic plays out.
static void throwDice(uint8_t d1, uint8_t d2) {
    forceDice(d1, d2);
    CHECK(roll());
    run();
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------
static void testTables() {
    int deeds = 0, streets = 0;
    for (uint8_t t = 0; t < TILES; t++) { deeds += isDeed(t); streets += type(t) == STREET; }
    CHECK_EQ(deeds, 28);
    CHECK_EQ(streets, 22);
    uint8_t tiles[4];
    static const uint8_t SIZE[GROUPS] = {2, 3, 3, 3, 3, 3, 3, 2, 4, 2};
    for (uint8_t g = 0; g < GROUPS; g++) CHECK_EQ(groupTiles(g, tiles), SIZE[g]);
    CHECK_EQ(price(39), 400);
    CHECK_EQ(houseCost(1), 50);
    CHECK_EQ(houseCost(39), 200);
    CHECK_EQ(RENT[TILE[39].aux][5], 2000);
    CHECK_EQ(sizeof(State), 120);
    for (int d = 0; d < 2; d++) {
        int jail = 0;
        for (auto &c : CARD[d]) jail += c.kind == GOOJF;
        CHECK_EQ(jail, 1);
    }
}

static void testDeal() {
    for (uint8_t n = 2; n <= 4; n++) {
        Setup s = {{HUMAN, HUMAN, (uint8_t)(n > 2 ? HUMAN : OFF), (uint8_t)(n > 3 ? HUMAN : OFF)}, 30, 99, 0};
        start(s);
        evlog.clear();
        run();
        CHECK_EQ(st.players, n);
        CHECK_EQ(count(EV_DEAL), n == 2 ? 8 : 2 * n);
        int owned[SEATS] = {0, 0, 0, 0};
        for (uint8_t t = 0; t < TILES; t++) if (owner(t) != BANK) { CHECK(isDeed(t)); owned[owner(t)]++; }
        for (uint8_t p = 0; p < n; p++) { CHECK_EQ(owned[p], n == 2 ? 4 : 2); CHECK_EQ(st.pl[p].cash, 1500); }
        CHECK(humanToAct());
        CHECK_EQ(st.cur, 0);
    }
}

static void testMoveAndGo() {
    fresh();
    throwDice(1, 2);                         // to Baltic (unowned): an offer
    CHECK_EQ(st.pl[0].pos, 3);
    CHECK_EQ(phase(), P_OFFER);
    CHECK(buy());
    run();
    CHECK_EQ(owner(3), 0);
    CHECK_EQ(st.pl[0].cash, 1440);
    CHECK_EQ(st.cur, 1);
    // Passing GO pays $200.
    st.pl[1].pos = 38;
    throwDice(1, 3);                         // to Community Chest (2)... make it a plain payout
    CHECK_EQ(st.pl[1].pos != 38, true);
    const Event *go = nullptr;
    for (auto &e : evlog) if (e.type == EV_PAY && e.c == R_GO) go = &e;
    CHECK(go && go->amount == 200 && go->b == 1);
    // Landing on GO with the dice: payday, $400 (a card to GO pays $200, testCards).
    run();
    fresh();
    st.pl[0].pos = 36;
    evlog.clear();
    throwDice(1, 3);
    CHECK_EQ(st.pl[0].pos, 0);
    CHECK(last(EV_PAY) && last(EV_PAY)->c == R_GO && last(EV_PAY)->amount == 400);
}

static void testRent() {
    fresh();
    give(1, 1);                              // Mediterranean, P2's
    throwDice(0, 1);                         // P1 lands on it
    CHECK_EQ(st.pl[0].cash, 1498);
    CHECK_EQ(st.pl[1].cash, 1502);
    // The full set doubles it; houses take over.
    give(3, 1);
    CHECK_EQ(rent(1, 0), 4);
    CHECK_EQ(rent(3, 0), 8);
    give(1, 1, 1);
    CHECK_EQ(rent(1, 0), 10);
    give(39, 1, 5);
    CHECK_EQ(rent(39, 0), 2000);
    // Railroads by how many; utilities by the roll.
    give(5, 1);
    CHECK_EQ(rent(5, 0), 25);
    give(15, 1); give(25, 1);
    CHECK_EQ(rent(5, 0), 100);
    give(35, 1);
    CHECK_EQ(rent(35, 0), 200);
    give(12, 1);
    CHECK_EQ(rent(12, 7), 28);
    give(28, 1);
    CHECK_EQ(rent(12, 7), 70);
    // Your own deed costs nothing.
    fresh();
    give(3, 0);
    throwDice(1, 2);
    CHECK_EQ(st.pl[0].cash, 1500);
    CHECK_EQ(st.cur, 1);
}

static void testTax() {
    fresh();
    throwDice(1, 3);                         // Income Tax
    CHECK_EQ(st.pl[0].cash, 1300);
    st.pl[1].pos = 35;
    throwDice(1, 2);                         // Luxury Tax
    CHECK_EQ(st.pl[1].cash, 1400);
    // Taxes, fines and bail pile up on Free Parking, for whoever lands there.
    CHECK_EQ(st.pot, 300);
    st.pl[0].pos = JAIL_TILE; st.pl[0].jail = 1;
    CHECK(payJail());
    CHECK_EQ(st.pot, 350);
    throwDice(4, 6);                         // from Jail to Free Parking
    CHECK_EQ(st.pl[0].cash, 1300 - 50 + 350);
    CHECK_EQ(st.pot, 0);
    CHECK_EQ(last(EV_LAND)->amount, 350);
}

static void testDoublesAndJail() {
    fresh();
    give(4 + 0, BANK);
    // Doubles: the same player again; three running: jail.
    st.pl[0].pos = 10;                       // from Just Visiting: 2+2 -> 14 (Virginia), offer
    throwDice(2, 2);
    CHECK_EQ(phase(), P_OFFER);
    CHECK(buy());
    run();
    CHECK_EQ(st.cur, 0);
    CHECK_EQ(st.doubles, 1);
    st.pl[0].pos = 0;
    throwDice(5, 5);                         // Just Visiting
    CHECK_EQ(st.cur, 0);
    throwDice(1, 1);                         // the third: jail, without moving
    CHECK_EQ(st.pl[0].pos, JAIL_TILE);
    CHECK_EQ(st.pl[0].jail, 1);
    CHECK_EQ(st.cur, 1);
    CHECK(last(EV_JAIL) != nullptr);
    // Go To Jail.
    st.pl[1].pos = 28;
    throwDice(1, 1);
    CHECK_EQ(st.pl[1].pos, JAIL_TILE);
    CHECK_EQ(st.pl[1].jail, 1);
    CHECK_EQ(st.cur, 0);                     // doubles do not roll again from jail
    // In jail: a miss stays put.
    throwDice(1, 2);
    CHECK_EQ(st.pl[0].pos, JAIL_TILE);
    CHECK_EQ(st.pl[0].jail, 2);
    CHECK_EQ(st.cur, 1);
    // Doubles get out and move, with no second roll.
    throwDice(3, 3);                         // 16: St. James
    CHECK_EQ(st.pl[1].jail, 0);
    CHECK_EQ(st.pl[1].pos, 16);
    CHECK(buy());
    run();
    CHECK_EQ(st.cur, 0);
    // The second miss, then the third pays $50 and moves.
    int cash = st.pl[0].cash;
    throwDice(1, 2);
    CHECK_EQ(st.pl[0].jail, 3);
    st.pl[1].pos = 10; throwDice(4, 6);      // P2 somewhere harmless (Free Parking)
    throwDice(1, 3);                         // 14: P1's own Virginia
    CHECK_EQ(st.pl[0].jail, 0);
    CHECK_EQ(st.pl[0].pos, 14);
    CHECK_EQ(st.pl[0].cash, cash - 50);
    // Paying first, then rolling as usual.
    fresh();
    st.pl[0].pos = JAIL_TILE; st.pl[0].jail = 1;
    CHECK(payJail());
    CHECK_EQ(st.pl[0].cash, 1450);
    CHECK(!payJail());
    throwDice(4, 6);                         // Free Parking
    CHECK_EQ(st.pl[0].pos, 20);
    // The card.
    st.pl[1].pos = JAIL_TILE; st.pl[1].jail = 1;
    CHECK(!useJailCard());
    st.pl[1].flags |= F_CARD << 1;
    CHECK(useJailCard());
    CHECK_EQ(st.pl[1].jail, 0);
    CHECK_EQ(st.pl[1].flags, 0);
}

static void testBuild() {
    fresh();
    give(6, 0);
    CHECK(!canBuild(6));                     // one of the three: not yet
    give(8, 0);
    CHECK(canBuild(6));                      // most of the group
    CHECK(build(6));
    CHECK_EQ(st.pl[0].cash, 1450);
    CHECK(!canBuild(6));                     // evenly, over what you hold
    CHECK(build(8));
    CHECK(!canBuild(9));                     // not yours
    give(9, 0);                              // the third joins them, bare
    CHECK(canBuild(9) && !canBuild(6));
    CHECK(build(9));
    CHECK(canBuild(6));
    CHECK(!canOffer(8));                     // not with buildings on the group
    CHECK(!canSell(1));
    CHECK(sell(9));
    CHECK_EQ(st.pl[0].cash, 1375);
    CHECK(!canSell(9));
    CHECK(canBuild(9) && !canBuild(6));
    for (int i = 0; i < 20; i++) for (uint8_t t : {6, 8, 9}) build(t);
    CHECK_EQ(level(6), 5); CHECK_EQ(level(8), 5); CHECK_EQ(level(9), 5);
    CHECK(!canBuild(6));
    CHECK_EQ(netWorth(0), st.pl[0].cash + 320 + 15 * 50);
    st.pl[0].cash = 40;
    give(9, 0, 4);
    CHECK(!canBuild(9));                     // cannot afford it
    CHECK(!build(1));                        // not yours
    cashIs(0, 1000);
    give(37, 0);
    CHECK(!canBuild(37));                    // a pair takes both
    give(39, 1);
    CHECK(!majority(0, 7) && !majority(1, 7));
    give(39, 0);
    CHECK(canBuild(39));
}

static void testAuction() {
    fresh(3);
    // Declined: nobody taps, it goes free to the next seat.
    throwDice(1, 2);
    CHECK(decline());
    run();
    CHECK_EQ(phase(), P_AUCTION);
    CHECK(!bid(1));                          // not until the stage has opened the lot
    update(false);
    CHECK(auc.started);
    runAuction();
    CHECK_EQ(owner(3), 1);
    CHECK_EQ(st.pl[1].cash, 1500);
    CHECK_EQ(last(EV_SOLD)->amount, 0);
    CHECK_EQ(st.cur, 1);
    // Taps: the first leads at $0, each one after raises a step.
    st.pl[1].pos = 36;
    throwDice(1, 2);                         // Boardwalk, $400: steps of $40
    CHECK(decline());
    run(); update(false);
    CHECK_EQ(auc.step, 40);
    CHECK(bid(1));                           // the lander may bid
    CHECK_EQ(auc.price, 0);
    CHECK(!bid(1));                          // not against yourself
    CHECK(bid(2));
    CHECK_EQ(auc.price, 40);
    CHECK(bid(0)); CHECK(bid(2));
    CHECK_EQ(auc.price, 120);
    CHECK_EQ(auc.leader, 2);
    st.pl[0].cash = 150;
    CHECK(!bid(0));                          // $160 is more than they have
    CHECK_EQ(auc.timer, AUCTION_WINDOW);     // P2 could still answer
    runAuction();
    CHECK_EQ(owner(39), 2);
    CHECK_EQ(st.pl[2].cash, 1500 - 120);
    // Offering your own deed: the others bid, the money comes to you.
    CHECK_EQ(st.cur, 2);
    CHECK(canOffer(39));
    CHECK(!canOffer(3));
    CHECK(offer(39));
    run(); update(false);
    CHECK_EQ(auc.leader, BANK);              // the bank opens at half its price
    CHECK_EQ(auc.price, 200);
    CHECK(!bid(2));                          // the seller sits it out
    CHECK(bid(1));
    CHECK(!bid(0));                          // $280 is more than they have
    CHECK_EQ(auc.price, 240);
    runAuction();
    CHECK_EQ(owner(39), 1);
    CHECK_EQ(st.pl[2].cash, 1500 - 120 + 240);
    CHECK_EQ(st.pl[1].cash, 1500 - 240);
    CHECK_EQ(st.cur, 2);                     // still their turn, before the roll
    CHECK_EQ(phase(), P_PREROLL);
    // Nobody goes higher: the bank keeps it, at half.
    give(1, 2);
    CHECK(offer(1));
    run();
    runAuction();
    CHECK_EQ(owner(1), BANK);
    CHECK_EQ(st.pl[2].cash, 1500 - 120 + 240 + 30);
    // Not enough to buy: straight to auction.
    fresh();
    st.pl[0].cash = 30;
    throwDice(1, 2);
    CHECK_EQ(phase(), P_AUCTION);
    update(false);
    CHECK(bid(0)); CHECK(bid(1)); CHECK(bid(0));
    CHECK_EQ(auc.timer, AUCTION_WINDOW);
    CHECK(bid(1));
    CHECK_EQ(auc.price, 30);                 // $60 list: steps of $10
    CHECK(!bid(0));                          // $40 is more than they have
    CHECK_EQ(auc.timer, AUCTION_LAST);       // ... so nobody can answer: the gavel comes early
    runAuction();
    CHECK_EQ(owner(3), 1);
}

static void testCpuBids() {
    // Two CPUs and a human who declines: the CPUs bid each other up, never
    // past what they have, and the lot always sells.
    Setup s = {{HUMAN, CPU + 1, CPU + 2, OFF}, 0, 7, 0};
    start(s);
    evlog.clear();
    run();
    memset(st.deed, BANK, sizeof st.deed);
    throwDice(3, 3);                         // Oriental, $100
    CHECK(decline());
    evlog.clear();
    run();
    runAuction();
    CHECK(owner(6) == 1 || owner(6) == 2);
    CHECK(count(EV_BID) >= 2);
    CHECK(last(EV_SOLD)->amount > 0);
    CHECK(last(EV_SOLD)->amount <= 130);
}

static void testDebt() {
    // Rent they cannot pay: houses go at half, then deeds to auction; with
    // nothing left, bankrupt, and the game is over.
    fresh();
    give(39, 1, 5); give(37, 1, 5);          // Boardwalk with a hotel: $2000
    give(6, 0, 2); give(8, 0, 2); give(9, 0, 2);
    give(5, 0);
    cashIs(0, 100);
    st.pl[0].pos = 36;
    forceDice(1, 2);
    CHECK(roll());
    run();
    // Six houses went back first ($25 each).
    CHECK_EQ(count(EV_SELL), 6);
    CHECK_EQ(phase(), P_AUCTION);            // then the stray railroad, before the set
    CHECK_EQ(auc.tile, 5);
    CHECK_EQ(auc.seller, 0);
    update(false);
    CHECK(!bid(0));
    CHECK(bid(1));                           // $120, over the bank's $100
    runAuction();
    CHECK_EQ(owner(5), 1);
    for (int i = 0; i < 3; i++) { CHECK_EQ(phase(), P_AUCTION); runAuction(); }
    CHECK_EQ(owner(6), BANK);                // the set went to the bank at half price
    CHECK_EQ(phase(), P_OVER);
    CHECK(st.pl[0].flags & F_BUST);
    CHECK_EQ(st.pl[0].cash, 0);
    CHECK_EQ(st.winner, 1);
    CHECK_EQ(st.over, BY_BANKRUPTCY + 1);
    // What there was: the cash, the houses, the railroad and the set.
    CHECK_EQ(st.pl[1].cash, 1500 - 120 + 100 + 150 + 120 + 160);
    for (int p = 0; p < 2; p++) CHECK_EQ(book[p], st.pl[p].cash);
    // A debt covered by a house: play goes on.
    fresh();
    give(39, 1);
    give(1, 0, 1); give(3, 0, 1);
    cashIs(0, 30);
    st.pl[0].pos = 36;
    throwDice(1, 2);                         // $50 rent
    CHECK_EQ(count(EV_SELL), 1);
    CHECK_EQ(st.pl[0].cash, 30 + 25 - 50);
    CHECK_EQ(st.cur, 1);
    CHECK_EQ(phase(), P_PREROLL);
}

// Put a card on top of its deck and land the player at the turn on it.
static void drawCard(uint8_t deck, uint8_t kind, uint8_t arg, uint8_t which = 0) {
    uint8_t idx = 0xFF;
    for (uint8_t i = 0; i < DECK; i++)
        if (CARD[deck][i].kind == kind && CARD[deck][i].arg == arg && !which--) { idx = i; break; }
    CHECK(idx != 0xFF);
    st.deck[deck][st.top[deck]] = idx;
    st.pl[st.cur].pos = deck == CHANCE_DECK ? 5 : 0;   // Chance at 7, Chest at 2
    st.doubles = 0;                          // (these doubles never add up to three)
    throwDice(1, 1);
}

static void testCards() {
    fresh(3);
    drawCard(CHANCE_DECK, MOVE_TO, 39);
    CHECK_EQ(st.pl[0].pos, 39);
    CHECK_EQ(phase(), P_OFFER);
    CHECK(decline()); run(); runAuction();
    CHECK_EQ(st.cur, 0);                     // doubles: again
    drawCard(CHANCE_DECK, MOVE_TO, 0);       // Advance to GO: collect
    CHECK_EQ(st.pl[0].pos, 0);
    CHECK_EQ(st.pl[0].cash, 1700);
    drawCard(CHANCE_DECK, MOVE_TO, 5);       // past GO to Reading: +$200, and the deed on offer
    CHECK_EQ(st.pl[0].pos, 5);
    CHECK_EQ(st.pl[0].cash, 1900);
    CHECK_EQ(phase(), P_OFFER);

    fresh(3);
    drawCard(CHANCE_DECK, BACK3, 0);         // from Chance (7) to Income Tax (4)
    CHECK_EQ(st.pl[0].pos, 4);
    CHECK_EQ(st.pl[0].cash, 1300);
    drawCard(CHANCE_DECK, COLLECT, 10);
    CHECK_EQ(st.pl[0].cash, 1350);
    drawCard(CHANCE_DECK, PAY, 3);
    CHECK_EQ(st.pl[0].cash, 1335);
    CHECK_EQ(st.cur, 0);
    fresh(3);
    drawCard(CHANCE_DECK, PAY_EACH, 10);
    CHECK_EQ(st.pl[0].cash, 1400);
    CHECK_EQ(st.pl[1].cash, 1550);
    CHECK_EQ(st.pl[2].cash, 1550);
    drawCard(CHEST_DECK, COLLECT_EACH, 2);
    CHECK_EQ(st.pl[0].cash, 1420);
    CHECK_EQ(st.pl[1].cash, 1540);
    fresh(3);
    give(6, 0, 3); give(8, 0, 3); give(9, 0, 5);
    drawCard(CHANCE_DECK, REPAIRS, 0);       // 6 houses, a hotel
    CHECK_EQ(st.pl[0].cash, 1500 - 6 * 25 - 100);
    drawCard(CHEST_DECK, REPAIRS, 1);
    CHECK_EQ(st.pl[0].cash, 1250 - 6 * 40 - 115);
    fresh(3);
    drawCard(CHANCE_DECK, GO_JAIL, 0);
    CHECK_EQ(st.pl[0].pos, JAIL_TILE);
    CHECK_EQ(st.pl[0].jail, 1);
    CHECK_EQ(st.cur, 1);
    // The get-out-of-jail card is held, and out of the deck meanwhile.
    drawCard(CHANCE_DECK, GOOJF, 0);
    CHECK(st.pl[1].flags & F_CARD);
    uint8_t held = st.deck[CHANCE_DECK][(st.top[CHANCE_DECK] + DECK - 1) % DECK];
    st.deck[CHANCE_DECK][st.top[CHANCE_DECK]] = held;
    uint8_t after = st.deck[CHANCE_DECK][(st.top[CHANCE_DECK] + 1) % DECK];
    st.pl[1].pos = 5;
    evlog.clear();
    forceDice(1, 1); CHECK(roll());
    for (int i = 0; i < 50 && !last(EV_CARD); i++) { update(false); drain(); }
    CHECK(last(EV_CARD) && last(EV_CARD)->b == after);
    // Nearest railroad: double rent. Nearest utility: ten times the roll.
    fresh(3);
    give(15, 1); give(25, 1);
    drawCard(CHANCE_DECK, NEAR_RAIL, 0);     // from 7: Pennsylvania R.R. (15)
    CHECK_EQ(st.pl[0].pos, 15);
    CHECK_EQ(st.pl[0].cash, 1400);
    CHECK_EQ(rent(15, 0), 50);               // the doubling was the card's, once
    fresh(3);
    give(12, 1);
    drawCard(CHANCE_DECK, NEAR_UTIL, 0);     // from 7: Electric Company (12), rolled 2
    CHECK_EQ(st.pl[0].pos, 12);
    CHECK_EQ(st.pl[0].cash, 1480);
    fresh(3);
    drawCard(CHANCE_DECK, NEAR_UTIL, 0);     // unowned: an offer
    CHECK_EQ(phase(), P_OFFER);
}

static void testClosing() {
    fresh(2, HUMAN, 2);
    give(39, 1);
    for (int turn = 0; turn < 4; turn++) {
        CHECK(active());
        st.pl[st.cur].pos = 10;
        throwDice(4, 6);                     // Free Parking
    }
    CHECK_EQ(phase(), P_OVER);
    CHECK_EQ(st.over, BY_CLOSING + 1);
    CHECK_EQ(st.winner, 1);                  // the deed tips it
    CHECK_EQ(count(EV_LASTROUND), 1);
    CHECK(!roll());
}

// ---------------------------------------------------------------------------
// Fuzz
// ---------------------------------------------------------------------------
static uint32_t fz = 1;
static uint32_t frnd() { fz ^= fz << 13; fz ^= fz >> 17; fz ^= fz << 5; return fz; }

struct GameStats { int rounds, sets, houses, auctions, freeSales, bids; long long sold, listed; bool bust; };

static void invariants() {
    for (uint8_t p = 0; p < st.players; p++) {
        CHECK(st.pl[p].cash >= 0);
        CHECK(st.pl[p].pos < TILES);
        CHECK_EQ(book[p], st.pl[p].cash);
    }
    for (uint8_t t = 0; t < TILES; t++) {
        uint8_t o = owner(t), l = level(t);
        CHECK(o == BANK || o < st.players);
        CHECK(l <= 5);
        if (!isDeed(t)) { CHECK(o == BANK && !l); continue; }
        // Houses only where their owner holds most of the group. (Levels may
        // be uneven: a bare street can join built ones.)
        if (l) CHECK(type(t) == STREET && majority(o, group(t)));
    }
}

// A whole game: CPUs play themselves, "humans" press at random.
static uint32_t playGame(const Setup &s, GameStats *gs, uint32_t stopAtTick = 0, State *snap = nullptr) {
    start(s);
    evlog.clear();
    uint32_t hash = 2166136261u, tick = 0;
    size_t seen = 0;
    for (; tick < 4000000; tick++) {
        drain();
        for (; seen < evlog.size(); seen++) {
            const Event &e = evlog[seen];
            for (uint32_t v : {(uint32_t)e.type, (uint32_t)e.a, (uint32_t)e.b, (uint32_t)e.c, (uint32_t)(uint16_t)e.amount})
                hash = (hash ^ v) * 16777619u;
            if (gs && e.type == EV_SOLD) {
                gs->auctions++; gs->sold += e.amount; gs->listed += price(e.b);
                gs->freeSales += !e.amount;
            }
            if (gs && e.type == EV_BID) gs->bids++;
            if (e.type == EV_TURN && !e.c && snap && tick >= stopAtTick) { *snap = checkpoint(); return hash; }
        }
        if (evlog.size() > 4096) { evlog.clear(); seen = 0; }
        if ((tick & 63) == 0 || phase() == P_OVER) invariants();
        if (phase() == P_OVER) break;
        if (phase() == P_AUCTION) {
            for (uint8_t p = 0; p < st.players; p++)
                if (isHuman(p) && frnd() % 90 == 0 && frnd() % 3) bid(p);
        } else if (humanToAct()) {
            uint8_t t = (uint8_t)(frnd() % TILES);
            if (phase() == P_OFFER) { if (frnd() % 4) buy(); else decline(); }
            else switch (frnd() % 12) {
                case 0: build(t); break;
                case 1: sell(t); break;
                case 2:
                    if (frnd() % 6 == 0) offer(t);
                    break;
                case 3: payJail(); break;
                case 4: useJailCard(); break;
                case 5: for (uint8_t k = 0; k < TILES; k++) { build(k); drain(); } break;
                default: roll(); break;
            }
        }
        update(false);
    }
    CHECK(phase() == P_OVER);
    if (gs) {
        gs->rounds = st.round;
        gs->bust = st.over == BY_BANKRUPTCY + 1;
        for (uint8_t g = 0; g < 8; g++) {
            uint8_t tiles[4];
            groupTiles(g, tiles);
            for (uint8_t p = 0; p < st.players; p++) gs->sets += majority(p, g);
        }
        for (uint8_t t = 0; t < TILES; t++) gs->houses += level(t);
    }
    return hash;
}

static void testFuzz(int games) {
    // Every mix of seats; the books balance and every game ends.
    int before = failures;
    for (int g = 0; g < games; g++) {
        Setup s = {};
        s.seed = (uint32_t)(g * 2654435761u + 1);
        s.roundCap = (uint8_t)(10 + 10 * (g % 6));
        fz = (uint32_t)(g + 1);
        uint8_t n = (uint8_t)(2 + g % 3);
        for (uint8_t k = 0; k < SEATS; k++)
            s.kind[k] = k < n ? (uint8_t)((frnd() % 3) ? CPU + frnd() % LEVELS : HUMAN) : (uint8_t)OFF;
        playGame(s, nullptr);
        CHECK(st.round <= s.roundCap);
        CHECK(st.winner < st.players && !(st.pl[st.winner].flags & F_BUST));
        if (failures != before) { printf("  (game %d)\n", g); break; }
    }
    // The same seed and the same presses: the same game.
    Setup s = {{HUMAN, CPU + 2, CPU, HUMAN}, 30, 4242, 0};
    fz = 77; uint32_t a = playGame(s, nullptr);
    fz = 77; uint32_t b = playGame(s, nullptr);
    CHECK_EQ(a, b);
    // A saved game: restored at the start of a turn, the all-CPU game plays
    // out exactly as it would have.
    Setup c = {{CPU + 1, CPU + 2, CPU, OFF}, 40, 9001, 0};
    playGame(c, nullptr);
    State end1 = st;
    State snap;
    memset(&snap, 0, sizeof snap);
    playGame(c, nullptr, 400, &snap);
    CHECK(snap.round > 1 && snap.round < end1.round);
    CHECK(restore(snap));
    evlog.clear();
    for (int i = 0; i < 4000000 && phase() != P_OVER; i++) { drain(); update(false); }
    drain();
    CHECK(memcmp(&end1, &st, sizeof st) == 0);
}

static int dealOverride;

// How games go, by seats and closing time: what the CPU numbers are tuned on.
static void table(int games) {
    printf("\nAll-CPU games (levels mixed), %d per row\n", games);
    printf("seats cap | bust%%  rounds(avg)  groups  houses | auctions  free%%  sold/list%%  bids/auction\n");
    for (uint8_t n = 2; n <= 4; n++)
        for (uint8_t cap : {10, 20, 30, 40}) {
            GameStats t = {};
            int busts = 0;
            for (int g = 0; g < games; g++) {
                Setup s = {{0, 0, 0, 0}, cap, (uint32_t)(g * 7919u + n * 131u + cap), (uint8_t)dealOverride};
                for (uint8_t k = 0; k < n; k++) s.kind[k] = (uint8_t)(CPU + (g + k) % LEVELS);
                GameStats gs = {};
                playGame(s, &gs);
                busts += gs.bust;
                t.rounds += gs.rounds; t.sets += gs.sets; t.houses += gs.houses; t.auctions += gs.auctions;
                t.freeSales += gs.freeSales; t.sold += gs.sold; t.listed += gs.listed; t.bids += gs.bids;
            }
            printf("  %d   %2d  |  %3d     %5.1f     %4.1f  %5.1f  |  %5.1f    %3d      %3d        %4.1f\n", n, cap,
                   busts * 100 / games, (double)t.rounds / games, (double)t.sets / games, (double)t.houses / games,
                   (double)t.auctions / games, t.auctions ? t.freeSales * 100 / t.auctions : 0,
                   t.listed ? (int)(t.sold * 100 / t.listed) : 0, t.auctions ? (double)t.bids / t.auctions : 0.0);
        }
}

int main(int argc, char **argv) {
    bool quick = argc > 1 && !strcmp(argv[1], "quick");
    if (argc > 2) dealOverride = atoi(argv[2]);         // tuning: deeds dealt to each player
    testTables();
    testDeal();
    testMoveAndGo();
    testRent();
    testTax();
    testDoublesAndJail();
    testBuild();
    testAuction();
    testCpuBids();
    testDebt();
    testCards();
    testClosing();
    testFuzz(quick ? 300 : 5000);
    if (!failures) table(quick ? 100 : 600);
    printf(failures ? "\n%d FAILED\n" : "\nall tests passed\n", failures);
    return failures != 0;
}
