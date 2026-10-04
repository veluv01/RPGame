// Host tests: the flash dictionary against the list it was built from, the
// rules against a second implementation, whole games between two CPUs, and
// saving. Run from the sketch folder (rpgame test does).
//
//   test_words.exe [--quick]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <string>
#include <vector>
#include "../../Ai.h"
#include "../../Dict.h"
#include "../../src/dict/DictData.h"
#include "../../Game.h"
#include "../../Words.h"

static int fails = 0, checks = 0;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long long _a = (a), _b = (b); if (_a != _b) { fails++; \
    printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

// The card is not part of these tests: the flash list answers for it.
namespace dict {
bool has(const uint8_t *w, uint8_t n) { return hasCore(w, n); }
}

static std::set<std::string> core, enable;

static void loadList(const char *path, std::set<std::string> &into) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof line, f)) {
        size_t n = strcspn(line, "\r\n");
        line[n] = 0;
        if (n) into.insert(line);
    }
    fclose(f);
}

static bool hasCore(const std::string &s) {
    uint8_t w[32];
    for (size_t i = 0; i < s.size(); i++) w[i] = (uint8_t)(s[i] - 'a' + 1);
    return dict::hasCore(w, (uint8_t)s.size());
}

static uint32_t seed = 12345;
static uint32_t rnd() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

// ---------------------------------------------------------------------------
static void testDictionary() {
    CHECK_EQ(core.size(), DICT_WORDS);
    // Every word is found.
    int missing = 0;
    for (const auto &w : core) missing += !hasCore(w);
    CHECK_EQ(missing, 0);
    // The scan gives each word once and nothing else.
    dict::Cursor c;
    dict::rewind(c);
    uint8_t w[16], n;
    std::set<std::string> seen;
    int strangers = 0, twice = 0;
    while (dict::next(c, w, n)) {
        std::string s;
        for (uint8_t i = 0; i < n; i++) s += (char)('a' + w[i] - 1);
        strangers += !core.count(s);
        twice += !seen.insert(s).second;
    }
    CHECK_EQ(strangers, 0);
    CHECK_EQ(twice, 0);
    CHECK_EQ(seen.size(), core.size());
    // Near misses are not: a letter changed, dropped, added; a suffix added.
    int wrong = 0, tried = 0;
    static const char *const SUF[] = {"s", "ed", "ing", "d", "er", "ly", "es"};
    for (const auto &word : core) {
        for (int k = 0; k < 12; k++) {
            std::string s = word;
            size_t at = rnd() % s.size();
            switch (k % 4) {
                case 0: s[at] = (char)('a' + rnd() % 26); break;
                case 1: s.erase(at, 1); break;
                case 2: s.insert(at, 1, (char)('a' + rnd() % 26)); break;
                case 3: s += SUF[rnd() % 7]; break;
            }
            if (s.size() < 2 || s.size() > 15) continue;
            tried++;
            wrong += hasCore(s) != (core.count(s) != 0);
        }
    }
    CHECK_EQ(wrong, 0);
    // The rest of ENABLE is not in it either.
    int extra = 0;
    for (const auto &word : enable) extra += hasCore(word) != (core.count(word) != 0);
    CHECK_EQ(extra, 0);
    for (const auto &word : core) if (!enable.empty() && !enable.count(word)) { CHECK(!"core word not in ENABLE"); break; }
    printf("dictionary: %d words, %d near misses, %d ENABLE words checked\n", (int)core.size(), tried, (int)enable.size());
}

// ---------------------------------------------------------------------------
// The rules again, the plain way: lay the tiles on a copy of the board and
// look at every row and column.
// ---------------------------------------------------------------------------
struct RefWord { int start, len, step; };
struct Ref { bool ok; int score; std::vector<RefWord> words; };

