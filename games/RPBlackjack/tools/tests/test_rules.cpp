// Host tests for the rules engine (Round.cpp).
//   rpgame test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>
#include "../../Round.h"
#include <rpgame/Input.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long _a = (long)(a), _b = (long)(b); if (_a != _b) { fails++; printf("FAIL %s:%d  %s == %s  (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

// Card helpers by face value: A=1, 2..10, J=11, Q=12, K=13; suit 0..3.
static uint8_t C(int v, int suit = 2) { return (uint8_t)(suit * 13 + v - 1); }
enum { A = 1, T = 10, J = 11, Q = 12, K = 13 };

static Hand mk(std::initializer_list<uint8_t> cs) {
    Hand h; h.clear();
    for (uint8_t c : cs) h.cards[h.count++] = c;
    return h;
}

static void testHandValues() {
    CHECK_EQ(mk({C(A), C(K)}).best(), 21);
    CHECK(mk({C(A), C(K)}).natural());
    CHECK_EQ(mk({C(A), C(A)}).best(), 12);
    CHECK_EQ(mk({C(A), C(A), C(9)}).best(), 21);
    CHECK_EQ(mk({C(A), C(6)}).best(), 17);
    CHECK(mk({C(A), C(6)}).soft());
    CHECK_EQ(mk({C(A), C(6), C(T)}).best(), 17);
    CHECK(!mk({C(A), C(6), C(T)}).soft());
    CHECK_EQ(mk({C(K), C(Q), C(2)}).hard(), 22);
    Hand s = mk({C(A), C(K)}); s.fromSplit = true;
    CHECK(!s.natural());                                   // split 21 is not blackjack
    CHECK(!mk({C(7), C(7), C(7)}).natural());
}

// Drive a round to a phase with scripted buttons. Returns frames used.
static int run(Round &r, uint8_t buttons, Phase until, int maxFrames = 2000) {
    for (int f = 0; f < maxFrames; f++) {
        if (r.phase == until) return f;
        r.update(buttons, buttons, false);
        Event e; while (r.popEvent(e)) {}
    }
    return -1;
}

// Start a round with a given bet and stacked cards: P1 D-up P2 D-hole, then draws.
static void startRound(Round &r, uint16_t bet, std::initializer_list<uint8_t> deck) {
    uint8_t d[16]; uint8_t n = 0;
    for (uint8_t c : deck) d[n++] = c;
    run(r, 0, Phase::InitBet);
    // Clear any rebet, then add $1 chips.
    while (r.initBet) { r.sel = B_CLEAR; r.update(A_BUTTON, A_BUTTON, false); Event e; while (r.popEvent(e)) {} }
    r.sel = B_1;
    for (uint16_t i = 0; i < bet; i++) { r.sel = B_1; r.update(A_BUTTON, A_BUTTON, false); Event e; while (r.popEvent(e)) {} }
    r.stackDeck(d, n);
    r.update(START_BUTTON, 0, false);
    Event e; while (r.popEvent(e)) {}
}

static void freshRound(Round &r, uint8_t rules) {
    r = Round();
    r.opt.rules = rules;
    r.seed(1234);
    r.newGame();
}

static void testPayouts() {
    Round r{};
    // Player blackjack pays 3:2.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(A), C(5), C(K), C(9)});
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.purse, 500 + 15);

    // Plain win: 20 vs 19.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(K), C(9), C(Q), C(T)});
    CHECK(r.phase == Phase::InitDeal);
    run(r, 0, Phase::PlayHand);
    r.update(DOWN_BUTTON, DOWN_BUTTON, false);       // stand
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.purse, 510);

    // Push 18 vs 18.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(K), C(8), C(8), C(T)});
    run(r, 0, Phase::PlayHand);
    r.update(DOWN_BUTTON, DOWN_BUTTON, false);
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.purse, 500);

    // Bust loses the bet.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(K), C(8), C(6), C(T), C(Q)});
    run(r, 0, Phase::PlayHand);
    r.update(UP_BUTTON, UP_BUTTON, false);           // hit: 26
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.purse, 490);

    // Odd bet blackjack rounds down: $5 pays $7.
    freshRound(r, RULES_CASINO);
    startRound(r, 5, {C(A), C(5), C(K), C(9)});
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.purse, 507);
}

