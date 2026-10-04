// Table rules and CPU players: hand-built betting spots, pots, stud's
// order, the draw heuristic, the CPU's equity, and a fuzz that plays
// thousands of hands of every game at every table with random input.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../../Table.h"
#include "../../Hand.h"
#include "../../Ai.h"
#include <rpgame/Input.h>          // button masks (the RPGame library)

void testCheck(bool ok, const char *what);
#define CHECKT(cond, ...) do { char _b[200]; snprintf(_b, sizeof _b, __VA_ARGS__); testCheck((cond), _b); } while (0)

static Table T;

static uint8_t card(const char *s) {
    static const char R[] = "23456789TJQKA", S[] = "cdhs";
    return makeCard((uint8_t)(strchr(R, s[0]) - R), (uint8_t)(strchr(S, s[1]) - S));
}
static uint8_t cards(const char *s, uint8_t *out) {
    uint8_t n = 0;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        out[n++] = card(s);
        s += 2;
    }
    return n;
}

// A bare betting spot: four live seats with these stacks, nothing bet.
static void spot(uint8_t game, uint8_t level, int32_t stack) {
    memset(&T, 0, sizeof T);
    T.game = game; T.level = level;
    T.phase = Phase::Betting;
    for (uint8_t s = 0; s < SEATS; s++) {
        T.seats[s].stack = stack;
        T.seats[s].state = S_LIVE;
        T.seats[s].mayRaise = 1;
    }
    T.lastRaise = T.bb();
}
static void blind(uint8_t s, int32_t amount) {
    T.seats[s].stack -= amount; T.seats[s].bet += amount; T.seats[s].put += amount;
    if (amount > T.curBet) T.curBet = amount;
}

static void testBetting() {
    // No limit, blinds 1/2 (seat 1 SB, seat 2 BB): a raise is to 4 at least.
    spot(HOLDEM, ROOKIE, 200);
    blind(1, 1); blind(2, 2);
    CHECKT(T.minTo(3) == 4, "NL min raise preflop %d", T.minTo(3));
    CHECKT(T.maxTo(3) == 200, "NL max is all in");
    T.act(3, A_RAISE, 6);                 // raise of 4
    CHECKT(T.minTo(0) == 10, "NL min re-raise %d", T.minTo(0));
    // Short all-in: seat 0 has 13 behind 0 - shove to 13 (< full 10? no, full is 10): make it 8.
    spot(HOLDEM, ROOKIE, 200);
    T.seats[0].stack = 14;
    T.act(1, A_RAISE, 10);                // bet 10
    T.act(2, A_CALL, 0);
    T.act(0, A_RAISE, 14);                // all in for 14: not a full raise (needs 20)
    CHECKT(T.seats[0].state == S_ALLIN && T.curBet == 14, "short all-in");
    CHECKT(!T.canRaise(1) && !T.canRaise(2), "a short all-in does not re-open the betting");
    CHECKT(T.canRaise(3), "who has not acted may still raise");
    CHECKT(T.minTo(3) == 24, "min raise over a short all-in %d", T.minTo(3));
    CHECKT(T.toCall(1) == 4, "calling the extra %d", T.toCall(1));

    // Pot limit: blinds 1/2, first to act may raise to 7.
    spot(OMAHA, ROOKIE, 500);
    blind(1, 1); blind(2, 2);
    CHECKT(T.maxTo(3) == 7, "PL max preflop %d", T.maxTo(3));
    // A pot of 20, facing a bet of 10: raise to 50.
    spot(OMAHA, ROOKIE, 500);
    T.pot = 20;
    T.act(1, A_RAISE, 10);
    CHECKT(T.maxTo(2) == 50, "PL max facing a bet %d", T.maxTo(2));
    CHECKT(T.minTo(2) == 20, "PL min raise %d", T.minTo(2));

    // Fixed limit: $2 then $4 bets, capped at four.
    spot(DRAW, ROOKIE, 500);
    T.street = 0;
    blind(1, 1); blind(2, 2);
    T.raises = 1;
    CHECKT(T.minTo(3) == 4 && T.maxTo(3) == 4, "FL raise preflop %d %d", T.minTo(3), T.maxTo(3));
    T.act(3, A_RAISE, 4);
    T.act(0, A_RAISE, 6);
    T.act(1, A_RAISE, 8);
    CHECKT(T.raises == 4 && !T.canRaise(2) && !T.canRaise(3), "FL cap at four bets (raises=%u)", T.raises);
    T.street = 1;                          // after the draw: big bets
    T.curBet = 0; T.raises = 0;
    CHECKT(T.betSize() == 4 && T.minTo(0) == 4, "FL big bet");
    // Stud: the bring-in ($1) is completed to $2.
    spot(STUD, ROOKIE, 500);
    blind(0, 1);
    CHECKT(T.minTo(1) == 2 && T.maxTo(1) == 2, "stud completion %d", T.minTo(1));
    T.act(1, A_RAISE, 2);
    CHECKT(T.seats[1].last == A_COMPLETE && T.raises == 1 && T.minTo(2) == 4, "stud raise after completion");
}

