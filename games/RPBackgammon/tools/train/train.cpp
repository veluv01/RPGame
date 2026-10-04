// The CPU's teacher: temporal-difference self-play for the network in
// Net.*, its benchmarks, and the export to src/ai/NetData.cpp.
//
//   python tools/train/train.py train out/net.bin --games 300000
//   python tools/train/train.py bench int:out/net.bin heur --games 10000
//   python tools/train/train.py export out/net.bin
//
// A network of floats is trained here (TD(lambda), the network playing both
// sides and learning from each position's successor, as Tesauro's TD-Gammon
// did). It evaluates a position just after a move, for the side that moved.
// The game's own rules (Rules.cpp), row encoding (Net.h) and
// integer evaluator (Net.cpp) are compiled in, so the integer network that
// is benchmarked here is bit for bit the one the handheld plays.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "../../Rules.h"
#include "../../Net.h"
#include "../../Ai.h"
#include "../../Race.h"

using namespace bg;
using net::H;
using net::ROWS;

namespace net {
int8_t HID_W[ROWS][H];
int16_t HID_B[H];
int8_t OUT_W[H];
int32_t OUT_B;
}

// ---------------------------------------------------------------------------
// The float network
// ---------------------------------------------------------------------------
// Any hidden size (a file says its own), so that two sizes can play each
// other; only the integer tables are fixed at the size this tool was built for.
static const int MAXH = 64;
struct FNet {
    int h;
    float w1[ROWS][MAXH];
    float b1[MAXH];
    float v[MAXH];
    float b2;
};

struct Rows { uint8_t row[48], k[48]; int n; };

static void rowsOf(const Board &b, uint8_t mover, Rows &r) {
    r.n = 0;
    net::forEachRow(b, mover, [&](uint8_t row, uint8_t k) { r.row[r.n] = row; r.k[r.n++] = k; });
}

static inline float sigmoidf(float x) { return 1.0f / (1.0f + expf(-x)); }

// The output before its sigmoid; h gets the hidden activations.
static float forward(const FNet &f, const Rows &r, float *h) {
    const int n = f.h;
    float acc[MAXH];
    memcpy(acc, f.b1, sizeof(float) * (size_t)n);
    for (int i = 0; i < r.n; i++) {
        const float *w = f.w1[r.row[i]];
        float k = r.k[i];
        for (int j = 0; j < n; j++) acc[j] += w[j] * k;
    }
    float out = f.b2;
    for (int j = 0; j < n; j++) { h[j] = sigmoidf(acc[j]); out += f.v[j] * h[j]; }
    return out;
}

static uint32_t xs = 0x9E3779B9u;
static uint32_t xrnd() { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }
static float frand() { return (float)(xrnd() >> 8) / 16777216.0f; }

static void initNet(FNet &f) {
    memset(&f, 0, sizeof f);
    f.h = H;
    for (int r = 0; r < ROWS; r++) for (int j = 0; j < H; j++) f.w1[r][j] = (frand() - 0.5f) * 0.2f;
    for (int j = 0; j < H; j++) { f.b1[j] = (frand() - 0.5f) * 0.2f; f.v[j] = (frand() - 0.5f) * 0.2f; }
}

static const float W1_LIMIT = 127.0f / net::W1_SCALE, W2_LIMIT = 127.0f / net::W2_SCALE;

static bool saveNet(const FNet &f, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    int32_t hdr[2] = {0x54454E42, f.h};     // "BNET"
    fwrite(hdr, sizeof hdr, 1, fp);
    for (int r = 0; r < ROWS; r++) fwrite(f.w1[r], sizeof(float), (size_t)f.h, fp);
    fwrite(f.b1, sizeof(float), (size_t)f.h, fp);
    fwrite(f.v, sizeof(float), (size_t)f.h, fp);
    fwrite(&f.b2, sizeof(float), 1, fp);
    fclose(fp);
    return true;
}

