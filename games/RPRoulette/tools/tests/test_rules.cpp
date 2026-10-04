// Host tests for the rules: Wheel, Spots, Nav and Roulette.
//   rpgame test
//   test_rules --dump     the spot model, diffed against ref_roulette.py
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define private public                 // the RNG, the queue and go() for white-box checks
#include "../../Roulette.h"
#undef private
#include "../../Nav.h"
#include "../../Spots.h"
#include "../../Wheel.h"
#include <rpgame/Input.h>          // button masks (the RPGame library)

using namespace spots;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long _a = (long)(a), _b = (long)(b); if (_a != _b) { fails++; printf("FAIL %s:%d  %s == %s  (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { checks++; const char *_a = (a), *_b = (b); if (strcmp(_a, _b)) { fails++; printf("FAIL %s:%d  %s == \"%s\"  (got \"%s\")\n", __FILE__, __LINE__, #a, _b, _a); } } while (0)

static const uint8_t DPAD = UP_BUTTON | DOWN_BUTTON | LEFT_BUTTON | RIGHT_BUTTON;

// A spot's words, for messages: "SPLIT 17/20".
static const char *label(uint8_t id, bool us) {
    static char buf[8][40];
    static int k;
    char *b = buf[k++ & 7];
    if (id == NONE) return "(stay)";
    strcpy(b, kindName(id, us));
    char nums[32];
    numbers(nums, id, us);
    if (*nums) { strcat(b, " "); strcat(b, nums); }
    return b;
}

static uint8_t lat(int u, int v) { return (uint8_t)(v * 24 + u); }
static uint8_t S(int n) { return straightId((uint8_t)n); }

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
static Event evs[4096];
static int nEv;
static void drain(Roulette &r) { Event e; while (r.popEvent(e)) if (nEv < 4096) evs[nEv++] = e; }
static void clearLog() { nEv = 0; }
static int countEv(Ev t) { int c = 0; for (int i = 0; i < nEv; i++) c += evs[i].type == t; return c; }
static const Event *lastEv(Ev t) { for (int i = nEv; i--;) if (evs[i].type == t) return &evs[i]; return nullptr; }
static bool hasSay(uint8_t line, uint8_t face) {
    for (int i = 0; i < nEv; i++) if (evs[i].type == Ev::Say && evs[i].a == line && evs[i].b == face) return true;
    return false;
}

static void tick(Roulette &r, uint8_t pressed = 0, uint8_t held = 0, bool busy = false) {
    r.update(pressed, (uint8_t)(pressed | held), busy);
    drain(r);
}
static int until(Roulette &r, Phase p, int max = 5000) {
    for (int i = 0; i < max; i++) { if (r.phase == p) return i; tick(r); }
    return -1;
}
static int32_t money(const Roulette &r) { return r.purse + r.onTable(); }
static void moveTo(Roulette &r, uint8_t spot) {
    nav::Glove g = nav::at(spot, r.us);
    r.cursor = spot; r.gx = g.x; r.gy = g.y;
}
static void pressOn(Roulette &r, uint8_t spot, uint8_t button) { moveTo(r, spot); tick(r, button); }
static void fresh(Roulette &r, bool us = false, uint8_t goal = GOAL_ENDLESS) {
    r = Roulette();
    r.opt.wheel = us; r.opt.goal = goal;
    r.seed(77);
    r.newGame();
    until(r, Phase::Betting);
    clearLog();
}
static bool spinning(Phase p) { return p >= Phase::NoMoreBets && p <= Phase::EndOfSpin; }
// SPIN on a forced number, played out to the next betting (or the end).
static void spinTo(Roulette &r, uint8_t n) {
    r.force(n);
    pressOn(r, SPIN, A_BUTTON);
    for (int i = 0; i < 2000 && spinning(r.phase); i++) tick(r);
}

// ---------------------------------------------------------------------------
// Wheel
// ---------------------------------------------------------------------------
static void testWheel() {
    for (int w = 0; w < 2; w++) {
        bool us = w;
        uint8_t P = wheel::pockets(us);
        int seen[38] = {0};
        for (uint8_t i = 0; i < P; i++) {
            uint8_t n = wheel::numberAt(i, us);
            CHECK(n < P);
            if (n < 38) seen[n]++;
            CHECK_EQ(wheel::indexOf(n, us), i);
            // Colours by wheel index (docs/design/wheel.md 1.3): the zeros
            // green; European odd red, American odd black.
            wheel::Colour c = wheel::colour(n);
            if (n == 0 || n == wheel::N00) CHECK_EQ(c, wheel::GREEN);
            else CHECK_EQ(c, ((i & 1) != us) ? wheel::RED_NUM : wheel::BLACK_NUM);
            uint8_t m = wheel::numberAt((uint8_t)((i + 1) % P), us);
            if (n && m && n != wheel::N00 && m != wheel::N00) CHECK(wheel::colour(m) != c);
        }
        for (uint8_t n = 0; n < P; n++) {
            CHECK_EQ(seen[n], 1);
            CHECK_EQ(wheel::numberAt(wheel::indexOf(n, us), us), n);
        }
    }
    CHECK_EQ(wheel::numberAt(0, false), 0);
    CHECK_EQ(wheel::numberAt(1, false), 32);           // 0 sits between 26 and 32
    CHECK_EQ(wheel::numberAt(36, false), 26);
    CHECK_EQ(wheel::indexOf(wheel::N00, true), 19);    // 00 opposite 0
    CHECK_EQ(wheel::numberAt(1, true), 28);
    CHECK_EQ(wheel::numberAt(37, true), 2);
    static const uint8_t REDS[18] = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36};
    int red = 0;
    for (uint8_t n = 1; n <= 36; n++) {
        bool isRed = memchr(REDS, n, 18) != nullptr;
        CHECK_EQ(wheel::colour(n), isRed ? wheel::RED_NUM : wheel::BLACK_NUM);
        red += isRed;
    }
    CHECK_EQ(red, 18);
    char buf[8];
    CHECK_EQ(wheel::name(buf, 17) - buf, 2); CHECK_STR(buf, "17");
    CHECK_EQ(wheel::name(buf, 5) - buf, 1);  CHECK_STR(buf, "5");
    CHECK_EQ(wheel::name(buf, 0) - buf, 1);  CHECK_STR(buf, "0");
    CHECK_EQ(wheel::name(buf, 37) - buf, 2); CHECK_STR(buf, "00");
    CHECK_EQ(wheel::name(buf, 36) - buf, 2); CHECK_STR(buf, "36");
}

// ---------------------------------------------------------------------------
// Spots
// ---------------------------------------------------------------------------
static bool glyphsOk(const char *s) {
    for (; *s; s++) {
        char c = *s;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) continue;
        if (!strchr(" \n!.-+?:$,/'*()<>=%#", c)) return false;
    }
    return true;
}
static int width35(const char *s) { int n = (int)strlen(s); return n ? 4 * n - 1 : 0; }