static void testBringInAndOrder() {
    spot(STUD, ROOKIE, 500);
    const char *H[4] = {"Ah Kh 2d", "3c 4c 2c", "5d 6d 9s", "7h 8h Qc"};
    for (uint8_t s = 0; s < 4; s++) { T.seats[s].n = cards(H[s], T.seats[s].cards); T.seats[s].up = 4; }
    CHECKT(T.bringInSeat() == 1, "2c brings in before 2d (%u)", T.bringInSeat());
    // Fourth street: a pair showing acts before ace-king.
    const char *U[4] = {"Ah Kh 2d Kd", "3c 4c 2c 2h", "5d 6d 9s 9d", "7h 8h Qc Ac"};
    for (uint8_t s = 0; s < 4; s++) { T.seats[s].n = cards(U[s], T.seats[s].cards); T.seats[s].up = 4 | 8; }
    CHECKT(hand::cat(T.showing(2)) == hand::PAIR && T.showing(2) > T.showing(1) && T.showing(2) > T.showing(3),
           "9s showing beat 2s and A-high");
}

static void testPots() {
    // Three all-ins and a folded contributor.
    spot(HOLDEM, ROOKIE, 0);
    int32_t put[4] = {50, 100, 200, 30};
    uint8_t st[4] = {S_ALLIN, S_ALLIN, S_LIVE, S_FOLDED};
    for (uint8_t s = 0; s < 4; s++) { T.seats[s].put = put[s]; T.seats[s].state = st[s]; }
    // Through showdown(): give everyone a hand. Seat 0 best, then 1, then 2.
    const char *H[4] = {"As Ad", "Ks Kd", "Qs Qd", "2c 7d"};
    for (uint8_t s = 0; s < 4; s++) T.seats[s].n = cards(H[s], T.seats[s].cards);
    T.nBoard = cards("3c 8h 9s Jd 4c", T.board);
    T.shownDown = 0xF;
    T.button = 3;
    T.pot = 380;
    T.phase = Phase::Showdown;
    std::vector<Event> evs;
    for (int i = 0; i < 400 && T.phase != Phase::HandOver; i++) {
        T.update(0, 0, false, true);
        Event e;
        while (T.popEvent(e)) evs.push_back(e);
    }
    CHECKT(T.nPots == 3, "three pots (%u)", T.nPots);
    CHECKT(T.pots[0].amount == 180 && T.pots[0].eligible == 7, "main pot %d/%x", T.pots[0].amount, T.pots[0].eligible);
    CHECKT(T.pots[1].amount == 100 && T.pots[1].eligible == 6, "side pot 1 %d", T.pots[1].amount);
    CHECKT(T.pots[2].amount == 100 && T.pots[2].eligible == 4, "side pot 2 %d", T.pots[2].amount);
    CHECKT(T.seats[0].stack == 180 && T.seats[1].stack == 100 && T.seats[2].stack == 100,
           "awards %d %d %d", T.seats[0].stack, T.seats[1].stack, T.seats[2].stack);

    // A split pot with an odd chip: first winner left of the button gets it.
    spot(HOLDEM, ROOKIE, 0);
    int32_t put2[4] = {50, 50, 1, 0};
    uint8_t st2[4] = {S_LIVE, S_LIVE, S_FOLDED, S_FOLDED};
    for (uint8_t s = 0; s < 4; s++) { T.seats[s].put = put2[s]; T.seats[s].state = st2[s]; }
    T.seats[0].n = cards("2c 3d", T.seats[0].cards);
    T.seats[1].n = cards("2h 3s", T.seats[1].cards);
    T.nBoard = cards("As Ks Qd Jh Tc", T.board);       // both play the board's straight
    T.shownDown = 3; T.button = 0; T.phase = Phase::Showdown;
    for (int i = 0; i < 400 && T.phase != Phase::HandOver; i++) { T.update(0, 0, false, true); Event e; while (T.popEvent(e)) {} }
    CHECKT(T.seats[1].stack == 51 && T.seats[0].stack == 50, "odd chip to the left of the button: %d %d",
           T.seats[0].stack, T.seats[1].stack);

    // Stud ties are rare in a random sample; exercise one deterministically.
    spot(STUD, ROOKIE, 0);
    for (uint8_t seat = 0; seat < 4; seat++) { T.seats[seat].put = put2[seat]; T.seats[seat].state = st2[seat]; }
    T.seats[0].n = cards("As Kd Qh Js Tc 2c 3d", T.seats[0].cards);
    T.seats[1].n = cards("Ah Kc Qs Jd Th 2d 3c", T.seats[1].cards);
    T.shownDown = 3; T.button = 0; T.phase = Phase::Showdown;
    for (int i = 0; i < 400 && T.phase != Phase::HandOver; i++) { T.update(0, 0, false, true); Event e; while (T.popEvent(e)) {} }
    CHECKT(T.seats[1].stack == 51 && T.seats[0].stack == 50, "stud split and odd chip: %d %d",
           T.seats[0].stack, T.seats[1].stack);

    // Uncalled bets come back.
    spot(HOLDEM, ROOKIE, 100);
    T.seats[1].stack = 40;
    T.act(0, A_RAISE, 100);
    T.act(1, A_CALL, 0);
    T.act(2, A_FOLD, 0);
    T.act(3, A_FOLD, 0);
    T.phase = Phase::EndStreet;
    T.street = 3;
    T.seats[0].n = cards("As Ad", T.seats[0].cards);
    T.seats[1].n = cards("Ks Kd", T.seats[1].cards);
    T.nBoard = cards("3c 8h 9s Jd 4c", T.board);
    T.update(0, 0, false, true);
    CHECKT(T.seats[0].stack == 60 && T.seats[0].put == 40 && T.pot == 80, "uncalled 60 returned (%d, pot %d)",
           T.seats[0].stack, T.pot);
}