static void testDealerPolicy() {
    Round r{};
    // Casino: dealer stands on soft 17 (A,6). Player 18 wins.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(K), C(A), C(8), C(6), C(5)});
    run(r, 0, Phase::OfferInsurance);
    r.sel = I_NO; r.update(A_BUTTON, A_BUTTON, false);
    run(r, 0, Phase::PlayHand);
    r.update(DOWN_BUTTON, DOWN_BUTTON, false);
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.dealer.count, 2);
    CHECK_EQ(r.purse, 510);

    // Classic (PPOT): dealer hits soft 17 (hard 7 <= 16) and draws the 5: 12... then keeps going.
    freshRound(r, RULES_CLASSIC);
    startRound(r, 10, {C(K), C(A), C(8), C(6), C(4), C(T)});
    run(r, 0, Phase::OfferInsurance);
    r.sel = I_NO; r.update(A_BUTTON, A_BUTTON, false);
    run(r, 0, Phase::PlayHand);
    r.update(DOWN_BUTTON, DOWN_BUTTON, false);
    run(r, 0, Phase::EndOfGame);
    CHECK(r.dealer.count >= 3);                           // A,6 -> hit
    CHECK_EQ(r.dealer.best(), 21);                        // A,6,4 = 21: stops
    CHECK_EQ(r.purse, 490);
}

static void testInsurance() {
    Round r{};
    // Dealer blackjack, full insurance: insurance pays 2:1, hand loses -> even.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(K), C(A), C(8), C(Q)});
    run(r, 0, Phase::OfferInsurance);
    CHECK_EQ(r.insureAmt, 5);
    r.sel = I_YES; r.update(A_BUTTON, A_BUTTON, false);
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.purse, 500);                               // PPOT paid 490 here

    // Partial insurance ($2 on a $10 bet): PPOT credited nothing at all.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(K), C(A), C(8), C(Q)});
    run(r, 0, Phase::OfferInsurance);
    r.insureAmt = 2; r.sel = I_YES; r.update(A_BUTTON, A_BUTTON, false);
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.purse, 500 - 10 - 2 + 6);

    // Insurance lost when the dealer has no blackjack.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(K), C(A), C(9), C(7)});         // player 19, dealer 18
    run(r, 0, Phase::OfferInsurance);
    r.sel = I_YES; r.update(A_BUTTON, A_BUTTON, false);
    run(r, 0, Phase::PlayHand);
    r.update(DOWN_BUTTON, DOWN_BUTTON, false);
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.purse, 500 - 5 + 10);

    // $1 bet: no insurance offered (half is zero).
    freshRound(r, RULES_CASINO);
    startRound(r, 1, {C(K), C(A), C(9), C(7)});
    CHECK(run(r, 0, Phase::PlayHand) >= 0);

    // Casino peeks on a ten: dealer blackjack beats player 20 before play.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(K), C(T), C(Q), C(A)});
    CHECK(run(r, 0, Phase::EndOfGame) >= 0);
    CHECK_EQ(r.purse, 490);
}

static void testSplitDouble() {
    Round r{};
    // Split 8s: hands 8+3 (then double to 8+3+K=21) and 8+K=18 vs dealer 10+7=17.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(8, 0), C(T), C(8, 1), C(7), C(3), C(K, 0), C(K, 1)});
    run(r, 0, Phase::PlayHand);
    CHECK(r.slotEnabled(P_SPLIT));
    r.sel = P_SPLIT; r.update(A_BUTTON, A_BUTTON, false);
    run(r, 0, Phase::PlayHand);
    CHECK_EQ(r.nHands, 2);
    CHECK_EQ(r.hands[0].best(), 11);
    CHECK(r.slotEnabled(P_DOUBLE));                       // double after split
    r.sel = P_DOUBLE; r.update(A_BUTTON, A_BUTTON, false);
    for (int i = 0; i < 400 && r.active == 0; i++) { r.update(0, 0, false); Event e; while (r.popEvent(e)) {} }
    CHECK_EQ(r.active, 1);
    run(r, 0, Phase::PlayHand);
    r.update(DOWN_BUTTON, DOWN_BUTTON, false);            // stand on 18
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.hands[0].best(), 21);
    CHECK_EQ(r.hands[1].best(), 18);
    CHECK_EQ(r.purse, 500 + 20 + 10);

    // Split aces get one card each and stand; A+K after a split pays 1:1.
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(A, 0), C(9), C(A, 1), C(8), C(K, 0), C(5)});
    run(r, 0, Phase::PlayHand);
    r.sel = P_SPLIT; r.update(A_BUTTON, A_BUTTON, false);
    run(r, 0, Phase::EndOfGame);
    CHECK_EQ(r.hands[0].count, 2);
    CHECK_EQ(r.hands[1].count, 2);
    CHECK_EQ(r.purse, 500 + 10 - 10 + 0);                 // 21 wins 10, A+5=16 loses to 17

    // Can't split K+Q (PPOT: same rank only).
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(K), C(9), C(Q), C(8)});
    run(r, 0, Phase::PlayHand);
    CHECK(!r.slotEnabled(P_SPLIT));
}

