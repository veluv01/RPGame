// The rules (Game.h): a phase machine stepped once a tick. It queues
// events for the stage and waits while the stage is busy showing them.
#pragma GCC optimize("Os")   // cold code: size over speed
#include <string.h>
#include "config.h"
#include "Game.h"
#include "Cpu.h"

namespace game {

using namespace layout;

State st;

static uint8_t ph;                          // Phase
static uint8_t dice[2], steps;              // the dice just thrown; the move to make
static bool again;                          // ... and whether it earns another roll
static State mark;                          // the game as the turn began

static Event q[16];
static uint8_t qHead, qCount;

static void push(uint8_t type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, int amount = 0) {
    if (qCount == 16) { qHead = (uint8_t)((qHead + 1) & 15); qCount--; }    // never stall: drop the oldest
    Event &e = q[(qHead + qCount++) & 15];
    e.type = type; e.a = a; e.b = b; e.c = c; e.amount = (int16_t)amount;
}

bool peekEvent(Event &e) {
    if (!qCount) return false;
    e = q[qHead];
    return true;
}

bool popEvent(Event &e) {
    if (!peekEvent(e)) return false;
    qHead = (uint8_t)((qHead + 1) & 15);
    qCount--;
    return true;
}

// The game's own random stream (presentation has fx::rnd): the same seed and
// the same presses play the same game.
static uint32_t rnd() {
    uint32_t x = st.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return st.rng = x;
}

#if CHGAME_DEBUG || defined(CHTEST)
static uint8_t forced[8], nForced;
void forceDice(uint8_t d1, uint8_t d2) {
    if (nForced + 2 <= 8) { forced[nForced++] = d1; forced[nForced++] = d2; }
}
static uint8_t throwDie() {
    while (nForced) {
        uint8_t d = forced[0];
        memmove(forced, forced + 1, --nForced);
        if (d) return d;                                // (0: no die forced in this place)
    }
    return (uint8_t)(1 + rnd() % 6);
}
#else
static uint8_t throwDie() { return (uint8_t)(1 + rnd() % 6); }
#endif

Phase phase() { return (Phase)ph; }
uint8_t die(uint8_t i) { return dice[i & 1]; }
bool humanToAct() { return isHuman(st.cur) && (ph == P_ROLL || ph == P_PICK) && !qCount; }

// ---------------------------------------------------------------------------
// The player at the turn
// ---------------------------------------------------------------------------
bool roll() {
    if (ph != P_ROLL) return false;
    dice[0] = throwDie();
    dice[1] = st.mode == ARCADE ? throwDie() : 0;
    bool match = st.mode == ARCADE ? dice[0] == dice[1] : dice[0] == 6;
    again = match && st.streak < MAX_AGAIN;
    push(EV_DICE, dice[0], dice[1], again);
    steps = dice[0];
    ph = st.mode == ARCADE && dice[0] != dice[1] ? P_PICK : P_MOVE;
    return true;
}

bool pick(uint8_t which) {
    if (ph != P_PICK || which > 1) return false;
    steps = dice[which];
    ph = P_MOVE;
    return true;
}

// A ladder or a snake under p, taken.
static void ride(uint8_t p) {
    int k = linkAt(st.pos[p]);
    if (k < 0) return;
    bool up = k < LADDERS;
    push(up ? EV_LADDER : EV_SNAKE, p, LINK[k].from, LINK[k].to);
    uint8_t &n = up ? st.ladders[p] : st.snakes[p];
    if (n < 255) n++;
    st.pos[p] = LINK[k].to;
}

static void move() {
    uint8_t p = st.cur, via;
    landing(st.pos[p], steps, &via);
    push(EV_MOVE, p, st.pos[p], via, steps);
    st.pos[p] = via;
    ride(p);
    uint8_t at = st.pos[p];
    if (at == LAST) {
        st.over = 1;
        st.winner = p;
        push(EV_OVER, p);
        ph = P_OVER;
        return;
    }
    // ARCADE: whoever is there already drops a row (and takes what it lands
    // on). Square 1 is safe.
    if (st.mode == ARCADE && at != 1)
        for (uint8_t r = 0; r < st.players; r++) {
            if (r == p || st.pos[r] != at) continue;
            push(EV_BUMP, r, at, below(at), p);
            st.pos[r] = below(at);
            if (st.bumps[p] < 255) st.bumps[p]++;
            ride(r);
        }
    if (again) {
        st.streak++;
        push(EV_TURN, p, isHuman(p), st.streak);
        ph = P_ROLL;
    } else ph = P_ENDTURN;
}

// ---------------------------------------------------------------------------
// Turns
// ---------------------------------------------------------------------------
static void beginTurn() {
    st.turn++;
    st.streak = 0;
    mark = st;
    mark.turn--;                                        // (restoring begins this turn again)
    push(EV_TURN, st.cur, isHuman(st.cur), 0);
    ph = P_ROLL;
}

void start(const Setup &s) {
    memset(&st, 0, sizeof st);
    st.rng = s.seed ? s.seed : 0x9E3779B9u;
    for (uint8_t k = 0; k < SEATS; k++) {
        if (!s.kind[k]) continue;
        st.pos[st.players] = 1;
        st.kind[st.players++] = s.kind[k];
    }
    st.mode = s.mode;
    qHead = qCount = 0;
    push(EV_START);
    ph = P_TURN;
}

void update(bool stageBusy) {
    if (stageBusy || qCount) return;
    switch (ph) {
        case P_TURN: beginTurn(); break;
        case P_ROLL: if (isCpu(st.cur)) roll(); break;
        case P_PICK:
            if (isCpu(st.cur)) {
                uint8_t w = cpu::pick(st.cur, dice[0], dice[1]);
                push(EV_PICK, st.cur, w);
                pick(w);
            }
            break;
        case P_MOVE: move(); break;
        case P_ENDTURN:
            st.cur = (uint8_t)((st.cur + 1) % st.players);
            ph = P_TURN;
            break;
    }
}

const State &checkpoint() { return mark; }

bool restore(const State &s) {
    if (s.players < 2 || s.players > SEATS || s.cur >= s.players || s.over || s.mode > ARCADE) return false;
    for (uint8_t p = 0; p < s.players; p++)
        if (!s.pos[p] || s.pos[p] >= LAST || !s.kind[p] || s.kind[p] >= CPU + LEVELS) return false;
    st = s;
    qHead = qCount = 0;
    push(EV_START);
    ph = P_TURN;
    return true;
}

}  // namespace game