static void testDrawHeuristic() {
    struct Case { const char *h; uint8_t level; uint8_t throws; const char *why; };
    static const Case C[] = {
        {"Ah Kd 7c 4s 2h", PRO, 4, "nothing: keep the ace, draw four"},
        {"Ah Kd 7c 4s 2h", ROOKIE, 3, "ROOKIE keeps a kicker with the ace"},
        {"Qh Td 7c 4s 2h", PRO, 3, "nothing: keep the two highest"},
        {"Jc Jd 7h 4s 2c", PRO, 3, "a pair: draw three"},
        {"2c 2d 9h Kh Qh", PRO, 3, "a low pair beats three to a flush"},
        {"2c 9h Th Kh Qh", PRO, 1, "four to a flush: draw one"},
        {"2h 2d 9h Kh Qh", PRO, 1, "a low pair breaks for four to a flush"},
        {"5c 6d 7h 8s Kd", PRO, 1, "open-ended straight draw: draw one"},
        {"9c 9d 9h 4s 2c", PRO, 2, "trips: draw two"},
        {"Kc Kd 4h 4s 2c", PRO, 1, "two pair: draw one"},
        {"5c 6d 7h 8s 9d", PRO, 0, "a straight stands pat"},
        {"2h 5h 9h Jh Ah", PRO, 0, "a flush stands pat"},
    };
    for (const Case &c : C) {
        uint8_t f[5];
        cards(c.h, f);
        uint8_t m = ai::discards(f, c.level), n = 0;
        for (uint8_t i = 0; i < 5; i++) n += (m >> i) & 1;
        CHECKT(n == c.throws, "%s (%s): threw %u", c.why, c.h, n);
    }
}