static Ref reference(const uint8_t *b, const wd::Placement *p, int n) {
    Ref r = {false, 0, {}};
    uint8_t g[225];
    bool fresh[225] = {};
    memcpy(g, b, 225);
    bool opening = true;
    for (int i = 0; i < 225; i++) opening &= !b[i];
    for (int i = 0; i < n; i++) {
        if (g[p[i].cell]) return r;
        g[p[i].cell] = p[i].tile;
        fresh[p[i].cell] = true;
    }
    int r0 = p[0].cell / 15, c0 = p[0].cell % 15, r1 = r0, c1 = c0;
    for (int i = 0; i < n; i++) {
        int rr = p[i].cell / 15, cc = p[i].cell % 15;
        r0 = rr < r0 ? rr : r0; r1 = rr > r1 ? rr : r1;
        c0 = cc < c0 ? cc : c0; c1 = cc > c1 ? cc : c1;
    }
    if (r0 != r1 && c0 != c1) return r;
    for (int rr = r0; rr <= r1; rr++) for (int cc = c0; cc <= c1; cc++) if (!g[rr * 15 + cc]) return r;
    bool touch = false;
    for (int i = 0; i < n; i++) {
        int rr = p[i].cell / 15, cc = p[i].cell % 15;
        static const int DR[4] = {-1, 1, 0, 0}, DC[4] = {0, 0, -1, 1};
        for (int d = 0; d < 4; d++) {
            int r2 = rr + DR[d], c2 = cc + DC[d];
            if (r2 < 0 || r2 > 14 || c2 < 0 || c2 > 14) continue;
            touch |= b[r2 * 15 + c2] != 0;
        }
    }
    if (opening ? !fresh[112] : !touch) return r;
    // Every run of two or more with a new tile in it is a word.
    for (int dir = 0; dir < 2; dir++) {
        int step = dir ? 15 : 1;
        for (int line = 0; line < 15; line++) {
            int i = 0;
            while (i < 15) {
                auto cell = [&](int k) { return dir ? k * 15 + line : line * 15 + k; };
                if (!g[cell(i)]) { i++; continue; }
                int j = i, sum = 0, mult = 1, news = 0;
                while (j < 15 && g[cell(j)]) {
                    int c = cell(j), v = (g[c] & 0x80) ? 0 : wd::VALUE[g[c] & 31];
                    if (fresh[c]) {
                        news++;
                        int pr = wd::premium((uint8_t)c);
                        if (pr == wd::DL) v *= 2;
                        if (pr == wd::TL) v *= 3;
                        if (pr == wd::DW) mult *= 2;
                        if (pr == wd::TW) mult *= 3;
                    }
                    sum += v;
                    j++;
                }
                if (j - i >= 2 && news) { r.words.push_back({cell(i), j - i, step}); r.score += sum * mult; }
                i = j;
            }
        }
    }
    if (r.words.empty()) return r;
    if (n == 7) r.score += 50;
    r.ok = true;
    return r;
}

static void place(uint8_t *b, const char *word, int row, int col, bool down) {
    for (int i = 0; word[i]; i++) b[(row + (down ? i : 0)) * 15 + col + (down ? 0 : i)] = (uint8_t)(word[i] - 'A' + 1);
}

static std::vector<wd::Placement> tiles(const uint8_t *b, const char *word, int row, int col, bool down) {
    std::vector<wd::Placement> p;
    for (int i = 0; word[i]; i++) {
        uint8_t cell = (uint8_t)((row + (down ? i : 0)) * 15 + col + (down ? 0 : i));
        if (!b[cell]) p.push_back({cell, word[i] >= 'a' ? (uint8_t)((word[i] - 'a' + 1) | wd::BLANK) : (uint8_t)(word[i] - 'A' + 1)});
    }
    return p;
}

