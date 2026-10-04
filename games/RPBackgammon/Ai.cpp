// The CPU's choice of play (see Ai.h): every play of the roll through the
// network, then, for the strongest, a roll deeper for the best few.
#pragma GCC optimize("Os", "no-ipa-sra")
#include <string.h>
#include "Ai.h"
#include "Net.h"
#include "Race.h"

namespace ai {

// Scores are the network's (Net.h: 4096 to one unit of log-odds). A race
// count is 1/16 roll; RACE_UNIT puts a roll at a quarter of a unit.
static const int32_t WIN = 0x3FFFFFFF;
static const int32_t RACE_UNIT = RACE_ROLL / 16;
static const uint8_t KEEP = 10;             // the plays the grandmaster looks at a roll deeper
static const int32_t MARGIN = 1640;         // ... those within 0.4 of its first favourite
static const uint32_t CAP = 14000;          // ... and positions to spend, at most
#ifndef AI_SLIPS
#define AI_SLIPS 3
#define AI_SLIP 1600
#endif
static const uint8_t SLIPS = AI_SLIPS;      // the beginner plays any of its best few, as likely one as another
static const int32_t SLIP = AI_SLIP;        // ... that are not worse than its best by more than this

struct Cand { int32_t score; Play play; };

static bg::Board root, work, reply;
static bg::Plays g1, g2;
static bg::Rng *rng;
static Cand cand[KEEP];
static uint8_t nCand, keep, side, level;
static uint8_t phase;                       // 0 every play once, 1 the best few again, 2 chosen
static bool racing;
static uint32_t count;
// The second look: candidate ci against every roll (ra >= rb) of the other side.
static uint8_t ci, ra, rb, bestCi, weightLeft;
static bool rollOpen, haveBest;
static int32_t rollBest;
static uint32_t sum, bestSum;

static void openCand() {
    work = root;
    const Play &p = cand[ci].play;
    for (uint8_t i = 0; i < p.n; i++) bg::doStep(work, side, p.from[i], p.die[i]);
    reply = work;
    ra = rb = 1;
    sum = 0;
    weightLeft = 36;
    rollOpen = false;
}

int32_t judge(const bg::Board &b) {
    uint8_t w;
    if (bg::result(b, w)) return WIN;
    return racing ? -(int32_t)race::cost(b, side) * RACE_UNIT : net::eval(b, side);
}

void start(const bg::Board &b, uint8_t s, uint8_t d1, uint8_t d2, uint8_t lvl, bg::Rng &noise) {
    root = work = b;
    side = s; level = lvl; rng = &noise;
    nCand = 0; count = 0; phase = 0;
    racing = !bg::contact(b);
    // A race is played by the count at every level: even a beginner bears off.
    keep = racing ? 1 : lvl == BEGINNER ? SLIPS : lvl == GRANDMASTER ? KEEP : 1;
    bg::begin(g1, work, s, d1, d2);
}

bool step(uint16_t n) {
    while (n && phase != 2) {
        uint8_t w;
        if (phase == 0) {
            if (!bg::next(g1, work)) {
                // Seen them all. A deeper look only if there is a choice worth it.
                int32_t margin = level == BEGINNER ? SLIP : MARGIN;
                while (nCand > 1 && cand[nCand - 1].score < cand[0].score - margin) nCand--;
                if (level == BEGINNER) cand[0] = cand[rng->next() % nCand];
                if (nCand < 2 || level == BEGINNER) { phase = 2; break; }
                phase = 1; ci = bestCi = 0; haveBest = false;
                openCand();
                continue;
            }
            n--; count++;
            Cand c;
            c.play.n = g1.depth;
            memcpy(c.play.from, g1.from, 4);
            memcpy(c.play.die, g1.die, 4);
            if (bg::result(work, w)) {                      // it wins: look no further
                c.score = WIN;
                cand[0] = c; nCand = 1; phase = 2;
                break;
            }
            c.score = racing ? -(int32_t)race::cost(work, side) * RACE_UNIT : net::eval(work, side);
            if (nCand < keep || c.score > cand[nCand - 1].score) {
                uint8_t i = nCand < keep ? nCand++ : (uint8_t)(keep - 1);
                for (; i && cand[i - 1].score < c.score; i--) cand[i] = cand[i - 1];
                cand[i] = c;
            }
            continue;
        }
        // The second look: the other side's best answer to each of its rolls.
        if (!rollOpen) {
            bg::begin(g2, reply, side ^ 1, ra, rb);
            rollBest = -WIN;
            rollOpen = true;
        }
        if (bg::next(g2, reply)) {
            n--; count++;
            int32_t v = bg::result(reply, w) ? WIN : net::eval(reply, side ^ 1);
            if (v > rollBest) rollBest = v;
            continue;
        }
        rollOpen = false;
        uint8_t wgt = ra == rb ? 1 : 2;                     // 6-5 comes up twice as often as 6-6
        uint32_t theirs = rollBest == WIN ? 65535u : net::chance(rollBest);
        sum += wgt * (65535u - theirs);
        weightLeft = (uint8_t)(weightLeft - wgt);
        if (++rb > ra) { rb = 1; ra++; }
        bool all = ra > 6;
        // Give up on a play that can no longer beat the best so far.
        if (!all && !(haveBest && sum + weightLeft * 65535u <= bestSum)) continue;
        if (all && (!haveBest || sum > bestSum)) { bestSum = sum; bestCi = ci; haveBest = true; }
        if (++ci >= nCand || count >= CAP) {
            cand[0] = cand[bestCi];
            phase = 2;
        } else openCand();
    }
    return phase == 2;
}

void chosen(Play &p) { p = cand[0].play; }
int32_t bestScore() { return cand[0].score; }
bool racingNow() { return racing; }

uint8_t considering() {
    if (phase == 0) return g1.depth ? g1.from[0] : 0;
    return cand[phase == 1 ? ci : 0].play.n ? cand[phase == 1 ? ci : 0].play.from[0] : 0;
}

uint32_t positions() { return count; }

}  // namespace ai
