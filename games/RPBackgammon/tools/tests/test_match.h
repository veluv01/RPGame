// Tests of the game flow (Match.cpp) and the CPU (Ai.cpp and Net.cpp), part of
// test_backgammon.cpp: whole games played through the same calls the screens
// make, with a second board kept from the events alone.
#include "../../Match.h"
#include "../../Net.h"
#include "../../Race.h"
#include "../../Cube.h"
#include "../../Notation.h"

struct Driven {
    uint32_t rng;               // the "player": its choices
    uint32_t hash;              // of the board at every pick-up
    long events, steps, takeBacks, cpuTurns, maxQueue;
    int saveAt;                 // tick to save and reload at (-1: never)
    bool reloaded;
    long games, doubles, takes, passes;
    long beavers, raccoons, forced;     // ... and with beavers and auto play on
};

static uint32_t drnd(Driven &d) { d.rng ^= d.rng << 13; d.rng ^= d.rng >> 17; d.rng ^= d.rng << 5; return d.rng; }

static uint32_t boardHash(const Board &b, uint32_t h) {
    const uint8_t *p = &b.n[0][0];
    for (size_t i = 0; i < sizeof b.n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

// Plays the match match:: holds to its end. Returns false on any failure.
static bool drive(Driven &d) {
    Board shadow = match::board, atRoll = match::board;
    uint8_t side = 0, d1 = 0, d2 = 0, scoreSeen[2] = {match::score[0], match::score[1]}, cubeSeen = match::cube;
    uint8_t offerSeen = 0;
    bool rolled = false, over = false, chose = false;   // chose: the human was asked to move this turn
    for (int tick = 0; !over; tick++) {
        if (tick > 2000000) { printf("FAIL a match did not end\n"); failures++; return false; }
        match::update(false);
        // Save and reload: only where the reload resumes exactly (the turn
        // has no steps of the human's yet).
        if (d.saveAt >= 0 && tick >= d.saveAt && !d.reloaded && match::active() &&
            (match::humanToRoll() || (!match::isHuman(match::side()) && !match::humanToAnswer()))) {
            match::Event peek;
            if (!match::peekEvent(peek)) {
                match::Record r;
                match::save(r);
                match::Setup junk = {match::setup.mode, match::setup.level, 1, 0xDEAD};
                match::start(junk);                         // forget everything
                if (!match::load(r)) { printf("FAIL reload refused\n"); failures++; return false; }
                d.reloaded = true;
            }
        }
        match::Event e;
        long n = 0;
        while (match::popEvent(e)) {
            n++; d.events++;
            switch (e.type) {
                case match::EV_START: shadow = match::board; rolled = false; cubeSeen = match::cube; break;
                case match::EV_TURN: side = e.a; CHECK_EQ(e.b, match::isHuman(e.a)); break;
                case match::EV_ROLL:
                    d1 = e.a; d2 = e.b; rolled = true; chose = false;
                    atRoll = shadow;
                    CHECK_EQ(e.d, side);
                    CHECK_EQ(e.c, maxPlayable(shadow, side, roll2(e.a, e.b)));
                    break;
                case match::EV_STEP: {
                    d.steps++;
                    if (!canStep(shadow, side, e.a, e.d)) { printf("FAIL illegal step in an event\n"); failures++; return false; }
                    CHECK_EQ(landing(e.a, e.d), e.b);
                    bool hit = doStep(shadow, side, e.a, e.d);
                    CHECK_EQ(hit, (e.c & match::F_HIT) != 0);
                    break;
                }
                case match::EV_UNDO:
                    undoStep(shadow, side, e.a, e.d, (e.c & match::F_HIT) != 0);
                    break;
                case match::EV_PICKUP: {
                    CHECK(rolled);
                    CHECK_EQ(e.a, side);
                    // The turn as a whole is a play the rules allow.
                    std::set<Ref> want = refPlays(atRoll, side, d1, d2);
                    if (!want.count(toRef(shadow))) {
                        printf("FAIL a turn ended on a position the rules do not allow: side %d roll %d-%d from %s\n", side,
                               d1, d2, show(atRoll));
                        failures++;
                        return false;
                    }
                    d.hash = boardHash(shadow, d.hash);
                    if (!match::isHuman(side)) d.cpuTurns++;
                    else if (!chose) {
                        // Never asked: no move at all, or played for the human
                        // (auto play) - either way the roll had no other play.
                        CHECK_EQ(want.size(), 1u);
                        if (memcmp(shadow.n, atRoll.n, sizeof shadow.n)) { CHECK(match::autoPlay); d.forced++; }
                    }
                    rolled = false;
                    break;
                }
                case match::EV_DOUBLE:
                    CHECK(!match::crawford);
                    CHECK(match::setup.length > 1);
                    CHECK_EQ(e.b, cubeSeen + 1);
                    CHECK(!rolled);
                    offerSeen = e.b;
                    d.doubles++;
                    break;
                case match::EV_BEAVER:          // the doubled side redoubles
                case match::EV_RACCOON:         // ... and the doubler again
                    CHECK(match::beavers);
                    CHECK_EQ(e.a, e.type == match::EV_BEAVER ? side ^ 1 : side);
                    CHECK_EQ(e.b, offerSeen + 1);
                    CHECK(e.b <= 6);
                    offerSeen = e.b;
                    if (e.type == match::EV_BEAVER) d.beavers++; else d.raccoons++;
                    break;
                case match::EV_TAKE:
                    CHECK_EQ(e.b, offerSeen);
                    CHECK(e.b >= cubeSeen + 1 && e.b <= cubeSeen + 3);
                    cubeSeen = e.b;
                    CHECK_EQ(match::cubeOwner, e.a);
                    d.takes++;
                    break;
                case match::EV_OVER: {
                    uint8_t w = 9;
                    if (e.c == match::BY_PLAY) {
                        CHECK_EQ(result(shadow, w), e.b); CHECK_EQ(w, e.a);
                        CHECK_EQ(e.d, (1 << cubeSeen) * e.b);
                    } else if (e.c == match::BY_PASS) { CHECK_EQ(e.d, 1 << cubeSeen); d.passes++; }
                    scoreSeen[e.a] = (uint8_t)(scoreSeen[e.a] + e.d);
                    CHECK_EQ(match::score[0], scoreSeen[0]);
                    CHECK_EQ(match::score[1], scoreSeen[1]);
                    d.hash = boardHash(shadow, d.hash) + e.a * 7u + e.b + e.d * 131u;
                    d.games++;
                    if (match::matchOver()) {
                        over = true;
                        CHECK(match::score[e.a] >= match::setup.length);
                        CHECK(match::score[e.a ^ 1] < match::setup.length);
                    } else CHECK(match::between());
                    break;
                }
                default: break;
            }
        }
        if (n > d.maxQueue) d.maxQueue = n;
        if (!over && memcmp(shadow.n, match::board.n, sizeof shadow.n) != 0) {
            printf("FAIL the events and the board disagree\n"); failures++; return false;
        }
        if (!valid(shadow)) { printf("FAIL invalid board in a match\n"); failures++; return false; }
        if (over) break;
        // The human's side of it.
        if (match::between()) match::nextGame();
        else if (match::humanToAnswer()) {
            uint32_t r = drnd(d) % 6;
            CHECK_EQ(match::canBeaver(), match::beavers && offerSeen < 6);
            if (match::beavered()) { if (r < 3) match::raccoon(); else match::take(); }
            else if (r < 2 && match::canBeaver()) match::beaver();
            else if (r < 4) match::take();
            else match::pass();
        } else if (match::humanToRoll()) {
            if (match::canDouble() && drnd(d) % 9 == 0) match::offerDouble();
            else match::roll();
        } else if (match::humanToConfirm()) {
            chose = true;
            if (drnd(d) % 4 == 0) { CHECK(match::takeBack()); d.takeBacks++; }
            else match::confirm();
        } else if (match::humanToMove()) {
            chose = true;
            if (match::canTakeBack() && drnd(d) % 6 == 0) { CHECK(match::takeBack()); d.takeBacks++; continue; }
            struct Opt { uint8_t from; Target t; };
            std::vector<Opt> opts;
            for (uint8_t f = BAR; f; f--) {
                Target tg[4];
                uint8_t k = match::targetsFrom(f, tg);
                for (uint8_t i = 0; i < k; i++) opts.push_back({f, tg[i]});
            }
            if (opts.empty()) { printf("FAIL a human turn with dice to play and nowhere to go\n"); failures++; return false; }
            const Opt &o = opts[drnd(d) % opts.size()];
            CHECK(match::play(o.from, o.t));
        }
    }
    return true;
}

static void testMatch(bool quick) {
    // Symmetry and sanity of the network.
    Board b;
    reset(b);
    CHECK_EQ(net::eval(b, WHITE), net::eval(b, RED));
    CHECK(net::chance(1000) > net::chance(0));
    CHECK(net::chance(-100000) < 100);
    CHECK(net::chance(100000) > 65400);
    printf("net: the opening position is worth %d to the side that just moved (chance %.3f)\n", (int)net::eval(b, WHITE),
           net::chance(net::eval(b, WHITE)) / 65535.0);
    // The incremental evaluation (from the position before) always equals a
    // fresh one, through positions in play order and in no order at all.
    {
        long n = 0, bad = 0;
        for (int i = 0; i < (quick ? 4000 : 40000); i++) {
            Board x;
            randomPosition(x, i & 3);
            uint8_t m = (uint8_t)rnd(2);
            Plays g;
            begin(g, x, m, (uint8_t)(1 + rnd(6)), (uint8_t)(1 + rnd(6)));
            while (next(g, x)) {
                int32_t inc = net::eval(x, m);
                net::forget();
                n++;
                if (net::eval(x, m) != inc) bad++;
                if (rnd(3) == 0) net::eval(x, m ^ 1);       // the other side's view between
            }
        }
        CHECK_EQ(bad, 0);
        printf("net: %ld incremental evaluations, all equal to fresh ones\n", n);
    }

    // The CPU chooses the same play however its thinking is sliced.
    for (int i = 0; i < (quick ? 60 : 400); i++) {
        randomPosition(b, 3);
        uint8_t s = (uint8_t)rnd(2), d1 = (uint8_t)(1 + rnd(6)), d2 = (uint8_t)(1 + rnd(6));
        for (uint8_t level = 0; level < ai::LEVELS; level++) {
            Rng n1, n2;
            n1.seed(99, 2); n2.seed(99, 2);
            ai::Play p1, p2;
            ai::start(b, s, d1, d2, level, n1);
            while (!ai::step(1)) {}
            ai::chosen(p1);
            uint32_t count = ai::positions();
            ai::start(b, s, d1, d2, level, n2);
            while (!ai::step(977)) {}
            ai::chosen(p2);
            CHECK_EQ(ai::positions(), count);
            CHECK(memcmp(&p1, &p2, sizeof p1) == 0);
        }
    }

    // Whole matches: single games and matches to 3, 5 and 7, against each
    // opponent and between two humans, the "human" doubling, taking and
    // passing at random.
    Driven total = {};
    total.saveAt = -1;
    int games = quick ? 12 : 120;
    static const uint8_t LEN[4] = {1, 3, 5, 7};
    for (int mode = 0; mode < 4; mode++)
        for (int g = 0; g < games; g++) {
            match::Setup s = {(uint8_t)(mode == 3 ? match::TWO_PLAYER : match::VS_CPU), (uint8_t)(mode % 3),
                              LEN[g & 3], (uint32_t)(g * 31 + mode + 1)};
            match::beavers = (g >> 2) & 1;
            match::autoPlay = (g >> 3) & 1;
            match::start(s);
            Driven d = {};
            d.rng = (uint32_t)(g * 977 + mode * 13 + 5);
            d.saveAt = -1;
            if (!drive(d)) return;
            total.events += d.events; total.steps += d.steps; total.takeBacks += d.takeBacks;
            total.cpuTurns += d.cpuTurns; total.games += d.games; total.doubles += d.doubles;
            total.takes += d.takes; total.passes += d.passes;
            total.beavers += d.beavers; total.raccoons += d.raccoons; total.forced += d.forced;
            if (d.maxQueue > total.maxQueue) total.maxQueue = d.maxQueue;
            CHECK(!match::active());
            CHECK(match::matchOver());
            if (failures) return;
        }
    printf("matches: %d, %ld games, %ld events, %ld steps, %ld take-backs; %ld doubles, %ld taken, %ld passed; "
           "most events in a tick %ld\n", 4 * games, total.games, total.events, total.steps, total.takeBacks,
           total.doubles, total.takes, total.passes, total.maxQueue);
    CHECK(total.maxQueue <= 8);
    CHECK(total.doubles > 0 && total.takes > 0 && total.passes > 0);
    printf("beavers: %ld beavers, %ld raccoons; auto play: %ld forced turns played for the human\n", total.beavers,
           total.raccoons, total.forced);
    CHECK(total.beavers > 0 && total.raccoons > 0 && total.forced > 0);

    // The same seed and the same choices give the same match - and so does
    // saving and reloading in the middle of it.
    for (int g = 0; g < (quick ? 6 : 60); g++) {
        match::Setup s = {match::VS_CPU, (uint8_t)(g % 3), LEN[(g / 3) & 3], (uint32_t)(5000 + g)};
        uint32_t hashes[3];
        match::beavers = match::autoPlay = g & 1;
        for (int run = 0; run < 3; run++) {
            match::start(s);
            Driven d = {};
            d.rng = (uint32_t)(g + 77);
            d.saveAt = run == 2 ? 20 + (g * 7) % 120 : -1;
            if (!drive(d)) return;
            hashes[run] = d.hash + match::winner * 3u + match::how + match::score[0] * 11u + match::score[1] * 13u;
            if (run == 2) CHECK(d.reloaded);
        }
        CHECK_EQ(hashes[0], hashes[1]);
        CHECK_EQ(hashes[0], hashes[2]);
    }
    match::beavers = match::autoPlay = false;

    // The Crawford rule: a side reaching match point makes the next game the
    // Crawford game (no doubling); the games after it have the cube again.
    {
        int crawfords = 0, after = 0;
        for (uint32_t seed = 1; seed <= (quick ? 6u : 30u); seed++) {
            match::Setup s = {match::TWO_PLAYER, 0, 5, seed};
            match::start(s);
            match::setScore(5, 3, 2, 0, match::CENTRE, false);
            match::Event e;
            bool inCrawford = false;
            for (int k = 0; k < 2000000 && !match::matchOver(); k++) {
                match::update(false);
                while (match::popEvent(e)) if (e.type == match::EV_DOUBLE) CHECK(!match::crawford);
                if (match::between()) {
                    match::nextGame();
                    inCrawford = match::crawford;
                    crawfords += inCrawford;
                    continue;
                }
                if (match::humanToAnswer()) match::take();
                else if (match::humanToRoll()) {
                    if (inCrawford) CHECK(!match::canDouble());
                    // Doubling only after the Crawford game (the trailer, at once).
                    if (match::postCrawford() && match::canDouble()) { after++; match::offerDouble(); }
                    else match::roll();
                } else if (match::humanToConfirm()) match::confirm();
                else if (match::humanToMove()) {
                    for (uint8_t f = BAR; f; f--) {
                        Target tg[4];
                        if (match::targetsFrom(f, tg)) { match::play(f, tg[0]); break; }
                    }
                }
            }
            CHECK(match::matchOver());
        }
        CHECK(crawfords > 0);
        CHECK(after > 0);
        printf("crawford: %d Crawford games, %d doubles after them\n", crawfords, after);
    }

    // The notation.
    {
        char buf[48];
        uint8_t f1[2] = {24, 13}, d1[2] = {6, 5}, h1[2] = {0, 0};
        CHECK(!strcmp(notate(buf, f1, d1, h1, 2), "24/18 13/8"));
        uint8_t f2[2] = {24, 18}, d2[2] = {6, 5}, h2[2] = {0, 0};
        CHECK(!strcmp(notate(buf, f2, d2, h2, 2), "24/13"));
        uint8_t f3[2] = {24, 18}, d3[2] = {6, 5}, h3[2] = {1, 0};
        CHECK(!strcmp(notate(buf, f3, d3, h3, 2), "24/18*/13"));
        uint8_t f4[4] = {13, 13, 6, 6}, d4[4] = {4, 4, 4, 4}, h4[4] = {0, 0, 0, 0};
        CHECK(!strcmp(notate(buf, f4, d4, h4, 4), "13/9(2) 6/2(2)"));
        uint8_t f5[2] = {25, 6}, d5[2] = {3, 6}, h5[2] = {1, 0};
        CHECK(!strcmp(notate(buf, f5, d5, h5, 2), "BAR/22* 6/OFF"));
        uint8_t f6[4] = {13, 9, 13, 9}, d6[4] = {4, 4, 4, 4}, h6[4] = {0, 0, 0, 0};
        CHECK(!strcmp(notate(buf, f6, d6, h6, 4), "13/5(2)"));
        printf("notation: %s\n", notate(buf, f5, d5, h5, 2));
    }

    // The cube's judgement: a take at money-like odds, a pass when far
    // behind; doubles near the cash point, never when a win takes the match.
    {
        CHECK(cube::wantsTake(7, 7, 1, false, 30000));                  // 46%: take
        CHECK(!cube::wantsTake(7, 7, 1, false, 8000));                  // 12%: pass
        CHECK(cube::wantsDouble(7, 7, 1, false, false, 47000, false));  // 72%
        CHECK(!cube::wantsDouble(7, 7, 1, false, false, 36000, false)); // 55%
        CHECK(!cube::wantsDouble(1, 5, 1, false, true, 60000, false));  // 1-away: a win is the match
        CHECK(cube::wantsDouble(4, 1, 1, false, true, 20000, false));   // behind after the Crawford game
        for (int a = 1; a <= 7; a++)
            for (int b = 1; b <= 7; b++) {
                CHECK(cube::equity(a, b, false) + cube::equity(b, a, false) >= 65535);
                CHECK(cube::equity(a, b, false) + cube::equity(b, a, false) <= 65537);
                if (a < 7) CHECK(cube::equity(a, b, false) >= cube::equity(a + 1, b, false));
            }
    }

    // Scripted positions and dice.
    match::Setup s = {match::TWO_PLAYER, 0, 1, 1};
    CHECK(match::startPosition(s, "w 2:1 r 6:5 5:5 4:5", WHITE));
    match::stackDice("21");
    match::Event e;
    while (match::popEvent(e)) {}
    match::update(false);
    while (match::popEvent(e)) {}
    CHECK(match::humanToRoll());
    match::roll();
    Target tg[4];
    CHECK(match::humanToMove());
    uint8_t nt = match::targetsFrom(2, tg), off = 9;
    for (uint8_t i = 0; i < nt; i++) if (tg[i].to == OFF) off = i;
    CHECK_EQ(nt, 2);                                    // 2/1 with the 1, or straight off with the 2
    CHECK(off != 9 && match::play(2, tg[off]));
    CHECK(!match::active());
    CHECK_EQ(match::winner, WHITE);
    CHECK_EQ(match::how, 2);                            // Red has borne none off: a gammon

    // Resigning.
    match::Setup v = {match::VS_CPU, 1, 3, 3};
    match::start(v);
    match::resign();
    CHECK(!match::active());
    CHECK_EQ(match::winner, RED);
    CHECK_EQ(match::reason, match::BY_RESIGNATION);
    CHECK_EQ(match::score[1], 1);
}