static bool loadNet(FNet &f, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "cannot open %s\n", path); return false; }
    int32_t hdr[2];
    memset(&f, 0, sizeof f);
    bool ok = fread(hdr, sizeof hdr, 1, fp) == 1 && hdr[0] == 0x54454E42 && hdr[1] > 0 && hdr[1] <= MAXH;
    if (ok) {
        size_t n = (size_t)hdr[1];
        f.h = hdr[1];
        for (int r = 0; r < ROWS && ok; r++) ok = fread(f.w1[r], sizeof(float), n, fp) == n;
        ok = ok && fread(f.b1, sizeof(float), n, fp) == n && fread(f.v, sizeof(float), n, fp) == n &&
             fread(&f.b2, sizeof(float), 1, fp) == 1;
    }
    fclose(fp);
    if (!ok) fprintf(stderr, "%s: not a network file\n", path);
    return ok;
}

// Round the floats into the integer tables the device uses.
static int clampi(long v, long lo, long hi) { return (int)(v < lo ? lo : v > hi ? hi : v); }
static void quantise(const FNet &f) {
    if (f.h != H) {
        fprintf(stderr, "this build's integer tables are for %d hidden units, the network has %d (use -H)\n", H, f.h);
        exit(1);
    }
    for (int r = 0; r < ROWS; r++)
        for (int j = 0; j < H; j++) net::HID_W[r][j] = (int8_t)clampi(lroundf(f.w1[r][j] * net::W1_SCALE), -127, 127);
    for (int j = 0; j < H; j++) {
        net::HID_B[j] = (int16_t)clampi(lroundf(f.b1[j] * net::W1_SCALE), -20000, 20000);
        net::OUT_W[j] = (int8_t)clampi(lroundf(f.v[j] * net::W2_SCALE), -127, 127);
    }
    net::OUT_B = (int32_t)lroundf(f.b2 * net::W2_SCALE * 256);
    net::forget();
}

// ---------------------------------------------------------------------------
// Players
// ---------------------------------------------------------------------------
struct Chosen { uint8_t n, from[4], die[4]; };

// Plays the roll: the play with the highest score(b) (b after the play).
template <class Score> static void playBest(Board &b, uint8_t side, uint8_t d1, uint8_t d2, Score score) {
    Plays g;
    Chosen best = {0, {0}, {0}};
    double bestScore = -1e30;
    begin(g, b, side, d1, d2);
    while (next(g, b)) {
        double s = score(b);
        if (s > bestScore) {
            bestScore = s;
            best.n = g.depth;
            memcpy(best.from, g.from, 4);
            memcpy(best.die, g.die, 4);
        }
    }
    for (uint8_t i = 0; i < best.n; i++) doStep(b, side, best.from[i], best.die[i]);
}

struct Player {
    enum Kind { RANDOM, PIPS, HEUR, FLOAT, INT, AI } kind = RANDOM;
    FNet *f = nullptr;
    int level = 0;
    std::string name;
};

static double heuristic(const Board &b, uint8_t s) {
    uint8_t o = s ^ 1;
    double v = (double)pips(b, o) - (double)pips(b, s) + 8.0 * b.n[o][BAR];
    // The other side's rearmost checker, in my numbering: blots above it can still be hit.
    int lowest = 25;
    if (b.n[o][BAR]) lowest = 0;
    else for (int q = 24; q >= 1; q--) if (b.n[o][q]) { lowest = 25 - q; break; }
    for (int p = 1; p <= 24; p++) {
        if (b.n[s][p] == 1 && p > lowest) v -= 0.4 * (25 - p);
        if (b.n[s][p] >= 2 && p >= 3 && p <= 8) v += 3.0;
    }
    return v;
}

static void playerMove(const Player &pl, Board &b, uint8_t side, uint8_t d1, uint8_t d2, Rng &aiRng) {
    switch (pl.kind) {
        case Player::RANDOM: {
            Plays g;
            begin(g, b, side, d1, d2);
            int n = 0;
            while (next(g, b)) n++;
            int pick = (int)(xrnd() % (uint32_t)n);
            begin(g, b, side, d1, d2);
            for (int k = 0; k <= pick; k++) next(g, b);
            break;
        }
        case Player::PIPS:
            playBest(b, side, d1, d2, [&](const Board &x) { return (double)pips(x, side ^ 1) - (double)pips(x, side); });
            break;
        case Player::HEUR:
            playBest(b, side, d1, d2, [&](const Board &x) { return heuristic(x, side); });
            break;
        // The networks play as the handheld's EXPERT does: every play once,
        // and a pure race by the count (Race.h).
        case Player::FLOAT:
        case Player::INT: {
            bool racing = !contact(b);
            playBest(b, side, d1, d2, [&](const Board &x) {
                Rows r;
                float h[MAXH];
                uint8_t w;
                if (result(x, w)) return 1e12;
                if (racing) return -(double)race::cost(x, side);
                if (pl.kind == Player::INT) return (double)net::eval(x, side);
                rowsOf(x, side, r);
                return (double)forward(*pl.f, r, h);
            });
            break;
        }
        case Player::AI: {
            ai::Play p;
            ai::start(b, side, d1, d2, (uint8_t)pl.level, aiRng);
            while (!ai::step(1000)) {}
            ai::chosen(p);
            for (uint8_t i = 0; i < p.n; i++) doStep(b, side, p.from[i], p.die[i]);
            break;
        }
    }
}