static uint16_t equityOf(uint8_t game, const char *own, uint8_t opp, uint16_t n) {
    ai::View v;
    memset(&v, 0, sizeof v);
    v.game = game;
    v.nOwn = cards(own, v.own);
    v.nOpp = opp;
    v.preDraw = game == DRAW;
    ai::begin(v, 12345, n);
    while (!ai::step(64)) {}
    return ai::equity();
}

static void testEquity() {
    uint16_t aa1 = equityOf(HOLDEM, "As Ah", 1, 20000), aa3 = equityOf(HOLDEM, "As Ah", 3, 20000);
    uint16_t s72 = equityOf(HOLDEM, "7c 2d", 1, 20000);
    CHECKT(aa1 > 210 && aa1 < 225, "AA vs 1: %u/256 (85%%)", aa1);
    CHECKT(aa3 > 155 && aa3 < 172, "AA vs 3: %u/256 (64%%)", aa3);
    CHECKT(s72 > 80 && s72 < 97, "72o vs 1: %u/256 (35%%)", s72);
    uint16_t om = equityOf(OMAHA, "As Ah Ks Kh", 1, 4000);
    CHECKT(om > 150 && om < 210, "Omaha AAKK double-suited vs 1: %u/256", om);
    uint16_t dr = equityOf(DRAW, "Ac Ad Ah 4s 2c", 3, 4000);
    CHECKT(dr > 128, "Draw trip aces vs 3: %u/256", dr);
    printf("equity: AA v1 %u, AA v3 %u, 72o v1 %u, Omaha AAKK %u, draw AAA v3 %u (/256)\n", aa1, aa3, s72, om, dr);
}

// ---------------------------------------------------------------------------
// Fuzz: whole sessions at every table, the human pressing buttons at random.
// ---------------------------------------------------------------------------
static uint32_t frs = 99;
static uint32_t frnd() { frs ^= frs << 13; frs ^= frs >> 17; frs ^= frs << 5; return frs; }

static int64_t money() {
    int64_t m = T.purse;
    for (uint8_t s = 0; s < SEATS; s++) m += T.seats[s].stack + T.seats[s].bet;
    return m + T.pot;
}

struct Cover { long hands, showdowns, sidePots, allins, youRaise, youFold, draws, rebuys, leaves, uncontested, splits; };
static Cover cov[GAMES];
// What the CPUs do, by table: [level][fold, check, call, bet/raise, all in], preflop/first street only.
static long acts[GAMES][LEVELS][5];
static long cpuHands[LEVELS], cpuPotBB[LEVELS];