static void testSpots() {
    static const uint8_t PAY[11] = {35, 17, 11, 11, 8, 8, 6, 5, 2, 2, 1};
    static const int EU_KINDS[12] = {37, 60, 12, 2, 22, 1, 0, 11, 3, 3, 6, 7};
    static const int US_KINDS[12] = {38, 60, 12, 3, 22, 0, 1, 11, 3, 3, 6, 7};
    for (int w = 0; w < 2; w++) {
        bool us = w;
        uint8_t P = wheel::pockets(us);
        int kinds[12] = {0}, nbet = 0, nstop = 0;
        uint16_t anchors[NSPOT];
        int nAnch = 0;
        for (uint8_t id = 0; id < NSPOT; id++) {
            if (!valid(id, us)) continue;
            nstop++;
            Kind k = kind(id, us);
            kinds[k]++;
            Geo g = geo(id, us);
            CHECK(g.x0 <= g.ax && g.ax <= g.x1 && g.y0 <= g.ay && g.ay <= g.y1);
            CHECK(g.x1 <= 127 && g.y1 <= 127 && g.y0 >= 48);
            uint16_t a = (uint16_t)(g.ax << 8 | g.ay);
            for (int i = 0; i < nAnch; i++) if (anchors[i] == a) { CHECK(!"two spots share an anchor"); printf("   %s\n", label(id, us)); }
            anchors[nAnch++] = a;
            if (!isBet(id)) { CHECK_EQ(k, BAR); CHECK_EQ(count(id, us), 0); continue; }
            nbet++;
            CHECK_EQ(inside(id), id < COLUMN);
            uint8_t cnt = 0;
            int ret = 0;
            for (uint8_t n = 0; n <= wheel::N00; n++) {
                bool c = covers(id, n, us);
                if (n >= P) { CHECK(!c); continue; }
                cnt = (uint8_t)(cnt + c);
                if (c) ret += 1 + payout(id, us);
            }
            CHECK_EQ(count(id, us), cnt);
            CHECK_EQ(payout(id, us), PAY[k]);
            // Every bet returns 36 units over the wheel's numbers (the house
            // edge is the zero), except the American top line's 35.
            CHECK_EQ(ret, k == TOP_LINE ? 35 : 36);
            if (!inside(id)) CHECK(!covers(id, 0, us) && !covers(id, wheel::N00, us));
        }
        const int *want = us ? US_KINDS : EU_KINDS;
        for (int k = 0; k < 12; k++) CHECK_EQ(kinds[k], want[k]);
        CHECK_EQ(nbet, us ? 159 : 157);
        CHECK_EQ(nstop, us ? 166 : 164);
        CHECK(!valid(NSPOT, us) && !valid(NONE, us));
        // Every number: 1 straight, 2-4 splits, 1 street, 1-4 corners, 1-2
        // six lines, 1 column, 1 dozen, 3 even-money bets.
        for (uint8_t n = 1; n <= 36; n++) {
            int byKind[12] = {0};
            for (uint8_t id = 0; id < NBET; id++)
                if (valid(id, us) && covers(id, n, us)) byKind[kind(id, us)]++;
            CHECK_EQ(byKind[STRAIGHT], 1);
            CHECK(byKind[SPLIT] >= 2 && byKind[SPLIT] <= 4);
            CHECK_EQ(byKind[STREET], 1);
            CHECK(byKind[CORNER] >= 1 && byKind[CORNER] <= 4);
            CHECK(byKind[SIX_LINE] >= 1 && byKind[SIX_LINE] <= 2);
            CHECK_EQ(byKind[COLUMN_BET], 1);
            CHECK_EQ(byKind[DOZEN_BET], 1);
            CHECK_EQ(byKind[EVEN_MONEY], 3);
        }
        for (uint8_t n = 0; n < P; n++) {
            uint8_t s = straightId(n);
            CHECK(valid(s, us));
            CHECK_EQ(kind(s, us), STRAIGHT);
            CHECK(covers(s, n, us));
            CHECK_EQ(count(s, us), 1);
        }
        // Plate width (text35: 4 px a character, less 1): name, numbers, the
        // largest amount and payout. Over 120 px the numbers are dropped.
        int dropReal = 0;
        for (uint8_t id = 0; id < NBET; id++) {
            if (!valid(id, us)) continue;
            char nums[32], text[96];
            numbers(nums, id, us);
            CHECK(glyphsOk(kindName(id, us)) && glyphsOk(nums));
            snprintf(text, sizeof text, "%s%s%s $250 35 TO 1", kindName(id, us), *nums ? " " : "", nums);
            if (width35(text) > 120) {
                snprintf(text, sizeof text, "%s $250 35 TO 1", kindName(id, us));
                CHECK(width35(text) <= 120);
            }
            snprintf(text, sizeof text, "%s%s%s $%d %d TO 1", kindName(id, us), *nums ? " " : "", nums,
                     inside(id) ? 100 : 250, payout(id, us));
            if (width35(text) > 120) {
                dropReal++;
                CHECK_EQ(kind(id, us), TOP_LINE);         // layout.md 0.3: only the top line
            }
        }
        CHECK_EQ(dropReal, us ? 1 : 0);
    }
    CHECK(!valid(DZERO, false) && !valid(ZERO_DZERO, false));
    CHECK(valid(DZERO, true) && valid(ZERO_DZERO, true));

    // Geometry (layout.md 1.2).
    for (int c = 1; c <= 12; c++)
        for (int r = 0; r < 3; r++) {
            Geo g = geo(S(3 * (c - 1) + r + 1), false);
            CHECK_EQ(g.x0, 9 * c); CHECK_EQ(g.x1, 9 * c + 7); CHECK_EQ(g.ax, 9 * c + 4);
            CHECK_EQ(g.y0, 69 - 10 * r); CHECK_EQ(g.y1, 77 - 10 * r); CHECK_EQ(g.ay, 73 - 10 * r);
        }
    for (int v = 0; v < 6; v++)
        for (int u = 0; u < 24; u++) {
            Geo g = geo(lat(u, v), true);
            CHECK_EQ(g.ax, (9 * u + 16 + (u & 1)) / 2);
            CHECK_EQ(g.ay, 78 - 5 * v);
        }
    Geo g = geo(S(5), false);
    CHECK(g.ax == 22 && g.ay == 63);
    g = geo(ZERO, false); CHECK(g.ax == 4 && g.ay == 63 && g.x0 == 1 && g.x1 == 7 && g.y0 == 49 && g.y1 == 77);
    g = geo(ZERO, true);  CHECK(g.ax == 4 && g.ay == 71 && g.y0 == 64 && g.y1 == 77);
    g = geo(DZERO, true); CHECK(g.ax == 4 && g.ay == 55 && g.y0 == 49 && g.y1 == 62);
    g = geo(ZERO_DZERO, true); CHECK(g.ax == 4 && g.ay == 63 && g.x0 == 4 && g.y1 == 63);
    for (int k = 0; k < 3; k++) {
        g = geo((uint8_t)(COLUMN + k), false);
        CHECK(g.x0 == 117 && g.x1 == 126 && g.ax == 122 && g.ay == 73 - 10 * k && g.y0 == 69 - 10 * k && g.y1 == 77 - 10 * k);
        g = geo((uint8_t)(DOZEN + k), false);
        CHECK(g.x0 == 9 + 36 * k && g.x1 == 43 + 36 * k && g.ax == 26 + 36 * k && g.ay == 83 && g.y0 == 79 && g.y1 == 87);
    }
    for (int k = 0; k < 6; k++) {
        g = geo((uint8_t)(BET_LOW + k), false);
        CHECK(g.x0 == 9 + 18 * k && g.x1 == 25 + 18 * k && g.ax == 17 + 18 * k && g.ay == 93 && g.y0 == 89 && g.y1 == 97);
    }
    g = geo(CLR, false); CHECK(g.x0 == 0 && g.x1 == 16 && g.ax == 8 && g.ay == 119 && g.y0 == 113 && g.y1 == 126);
    for (int k = 0; k < 5; k++) {
        g = geo((uint8_t)(CHIP0 + k), false);
        CHECK(g.x0 == 18 + 17 * k && g.x1 == 33 + 17 * k && g.ax == 26 + 17 * k && g.ay == 119);
    }
    g = geo(SPIN, false); CHECK(g.x0 == 104 && g.x1 == 127 && g.ax == 115 && g.ay == 119);
    for (uint8_t id = CLR; id < SPIN; id++) CHECK(geo(id, false).x1 < geo((uint8_t)(id + 1), false).x0);

    // Ids and words.
    CHECK_EQ(S(17), lat(11, 3)); CHECK_EQ(S(1), lat(1, 1)); CHECK_EQ(S(36), lat(23, 5));
    CHECK_EQ(S(0), ZERO); CHECK_EQ(S(37), DZERO);
    struct { uint8_t id; bool us; const char *name, *nums; } W[] = {
        {S(17), 0, "STRAIGHT", "17"}, {ZERO, 0, "STRAIGHT", "0"}, {DZERO, 1, "STRAIGHT", "00"},
        {lat(12, 3), 0, "SPLIT", "17/20"}, {lat(11, 4), 0, "SPLIT", "17/18"}, {ZERO_DZERO, 1, "SPLIT", "0/00"},
        {lat(11, 0), 0, "STREET", "16-18"}, {lat(10, 0), 0, "SIX LINE", "13-18"},
        {lat(12, 4), 0, "CORNER", "17/18/20/21"},
        {lat(0, 0), 0, "FIRST FOUR", "0/1/2/3"}, {lat(0, 0), 1, "TOP LINE", "0/00/1/2/3"},
        {lat(0, 1), 0, "SPLIT", "0/1"}, {lat(0, 1), 1, "SPLIT", "0/1"},
        {lat(0, 2), 0, "TRIO", "0/1/2"}, {lat(0, 2), 1, "TRIO", "0/1/2"},
        {lat(0, 3), 0, "SPLIT", "0/2"}, {lat(0, 3), 1, "TRIO", "0/00/2"},
        {lat(0, 4), 0, "TRIO", "0/2/3"}, {lat(0, 4), 1, "TRIO", "00/2/3"},
        {lat(0, 5), 0, "SPLIT", "0/3"}, {lat(0, 5), 1, "SPLIT", "00/3"},
        {COLUMN, 0, "1st COLUMN", ""}, {COLUMN + 1, 0, "2nd COLUMN", ""}, {COLUMN + 2, 0, "3rd COLUMN", ""},
        {DOZEN, 0, "1st DOZEN", "1-12"}, {DOZEN + 1, 0, "2nd DOZEN", "13-24"}, {DOZEN + 2, 1, "3rd DOZEN", "25-36"},
        {BET_LOW, 0, "LOW", "1-18"}, {BET_EVEN, 0, "EVEN", ""}, {BET_RED, 0, "RED", ""},
        {BET_BLACK, 0, "BLACK", ""}, {BET_ODD, 0, "ODD", ""}, {BET_HIGH, 1, "HIGH", "19-36"},
        {CLR, 0, "CLEAR", ""}, {CHIP0, 0, "$1", ""}, {CHIP0 + 1, 0, "$5", ""}, {CHIP0 + 2, 0, "$10", ""},
        {CHIP0 + 3, 0, "$25", ""}, {CHIP0 + 4, 0, "$100", ""}, {SPIN, 1, "SPIN", ""},
    };
    for (auto &t : W) {
        char nums[32];
        char *end = numbers(nums, t.id, t.us);
        CHECK_STR(kindName(t.id, t.us), t.name);
        CHECK_STR(nums, t.nums);
        CHECK_EQ(end - nums, (long)strlen(t.nums));
    }
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------
static bool isLine(uint8_t id) { return id < ZERO && !((id / 24) & (id % 24) & 1); }
static bool isCoarse(uint8_t id) { return !isLine(id) && id != ZERO_DZERO; }

// One step from glove g; NONE if it stays.
static uint8_t go1(nav::Glove g, int dx, int dy, bool tap, bool us) {
    return nav::step(g, (int8_t)dx, (int8_t)dy, tap, us) ? g.spot : NONE;
}

static const int DIRS[8][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

static void testNavTable() {
    // layout.md section 3, "Simulated results": tap U D L R, run U D L R.
    struct Row { const char *from; uint8_t spot; bool us; uint8_t want[8]; };
    const Row T[] = {
        {"17", S(17), 0, {lat(11, 4), lat(11, 2), lat(10, 3), lat(12, 3), S(18), S(16), S(14), S(20)}},
        {"17/18/20/21", lat(12, 4), 0, {lat(12, 5), lat(12, 3), lat(11, 4), lat(13, 4), S(21), S(20), S(18), S(21)}},
        {"16-18 street", lat(11, 0), 0, {S(16), DOZEN + 1, lat(10, 0), lat(12, 0), S(16), DOZEN + 1, S(13), S(19)}},
        {"0", ZERO, 0, {lat(0, 4), lat(0, 2), COLUMN + 1, lat(0, 3), S(3), DOZEN, NONE, S(2)}},
        {"0/1/2/3", lat(0, 0), 0, {lat(0, 1), DOZEN, lat(23, 0), lat(1, 0), S(1), DOZEN, ZERO, S(1)}},
        {"US 0", ZERO, 1, {lat(0, 2), lat(0, 1), COLUMN, lat(0, 1), DZERO, DOZEN, NONE, S(1)}},
        {"US 00", DZERO, 1, {lat(0, 5), lat(0, 4), COLUMN + 2, lat(0, 5), NONE, ZERO, NONE, S(3)}},
        {"US 0/00", ZERO_DZERO, 1, {DZERO, ZERO, COLUMN + 1, lat(0, 3), DZERO, ZERO, NONE, S(2)}},
        {"36", S(36), 0, {SPIN, lat(23, 4), lat(22, 5), COLUMN + 2, NONE, S(35), S(33), COLUMN + 2}},
        {"2:1 bot", COLUMN, 0, {COLUMN + 1, DOZEN + 2, S(34), ZERO, COLUMN + 1, DOZEN + 2, S(34), NONE}},
        {"2nd 12", DOZEN + 1, 0, {lat(12, 0), BET_RED, DOZEN, DOZEN + 2, S(16), BET_RED, DOZEN, DOZEN + 2}},
        {"RED", BET_RED, 0, {DOZEN + 1, CHIP0 + 2, BET_EVEN, BET_BLACK, DOZEN + 1, CHIP0 + 2, BET_EVEN, BET_BLACK}},
        {"CLR", CLR, 0, {BET_LOW, lat(0, 5), SPIN, CHIP0, BET_LOW, NONE, NONE, CHIP0}},
        {"SPIN", SPIN, 0, {BET_HIGH, S(36), CHIP0 + 4, CLR, BET_HIGH, NONE, CHIP0 + 4, NONE}},
    };
    static const char *COL[8] = {"tap U", "tap D", "tap L", "tap R", "run U", "run D", "run L", "run R"};
    int bad = 0;
    for (const Row &row : T) {
        for (int i = 0; i < 8; i++) {
            const int *d = DIRS[i & 3];
            uint8_t got = go1(nav::at(row.spot, row.us), d[0], d[1], i < 4, row.us);
            checks++;
            if (got != row.want[i]) {
                fails++; bad++;
                printf("FAIL nav table: from %s %s: want %s, got %s\n", row.from, COL[i],
                       label(row.want[i], row.us), label(got, row.us));
            }
        }
    }
    if (!bad) printf("nav: layout.md section 3 table reproduced (%d moves)\n", (int)(sizeof T / sizeof T[0]) * 8);

    // In a wide cell the glove keeps its x: "the six line under the glove x"
    // and "RED / BLACK by glove x".
    for (int x = 49; x <= 75; x++) {
        nav::Glove g = nav::at(DOZEN + 1, false);
        g.x = (uint8_t)x;
        uint8_t up = go1(g, 0, -1, true, false);
        CHECK(up < 24);                                     // the bottom edge, v = 0
        int best = 999;
        for (int u = 0; u < 24; u++) { int dx = abs(geo(lat(u, 0), false).ax - x); if (dx < best) best = dx; }
        CHECK_EQ(abs(geo(up, false).ax - x), best);
        uint8_t down = go1(g, 0, 1, true, false);
        CHECK_EQ(down, x < 62 ? BET_RED : x > 62 ? BET_BLACK : BET_RED);
    }
    // Runs down and back up come home (sticky x): every bottom-row number,
    // and from each dozen at any x through the even-money row.
    for (int c = 1; c <= 12; c++) {
        nav::Glove g = nav::at(S(3 * c - 2), false);
        CHECK(nav::step(g, 0, 1, false, false) && g.spot >= DOZEN && g.spot < DOZEN + 3);
        CHECK(nav::step(g, 0, -1, false, false));
        CHECK_EQ(g.spot, S(3 * c - 2));
    }
    for (int k = 0; k < 3; k++)
        for (int x = 13 + 36 * k; x <= 39 + 36 * k; x++) {
            nav::Glove g = nav::at((uint8_t)(DOZEN + k), false);
            g.x = (uint8_t)x;
            CHECK(nav::step(g, 0, 1, false, false) && g.spot >= BET_LOW && g.spot < NBET);
            CHECK(nav::step(g, 0, -1, false, false));
            CHECK_EQ(g.spot, DOZEN + k);
        }
    // Holding RIGHT from 1 runs along the bottom row and stops at its 2:1.
    nav::Glove g = nav::at(S(1), false);
    for (int n = 4; n <= 34; n += 3) { CHECK(nav::step(g, 1, 0, false, false)); CHECK_EQ(g.spot, S(n)); }
    CHECK(nav::step(g, 1, 0, false, false)); CHECK_EQ(g.spot, COLUMN);
    nav::Glove h = g;
    CHECK(!nav::step(g, 1, 0, false, false));
    CHECK(g.spot == h.spot && g.x == h.x && g.y == h.y);
    // A diagonal tap from 17 up-right lands on the corner 17/18/20/21.
    CHECK_EQ(go1(nav::at(S(17), false), 1, -1, true, false), lat(12, 4));
}

// Every glove state reachable by taps (spot plus the x it keeps in a wide
// cell): all 8 directions handled, taps reach every spot from every state,
// runs never stop on a line.
static void testNavGraph() {
    for (int w = 0; w < 2; w++) {
        bool us = w;
        static uint8_t seen[NSPOT][128];
        static nav::Glove states[NSPOT * 128];
        static int16_t next[NSPOT * 128][8];
        static int16_t index[NSPOT][128];
        memset(seen, 0, sizeof seen);
        int n = 0, bad = 0;
        for (uint8_t id = 0; id < NSPOT; id++)
            if (valid(id, us)) {
                nav::Glove g = nav::at(id, us);
                if (!seen[id][g.x]) { seen[id][g.x] = 1; index[id][g.x] = (int16_t)n; states[n++] = g; }
            }
        for (int i = 0; i < n; i++) {
            for (int d = 0; d < 8; d++) {
                nav::Glove g = states[i];
                bool ok = nav::step(g, (int8_t)DIRS[d][0], (int8_t)DIRS[d][1], true, us);
                if (!ok || !valid(g.spot, us) || g.spot == states[i].spot || g.x > 127) { bad++; next[i][d] = -1; continue; }
                if (!seen[g.spot][g.x]) { seen[g.spot][g.x] = 1; index[g.spot][g.x] = (int16_t)n; states[n++] = g; }
                next[i][d] = index[g.spot][g.x];
                // The same press held: whole cells only, or nowhere.
                nav::Glove r = states[i];
                if (nav::step(r, (int8_t)DIRS[d][0], (int8_t)DIRS[d][1], false, us)) {
                    if (!valid(r.spot, us) || !isCoarse(r.spot) || r.spot == states[i].spot) {
                        bad++;
                        printf("   run from %s -> %s\n", label(states[i].spot, us), label(r.spot, us));
                    }
                }
            }
        }
        CHECK_EQ(bad, 0);
        // Forward from 17 reaches every spot; backward from 17 reaches every
        // state: so every state reaches every spot.
        uint8_t root = 0;
        for (int i = 0; i < n; i++) if (states[i].spot == S(17)) root = (uint8_t)i;
        static uint8_t fw[NSPOT * 128], bw[NSPOT * 128];
        memset(fw, 0, sizeof fw); memset(bw, 0, sizeof bw);
        static int16_t queue[NSPOT * 128];
        int qh = 0, qt = 0;
        fw[root] = 1; queue[qt++] = root;
        while (qh < qt) {
            int i = queue[qh++];
            for (int d = 0; d < 8; d++) { int j = next[i][d]; if (j >= 0 && !fw[j]) { fw[j] = 1; queue[qt++] = (int16_t)j; } }
        }
        bool spotSeen[NSPOT] = {false};
        for (int i = 0; i < n; i++) if (fw[i]) spotSeen[states[i].spot] = true;
        for (uint8_t id = 0; id < NSPOT; id++) if (valid(id, us) && !spotSeen[id]) { CHECK(!"tap unreachable"); printf("   %s\n", label(id, us)); }
        bool grew = true;
        bw[root] = 1;
        while (grew) {
            grew = false;
            for (int i = 0; i < n; i++)
                if (!bw[i])
                    for (int d = 0; d < 8; d++) { int j = next[i][d]; if (j >= 0 && bw[j]) { bw[i] = 1; grew = true; break; } }
        }
        int stuck = 0;
        for (int i = 0; i < n; i++) stuck += !bw[i];
        CHECK_EQ(stuck, 0);
        printf("nav %s: %d glove states, every spot reachable by taps from each\n", us ? "US" : "EU", n);
    }
}

// ---------------------------------------------------------------------------
// The rules
// ---------------------------------------------------------------------------
static void testFlow() {
    Roulette r{};
    r.seed(5);
    r.newGame();
    drain(r);
    CHECK(r.phase == Phase::Welcome);
    CHECK_EQ(r.purse, START_PURSE);
    CHECK_EQ(r.chip, 1);
    CHECK_EQ(r.cursor, S(17));
    CHECK(!r.us);
    CHECK(hasSay(L_WELCOME, F_SMILE));
    CHECK_EQ(until(r, Phase::Betting), 31);
    r.opt.pace = PACE_QUICK;
    r.opt.wheel = WHEEL_AMERICAN;
    r.newGame();
    CHECK(r.us);
    CHECK_EQ(until(r, Phase::Betting), 16);

    // The glove: taps half a cell, held runs whole cells, two at once a diagonal.
    fresh(r);
    tick(r, RIGHT_BUTTON);
    CHECK_EQ(r.cursor, lat(12, 3));
    CHECK_EQ(countEv(Ev::Cursor), 1);
    CHECK_EQ(lastEv(Ev::Cursor)->a, lat(12, 3));
    moveTo(r, S(17));
    tick(r, 0, RIGHT_BUTTON);
    CHECK_EQ(r.cursor, S(20));
    moveTo(r, S(17));
    tick(r, UP_BUTTON | RIGHT_BUTTON);
    CHECK_EQ(r.cursor, lat(12, 4));
    CHECK_EQ(r.gx, 62); CHECK_EQ(r.gy, 58);
    moveTo(r, S(36));
    clearLog();
    tick(r, 0, UP_BUTTON);                          // a run stops at the edge, silently
    CHECK_EQ(r.cursor, S(36));
    CHECK_EQ(nEv, 0);
    CHECK_EQ(r.dropped(), 0);
}

static void testLimits() {
    Roulette r{};
    fresh(r);
    // Inside: $100 a spot.
    r.chip = 4;
    pressOn(r, S(17), A_BUTTON);
    CHECK_EQ(r.bet[S(17)], 100);
    CHECK_EQ(r.purse, 400);
    const Event *e = lastEv(Ev::BetAdd);
    CHECK(e && e->a == S(17) && e->b == 4 && e->amount == 100);
    clearLog();
    tick(r, A_BUTTON);
    e = lastEv(Ev::Deny);
    CHECK(e && e->a == D_SPOT_MAX && e->b == S(17));
    CHECK_EQ(r.purse, 400);
    r.chip = 0;
    clearLog();
    tick(r, A_BUTTON);
    CHECK(lastEv(Ev::Deny) && lastEv(Ev::Deny)->a == D_SPOT_MAX);
    // Outside: $250 a spot.
    r.chip = 4;
    pressOn(r, BET_RED, A_BUTTON);
    tick(r, 0, A_BUTTON);                           // held A repeats
    CHECK_EQ(r.bet[BET_RED], 200);
    clearLog();
    tick(r, 0, A_BUTTON);
    CHECK(lastEv(Ev::Deny) && lastEv(Ev::Deny)->a == D_SPOT_MAX);
    r.chip = 3;
    tick(r, A_BUTTON); tick(r, A_BUTTON);
    CHECK_EQ(r.bet[BET_RED], 250);
    clearLog();
    tick(r, A_BUTTON);
    CHECK(lastEv(Ev::Deny) && lastEv(Ev::Deny)->a == D_SPOT_MAX);
    CHECK_EQ(r.purse, 150);
    CHECK_EQ(r.spotMax(S(17)), INSIDE_MAX); CHECK_EQ(r.spotMax(lat(0, 0)), INSIDE_MAX);
    CHECK_EQ(r.spotMax(ZERO), INSIDE_MAX); CHECK_EQ(r.spotMax(COLUMN), OUTSIDE_MAX);
    CHECK_EQ(r.spotMax(BET_HIGH), OUTSIDE_MAX); CHECK_EQ(r.spotMax(SPIN), 0);
    // The table: $1000.
    r.purse += 5000;
    r.chip = 4;
    for (int n = 1; n <= 6; n++) pressOn(r, S(n), A_BUTTON);
    r.chip = 3;
    pressOn(r, BET_BLACK, A_BUTTON); tick(r, A_BUTTON);
    CHECK_EQ(r.onTable(), 1000);
    r.chip = 0;
    clearLog();
    pressOn(r, S(36), A_BUTTON);
    CHECK(lastEv(Ev::Deny) && lastEv(Ev::Deny)->a == D_TABLE_MAX && lastEv(Ev::Deny)->b == S(36));
    CHECK_EQ(r.bet[S(36)], 0);
    // The purse.
    fresh(r);
    r.chip = 4;
    for (int n = 1; n <= 5; n++) pressOn(r, S(n), A_BUTTON);
    CHECK_EQ(r.purse, 0);
    CHECK_EQ(r.chip, 4);                            // nothing affordable: it stays
    clearLog();
    pressOn(r, S(6), A_BUTTON);
    CHECK(lastEv(Ev::Deny) && lastEv(Ev::Deny)->a == D_NO_CASH);
    pressOn(r, CHIP0, A_BUTTON);                    // a chip button the purse can't pay
    CHECK(lastEv(Ev::Deny) && lastEv(Ev::Deny)->a == D_NO_CASH && lastEv(Ev::Deny)->b == CHIP0);
    // place() applies the limits too.
    fresh(r);
    r.place(S(17), 101);
    CHECK_EQ(r.bet[S(17)], 0);
    r.place(BET_ODD, 250);
    CHECK_EQ(r.bet[BET_ODD], 250);
    r.place(DZERO, 5);                              // not on a European table
    CHECK_EQ(r.bet[DZERO], 0);
    r.place(SPIN, 5);
    CHECK_EQ(r.purse, 250);
    CHECK_EQ(r.dropped(), 0);
}

static void testChips() {
    Roulette r{};
    fresh(r);
    // Chip buttons.
    pressOn(r, CHIP0 + 3, A_BUTTON);
    CHECK_EQ(r.chip, 3);
    CHECK(lastEv(Ev::ChipSel) && lastEv(Ev::ChipSel)->a == 3);
    clearLog();
    tick(r, 0, A_BUTTON);                           // held A on a button: nothing
    CHECK_EQ(nEv, 0);
    // SELECT cycles through what the purse can afford.
    static const uint8_t CYCLE[5] = {4, 0, 1, 2, 3};
    for (int i = 0; i < 5; i++) { tick(r, SELECT_BUTTON); CHECK_EQ(r.chip, CYCLE[i]); }
    r.place(BET_RED, 250); r.place(BET_BLACK, 238);
    CHECK_EQ(r.purse, 12);
    r.chip = 1;
    tick(r, SELECT_BUTTON); CHECK_EQ(r.chip, 2);
    tick(r, SELECT_BUTTON); CHECK_EQ(r.chip, 0);    // $25 and $100 skipped
    tick(r, SELECT_BUTTON); CHECK_EQ(r.chip, 1);
    r.place(BET_EVEN, 12);
    clearLog();
    tick(r, SELECT_BUTTON);                         // nothing affordable
    CHECK_EQ(r.chip, 1);
    CHECK_EQ(countEv(Ev::ChipSel), 0);
    // An unaffordable chip drops to the largest affordable one.
    fresh(r);
    r.place(BET_RED, 250); r.place(BET_BLACK, 220);
    r.chip = 3;
    pressOn(r, S(17), A_BUTTON);                    // $25 of $30: $5 left
    CHECK_EQ(r.purse, 5);
    CHECK_EQ(r.chip, 1);
    CHECK(lastEv(Ev::ChipSel) && lastEv(Ev::ChipSel)->a == 1);
    CHECK_EQ(r.dropped(), 0);
}

static void testClearAndTake() {
    Roulette r{};
    fresh(r);
    r.place(S(17), 10); r.place(BET_RED, 20);
    CHECK_EQ(r.purse, 470);
    clearLog();
    pressOn(r, CLR, A_BUTTON);
    const Event *e = lastEv(Ev::ClearArmed);
    CHECK(e && e->amount == 30);
    CHECK_EQ(r.clrArm, CLR_ARM_FRAMES);
    CHECK_EQ(r.purse, 470);
    tick(r, A_BUTTON);
    e = lastEv(Ev::Clear);
    CHECK(e && e->amount == 30);
    CHECK_EQ(r.purse, 500);
    CHECK_EQ(r.onTable(), 0);
    CHECK_EQ(r.clrArm, 0);
    // A move disarms.
    r.place(S(17), 10);
    pressOn(r, CLR, A_BUTTON);
    tick(r, RIGHT_BUTTON);
    CHECK_EQ(r.cursor, CHIP0);
    CHECK_EQ(r.clrArm, 0);
    tick(r, LEFT_BUTTON);
    CHECK_EQ(r.cursor, CLR);
    clearLog();
    tick(r, A_BUTTON);                              // arms again, no clear
    CHECK_EQ(countEv(Ev::Clear), 0);
    CHECK_EQ(countEv(Ev::ClearArmed), 1);
    // B disarms (and is not a deny).
    clearLog();
    tick(r, B_BUTTON);
    CHECK_EQ(r.clrArm, 0);
    CHECK_EQ(countEv(Ev::Deny), 0);
    CHECK_EQ(r.onTable(), 10);
    // So does time.
    tick(r, A_BUTTON);
    for (int i = 0; i < 85; i++) tick(r);
    clearLog();
    tick(r, A_BUTTON);
    CHECK_EQ(countEv(Ev::Clear), 1);
    r.place(S(17), 10);
    tick(r, A_BUTTON);
    for (int i = 0; i < CLR_ARM_FRAMES; i++) tick(r);
    CHECK_EQ(r.clrArm, 0);
    clearLog();
    tick(r, A_BUTTON);
    CHECK_EQ(countEv(Ev::Clear), 0);
    CHECK_EQ(countEv(Ev::ClearArmed), 1);
    // Nothing to clear.
    fresh(r);
    clearLog();
    pressOn(r, CLR, A_BUTTON);
    e = lastEv(Ev::Deny);
    CHECK(e && e->a == D_NOTHING && e->b == CLR);
    CHECK_EQ(r.clrArm, 0);
    // B takes back min(bet, chip).
    fresh(r);
    r.place(S(17), 12);
    r.chip = 1;
    pressOn(r, S(17), B_BUTTON);
    CHECK_EQ(r.bet[S(17)], 7);
    e = lastEv(Ev::BetRemove);
    CHECK(e && e->a == S(17) && e->amount == 5);
    tick(r, 0, B_BUTTON);                           // held B repeats
    CHECK_EQ(r.bet[S(17)], 2);
    tick(r, B_BUTTON);
    CHECK_EQ(r.bet[S(17)], 0);
    CHECK_EQ(lastEv(Ev::BetRemove)->amount, 2);
    CHECK_EQ(r.purse, 500);
    clearLog();
    tick(r, B_BUTTON);
    e = lastEv(Ev::Deny);
    CHECK(e && e->a == D_NOTHING && e->b == S(17));
    clearLog();
    pressOn(r, CHIP0 + 2, B_BUTTON);
    CHECK(lastEv(Ev::Deny) && lastEv(Ev::Deny)->a == D_NOTHING);
    CHECK_EQ(r.dropped(), 0);
}

static void testSpinFlow() {
    Roulette r{};
    fresh(r);
    clearLog();
    pressOn(r, SPIN, A_BUTTON);
    const Event *e = lastEv(Ev::Deny);
    CHECK(e && e->a == D_NO_BET && e->b == SPIN);
    CHECK(r.phase == Phase::Betting);
    r.place(S(17), 10);
    r.force(17);
    clearLog();
    tick(r, A_BUTTON);
    CHECK(r.phase == Phase::NoMoreBets);
    CHECK_EQ(r.number, 17);
    e = lastEv(Ev::NoMoreBets);
    CHECK(e && e->a == 17);
    CHECK(hasSay(L_NO_MORE, F_RAISED));
    CHECK_EQ(until(r, Phase::Spin), 41);
    clearLog();
    tick(r);
    CHECK(lastEv(Ev::Spin) && lastEv(Ev::Spin)->a == 17);
    for (int i = 0; i < 2; i++) { tick(r); CHECK(r.phase == Phase::Spin); }   // not before the presenter has it
    for (int i = 0; i < 100; i++) tick(r, 0, 0, true);
    CHECK(r.phase == Phase::Spin);
    tick(r);
    CHECK(r.phase == Phase::Result);
    clearLog();
    tick(r);
    CHECK(lastEv(Ev::Result) && lastEv(Ev::Result)->a == 17);
    e = lastEv(Ev::Say);
    CHECK(e && e->a == L_RESULT && e->b == F_NORMAL && e->c == 17);
    CHECK_EQ(r.stats.spins, 1);
    CHECK_EQ(r.history[0], 17);
    CHECK_EQ(r.nHist, 1);
    CHECK_EQ(until(r, Phase::Settle), 31);
    clearLog();
    tick(r);
    e = lastEv(Ev::Settle);
    CHECK(e && e->a == 17 && e->amount == 350);
    CHECK_EQ(r.bet[S(17)], 10);                     // the stake stays up
    CHECK_EQ(r.purse, 490 + 350);
    for (int i = 0; i < 2; i++) { tick(r); CHECK(r.phase == Phase::Settle); }
    for (int i = 0; i < 100; i++) { tick(r, 0, 0, true); CHECK(r.phase == Phase::Settle); }
    tick(r);
    CHECK(r.phase == Phase::EndOfSpin);
    clearLog();
    tick(r);
    CHECK(r.phase == Phase::Betting);
    e = lastEv(Ev::Rebet);
    CHECK(e && e->amount == 0);
    CHECK(hasSay(L_PLACE, F_NORMAL));
    CHECK_EQ(r.cursor, SPIN);
    CHECK_EQ(r.gx, 115); CHECK_EQ(r.gy, 119);
    // Buttons outside betting do nothing.
    r.force(3);
    tick(r, A_BUTTON);
    clearLog();
    for (int i = 0; i < 30; i++) tick(r, (uint8_t)(A_BUTTON | B_BUTTON | SELECT_BUTTON | DPAD));
    CHECK_EQ(countEv(Ev::Cursor) + countEv(Ev::BetAdd) + countEv(Ev::BetRemove) + countEv(Ev::ChipSel) + countEv(Ev::Deny), 0);
    // Forced numbers queue, in order; a 00 on a European wheel is skipped.
    fresh(r);
    r.place(BET_RED, 5);
    r.force(5); r.force(wheel::N00); r.force(0);
    spinTo(r, 9);
    CHECK_EQ(r.history[0], 5);
    spinTo(r, 9);
    CHECK_EQ(r.history[0], 0);
    spinTo(r, 9);
    CHECK_EQ(r.history[0], 9);
    fresh(r, true);
    r.place(BET_RED, 5);
    spinTo(r, wheel::N00);
    CHECK_EQ(r.history[0], wheel::N00);
    CHECK_EQ(r.stats.hits[wheel::N00], 1);
    CHECK_EQ(r.dropped(), 0);
}

// $10 on one spot, one number: the Settle amount, and the layout and purse after.
static void checkBet(bool us, uint8_t s, uint8_t n, int32_t wantWin) {
    Roulette r{};
    fresh(r, us);
    r.place(s, 10);
    spinTo(r, n);
    const Event *e = lastEv(Ev::Settle);
    CHECK(e && e->a == n);
    if (!e) return;
    checks++;
    if (e->amount != wantWin) {
        fails++;
        printf("FAIL settle: $10 on %s, %s: won %ld, want %ld\n", label(s, us), us && n == 37 ? "00" : "", (long)e->amount, (long)wantWin);
    }
    CHECK_EQ(r.bet[s], 10);                         // winners stay; losers go down again
    CHECK_EQ(money(r), 500 + (wantWin ? wantWin : -10));
    CHECK(r.phase == Phase::Betting);
    CHECK_EQ(r.dropped(), 0);
}

static void testSettle() {
    // One of each kind, winning and losing.
    checkBet(false, S(17), 17, 350);   checkBet(false, S(17), 18, 0);
    checkBet(false, ZERO, 0, 350);     checkBet(true, DZERO, 37, 350);    checkBet(true, DZERO, 0, 0);
    checkBet(false, lat(12, 3), 20, 170); checkBet(false, lat(12, 3), 21, 0);
    checkBet(true, ZERO_DZERO, 37, 170);  checkBet(true, ZERO_DZERO, 0, 170); checkBet(true, ZERO_DZERO, 1, 0);
    checkBet(false, lat(11, 0), 16, 110); checkBet(false, lat(11, 0), 19, 0);
    checkBet(false, lat(0, 2), 0, 110);   checkBet(true, lat(0, 3), 37, 110); checkBet(true, lat(0, 3), 3, 0);
    checkBet(false, lat(12, 4), 21, 80);  checkBet(false, lat(12, 4), 22, 0);
    checkBet(false, lat(0, 0), 3, 80);    checkBet(false, lat(0, 0), 4, 0);
    checkBet(true, lat(0, 0), 37, 60);    checkBet(true, lat(0, 0), 2, 60);   checkBet(true, lat(0, 0), 4, 0);
    checkBet(false, lat(10, 0), 13, 50);  checkBet(false, lat(10, 0), 18, 50); checkBet(false, lat(10, 0), 19, 0);
    checkBet(false, COLUMN, 34, 20);      checkBet(false, COLUMN, 35, 0);
    checkBet(false, DOZEN + 1, 24, 20);   checkBet(false, DOZEN + 1, 25, 0);
    checkBet(false, BET_RED, 1, 10);      checkBet(false, BET_RED, 2, 0);
    checkBet(false, BET_BLACK, 2, 10);    checkBet(false, BET_EVEN, 2, 10);   checkBet(false, BET_ODD, 1, 10);
    checkBet(false, BET_LOW, 18, 10);     checkBet(false, BET_HIGH, 19, 10);  checkBet(false, BET_HIGH, 18, 0);
    // The zeros lose every outside bet.
    for (int w = 0; w < 2; w++)
        for (uint8_t s = COLUMN; s < NBET; s++) {
            checkBet(w, s, 0, 0);
            if (w) checkBet(w, s, wheel::N00, 0);
        }

    // $1 on every spot, every number: the winnings are the payouts of the
    // spots that cover it, and over the wheel each spot returns 36 (35).
    for (int w = 0; w < 2; w++) {
        bool us = w;
        Roulette r{};
        fresh(r, us);
        int nb = 0;
        for (uint8_t s = 0; s < NBET; s++) if (valid(s, us)) { r.place(s, 1); drain(r); nb++; }
        CHECK_EQ(r.onTable(), nb);
        long total = 0;
        for (uint8_t n = 0; n < wheel::pockets(us); n++) {
            int32_t want = 0, covered = 0;
            for (uint8_t s = 0; s < NBET; s++) if (valid(s, us) && covers(s, n, us)) { want += payout(s, us); covered++; }
            int32_t m0 = money(r);
            clearLog();
            spinTo(r, n);
            const Event *e = lastEv(Ev::Settle);
            CHECK(e && e->amount == want);
            CHECK_EQ(money(r), m0 + want - (nb - covered));
            CHECK_EQ(r.onTable(), nb);                // all re-placed
            total += want + covered;
        }
        CHECK_EQ(total, 36L * nb - (us ? 1 : 0));
        CHECK_EQ(r.dropped(), 0);
    }

    // Winners stay up, losers are placed again.
    Roulette r{};
    fresh(r);
    r.place(BET_RED, 10); r.place(BET_BLACK, 10); r.place(S(17), 5);
    CHECK_EQ(r.purse, 475);
    clearLog();
    spinTo(r, 17);
    CHECK_EQ(lastEv(Ev::Settle)->amount, 185);
    CHECK_EQ(lastEv(Ev::Rebet)->amount, 10);
    CHECK_EQ(r.purse, 650);
    CHECK(r.bet[BET_RED] == 10 && r.bet[BET_BLACK] == 10 && r.bet[S(17)] == 5);
    CHECK(hasSay(L_BIG_WIN, F_SURPRISED));          // a straight hit
    CHECK_EQ(r.cursor, SPIN);
    // The croupier's lines.
    fresh(r); r.place(BET_RED, 10); clearLog(); spinTo(r, 1);
    CHECK(hasSay(L_WINNER, F_ANGRY));
    clearLog(); spinTo(r, 2);
    CHECK(hasSay(L_HOUSE, F_SMILE));
    fresh(r); r.place(lat(12, 3), 10); clearLog(); spinTo(r, 20);
    CHECK(hasSay(L_BIG_WIN, F_SURPRISED));          // 17 x the stake
    fresh(r); r.place(BET_RED, 10); r.place(BET_BLACK, 10); clearLog(); spinTo(r, 1);
    CHECK(hasSay(L_HOUSE, F_SMILE));                // even: the house thanks you
    fresh(r); r.place(S(17), 1); r.place(BET_RED, 100); clearLog(); spinTo(r, 17);
    CHECK(hasSay(L_BIG_WIN, F_SURPRISED));          // any straight hit, even at a loss
    // Losers that can't all be afforded stay off.
    fresh(r);
    r.place(BET_RED, 245); r.place(BET_BLACK, 245);
    spinTo(r, 1);
    CHECK_EQ(r.purse, 10);
    CHECK(r.bet[BET_RED] == 245 && r.bet[BET_BLACK] == 245);
    clearLog();
    spinTo(r, 0);
    CHECK_EQ(lastEv(Ev::Rebet)->amount, 0);
    CHECK_EQ(r.onTable(), 0);
    CHECK_EQ(r.purse, 10);
    CHECK(r.phase == Phase::Betting);
    CHECK(r.cursor == SPIN);                        // where it was
    CHECK_EQ(r.dropped(), 0);
}

static void testStats() {
    Roulette r{};
    fresh(r);
    r.place(S(17), 10);
    spinTo(r, 17);
    CHECK_EQ(r.stats.spins, 1);
    CHECK_EQ(r.stats.hits[17], 1);
    CHECK_EQ(r.stats.straightHits, 1);
    CHECK_EQ(r.stats.wagered, 10);
    CHECK_EQ(r.stats.spinsWon, 1);
    CHECK_EQ(r.stats.biggestWin, 350);
    CHECK_EQ(r.stats.bestPurse, 850);
    spinTo(r, 18);
    CHECK_EQ(r.stats.spins, 2);
    CHECK_EQ(r.stats.straightHits, 1);
    CHECK_EQ(r.stats.wagered, 20);
    CHECK_EQ(r.stats.spinsWon, 1);
    CHECK_EQ(r.stats.biggestWin, 350);
    CHECK_EQ(r.stats.bestPurse, 850);
    for (int n = 1; n <= 9; n++) spinTo(r, (uint8_t)n);
    CHECK_EQ(r.nHist, 8);
    for (int i = 0; i < 8; i++) CHECK_EQ(r.history[i], 9 - i);
    CHECK_EQ(r.stats.spins, 11);
    CHECK_EQ(r.dropped(), 0);
}

static void testGameOver() {
    Roulette r{};
    // The goal: purse plus layout reaching it.
    static const int32_t GOALS[2] = {1000, 5000};
    for (int g = 0; g < 2; g++) {
        fresh(r, false, (uint8_t)g);
        CHECK_EQ(r.goal(), GOALS[g]);
        r.purse = GOALS[g] - 10;
        r.place(BET_RED, 10);
        clearLog();
        spinTo(r, 2);                               // lose: not yet
        CHECK(r.phase == Phase::Betting);
        r.purse += 10;
        spinTo(r, 1);
        CHECK(r.phase == Phase::GameWon);
        CHECK_EQ(r.purse + r.onTable(), GOALS[g]);
        CHECK_EQ(r.stats.gamesWon, 1);
        CHECK(lastEv(Ev::GameOver) && lastEv(Ev::GameOver)->a == 1);
        for (int i = 0; i < 50; i++) tick(r, A_BUTTON);
        CHECK(r.phase == Phase::GameWon);
    }
    fresh(r, false, GOAL_ENDLESS);
    CHECK_EQ(r.goal(), 0x7FFFFFFF);
    r.purse = 1000000000;
    r.place(BET_RED, 10);
    spinTo(r, 1);
    CHECK(r.phase == Phase::Betting);
    // Broke.
    fresh(r);
    r.place(BET_RED, 250); r.place(BET_BLACK, 250);
    clearLog();
    spinTo(r, 0);
    CHECK(r.phase == Phase::GameLost);
    CHECK_EQ(r.purse, 0);
    CHECK_EQ(r.onTable(), 0);
    CHECK_EQ(r.stats.gamesBroke, 1);
    CHECK(lastEv(Ev::GameOver) && lastEv(Ev::GameOver)->a == 0);
    CHECK_EQ(lastEv(Ev::Rebet)->amount, 0);
    CHECK_EQ(r.dropped(), 0);
}

static void testWheelChange() {
    Roulette r{};
    // Changed from the pause menu while betting: at once, the layout home.
    fresh(r);
    r.place(BET_RED, 50); r.place(S(17), 10);
    r.opt.wheel = WHEEL_AMERICAN;
    clearLog();
    tick(r);
    CHECK(r.us);
    CHECK_EQ(r.onTable(), 0);
    CHECK_EQ(r.purse, 500);
    CHECK(lastEv(Ev::Clear) && lastEv(Ev::Clear)->amount == 60);
    // Changed during a spin: it takes effect at the next betting.
    fresh(r);
    r.place(BET_RED, 50); r.place(S(17), 10);
    clearLog();
    r.force(1);
    pressOn(r, SPIN, A_BUTTON);
    r.opt.wheel = WHEEL_AMERICAN;
    for (int i = 0; i < 2000 && spinning(r.phase); i++) tick(r);
    CHECK(r.us);
    // No rebet onto a table that is about to change: the winner's stake
    // goes home (Clear) before the (empty) Rebet, so a presenter that reads
    // bet[] when it drains the queue still has the chips to fly.
    CHECK_EQ(lastEv(Ev::Rebet)->amount, 0);
    CHECK_EQ(countEv(Ev::Clear), 1);
    CHECK(lastEv(Ev::Clear) && lastEv(Ev::Clear)->amount == 50);
    CHECK(lastEv(Ev::Clear) < lastEv(Ev::Rebet));
    CHECK_EQ(r.onTable(), 0);
    CHECK_EQ(r.purse, 540);
    // Losers that the purse could cover: still not placed again.
    fresh(r);
    r.place(BET_RED, 10); r.place(BET_BLACK, 10);
    clearLog();
    r.force(2);
    pressOn(r, SPIN, A_BUTTON);
    r.opt.wheel = WHEEL_AMERICAN;
    for (int i = 0; i < 2000 && spinning(r.phase); i++) tick(r);
    CHECK(r.us);
    CHECK_EQ(lastEv(Ev::Rebet)->amount, 0);
    CHECK(lastEv(Ev::Clear) && lastEv(Ev::Clear)->amount == 10 && lastEv(Ev::Clear) < lastEv(Ev::Rebet));
    CHECK_EQ(r.onTable(), 0);
    CHECK_EQ(r.purse, 500);                         // RED lost 10, BLACK won 10 and came home
    // An American-only spot under the glove moves to a valid one.
    fresh(r, true);
    r.place(ZERO_DZERO, 5);
    moveTo(r, DZERO);
    r.opt.wheel = WHEEL_EURO;
    r.go(Phase::Betting);
    CHECK(!r.us);
    CHECK(valid(r.cursor, false));
    CHECK_EQ(r.purse, 500);
    fresh(r, true);                                 // on the 0, which moves from y 71 to 63
    moveTo(r, ZERO);
    r.opt.wheel = WHEEL_EURO;
    r.go(Phase::Betting);
    CHECK_EQ(r.cursor, ZERO);
    CHECK_EQ(r.gy, 63);
    // resume() latches the wheel too, and keeps the purse and the layout.
    fresh(r);
    r.place(BET_RED, 20);
    clearLog();
    r.resume();
    drain(r);
    CHECK(r.phase == Phase::Welcome);
    CHECK(hasSay(L_GOOD_LUCK, F_NORMAL));
    CHECK_EQ(r.purse, 480);
    CHECK_EQ(r.bet[BET_RED], 20);
    CHECK_EQ(until(r, Phase::Betting), 31);
    fresh(r, true);
    r.place(ZERO_DZERO, 20);
    r.opt.wheel = WHEEL_EURO;
    clearLog();
    r.resume();
    drain(r);
    CHECK(!r.us);
    CHECK_EQ(r.purse, 500);
    CHECK_EQ(r.onTable(), 0);
    // The presenter resets from the state after resume(): a queued Clear
    // would send the refund home a second time (its purse stuck short).
    CHECK_EQ(nEv, 1);
    CHECK(hasSay(L_GOOD_LUCK, F_NORMAL));
    CHECK_EQ(r.dropped(), 0);
}

// CONTINUE after SAVE & QUIT, the way the screens do it: on the same
// object (Screens: quitNow(), then resume() from the title), and from the
// save at boot (Save::load: loadLayout(), then resume()). quitNow() leaves
// bet[] as the layout to save, already paid back into the purse; CONTINUE
// must not count it twice.
static void testContinue() {
    static const Phase PH[] = {Phase::Welcome, Phase::Betting, Phase::NoMoreBets, Phase::Spin, Phase::Result,
                               Phase::Settle, Phase::EndOfSpin};
    for (int flow = 0; flow < 4; flow++)
        for (int i = 0; i < (int)(sizeof PH / sizeof PH[0]); i++)
            for (int change = 0; change < 2; change++) {
                Phase p = PH[i];
                Roulette r{};
                fresh(r);
                r.place(S(17), 10); r.place(BET_RED, 20); r.place(BET_BLACK, 5);
                r.force(17);
                if (p == Phase::Welcome) r.resume();
                else if (p != Phase::Betting) { pressOn(r, SPIN, A_BUTTON); until(r, p); }
                r.quitNow();
                int32_t want = r.purse;                     // everything the player has
                uint8_t pairs[96];
                uint8_t n = r.saveLayout(pairs, 48);
                uint8_t layout[NBET];
                memcpy(layout, r.bet, sizeof layout);
                if (change) r.opt.wheel = WHEEL_AMERICAN;   // options on the title
                Roulette &c = r;
                Roulette s{};
                if (flow >= 2) {                            // from the save at boot
                    s.opt = r.opt; s.stats = r.stats; s.purse = want;
                    if (flow == 2) { s.loadLayout(pairs, n); s.resume(); }
                    else { s.resume(); s.loadLayout(pairs, n); }
                } else {                                    // same session
                    r.resume();
                    if (flow == 1) r.loadLayout(pairs, n);
                }
                Roulette &t = flow >= 2 ? s : c;
                checks++;
                if (money(t) != want) {
                    fails++;
                    printf("FAIL continue (flow %d, phase %d%s): money %ld, want %ld\n", flow, (int)p,
                           change ? ", wheel changed" : "", (long)money(t), (long)want);
                }
                CHECK(t.phase == Phase::Welcome);
                CHECK_EQ(t.us, change != 0);
                // A wheel change takes the kept layout home; loadLayout()
                // (the save's copy) then places it on the new wheel.
                if (change && flow == 0) CHECK_EQ(t.onTable(), 0);
                else CHECK(memcmp(t.bet, layout, sizeof layout) == 0);
                CHECK_EQ(t.cursor, t.onTable() ? SPIN : S(17));
                // Play on: the money still adds up.
                int32_t m = money(t);
                until(t, Phase::Betting);
                CHECK_EQ(money(t), m);
                if (!t.onTable()) t.place(BET_ODD, 5);
                spinTo(t, 2);
                CHECK(t.phase == Phase::Betting);
                CHECK_EQ(t.dropped(), 0);
            }
    // Can't cover the layout again (a quit mid-spin that lost most of it,
    // then the purse spent elsewhere): it stays off, the purse is kept.
    Roulette r{};
    fresh(r);
    r.place(BET_RED, 250); r.place(BET_BLACK, 200);
    r.quitNow();
    r.purse = 100;
    r.resume();
    CHECK_EQ(r.onTable(), 0);
    CHECK_EQ(r.purse, 100);
    CHECK_EQ(r.cursor, S(17));
}

// SAVE & QUIT in every phase: the purse ends up with everything that is
// the player's, a decided spin settled first; bet[] keeps the layout.
static void testQuit() {
    static const Phase PH[] = {Phase::Welcome, Phase::Betting, Phase::NoMoreBets, Phase::Spin, Phase::Spin,
                               Phase::Result, Phase::Result, Phase::Settle, Phase::Settle, Phase::EndOfSpin};
    for (int i = 0; i < (int)(sizeof PH / sizeof PH[0]); i++) {
        Phase p = PH[i];
        bool later = i > 0 && PH[i - 1] == p;      // the second visit: one tick into the phase
        Roulette r{};
        fresh(r);
        r.place(S(17), 10); r.place(BET_RED, 20); r.place(BET_BLACK, 5);
        r.force(17);
        if (p == Phase::Welcome) r.resume();
        else if (p != Phase::Betting) {
            pressOn(r, SPIN, A_BUTTON);
            until(r, p);
            if (later) tick(r);
            CHECK(r.phase == p);
        }
        bool spun = spinning(p);
        r.quitNow();
        CHECK(r.phase == Phase::Quit);
        checks++;
        int32_t want = spun ? 500 + 355 - 20 : 500;
        if (r.purse != want) { fails++; printf("FAIL quit in phase %d%s: purse %ld, want %ld\n", (int)p, later ? "+1" : "", (long)r.purse, (long)want); }
        CHECK(r.bet[S(17)] == 10 && r.bet[BET_RED] == 20 && r.bet[BET_BLACK] == 5);
        CHECK_EQ(r.stats.spins, spun ? 1 : 0);
        CHECK_EQ(r.stats.hits[17], spun ? 1 : 0);
        CHECK_EQ(r.nHist, spun ? 1 : 0);
        CHECK_EQ(r.stats.wagered, spun ? 35 : 0);
        r.quitNow();                                // once only
        CHECK_EQ(r.purse, want);
        for (int k = 0; k < 20; k++) tick(r, A_BUTTON);
        CHECK(r.phase == Phase::Quit);
        CHECK_EQ(r.purse, want);
        CHECK_EQ(r.dropped(), 0);
    }
    // At the end of a game.
    Roulette r{};
    fresh(r, false, GOAL_1000);
    r.purse = 990;
    r.place(BET_RED, 10);
    spinTo(r, 1);
    CHECK(r.phase == Phase::GameWon);
    r.quitNow();
    CHECK_EQ(r.purse, 1000);
    fresh(r);
    r.place(BET_RED, 250); r.place(BET_BLACK, 250);
    spinTo(r, 0);
    r.quitNow();
    CHECK_EQ(r.purse, 0);
}

static void testSaveLayout() {
    Roulette r{};
    fresh(r);
    r.place(S(17), 10); r.place(BET_RED, 20); r.place(BET_BLACK, 5); r.place(DOZEN, 7);
    uint8_t pairs[96];
    uint8_t n = r.saveLayout(pairs, 48);
    CHECK_EQ(n, 4);
    static const uint8_t WANT[8] = {S(17), 10, DOZEN, 7, BET_RED, 20, BET_BLACK, 5};
    CHECK(memcmp(pairs, WANT, 8) == 0);
    CHECK_EQ(r.saveLayout(pairs, 2), 2);
    n = r.saveLayout(pairs, 48);
    r.quitNow();
    CHECK_EQ(r.purse, 500);
    CHECK_EQ(r.saveLayout(pairs, 48), 4);           // quit keeps the layout to save
    // CONTINUE: placed again, paid from the purse.
    Roulette s{};
    fresh(s);
    clearLog();
    s.loadLayout(pairs, n);
    drain(s);
    CHECK_EQ(s.purse, 458);
    CHECK(memcmp(s.bet, r.bet, sizeof s.bet) == 0);
    CHECK(lastEv(Ev::Rebet) && lastEv(Ev::Rebet)->amount == 42);
    CHECK_EQ(s.cursor, SPIN);
    // Not if the purse can't cover it all.
    fresh(s);
    s.purse = 41;
    s.loadLayout(pairs, n);
    CHECK_EQ(s.onTable(), 0);
    CHECK_EQ(s.purse, 41);
    // Spots the wheel doesn't have are skipped.
    fresh(s);
    static const uint8_t MIX[6] = {DZERO, 5, ZERO_DZERO, 5, S(17), 10};
    s.loadLayout(MIX, 3);
    CHECK_EQ(s.onTable(), 10);
    CHECK_EQ(s.purse, 490);
    fresh(s, true);
    s.loadLayout(MIX, 3);
    CHECK_EQ(s.onTable(), 20);
    // Never past the limits.
    fresh(s);
    static const uint8_t BIG[4] = {S(17), 150, BET_RED, 250};
    s.loadLayout(BIG, 2);
    CHECK_EQ(s.bet[S(17)], 0);
    CHECK_EQ(s.bet[BET_RED], 250);
    CHECK_EQ(s.purse, 250);
    CHECK_EQ(s.dropped(), 0);
}

static void testLines() {
    Roulette r{};
    char buf[64];
    struct { uint8_t line, n; const char *want; } L[] = {
        {L_WELCOME, 0, "WELCOME TO\nTHE WHEEL!"}, {L_GOOD_LUCK, 0, "GOOD LUCK!"},
        {L_PLACE, 0, "PLACE YOUR\nBETS"}, {L_NO_MORE, 0, "NO MORE\nBETS!"},
        {L_RESULT, 17, "17 BLACK"}, {L_RESULT, 0, "0 GREEN"}, {L_RESULT, 37, "00 GREEN"},
        {L_RESULT, 32, "32 RED"}, {L_RESULT, 1, "1 RED"},
        {L_WINNER, 0, "WINNER!"}, {L_BIG_WIN, 0, "INCREDIBLE!"}, {L_HOUSE, 0, "THE HOUSE\nTHANKS YOU"},
    };
    for (auto &t : L) CHECK_STR(r.lineText(t.line, t.n, buf), t.want);
    CHECK_STR(r.lineText(LINE_COUNT, 0, buf), "");
    for (uint8_t line = 0; line < LINE_COUNT; line++)
        for (uint8_t n = 0; n <= 37; n++) {
            const char *s = r.lineText(line, n, buf);
            CHECK(glyphsOk(s) && *s);
            int lines = 1, col = 0, widest = 0;
            for (; *s; s++) {
                if (*s == '\n') { lines++; col = 0; } else if (++col > widest) widest = col;
            }
            CHECK(lines <= 4 && widest <= 12);
            if (line != L_RESULT) break;
        }
}

// Random play: money moves only at Settle (the winnings) and EndOfSpin (the
// losers); the limits hold; every spin finishes; nothing is dropped.
static void testFuzz() {
    static const uint8_t TAP[] = {UP_BUTTON, DOWN_BUTTON, LEFT_BUTTON, RIGHT_BUTTON,
                                  UP_BUTTON | LEFT_BUTTON, DOWN_BUTTON | RIGHT_BUTTON};
    long totalSpins = 0, ticks = 0, seenEv[16] = {0}, denies[5] = {0}, broke = 0, quits = 0;
    int bad = 0;
    for (uint32_t seed = 1; seed <= 40; seed++) {
        Roulette r{};
        r.opt.wheel = (uint8_t)(seed & 1);
        r.opt.goal = GOAL_ENDLESS;
        r.opt.pace = (uint8_t)((seed >> 1) & 1);
        r.seed(seed * 2654435761u);
        r.newGame();
        drain(r);
        uint32_t x = seed * 7919u;
        auto rnd = [&]() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; };
        int spins = 0, inPhase = 0, spinTicks = 0, betTicks = 0, limit = 30;
        int32_t M = money(r), winExp = 0, lostExp = 0;
        Phase last = r.phase;
        for (int f = 0; f < 3000000 && spins < 400; f++) {
            ticks++;
            if (r.phase == Phase::GameLost) {
                r.newGame(); drain(r); M = money(r); last = r.phase;
                continue;
            }
            uint8_t p = 0, h = 0;
            bool busy = (rnd() % 3) == 0;
            uint32_t k = rnd() % 100;
            if (r.phase == Phase::Betting) {
                if (++betTicks > limit) {
                    if (r.onTable() == 0) {
                        uint8_t s;
                        do s = (uint8_t)(rnd() % NBET); while (!valid(s, r.us));
                        moveTo(r, s);
                        p = A_BUTTON;
                    } else { moveTo(r, SPIN); p = A_BUTTON; }
                } else if (k < 30) p = TAP[rnd() % 6];
                else if (k < 45) h = TAP[rnd() % 4];
                else if (k < 60) p = A_BUTTON;
                else if (k < 70) h = A_BUTTON;
                else if (k < 78) p = B_BUTTON;
                else if (k < 82) p = SELECT_BUTTON;
                else if (k < 83) r.opt.wheel ^= 1;
                else if (k < 84) r.opt.pace ^= 1;
            } else {
                p = (uint8_t)(rnd() & 0xBF);       // anything but START
            }
            if (rnd() % 1500 == 0 && r.phase != Phase::GameLost) {
                // SAVE & QUIT here, then CONTINUE: a decided spin is settled
                // first, the layout comes back if the purse covers it, and
                // not a dollar appears or vanishes.
                int32_t want = M;
                if (spinning(r.phase)) {
                    int32_t win = 0, lost = 0;
                    for (uint8_t s = 0; s < NBET; s++) {
                        if (!r.bet[s]) continue;
                        if (covers(s, r.number, r.us)) win += r.bet[s] * payout(s, r.us);
                        else lost += r.bet[s];
                    }
                    bool settled = r.phase == Phase::EndOfSpin || (r.phase == Phase::Settle && r.step);
                    want = M + (settled ? 0 : win) - lost;
                    spins++;                       // finished by the quit
                }
                r.quitNow();
                quits++;
                if (r.purse != want) { bad++; if (bad < 5) printf("   quit: purse %ld, want %ld\n", (long)r.purse, (long)want); }
                uint8_t pairs[96];
                uint8_t n = r.saveLayout(pairs, 48);
                if (rnd() % 4 == 0) r.opt.wheel ^= 1;      // the options, on the title
                if (quits % 3 == 0) {
                    // CONTINUE in the same session (Screens: resume() only).
                    if (r.purse > 0) r.resume(); else r.newGame();
                    drain(r);
                } else {
                    // From the save at boot: Save::load (loadLayout), then
                    // CONTINUE (resume); or the other way round.
                    Roulette s{};
                    s.opt = r.opt; s.stats = r.stats; s.purse = r.purse;
                    memcpy(s.history, r.history, sizeof s.history); s.nHist = r.nHist;
                    s.seed(rnd());
                    if (s.purse <= 0) s.newGame();
                    else if (quits % 3 == 1) { s.loadLayout(pairs, n); s.resume(); }
                    else { s.resume(); s.loadLayout(pairs, n); }
                    drain(s);
                    r = s;
                }
                M = money(r);
                if (want > 0 && M != want) { bad++; if (bad < 5) printf("   continue: money %ld, want %ld\n", (long)M, (long)want); }
                last = r.phase; inPhase = 0; spinTicks = 0;
                continue;
            }
            if (r.phase == Phase::Settle && !r.step) {
                winExp = 0;
                for (uint8_t s = 0; s < NBET; s++) if (r.bet[s] && covers(s, r.number, r.us)) winExp += r.bet[s] * payout(s, r.us);
            }
            Phase before = r.phase;
            if (before == Phase::EndOfSpin) {
                lostExp = 0;
                for (uint8_t s = 0; s < NBET; s++) if (!covers(s, r.number, r.us)) lostExp += r.bet[s];
            }
            r.update(p, (uint8_t)(p | h), busy);
            Event e;
            bool any = false;
            while (r.popEvent(e)) {
                any = true;
                seenEv[(int)e.type & 15]++;
                if (e.type == Ev::Deny) denies[e.a % 5]++;
                if (e.type == Ev::GameOver) broke++;
                if (e.type == Ev::Settle) {
                    if (e.amount != winExp) { bad++; if (bad < 5) printf("   settle %ld, want %ld\n", (long)e.amount, (long)winExp); }
                    M += e.amount;
                }
            }
            if (before == Phase::EndOfSpin && r.phase != Phase::EndOfSpin) {
                M -= lostExp;
                spins++;
                if (spinTicks > 400) { bad++; if (bad < 5) printf("   spin took %d ticks\n", spinTicks); }
            }
            if (money(r) != M) { bad++; if (bad < 5) printf("   seed %u tick %d: money %ld, want %ld (phase %d)\n", seed, f, (long)money(r), (long)M, (int)r.phase); M = money(r); }
            if (r.purse < 0) bad++;
            if (any) {
                if (r.onTable() > TABLE_MAX) bad++;
                for (uint8_t s = 0; s < NBET; s++)
                    if (r.bet[s] > r.spotMax(s) || (r.bet[s] && !valid(s, r.us))) bad++;
                if (!valid(r.cursor, r.us)) bad++;
                // The glove stands on the cursor's spot (its x free only in a wide cell).
                Geo g = geo(r.cursor, r.us);
                bool wide = r.cursor >= DOZEN && r.cursor < NBET;
                if (r.gy != g.ay || (wide ? r.gx < g.x0 + 4 || r.gx > g.x1 - 4 : r.gx != g.ax)) {
                    bad++;
                    if (bad < 5) printf("   glove (%d,%d) off %s\n", r.gx, r.gy, label(r.cursor, r.us));
                }
            }
            if (spinning(r.phase)) spinTicks++;
            if (r.phase == Phase::Betting && before != Phase::Betting) { betTicks = 0; spinTicks = 0; limit = 5 + (int)(rnd() % 60); }
            if (r.phase == last) { if (++inPhase > 20000) { bad++; printf("   stuck in phase %d\n", (int)r.phase); break; } }
            else inPhase = 0;
            last = r.phase;
        }
        CHECK_EQ(spins, 400);
        CHECK_EQ(r.dropped(), 0);
        totalSpins += spins;
    }
    CHECK_EQ(bad, 0);
    printf("fuzz: %ld spins, %ld ticks; %ld chips down, %ld taken back, %ld CLR armed, %ld cleared "
           "(incl. wheel changes), %ld rebets, %ld cursor moves, %ld broke, %ld quit+continue; denies %ld/%ld/%ld/%ld/%ld\n",
           totalSpins, ticks, seenEv[(int)Ev::BetAdd], seenEv[(int)Ev::BetRemove], seenEv[(int)Ev::ClearArmed],
           seenEv[(int)Ev::Clear], seenEv[(int)Ev::Rebet], seenEv[(int)Ev::Cursor], broke, quits,
           denies[0], denies[1], denies[2], denies[3], denies[4]);
    for (int t = 0; t <= (int)Ev::GameOver; t++) CHECK(seenEv[t] > 0);
}

static void testRng() {
    Roulette r{};
    r.seed(0);
    CHECK_EQ(r.rng, 0x9E3779B9u);
    static const double CRIT[2] = {67.985, 69.346};   // chi-square 0.999, df 36 / 37
    for (int w = 0; w < 2; w++) {
        uint32_t m = 37u + (uint32_t)w;
        long cnt[38] = {0};
        const long N = 3700000;
        r.seed(0xC0FFEEu + (uint32_t)w);
        for (long i = 0; i < N; i++) cnt[r.rand32() % m]++;
        double e = (double)N / m, chi = 0;
        for (uint32_t i = 0; i < m; i++) chi += (cnt[i] - e) * (cnt[i] - e) / e;
        CHECK(chi < CRIT[w]);
        printf("rng %%%u: chi-square %.1f (0.999 critical %.1f)\n", m, chi, CRIT[w]);
    }
}

// ---------------------------------------------------------------------------
// --dump: every bet spot, for tools/tests/ref_roulette.py
// ---------------------------------------------------------------------------
static void dump() {
    for (int w = 0; w < 2; w++) {
        bool us = w;
        for (uint8_t id = 0; id < NBET; id++) {
            if (!valid(id, us)) continue;
            char nums[32];
            numbers(nums, id, us);
            printf("%s %d %s|%s|%d|", us ? "US" : "EU", id, kindName(id, us), nums, payout(id, us));
            bool first = true;
            for (uint8_t n = 0; n < wheel::pockets(us); n++)
                if (covers(id, n, us)) { printf(first ? "%d" : ",%d", n); first = false; }
            printf("\n");
        }
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--dump")) { dump(); return 0; }
    testWheel();
    testSpots();
    testNavTable();
    testNavGraph();
    testFlow();
    testLimits();
    testChips();
    testClearAndTake();
    testSpinFlow();
    testSettle();
    testStats();
    testGameOver();
    testWheelChange();
    testQuit();
    testContinue();
    testSaveLayout();
    testLines();
    testFuzz();
    testRng();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