static bool makePlayer(Player &pl, const char *spec) {
    pl.name = spec;
    if (!strcmp(spec, "random")) { pl.kind = Player::RANDOM; return true; }
    if (!strcmp(spec, "pips")) { pl.kind = Player::PIPS; return true; }
    if (!strcmp(spec, "heur")) { pl.kind = Player::HEUR; return true; }
    const char *colon = strchr(spec, ':');
    if (!colon) return false;
    pl.f = new FNet;
    if (!loadNet(*pl.f, colon + 1)) return false;
    if (!strncmp(spec, "float:", 6)) pl.kind = Player::FLOAT;
    else if (!strncmp(spec, "int:", 4)) pl.kind = Player::INT;
    else if (!strncmp(spec, "ai", 2)) { pl.kind = Player::AI; pl.level = spec[2] - '0'; }
    else return false;
    return true;
}

// One game; returns the winner's index (0 = `white`), `how` 1/2/3.
static int playGame(const Player &white, const Player &red, uint32_t seed, int *how = nullptr, long *turns = nullptr) {
    Board b;
    reset(b);
    Rng dice, aiRng;
    dice.seed(seed, 1);
    aiRng.seed(seed, 2);
    uint8_t side = (uint8_t)(seed & 1), w = 0;
    for (;;) {
        uint8_t d1 = dice.die(), d2 = dice.die();
        playerMove(side == WHITE ? white : red, b, side, d1, d2, aiRng);
        if (turns) ++*turns;
        int r = result(b, w);
        if (r) { if (how) *how = r; return w; }
        side ^= 1;
    }
}

// A against B over `games` pairs: each pair the same dice, sides swapped.
// The integer tables hold one network at a time: when both are integer
// networks, each is loaded before its move (slow, only for comparisons).
static double bench(Player &a, Player &b, int games, bool quiet = false) {
    bool aInt = a.kind == Player::INT || a.kind == Player::AI, bInt = b.kind == Player::INT || b.kind == Player::AI;
    if (aInt && bInt && a.f != b.f) { fprintf(stderr, "bench: two different integer networks at once is not supported\n"); exit(1); }
    if (aInt) quantise(*a.f);
    else if (bInt) quantise(*b.f);
    long wins = 0, total = 0, turns = 0;
    int how[4] = {0, 0, 0, 0};
    for (int g = 0; g < games; g++) {
        int h = 0;
        uint32_t seed = 1000003u * (uint32_t)(g + 1);
        if (playGame(a, b, seed, &h, &turns) == 0) { wins++; how[h]++; }
        if (playGame(b, a, seed, &h, &turns) == 1) { wins++; how[h]++; }
        total += 2;
    }
    double p = (double)wins / (double)total, se = sqrt(p * (1 - p) / (double)total);
    if (!quiet)
        printf("%s vs %s: %.2f%% +/- %.2f (%ld games, %.0f turns each; wins: %d single, %d gammon, %d backgammon)\n",
               a.name.c_str(), b.name.c_str(), 100 * p, 196 * se, total, (double)turns / (double)total, how[1],
               how[2], how[3]);
    return p;
}

// ---------------------------------------------------------------------------
// Training
// ---------------------------------------------------------------------------
struct TrainOpts {
    long games = 300000;
    float alpha = 0.1f, alphaEnd = 0.02f, lambda = 0.7f;
    long report = 25000;
    uint32_t seed = 1;
    float gammon = 0.1f;        // a gammon is worth this much more than a plain win, a backgammon twice that
    const char *init = nullptr;
    bool integer = false;       // play and learn through the integer evaluator's values (fine-tuning)
};