static bool fuzz(uint8_t game, uint8_t level, int hands, bool demo) {
    Cover &cv = cov[game];
    memset(&T, 0, sizeof T);
    T.newPurse();
    T.purse = 1000000;
    T.demo = demo;
    T.opt.goal = GOAL_ENDLESS;
    T.sitDown(game, level, maxBuyIn(level), 1234 + game * 7 + level);
    { Event e; while (T.popEvent(e)) {} }
    int64_t expect = money();
    int done = 0, sinceHand = 0;
    uint8_t winners = 0, potsWon = 0;
    bool seen[52];
    memset(seen, 0, sizeof seen);
    int64_t winSum = 0;
    int32_t putSum = 0;
    while (done < hands) {
        uint8_t pressed = 0;
        static const uint8_t B[6] = {A_BUTTON, LEFT_BUTTON, RIGHT_BUTTON, UP_BUTTON, DOWN_BUTTON, 0};
        if (frnd() % 3 == 0) pressed = B[frnd() % 6];
        if (T.phase == Phase::HandOver && frnd() % 50 == 0) T.sel = N_LEAVE;
        T.update(pressed, pressed, false, true);
        if (++sinceHand > 200000) {
            CHECKT(false, "game %u level %u: stalled in phase %u", game, level, (unsigned)T.phase);
            return false;
        }
        for (uint8_t s = 0; s < SEATS; s++)
            if (T.seats[s].stack < 0 || T.seats[s].bet < 0) { CHECKT(false, "negative stack/bet"); return false; }
        Event e;
        while (T.popEvent(e)) {
            switch (e.type) {
                case Ev::Shuffle: memset(seen, 0, sizeof seen); winSum = 0; break;
                case Ev::Join: expect += e.amount; break;
                case Ev::Deal:
                    if (seen[e.c]) { CHECKT(false, "game %u: card %u dealt twice", game, e.c); return false; }
                    seen[e.c] = true;
                    break;
                case Ev::Win:
                    winSum += e.amount;
                    if (e.c == 0xFF) cv.uncontested++;
                    if (e.b == 0) winners++;
                    if (e.b > potsWon) potsWon = e.b;
                    break;
                case Ev::Action:
                    if (demo || e.a != YOU) {
                        uint8_t k = e.b == A_FOLD ? 0 : e.b == A_CHECK ? 1 : e.b == A_CALL ? 2 : e.b == A_ALLIN ? 4 : 3;
                        if (T.street == 0) acts[game][level][k]++;
                    }
                    if (e.b == A_ALLIN) cv.allins++;
                    if (e.a == YOU && !demo && (e.b == A_RAISE || e.b == A_BET)) cv.youRaise++;
                    if (e.a == YOU && !demo && e.b == A_FOLD) cv.youFold++;
                    break;
                case Ev::Discard: cv.draws++; break;
                case Ev::Rebuy: cv.rebuys++; break;
                case Ev::Reveal: break;
                case Ev::HandEnd: {
                    putSum = 0;
                    for (uint8_t s = 0; s < SEATS; s++) putSum += T.seats[s].put;
                    if (winSum != putSum) { CHECKT(false, "game %u: won %lld of a %d pot", game, (long long)winSum, putSum); return false; }
                    int64_t m = money();
                    if (m != expect) {
                        CHECKT(false, "game %u level %u hand %d: money %lld, expected %lld", game, level, done,
                               (long long)m, (long long)expect);
                        return false;
                    }
                    done++;
                    cv.hands++;
                    if (demo) { cpuHands[level]++; cpuPotBB[level] += putSum / T.bb(); }
                    if (T.nPots) cv.showdowns++;
                    if (potsWon) cv.sidePots++;
                    if (winners > 1) cv.splits++;
                    winners = potsWon = 0;
                    T.nPots = 0;
                    sinceHand = 0;
                    break;
                }
                default: break;
            }
        }
        if (T.phase == Phase::Leave || T.phase == Phase::Broke || T.phase == Phase::Won) {
            cv.leaves++;
            if (T.purse < minBuyIn(level)) T.purse += 1000000, expect += 1000000;
            T.sitDown(game, level, minBuyIn(level) + (int32_t)(frnd() % (uint32_t)(maxBuyIn(level) - minBuyIn(level))),
                      frnd());
            { Event e2; while (T.popEvent(e2)) {} }
            expect = money();
        }
    }
    return true;
}

static void testHonesty() {
    // The CPUs' choices don't depend on cards they can't see: swap two other
    // players' hidden hands before the first CPU decides, and it decides the same.
    for (uint8_t game = 0; game < GAMES; game++) {
        int agree = 0;
        for (int trial = 0; trial < 40; trial++) {
            Event first[2];
            for (int run = 0; run < 2; run++) {
                memset(&T, 0, sizeof T);
                T.purse = 100000;
                T.opt.goal = GOAL_ENDLESS;
                T.demo = true;
                T.sitDown(game, PRO, 400, 777 + trial);
                bool swapped = false;
                first[run].type = Ev::Shuffle;
                for (int i = 0; i < 20000; i++) {
                    if (!swapped && T.phase == Phase::Betting && T.actor != 0xFF) {
                        uint8_t a = (uint8_t)((T.actor + 1) & 3), b = (uint8_t)((T.actor + 2) & 3);
                        if (run == 1) {
                            // Swap only face-down cards.
                            for (uint8_t k = 0; k < 7; k++)
                                if (!((T.seats[a].up >> k) & 1) && !((T.seats[b].up >> k) & 1) && k < T.seats[a].n && k < T.seats[b].n) {
                                    uint8_t t = T.seats[a].cards[k]; T.seats[a].cards[k] = T.seats[b].cards[k]; T.seats[b].cards[k] = t;
                                }
                        }
                        swapped = true;
                    }
                    T.update(0, 0, false, true);
                    Event e;
                    bool got = false;
                    while (T.popEvent(e)) if (swapped && e.type == Ev::Action && !got) { first[run] = e; got = true; }
                    if (got) break;
                }
            }
            agree += first[0].type == Ev::Action && first[0].a == first[1].a && first[0].b == first[1].b &&
                     first[0].amount == first[1].amount;
        }
        CHECKT(agree == 40, "game %u: CPU decisions ignore hidden cards (%d/40)", game, agree);
    }
}