static void testRules() {
    // The premium squares: the standard board has 8 TW, 17 DW (the centre
    // among them), 12 TL and 24 DL.
    int count[5] = {};
    for (int c = 0; c < 225; c++) count[wd::premium((uint8_t)c)]++;
    CHECK_EQ(count[wd::TW], 8);
    CHECK_EQ(count[wd::DW], 17);
    CHECK_EQ(count[wd::TL], 12);
    CHECK_EQ(count[wd::DL], 24);
    CHECK_EQ(wd::premium(112), wd::DW);
    CHECK_EQ(wd::premium(0), wd::TW);
    CHECK_EQ(wd::premium(16), wd::DW);
    CHECK_EQ(wd::premium(20), wd::TL);
    CHECK_EQ(wd::premium(3), wd::DL);
    int total = 0, value = 0;
    for (int t = 1; t <= 27; t++) { total += wd::COUNT[t]; value += wd::COUNT[t] * wd::VALUE[t]; }
    CHECK_EQ(total, 100);
    CHECK_EQ(value, 187);

    uint8_t b[225] = {};
    wd::Result r;
    // The opening: HORN across the centre (a double word).
    auto p = tiles(b, "HORN", 7, 4, false);
    CHECK_EQ(wd::check(b, p.data(), (uint8_t)p.size(), r), wd::OK);
    CHECK_EQ(r.score, 14);       // (4+1+1+1) x 2
    p = tiles(b, "HORNS", 7, 3, false);
    CHECK_EQ(wd::check(b, p.data(), (uint8_t)p.size(), r), wd::OK);
    CHECK_EQ(r.score, 24);       // the H on the double letter at 7,3: (8+1+1+1+1) x 2
    p = tiles(b, "HORN", 6, 4, false);
    CHECK_EQ(wd::check(b, p.data(), (uint8_t)p.size(), r), wd::E_CENTRE);
    p = tiles(b, "H", 7, 7, false);
    CHECK_EQ(wd::check(b, p.data(), (uint8_t)p.size(), r), wd::E_ALONE);
    place(b, "HORN", 7, 4, false);
    // FARM down through the R: F A . M, premiums under new tiles only.
    p = tiles(b, "FARM", 5, 6, true);
    CHECK_EQ(p.size(), 3u);
    CHECK_EQ(wd::check(b, p.data(), (uint8_t)p.size(), r), wd::OK);
    CHECK_EQ(r.nWords, 1);
    CHECK_EQ(r.score, 4 + 2 + 1 + 6);          // A and M on double letters; the R was there
    place(b, "FARM", 5, 6, true);
    // A blank scores nothing: a blank A under the N makes NA down and MA across.
    p = tiles(b, "Na", 7, 7, true);
    CHECK_EQ(wd::check(b, p.data(), (uint8_t)p.size(), r), wd::OK);
    CHECK_EQ(r.nWords, 2);
    CHECK_EQ(r.score, 1 + 3);
    // Not in a line; with a gap; touching nothing; on a taken square.
    wd::Placement bad1[2] = {{0, 1}, {16, 2}};
    CHECK_EQ(wd::check(b, bad1, 2, r), wd::E_LINE);
    wd::Placement bad2[2] = {{120, 1}, {123, 2}};       // row 8: columns 0 and 3, nothing between
    CHECK_EQ(wd::check(b, bad2, 2, r), wd::E_GAP);
    wd::Placement bad3[2] = {{0, 1}, {1, 2}};
    CHECK_EQ(wd::check(b, bad3, 2, r), wd::E_ALONE);
    wd::Placement bad4[1] = {{(uint8_t)(7 * 15 + 4), 1}};
    CHECK_EQ(wd::check(b, bad4, 1, r), wd::E_TAKEN);
    uint8_t before[225];
    memcpy(before, b, 225);
    // Seven tiles: the bonus.
    uint8_t e[225] = {};
    p = tiles(e, "PLAYERS", 7, 1, false);
    CHECK_EQ(wd::check(e, p.data(), 7, r), wd::OK);
    CHECK_EQ(r.score, (3 + 1 + 1 * 2 + 4 + 1 + 1 + 1) * 2 + 50);
    CHECK(memcmp(before, b, 225) == 0);

    // Random plays on random boards, against the reference.
    int agree = 0, legal = 0;
    for (int round = 0; round < 20000; round++) {
        if (round % 40 == 0) memset(b, 0, 225);
        wd::Placement q[7];
        int n = 1 + rnd() % 7, row = rnd() % 15, col = rnd() % 15;
        bool down = rnd() & 1;
        int k = 0;
        // Mostly sensible: tiles along a line, skipping taken squares; sometimes wild.
        bool wild = rnd() % 8 == 0;
        for (int i = 0; k < n && i < 15; i++) {
            int rr = wild ? (int)(rnd() % 15) : row + (down ? i : 0), cc = wild ? (int)(rnd() % 15) : col + (down ? 0 : i);
            if (rr > 14 || cc > 14) break;
            if (b[rr * 15 + cc] && rnd() % 6) continue;
            if (rnd() % 12 == 0) continue;                              // a gap
            q[k++] = {(uint8_t)(rr * 15 + cc), (uint8_t)((1 + rnd() % 26) | (rnd() % 10 == 0 ? wd::BLANK : 0))};
        }
        if (!k) continue;
        bool dup = false;
        for (int i = 0; i < k; i++) for (int j = 0; j < i; j++) dup |= q[i].cell == q[j].cell;
        uint8_t err = wd::check(b, q, (uint8_t)k, r);
        Ref ref = reference(b, q, k);
        if (dup) { CHECK(err != wd::OK); continue; }
        bool same = (err == wd::OK) == ref.ok;
        if (same && ref.ok) {
            same = r.score == ref.score && r.nWords == ref.words.size();
            for (auto &w : ref.words) {
                bool found = false;
                for (int i = 0; i < r.nWords; i++)
                    found |= r.word[i].start == w.start && r.word[i].len == w.len && r.word[i].step == w.step;
                same &= found;
            }
        }
        if (!same) {
            fails++;
            printf("FAIL rules disagree: round %d err %d ref %d score %d/%d\n", round, err, ref.ok, r.score, ref.score);
            if (fails > 10) return;
        }
        agree++;
        if (ref.ok) { legal++; for (int i = 0; i < k; i++) b[q[i].cell] = q[i].tile; }
    }
    checks += agree;
    printf("rules: %d random plays agree with the reference (%d legal)\n", agree, legal);
}