static void train(const char *outPath, const TrainOpts &o) {
    FNet *f = new FNet, *e = new FNet;
    xs = 0x9E3779B9u ^ (o.seed * 2654435761u);
    if (o.init) { if (!loadNet(*f, o.init)) exit(1); }
    else initNet(*f);
    Player heur, self;
    makePlayer(heur, "heur");
    self.kind = o.integer ? Player::INT : Player::FLOAT;
    self.f = f;
    self.name = "net";
    Rng dice;
    dice.seed(o.seed, 7);
    long plies = 0;
    for (long game = 1; game <= o.games; game++) {
        float alpha = o.alpha + (o.alphaEnd - o.alpha) * (float)game / (float)o.games;
        Board b;
        reset(b);
        memset(e, 0, sizeof *e);
        uint8_t side = (uint8_t)(xrnd() & 1), w = 0;
        bool have = false;
        float yPrev = 0;
        for (;;) {
            uint8_t d1 = dice.die(), d2 = dice.die();
            if (o.integer) quantise(*f);
            Rng unused;
            playerMove(self, b, side, d1, d2, unused);
            plies++;
            bool over = result(b, w) != 0;
            Rows r;
            float h[MAXH], y, value = 0;
            if (over) {
                // What the game was worth to White, 0..1: mostly whether it
                // won, a little how (so a lost game is still worth saving
                // from a gammon, and a won one worth winning well).
                float worth = 0.5f + 0.5f * (1.0f + o.gammon * (float)(result(b, w) - 1)) / (1.0f + 2.0f * o.gammon);
                y = w == WHITE ? worth : 1.0f - worth;
            }
            else {
                rowsOf(b, side, r);
                value = sigmoidf(forward(*f, r, h));
                y = side == WHITE ? value : 1.0f - value;
            }
            if (have) {
                // Move every weight along its trace, by how much better or
                // worse White's chances now look than a move ago.
                float step = alpha * (y - yPrev);
                for (int i = 0; i < ROWS; i++)
                    for (int j = 0; j < H; j++) {
                        float x = f->w1[i][j] + step * e->w1[i][j];
                        f->w1[i][j] = x < -W1_LIMIT ? -W1_LIMIT : x > W1_LIMIT ? W1_LIMIT : x;
                    }
                for (int j = 0; j < H; j++) {
                    f->b1[j] += step * e->b1[j];
                    float x = f->v[j] + step * e->v[j];
                    f->v[j] = x < -W2_LIMIT ? -W2_LIMIT : x > W2_LIMIT ? W2_LIMIT : x;
                }
                f->b2 += step * e->b2;
            }
            if (over) break;
            // Traces: decay, then add this position's gradient of White's chance.
            float g = (side == WHITE ? 1.0f : -1.0f) * value * (1.0f - value);
            for (int i = 0; i < ROWS; i++)
                for (int j = 0; j < H; j++) e->w1[i][j] *= o.lambda;
            e->b2 = o.lambda * e->b2 + g;
            for (int j = 0; j < H; j++) {
                e->v[j] = o.lambda * e->v[j] + g * h[j];
                float gh = g * f->v[j] * h[j] * (1.0f - h[j]);
                e->b1[j] = o.lambda * e->b1[j] + gh;
                for (int i = 0; i < r.n; i++) e->w1[r.row[i]][j] += gh * (float)r.k[i];
            }
            yPrev = y;
            have = true;
            side ^= 1;
        }
        if (game % o.report == 0 || game == o.games) {
            saveNet(*f, outPath);
            Player fl;
            fl.kind = Player::FLOAT; fl.f = f; fl.name = "float";
            double p = bench(fl, heur, 1000, true);
            float big1 = 0, big2 = 0;
            for (int i = 0; i < ROWS; i++) for (int j = 0; j < H; j++) if (fabsf(f->w1[i][j]) > big1) big1 = fabsf(f->w1[i][j]);
            for (int j = 0; j < H; j++) if (fabsf(f->v[j]) > big2) big2 = fabsf(f->v[j]);
            printf("game %ld: alpha %.3f, %.0f plies a game, vs heur %.1f%%, largest weights %.2f / %.2f\n", game, alpha,
                   (double)plies / (double)game, 100 * p, big1, big2);
            fflush(stdout);
        }
    }
    saveNet(*f, outPath);
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------
static void exportNet(const char *inPath, const char *outPath) {
    FNet *f = new FNet;
    if (!loadNet(*f, inPath)) exit(1);
    quantise(*f);
    FILE *fp = fopen(outPath, "wb");
    if (!fp) { fprintf(stderr, "cannot write %s\n", outPath); exit(1); }
    fprintf(fp, "// GENERATED by tools/train/train.py export - do not edit.\n"
                "// The network's tables: what the CPU learned playing itself (see Net.h).\n"
                "#include \"../../Net.h\"\n\n"
                "#if NET_H != %d || NET_W1_SCALE != %d\n#error \"NetData.cpp was exported for another network shape\"\n#endif\n\n"
                "namespace net {\n\n", H, net::W1_SCALE);
    fprintf(fp, "NET_CONST int8_t HID_W[ROWS][H] = {\n");
    for (int r = 0; r < ROWS; r++) {
        fprintf(fp, "    {");
        for (int j = 0; j < H; j++) fprintf(fp, "%d%s", net::HID_W[r][j], j + 1 < H ? ", " : "");
        fprintf(fp, "},\n");
    }
    fprintf(fp, "};\n\nNET_CONST int16_t HID_B[H] = {");
    for (int j = 0; j < H; j++) fprintf(fp, "%d%s", net::HID_B[j], j + 1 < H ? ", " : "");
    fprintf(fp, "};\n\nNET_CONST int8_t OUT_W[H] = {");
    for (int j = 0; j < H; j++) fprintf(fp, "%d%s", net::OUT_W[j], j + 1 < H ? ", " : "");
    fprintf(fp, "};\n\nNET_CONST int32_t OUT_B = %d;\n\n}  // namespace net\n", (int)net::OUT_B);
    fclose(fp);
    printf("wrote %s (%d bytes of tables)\n", outPath, ROWS * H + 3 * H + 4);
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: train|bench|export ...\n"); return 2; }
    std::string cmd = argv[1];
    auto opt = [&](const char *name, const char *def) -> const char * {
        for (int i = 2; i + 1 < argc; i++) if (!strcmp(argv[i], name)) return argv[i + 1];
        return def;
    };
    auto flag = [&](const char *name) {
        for (int i = 2; i < argc; i++) if (!strcmp(argv[i], name)) return true;
        return false;
    };
    if (cmd == "train" && argc >= 3) {
        TrainOpts o;
        o.games = atol(opt("--games", "300000"));
        o.alpha = (float)atof(opt("--alpha", "0.1"));
        o.alphaEnd = (float)atof(opt("--alpha-end", "0.02"));
        o.lambda = (float)atof(opt("--lambda", "0.7"));
        o.report = atol(opt("--report", "25000"));
        o.seed = (uint32_t)atol(opt("--seed", "1"));
        o.gammon = (float)atof(opt("--gammon", "0.1"));
        o.init = opt("--init", nullptr);
        o.integer = flag("--integer");
        train(argv[2], o);
        return 0;
    }
    if (cmd == "bench" && argc >= 4) {
        Player a, b;
        if (!makePlayer(a, argv[2]) || !makePlayer(b, argv[3])) { fprintf(stderr, "bad player\n"); return 2; }
        // The same file for both integer players: share the tables.
        if (a.f && b.f) {
            const char *fa = strchr(argv[2], ':') + 1, *fb = strchr(argv[3], ':') + 1;
            if (!strcmp(fa, fb)) b.f = a.f;
        }
        bench(a, b, atoi(opt("--games", "5000")) / 2);
        return 0;
    }
    if (cmd == "export" && argc >= 4) { exportNet(argv[2], argv[3]); return 0; }
    fprintf(stderr, "usage: train <out.bin> [--games N --alpha A --alpha-end A --lambda L --gammon G --seed S --init in.bin --report N --integer]\n"
                    "       bench <A> <B> [--games N]   players: random pips heur float:<bin> int:<bin> ai0:<bin> ai1:<bin> ai2:<bin>\n"
                    "       export <in.bin> <NetData.cpp>\n");
    return 2;
}