static void testBankruptcyAfterPeek() {
    Round r{};
    freshRound(r, RULES_CASINO);
    r.purse = 10;
    startRound(r, 10, {C(K), C(A), C(8), C(Q)});
    run(r, 0, Phase::OfferInsurance);
    CHECK(!r.slotEnabled(I_YES) || r.insuranceMax() == 0);
    r.sel = I_NO; r.update(A_BUTTON, A_BUTTON, false);
    CHECK(run(r, 0, Phase::GameLost) >= 0);               // PPOT went on with purse 0
}

static void testNaturalAutoStands() {
    Round r{};
    freshRound(r, RULES_CASINO);
    startRound(r, 10, {C(A), C(9), C(K), C(8)});
    // No input needed: the natural is paid without a decision.
    CHECK(run(r, 0, Phase::EndOfGame) >= 0);
    CHECK_EQ(r.purse, 515);
}

static void testGoal() {
    Round r{};
    freshRound(r, RULES_CASINO);
    r.purse = 990;
    startRound(r, 10, {C(K), C(9), C(Q), C(8)});
    run(r, 0, Phase::PlayHand);
    r.update(DOWN_BUTTON, DOWN_BUTTON, false);
    CHECK(run(r, 0, Phase::GameWon) >= 0);
}

// Random legal play for many hands: money is conserved hand by hand, the
// purse never goes negative and the flow never stalls.
static void testFuzz() {
    for (uint32_t seed = 1; seed <= 40; seed++) {
        Round r{};
        r.opt.rules = (seed & 1) ? RULES_CLASSIC : RULES_CASINO;
        r.opt.goal = GOAL_ENDLESS;
        r.seed(seed * 2654435761u);
        r.newGame();
        uint32_t x = seed;
        int hands = 0, stall = 0;
        int32_t money = r.purse;          // purse + everything on the table
        int32_t onTable = 0;
        Phase last = r.phase;
        for (int f = 0; f < 200000 && hands < 400; f++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            static const uint8_t B[] = {A_BUTTON, LEFT_BUTTON, RIGHT_BUTTON, UP_BUTTON, DOWN_BUTTON, B_BUTTON, 0, 0, START_BUTTON};
            uint8_t b = B[x % sizeof B];
            if (r.phase == Phase::EndOfGame) { r.sel = E_CONTINUE; b = A_BUTTON; }
            if (r.phase == Phase::GameLost || r.phase == Phase::Quit) break;
            r.update(b, b, false);
            Event e;
            while (r.popEvent(e)) {
                switch (e.type) {
                    case Ev::BetAdd: case Ev::Insure: onTable += e.amount; break;
                    case Ev::BetRemove: onTable -= e.amount; break;
                    case Ev::Split: case Ev::Double: onTable += e.amount; break;
                    default: break;
                }
            }
            CHECK(r.purse >= 0);
            if (r.phase == Phase::EndOfGame && last != Phase::EndOfGame) {
                hands++;
                // Settled: the table is empty again and nothing leaked.
                CHECK(r.purse >= 0);
                onTable = 0;
                money = r.purse;
            }
            if (r.phase == Phase::InitBet) {
                // Between hands only the rebet is on the table.
                CHECK_EQ(r.purse + r.initBet, money);
                onTable = r.initBet;
            }
            if (r.phase == Phase::PlayHand || r.phase == Phase::OfferInsurance) {
                // Mid-hand, before anything is paid out: every chip that left
                // the purse is on the table.
                CHECK_EQ(r.purse + onTable, money);
            }
            if (r.phase == last) { if (++stall > 20000) { CHECK(!"stuck"); break; } }
            else stall = 0;
            last = r.phase;
        }
        CHECK(hands > 0);
    }
}

int main() {
    testHandValues();
    testPayouts();
    testDealerPolicy();
    testInsurance();
    testSplitDouble();
    testBankruptcyAfterPeek();
    testNaturalAutoStands();
    testGoal();
    testFuzz();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
