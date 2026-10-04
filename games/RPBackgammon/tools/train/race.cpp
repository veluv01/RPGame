// Fits the race table in src/ai/RaceData.cpp, and says how well it plays.
//
//   python tools/train/train.py race [src/ai/RaceData.cpp]
//
// With all fifteen checkers home there are 54,264 ways to arrange them, few
// enough to work out exactly how many rolls each needs with perfect play.
// The handheld has no room for that table (it would fill the flash), so
// this fits race::cost()'s 25 numbers to it by least squares, then plays
// every position by the fitted count and reports how many rolls that costs
// against perfect play.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include "../../Rules.h"
#include "../../Race.h"

using namespace bg;

namespace race { uint8_t TABLE[TABLE_SIZE]; }

static std::vector<float> exact, policy;    // rolls needed: perfect play, and playing by race::cost
static std::vector<uint8_t> known, knownP;

static uint32_t key(const Board &b) {
    uint32_t k = 0;
    for (int p = 6; p >= 1; p--) k = k * 16 + b.n[WHITE][p];
    return k;
}

static void setHome(Board &b, uint32_t k) {
    memset(&b, 0, sizeof b);
    int sum = 0;
    for (int p = 1; p <= 6; p++) { b.n[WHITE][p] = (uint8_t)(k & 15); sum += k & 15; k >>= 4; }
    b.n[WHITE][OFF] = (uint8_t)(CHECKERS - sum);
    b.n[RED][OFF] = CHECKERS;
}

// Expected rolls to bear off from b. byCost: choosing plays by race::cost
// rather than by what is truly best.
static float rolls(Board &b, bool byCost) {
    uint32_t k = key(b);
    std::vector<float> &memo = byCost ? policy : exact;
    std::vector<uint8_t> &done = byCost ? knownP : known;
    if (!k) return 0;
    if (done[k]) return memo[k];
    float total = 0;
    for (uint8_t a = 1; a <= 6; a++)
        for (uint8_t c = 1; c <= a; c++) {
            Plays g;
            begin(g, b, WHITE, a, c);
            float best = 1e9f;
            int bestCost = 1 << 30;
            while (next(g, b)) {
                if (byCost) {
                    int cst = race::cost(b, WHITE);
                    if (cst < bestCost) { bestCost = cst; best = rolls(b, true); }
                } else {
                    float e = rolls(b, false);
                    if (e < best) best = e;
                }
            }
            total += (a == c ? 1.0f : 2.0f) * best;
        }
    done[k] = 1;
    return memo[k] = 1.0f + total / 36.0f;
}

// Least squares by the normal equations (Gaussian elimination, 25 unknowns).
static void solve(std::vector<std::vector<double>> &a, std::vector<double> &x) {
    int n = (int)a.size();
    for (int i = 0; i < n; i++) {
        int piv = i;
        for (int r = i + 1; r < n; r++) if (fabs(a[r][i]) > fabs(a[piv][i])) piv = r;
        std::swap(a[i], a[piv]);
        if (fabs(a[i][i]) < 1e-12) continue;
        for (int r = 0; r < n; r++) {
            if (r == i) continue;
            double f = a[r][i] / a[i][i];
            for (int c = i; c <= n; c++) a[r][c] -= f * a[i][c];
        }
    }
    x.assign(n, 0);
    for (int i = 0; i < n; i++) x[i] = fabs(a[i][i]) < 1e-12 ? 0 : a[i][n] / a[i][i];
}

static void features(const Board &b, double *f) {
    // The same shape as race::cost: per home point, a first, second and
    // third checker, then each one more; and a constant.
    memset(f, 0, sizeof(double) * race::TABLE_SIZE);
    for (int p = 1; p <= 6; p++) {
        int n = b.n[WHITE][p], base = (p - 1) * 4;
        if (n >= 1) f[base] = 1;
        if (n >= 2) f[base + 1] = 1;
        if (n >= 3) f[base + 2] = 1;
        if (n > 3) f[base + 3] = n - 3;
    }
    f[24] = 1;
}

int main(int argc, char **argv) {
    exact.assign(1 << 24, 0); policy.assign(1 << 24, 0);
    known.assign(1 << 24, 0); knownP.assign(1 << 24, 0);
    // Every arrangement of up to fifteen checkers on six points.
    std::vector<uint32_t> states;
    Board b;
    for (uint32_t k = 1; k < (1u << 24); k++) {
        int sum = 0;
        for (int i = 0; i < 6; i++) sum += (k >> (4 * i)) & 15;
        if (sum <= CHECKERS) states.push_back(k);
    }
    for (uint32_t k : states) { setHome(b, k); rolls(b, false); }
    printf("%zu positions; a full home board (5-5-5 on the 6, 5, 4 points) needs %.3f rolls\n", states.size() + 1,
           (setHome(b, 0x555000), rolls(b, false)));

    // Fit. Positions weigh the same; a start nearer real play would barely move the numbers.
    const int N = race::TABLE_SIZE;
    std::vector<std::vector<double>> a(N, std::vector<double>(N + 1, 0));
    double f[race::TABLE_SIZE];
    for (uint32_t k : states) {
        setHome(b, k);
        features(b, f);
        double y = exact[k] * 16.0;
        for (int i = 0; i < N; i++) {
            if (f[i] == 0) continue;
            for (int j = 0; j < N; j++) a[i][j] += f[i] * f[j];
            a[i][N] += f[i] * y;
        }
    }
    std::vector<double> x;
    solve(a, x);
    for (int i = 0; i < N; i++) {
        long v = lround(x[i]);
        race::TABLE[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    double err = 0, worst = 0;
    for (uint32_t k : states) {
        setHome(b, k);
        double d = fabs(race::cost(b, WHITE) / 16.0 - exact[k]);
        err += d;
        if (d > worst) worst = d;
    }
    printf("fit: mean error %.3f rolls, worst %.3f\n", err / (double)states.size(), worst);

    // Play by it.
    double lost = 0, worstLost = 0;
    for (uint32_t k : states) {
        setHome(b, k);
        double d = rolls(b, true) - exact[k];
        lost += d;
        if (d > worstLost) worstLost = d;
    }
    setHome(b, 0x555000);
    printf("playing by the table: %.4f rolls worse than perfect on average, %.3f at worst; from 5-5-5: %.3f against %.3f\n",
           lost / (double)states.size(), worstLost, rolls(b, true), exact[key(b)]);

    if (argc > 1) {
        FILE *fp = fopen(argv[1], "wb");
        if (!fp) { fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
        fprintf(fp, "// GENERATED by tools/train/train.py race - do not edit.\n"
                    "// race::cost()'s table, fitted to the exact rolls-to-bear-off of every\n"
                    "// home-board position (see Race.h).\n"
                    "#include \"../../Race.h\"\n\nnamespace race {\n\nRACE_CONST uint8_t TABLE[TABLE_SIZE] = {\n");
        for (int p = 0; p < 6; p++)
            fprintf(fp, "    %d, %d, %d, %d,     // the %d point: a first, second and third checker, each one more\n",
                    race::TABLE[p * 4], race::TABLE[p * 4 + 1], race::TABLE[p * 4 + 2], race::TABLE[p * 4 + 3], p + 1);
        fprintf(fp, "    %d,                // and to start with\n};\n\n}  // namespace race\n", race::TABLE[24]);
        fclose(fp);
        printf("wrote %s\n", argv[1]);
    }
    return 0;
}