// ---------------------------------------------------------------------------
static bool tilesAddUp() {
    int have[28] = {};
    for (int c = 0; c < 225; c++) if (game::board[c]) have[wd::tileOf(game::board[c])]++;
    for (int s = 0; s < 2; s++) for (int i = 0; i < 7; i++) if (game::rack[s][i]) have[game::rack[s][i]]++;
    int out = 0;
    for (int t = 1; t <= 27; t++) { if (have[t] > wd::COUNT[t]) return false; out += have[t]; }
    return out + game::bagLeft == 100;
}

static void testGames(int games) {
    long turns = 0, plays = 0, swaps = 0, passes = 0, total[2] = {0, 0}, bingos = 0, tried = 0, best = 0;
    int wins[3] = {};
    for (int g = 0; g < games; g++) {
        game::Setup s = {game::TWO_PLAYER, 0, 1000u + (uint32_t)g};
        game::start(s);
        // Side 0 the high roller, side 1 the others in turn.
        uint8_t level[2] = {ai::HIGH_ROLLER, (uint8_t)(g % 3)};
        int mine[2] = {0, 0}, guard = 0;
        while (!game::over && guard++ < 400) {
            uint8_t side = game::turn;
            ai::start(side, level[side]);
            while (!ai::step(500)) {}
            tried += ai::tried();
            game::Play pl;
            wd::Result r;
            turns++;
            if (ai::chosen(pl, r)) {
                // Again, the plain way: legal, scored the same, every word in the list.
                Ref ref = reference(game::board, pl.p, pl.n);
                CHECK(ref.ok);
                CHECK_EQ(r.score, ref.score);
                uint8_t g2[225];
                memcpy(g2, game::board, 225);
                for (int i = 0; i < pl.n; i++) g2[pl.p[i].cell] = pl.p[i].tile;
                for (auto &w : ref.words) {
                    std::string word;
                    for (int i = 0; i < w.len; i++) word += (char)('a' + (g2[w.start + i * w.step] & 31) - 1);
                    if (!core.count(word)) { fails++; printf("FAIL game %d: \"%s\" is not a word\n", g, word.c_str()); }
                }
                // The tiles came from the rack.
                int have[28] = {};
                for (int i = 0; i < 7; i++) have[game::rack[side][i]]++;
                for (int i = 0; i < pl.n; i++) CHECK(have[wd::tileOf(pl.p[i].tile)]-- > 0);
                wd::Span bad;
                CHECK(game::wordsOk(pl, r, true, bad));
                if (pl.n == 7) bingos++;
                if (r.score > best) best = r.score;
                mine[side] += r.score;
                game::play(pl, r);
                plays++;
            } else if (uint8_t m = ai::swapMask()) {
                game::swap(m);
                swaps++;
            } else {
                game::pass();
                passes++;
            }
            CHECK(tilesAddUp());
            if (!game::over) {
                CHECK_EQ(game::score[0], mine[0]);
                CHECK_EQ(game::score[1], mine[1]);
                // Save, wreck, load: the same game.
                if (guard % 7 == 0) {
                    game::Record rec;
                    game::save(rec);
                    uint8_t b2[225], r2[2][7];
                    memcpy(b2, game::board, 225);
                    memcpy(r2, game::rack, 14);
                    uint8_t bag = game::bagLeft, turn = game::turn;
                    memset(game::board, 3, 225);
                    memset(game::rack, 0, 14);
                    game::bagLeft = 0;
                    CHECK(game::load(rec));
                    CHECK(memcmp(b2, game::board, 225) == 0 && memcmp(r2, game::rack, 14) == 0);
                    CHECK_EQ(game::bagLeft, bag);
                    CHECK_EQ(game::turn, turn);
                    CHECK_EQ(game::score[0], mine[0]);
                }
            }
        }
        CHECK(game::over);
        CHECK_EQ(game::score[0] - game::rackPenalty[0], mine[0]);
        CHECK_EQ(game::score[1] - game::rackPenalty[1], mine[1]);
        total[0] += game::score[0];
        total[1] += game::score[1];
        wins[game::winner()]++;
    }
    printf("games: %d played, %ld turns (%ld plays, %ld swaps, %ld passes), %ld bingos, best play %ld\n",
           games, turns, plays, swaps, passes, bingos, best);
    printf("       high roller averages %ld, the others %ld; wins %d-%d, %d ties; %ld placements scored a turn\n",
           total[0] / games, total[1] / games, wins[0], wins[1], wins[2], tried / (turns ? turns : 1));
}

int main(int argc, char **argv) {
    bool quick = argc > 1 && !strcmp(argv[1], "--quick");
    loadList("tools/dict/core.txt", core);
    loadList("tools/dict/data/enable1.txt", enable);
    if (core.empty()) { printf("tools/dict/core.txt not found: run from the sketch folder\n"); return 2; }
    testDictionary();
    testRules();
    testGames(quick ? 30 : 600);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
