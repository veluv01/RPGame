#pragma GCC optimize("Os")   // cold code: size over speed
// The spot model (docs/design/layout.md section 2). The lattice is all
// formula; the 22 fixed cells (zeros, outside bets, the bar) are a small
// table, which is less flash than the code to compute them.
#include "Spots.h"
#include "Wheel.h"

namespace spots {

bool valid(uint8_t id, bool us) {
    return id < NSPOT && (us || (uint8_t)(id - DZERO) > 1);   // 00 and 0/00: American only
}

bool inside(uint8_t id) { return id < COLUMN; }

bool covers(uint8_t id, uint8_t n, bool us) {
    bool z = n == 0 || n >= wheel::N00;
    unsigned m = n - 1u;
    if (id < ZERO) {
        // On the half-cell lattice a number's centre is (2 col + 1, 2 row + 1)
        // (col, row from 0); a lattice point bets on the numbers within one
        // step of it, every row of them on the bottom edge (v = 0). The zero
        // column lies left of u = 0: there the European 0 is always taken; the
        // American 0 beside v <= 3 and the 00 beside v >= 3, both at v = 0.
        unsigned u = id % 24u, v = id / 24u;
        if (z) return !u && (us ? (n ? v >= 3 || !v : v <= 3) : !n);
        return u - m / 3 * 2 <= 2 && (!v || v - m % 3 * 2 <= 2);
    }
    if (id < COLUMN) return z && (id == ZERO_DZERO || (n == 0) == (id == ZERO));
    unsigned k = id - COLUMN;
    if (z || k >= 12) return false;              // the outside bets lose on the zeros
    if (k < 3) return m % 3 == k;
    if (k < 6) return m / 12 == k - 3;
    // LOW EVEN RED BLACK ODD HIGH: k and 5 - k are opposites. Red: odd,
    // flipped in 11..18 and 29..36 (wheel::colour()).
    k -= 6;
    unsigned j = k < 3 ? k : 5 - k;
    bool t = j == 0 ? m < 18 : j == 1 ? !(n & 1) : (n & 1) != (m % 18 >= 10);
    return t == (k < 3);
}

uint8_t count(uint8_t id, bool us) {
    unsigned c = 0;
    for (unsigned n = 0; n <= wheel::N00; n++) c += covers(id, (uint8_t)n, us);
    return (uint8_t)c;
}

Kind kind(uint8_t id, bool us) {
    if (id >= COLUMN) return id >= NBET ? BAR : (Kind)(COLUMN_BET + (id >= DOZEN) + (id >= BET_LOW));
    // By size: 1..6 -> STRAIGHT SPLIT STREET CORNER TOP_LINE SIX_LINE; on the
    // zero line (u = 0) three is a trio and four the first four.
    unsigned n = count(id, us);
    return (Kind)(n - 1 + (n > 3) + (n > 4) + (id % 24u == 0 && n - 3 <= 1));
}

uint8_t straightId(uint8_t n) {
    if (n == 0) return ZERO;
    if (n >= wheel::N00) return DZERO;
    unsigned m = n - 1u;
    return (uint8_t)(m % 3 * 48 + m / 3 * 2 + 25);
}

// The fixed cells, ids 144..165, then the American 0: {ax, ay, x0, y0, x1, y1}.
static const Geo CELLS[23] = {
    {4, 63, 1, 49, 7, 77},          {4, 55, 1, 49, 7, 62},          {4, 63, 4, 63, 4, 63},       // 0 00 0/00
    {122, 73, 117, 69, 126, 77},    {122, 63, 117, 59, 126, 67},    {122, 53, 117, 49, 126, 57}, // 2 TO 1
    {26, 83, 9, 79, 43, 87},        {62, 83, 45, 79, 79, 87},       {98, 83, 81, 79, 115, 87},   // dozens
    {17, 93, 9, 89, 25, 97},        {35, 93, 27, 89, 43, 97},       {53, 93, 45, 89, 61, 97},    // LOW EVEN RED
    {71, 93, 63, 89, 79, 97},       {89, 93, 81, 89, 97, 97},       {107, 93, 99, 89, 115, 97},  // BLACK ODD HIGH
    {8, 119, 0, 113, 16, 126},      {26, 119, 18, 113, 33, 126},    {43, 119, 35, 113, 50, 126}, // CLR $1 $5
    {60, 119, 52, 113, 67, 126},    {77, 119, 69, 113, 84, 126},    {94, 119, 86, 113, 101, 126},// $10 $25 $100
    {115, 119, 104, 113, 127, 126},                                                              // SPIN
    {4, 71, 1, 64, 7, 77},                                                                       // American 0
};

void geo(uint8_t id, bool us, Geo &g) {
    if (id >= ZERO) { g = CELLS[id == ZERO && us ? 22 : id - ZERO]; return; }
    // Lattice anchor: even u on the grid line x = 8 + 9u/2, odd u at a
    // column's centre; v from the bottom edge (y 78) up in 5 px steps.
    unsigned u = id % 24u, v = id / 24u, x = (9 * u + 16 + (u & 1)) >> 1, y = 78 - 5 * v;
    unsigned w = u & v & 1;                      // a number's cell: 8 x 9 inside
    g.ax = (uint8_t)x; g.x0 = (uint8_t)(x - 4 * w); g.x1 = (uint8_t)(x + 3 * w);
    g.ay = (uint8_t)y; g.y0 = (uint8_t)(y - 4 * w); g.y1 = (uint8_t)(y + 4 * w);
}

// Names, packed: kinds 0..7, then ids 147..165 in order.
static const char NAMES[] =
    "STRAIGHT\0SPLIT\0STREET\0TRIO\0CORNER\0FIRST FOUR\0TOP LINE\0SIX LINE\0"
    "1st COLUMN\0002nd COLUMN\0003rd COLUMN\0"
    "1st DOZEN\0002nd DOZEN\0003rd DOZEN\0"
    "LOW\0EVEN\0RED\0BLACK\0ODD\0HIGH\0"
    "CLEAR\0$1\0$5\0$10\0$25\0$100\0SPIN";

const char *kindName(uint8_t id, bool us) {
    unsigned i = id >= COLUMN ? id - COLUMN + 8u : (unsigned)kind(id, us);
    const char *s = NAMES;
    while (i--) while (*s++) {}
    return s;
}

char *numbers(char *p, uint8_t id, bool us) {
    *p = 0;
    Kind k = kind(id, us);
    if (k == COLUMN_BET || k == BAR || (k == EVEN_MONEY && id != BET_LOW && id != BET_HIGH)) return p;
    // Runs of numbers as "first-last", the rest joined with '/', 0 and 00 first.
    bool run = k == STREET || k == SIX_LINE || k >= DOZEN_BET;
    uint8_t last = NONE;
    char *start = p;
    for (unsigned i = 0; i <= wheel::N00; i++) {
        uint8_t n = (uint8_t)(i < 2 ? i * wheel::N00 : i - 1);
        if (!covers(id, n, us)) continue;
        if (p == start || !run) {
            if (p != start) *p++ = '/';
            p = wheel::name(p, n);
        }
        last = n;
    }
    if (run) {
        *p++ = '-';
        p = wheel::name(p, last);
    }
    return p;
}

}  // namespace spots
