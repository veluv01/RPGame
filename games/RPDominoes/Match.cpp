// A match (Match.h): a small state machine over the rules, turn by turn,
// with a queue of events for the stage to show.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed
#include <string.h>
#include "Match.h"

namespace match {

dom::Round round;
Setup setup;
uint16_t score[2];
uint8_t winner, reason, points, rounds;

enum Phase : uint8_t {
    OFF,
    SET,            // the deal is being shown: then the heaviest double is set
    TURN,           // hand the turn to round.turn
    HUMAN,          // waiting for a tile
    HUMAN_DRAW,     // ... for a draw
    CPU,            // it plays, or draws one
    BETWEEN,        // a round is over, the match goes on
    OVER,           // the match is over
};
static Phase phase;
static uint8_t leader;              // who leads the next round
static dom::Rng rng, cpuRng;

#ifdef MATCH_SCRIPTED
static uint8_t stacked[dom::TILES], nStacked;   // the next deal, and the boneyard's order after it
static uint8_t boneAt;
static bool stackDealt;
#endif

static Event q[8];
static uint8_t qHead, qCount;

static void push(uint8_t type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0) {
    if (qCount == 8) { qHead = (uint8_t)((qHead + 1) & 7); qCount--; }   // never stall: drop the oldest
    Event &e = q[(qHead + qCount++) & 7];
    e.type = type; e.a = a; e.b = b; e.c = c; e.d = d;
}

bool peekEvent(Event &e) {
    if (!qCount) return false;
    e = q[qHead];
    return true;
}

bool popEvent(Event &e) {
    if (!qCount) return false;
    e = q[qHead];
    qHead = (uint8_t)((qHead + 1) & 7);
    qCount--;
    return true;
}

bool isHuman(uint8_t s) { return setup.mode == TWO_PLAYER || s == 0; }
bool active() { return phase != OFF && phase != BETWEEN && phase != OVER; }
bool between() { return phase == BETWEEN; }
bool matchOver() { return phase == OVER; }
bool humanToPlay() { return phase == HUMAN; }
bool humanToDraw() { return phase == HUMAN_DRAW; }

static void newRound() {
    uint8_t order[dom::TILES];
    dom::shuffle(order, rng);
#ifdef MATCH_SCRIPTED
    // The tiles named come first, in the order named; the rest keep theirs.
    if (stackDealt) nStacked = 0;               // (one round only)
    stackDealt = nStacked != 0;
    uint8_t out[dom::TILES], k = 0;
    uint32_t used = 0;
    for (uint8_t i = 0; i < nStacked; i++) { out[k++] = stacked[i]; used |= 1u << stacked[i]; }
    for (uint8_t i = 0; i < dom::TILES; i++) if (!((used >> order[i]) & 1)) out[k++] = order[i];
    memcpy(order, out, sizeof order);
    boneAt = 14;
#endif
    dom::deal(round, setup.game, order, leader);
    qHead = qCount = 0;
    push(EV_START);
    push(EV_DEAL);
    phase = rounds ? TURN : SET;
}

void start(const Setup &s) {
    setup = s;
    if (setup.target < 5) setup.target = 5;
    rng.seed(s.seed, 1);
    cpuRng.seed(s.seed, 2);
    score[0] = score[1] = 0;
    winner = reason = points = rounds = leader = 0;
    newRound();
}

void nextRound() {
    if (phase == BETWEEN) newRound();
}

// The round is over: its points to the winner, and the match goes on or not.
static void finish(uint8_t why) {
    reason = why;
    points = dom::settle(round, why, winner);
    if (winner < 2) { score[winner] = (uint16_t)(score[winner] + points); leader = winner; }
    else leader ^= 1;
    rounds++;
    uint16_t top = score[0] > score[1] ? score[0] : score[1];
    phase = top >= target() && score[0] != score[1] ? OVER : BETWEEN;
    push(EV_ROUND, winner, why, points);
}

static void moved() {
    uint8_t why;
    if (dom::over(round, why)) finish(why);
    else phase = TURN;
}

// What the side can do now: play, draw, or (told at once) pass.
static void ready(uint8_t s) {
    if (dom::canPlay(round, s)) phase = isHuman(s) ? HUMAN : CPU;
    else if (round.bone) phase = isHuman(s) ? HUMAN_DRAW : CPU;
    else {
        dom::passed(round, s);
        push(EV_PASS, s);
        moved();
    }
}

static void playNow(uint8_t s, uint8_t tile, uint8_t arm) {
    uint8_t pts = dom::place(round, s, tile, arm);
    score[s] = (uint16_t)(score[s] + pts);
    push(EV_PLAY, s, tile, arm, pts);
    moved();
}

static void drawNow(uint8_t s) {
    uint8_t tile = dom::nth(round.bone, rng.below(dom::count(round.bone)));
#ifdef MATCH_SCRIPTED
    while (boneAt < nStacked) {                             // a scripted boneyard
        uint8_t t = stacked[boneAt++];
        if ((round.bone >> t) & 1) { tile = t; break; }
    }
#endif
    dom::drew(round, s, tile);
    push(EV_DRAW, s, tile);
    ready(s);
}

void update(bool stageBusy) {
    if (stageBusy || qCount) return;
    uint8_t s = round.turn;
    switch (phase) {
        case SET: {
            uint8_t tile;
            s = dom::opener(round, tile);
            push(EV_TURN, s, isHuman(s), 1);
            playNow(s, tile, 0);
            break;
        }
        case TURN:
            push(EV_TURN, s, isHuman(s), 0);
            ready(s);
            break;
        case CPU: {
            ai::Move m;
            if (ai::choose(round, s, setup.level, cpuRng, m)) playNow(s, m.tile, m.arm);
            else drawNow(s);
            break;
        }
        default:
            break;
    }
}

uint8_t options(uint8_t tile, uint8_t *arms) {
    if (phase != HUMAN || tile >= dom::TILES || !((round.hand[round.turn] >> tile) & 1)) return 0;
    return dom::options(round, tile, arms);
}

bool play(uint8_t tile, uint8_t arm) {
    uint8_t arms[dom::ARMS];
    if (!options(tile, arms) || arm >= dom::ARMS || !((dom::armsFor(round, tile) >> arm) & 1)) return false;
    playNow(round.turn, tile, arm);
    return true;
}

void draw() {
    if (phase == HUMAN_DRAW) drawNow(round.turn);
}

bool hint(ai::Move &m) {
    dom::Rng r = cpuRng;
    return phase == HUMAN && ai::choose(round, round.turn, ai::SHARK, r, m);
}

// ---------------------------------------------------------------------------
// Saved games
// ---------------------------------------------------------------------------
// Between rounds, the screens begin the next round before saving (nextRound),
// so a saved game is never between two.
void save(Record &r) {
    memset(&r, 0, sizeof r);
    r.round = round;
    r.setup = setup;
    r.score[0] = score[0]; r.score[1] = score[1];
    r.rng[0] = rng.state; r.rng[1] = cpuRng.state;
    r.leader = leader; r.rounds = rounds;
}

bool load(const Record &r) {
    dom::Round c = r.round;
    uint8_t why;
    bool ok = dom::rebuild(c) && !dom::over(c, why) && r.setup.mode <= TWO_PLAYER && r.setup.level < LEVELS &&
              r.setup.target >= 5 && r.leader <= 1 && r.rng[0] && r.rng[1];
    if (!ok) return false;
    round = c;
    setup = r.setup;
    score[0] = r.score[0]; score[1] = r.score[1];
    rng.state = r.rng[0]; cpuRng.state = r.rng[1];
    leader = r.leader; rounds = r.rounds;
#ifdef MATCH_SCRIPTED
    nStacked = 0;
#endif
    qHead = qCount = 0;
    push(EV_START, 1);
    phase = !round.n && !rounds ? SET : TURN;
    return true;
}

#ifdef MATCH_SCRIPTED
void stackDeal(const char *p) {
    nStacked = 0;
    uint32_t used = 0;
    for (; *p && nStacked < dom::TILES; p++) {
        if (*p < '0' || *p > '6' || p[1] < '0' || p[1] > '6') continue;
        uint8_t t = dom::tileOf((uint8_t)(p[0] - '0'), (uint8_t)(p[1] - '0'));
        p++;
        if ((used >> t) & 1) continue;
        used |= 1u << t;
        stacked[nStacked++] = t;
    }
    stackDealt = false;
}

void setScore(uint16_t a, uint16_t b) {
    score[0] = a; score[1] = b;
}

void endRound(uint8_t w) {
    if (!active() || !round.n) return;
    round.bone |= round.hand[w & 1];
    round.hand[w & 1] = 0;
    moved();
}
#endif

}  // namespace match
