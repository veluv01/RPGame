// The CPU's rules of thumb, by level (Ai.h says what each level weighs).
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed
#include "Ai.h"

namespace ai {

using namespace dom;

// What the play is worth to `side`, by rule of thumb.
static int16_t weigh(const Round &r, uint8_t side, uint8_t tile, uint8_t arm, bool shark) {
    Round c = r;
    uint8_t pts = place(c, side, tile, arm);
    bool fives = r.game == FIVES;
    int16_t s = (int16_t)(pts * 8 + pips(tile) * (fives ? 1 : 2) + (isDouble(tile) ? 4 : 0));
    if (!c.hand[side]) return (int16_t)(s + 2000);          // out: the round is won
    if (!shark) return s;
    // Its own next turn: tiles it could still play.
    uint8_t open = 0, top = c.spinner && c.len[E] && c.len[W] ? ARMS : N;
    for (uint8_t t = 0; t < TILES; t++)
        if (((c.hand[side] >> t) & 1) && armsFor(c, t)) s += 3;
    // Ends the other side has been seen without.
    for (uint8_t a = 0; a < top; a++) open |= (uint8_t)(1 << c.end[a]);
    for (uint8_t v = 0; v < 7; v++)
        if (((open >> v) & 1) && ((c.voids[side ^ 1] >> v) & 1)) s += 5;
    // The reply: each tile it cannot see, at the odds the other side holds it.
    uint32_t unseen = c.hand[side ^ 1] | c.bone;
    uint8_t held = count(c.hand[side ^ 1]), all = count(unseen);
    int16_t reply = 0;
    if (!all) return s;
    for (uint8_t t = 0; t < TILES; t++) {
        if (!((unseen >> t) & 1)) continue;
        uint8_t m = armsFor(c, t), best = 0;
        if (!m) continue;
        reply += 2;                                         // it can be played at all
        if (!fives) continue;
        for (uint8_t a = 0; a < ARMS; a++) {
            if (!((m >> a) & 1)) continue;
            Round d = c;
            uint8_t p = place(d, side ^ 1, t, a);
            if (p > best) best = p;
        }
        reply += best * 6;
    }
    return (int16_t)(s - reply * held / all);
}

bool choose(const Round &r, uint8_t side, uint8_t level, Rng &rng, Move &m) {
    // A ROOKIE plays the first thing that fits, half the time.
    bool any = level == ROOKIE && (rng.next() & 0x10000);
    uint8_t seen = 0, pick = 0;
    int16_t best = -32768;
    if (any) {
        for (uint8_t t = 0; t < TILES; t++) if (((r.hand[side] >> t) & 1) && armsFor(r, t)) seen++;
        if (seen) pick = rng.below(seen);
        seen = 0;
    }
    bool found = false;
    for (uint8_t t = 0; t < TILES; t++) {
        if (!((r.hand[side] >> t) & 1)) continue;
        uint8_t arms[ARMS], n = options(r, t, arms);
        if (!n) continue;
        if (any) {
            if (seen++ == pick) { m.tile = t; m.arm = arms[0]; return true; }
            continue;
        }
        for (uint8_t i = 0; i < n; i++) {
            int16_t s = weigh(r, side, t, arms[i], level >= SHARK);
            if (s > best) { best = s; m.tile = t; m.arm = arms[i]; found = true; }
        }
    }
    return found;
}

}  // namespace ai