// Your statistics follow the money: hands dealt, net winnings, best purse.
static void testStats() {
    memset(&T, 0, sizeof T);
    T.newPurse();
    T.opt.goal = GOAL_ENDLESS;
    T.sitDown(HOLDEM, ROOKIE, 200, 4321);
    int32_t startWealth = T.wealth();
    int hands = 0;
    for (int i = 0; i < 200000 && hands < 30; i++) {
        uint8_t pressed = (i & 7) == 0 ? A_BUTTON : 0;
        if (T.phase == Phase::Rebuy || T.phase == Phase::Leave) break;
        T.update(pressed, pressed, false, true);
        Event e;
        while (T.popEvent(e)) if (e.type == Ev::HandEnd) hands++;
    }
    const GameStats &g = T.stats.g[HOLDEM];
    CHECKT(hands >= 10, "stats: %d hands played", hands);
    CHECKT((int)g.hands >= hands && (int)g.hands <= hands + 1, "stats: hands %u for %d played", g.hands, hands);
    // Leave (mid-hand or not): the net is exactly what the purse moved.
    T.leave();
    CHECKT(g.net == T.wealth() - startWealth, "stats: net %d, wealth moved %d", g.net, T.wealth() - startWealth);
    CHECKT(T.stats.bestPurse >= startWealth, "stats: best purse %d", T.stats.bestPurse);
}

void testTable() {
    testStats();
    testBetting();
    testBringInAndOrder();
    testPots();
    for (uint8_t g = 0; g < GAMES; g++)
        for (uint8_t lv = 0; lv < LEVELS; lv++) {
            bool ok = fuzz(g, lv, g == OMAHA ? 400 : 1500, false) && fuzz(g, lv, 200, true);
            CHECKT(ok, "fuzz game %u level %u", g, lv);
        }
    for (uint8_t g = 0; g < GAMES; g++)
        for (uint8_t lv = 0; lv < LEVELS; lv++) {
            long *a = acts[g][lv], n = a[0] + a[1] + a[2] + a[3] + a[4];
            if (!n) n = 1;
            printf("%-11s %-6s first street: fold %2ld%% check %2ld%% call %2ld%% raise %2ld%% all-in %2ld%%, "
                   "pots %ld bb\n", VARIANTS[g].name, LEVEL_NAME[lv], a[0] * 100 / n, a[1] * 100 / n,
                   a[2] * 100 / n, a[3] * 100 / n, a[4] * 100 / n, cpuPotBB[lv] / (cpuHands[lv] ? cpuHands[lv] : 1));
        }
    for (uint8_t g = 0; g < GAMES; g++) {
        const Cover &c = cov[g];
        printf("fuzz %-11s hands %ld showdowns %ld side-pots %ld splits %ld uncontested %ld all-ins %ld "
               "your raises %ld folds %ld draws %ld rebuys %ld leaves %ld\n", VARIANTS[g].name, c.hands, c.showdowns,
               c.sidePots, c.splits, c.uncontested, c.allins, c.youRaise, c.youFold, c.draws, c.rebuys, c.leaves);
        CHECKT(c.sidePots > 0 && (c.splits > 0 || g == DRAW || g == STUD) && c.allins > 0 && c.youRaise > 0 && c.rebuys > 0 && c.leaves > 0,
               "%s: the fuzz reached side pots, all-ins, raises, rebuys and leaving (split fixtures above)", VARIANTS[g].name);
    }
}

void testAi() {
    testDrawHeuristic();
    testEquity();
    testHonesty();
}
