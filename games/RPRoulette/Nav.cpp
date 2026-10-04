#pragma GCC optimize("Os")   // cold code: size over speed
// Banded nearest-spot navigation (docs/design/layout.md section 3), after
// CHChess's nearest() (in its Screens.cpp). Two details pin down what
// the spec leaves open, so that its table of results comes out exactly
// (tools/tests/test_rules.cpp checks all 112 moves):
//  - the up/down band tolerance is 1 px - cells one grid line apart are in
//    each other's band - and 2 px when either spot is a dozen or an
//    even-money cell. (2 px everywhere would put column 1 in the zero's
//    band, so holding UP on the American 0 would reach 2, not 00.)
//  - a dozen or even-money candidate is measured where the glove would
//    stand on it (its sticky x), not at its anchor.
#include <stddef.h>
#include "Nav.h"
#include "Spots.h"

namespace nav {

using spots::Geo;

// Geo's extents by axis: lo(g, a) = x0 or y0, hi(g, a) = x1 or y1 (a = 1: y).
static_assert(offsetof(Geo, y0) == offsetof(Geo, x0) + 1 && offsetof(Geo, x1) == offsetof(Geo, x0) + 2 &&
              offsetof(Geo, y1) == offsetof(Geo, x0) + 3, "Geo: x0 y0 x1 y1");
static int lo(const Geo &g, int a) { return (&g.x0)[a]; }
static int hi(const Geo &g, int a) { return (&g.x0)[a + 2]; }

// Dozens and even-money cells: the glove keeps its x inside them.
static bool wide(unsigned id) { return id - spots::DOZEN < 9u; }

// Where the glove stands on spot id (cell c), coming from x.
static int xOn(unsigned id, const Geo &c, int x) {
    if (!wide(id)) return c.ax;
    int l = c.x0 + 4, h = c.x1 - 4;
    return x < l ? l : x > h ? h : x;
}

// Runs visit whole cells only: the straights, 0, 00, the outside bets, the bar.
static bool coarse(unsigned id) {
    if (id < spots::ZERO) return (id / 24) & (id % 24) & 1;
    return id != spots::ZERO_DZERO;
}

Glove at(uint8_t spot, bool us) {
    Geo c;
    spots::geo(spot, us, c);
    return Glove{spot, c.ax, c.ay};
}

bool step(Glove &g, int8_t dx, int8_t dy, bool tap, bool us) {
    if (!dx && !dy) return false;
    bool ortho = !dx || !dy;
    int a = !dy;                                 // the axis across the motion: 0 x (up/down), 1 y
    Geo f, c;
    spots::geo(g.spot, us, f);
    int fx = g.x, fy = g.y;
    // A run from a line or a corner first snaps across the motion to the
    // cell up and to the right, so it never stalls on a line row.
    unsigned id = g.spot;
    if (!tap && ortho && id < spots::ZERO && !coarse(id)) {
        spots::geo((uint8_t)((id / 24 | 1) * 24 + (id % 24 | 1)), us, c);   // (u | 1, v | 1)
        (&f.x0)[a] = (&c.x0)[a];
        (&f.x0)[a + 2] = (&c.x0)[a + 2];
        if (a) fy = c.ay; else fx = c.ax;
    }
    // Lowest key wins: tier << 22 | primary << 11 | secondary.
    //   0 in the band ahead: (along, side)
    //   1 ahead, up/down in a 45-degree cone (any diagonal): along + 2 side
    //   2 taps, in the band behind (wrap): the farthest, then side
    //   3 taps, anywhere behind: chess's 4 along + side
    uint32_t bestK = 0xFFFFFFFFu;
    unsigned best = spots::NONE;
    for (id = 0; id < spots::NSPOT; id++) {
        if (id == g.spot || !spots::valid((uint8_t)id, us) || (!tap && !coarse(id))) continue;
        spots::geo((uint8_t)id, us, c);
        int ex = xOn(id, c, fx) - fx, ey = c.ay - fy;
        int along = ex * dx + ey * dy, side = ex * dy - ey * dx;
        if (side < 0) side = -side;
        // In the band: the extents across the motion overlap - exactly
        // left/right; up/down across a grid line (two for a wide cell).
        int t = a ? 0 : 1 + (wide(id) || wide(g.spot));
        bool band = ortho && lo(c, a) <= hi(f, a) + t && hi(c, a) + t >= lo(f, a);
        uint32_t k;
        if (along > 0) {
            if (band) k = (uint32_t)(along << 11 | side);
            else if (!ortho || (!a && side <= along)) k = 1u << 22 | (uint32_t)(along + 2 * side) << 11;
            else continue;
        } else if (!tap) continue;
        else if (band) k = 2u << 22 | (uint32_t)(512 + along) << 11 | (uint32_t)side;
        else k = 3u << 22 | (uint32_t)(1536 + 4 * along + side) << 11;
        if (k < bestK) bestK = k, best = id;
    }
    if (best == spots::NONE) return false;
    spots::geo((uint8_t)best, us, c);
    g.spot = (uint8_t)best;
    g.x = (uint8_t)xOn(best, c, fx);
    g.y = c.ay;
    return true;
}

}  // namespace nav
