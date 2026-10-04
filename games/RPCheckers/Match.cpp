#pragma GCC optimize("Os")   // cold code: size over speed
// A game of checkers (Match.h): the CPU's levels, the step history that undo
// and saved games replay, the event queue and the turn's flow.
#include <string.h>
#include "Match.h"

namespace match {

// Node budget, and how far below the best a step may score and still be
// picked (a man is 100). Not yet timed on the board.
const eng::Level LEVEL[LEVELS] = {
    {300, 60},          // TOURIST
    {4000, 12},         // DEALER
    {30000, 0},         // THE HOUSE
};

uint8_t board[64];
Setup setup;
uint8_t lastFrom = 0xFF, lastTo = 0xFF;
Result result;
Reason reason;
uint16_t plies;

enum Phase : uint8_t { OFF, WAIT, HUMAN, CPU, CPU_PICKED, OVER };
static Phase phase;
static int16_t cpuStep;
static bool abortReq, inThink;
static uint8_t hopN;                 // jumps so far in the move being played
static uint8_t moveFrom;             // where it started

// Steps since `base` (the start position, or a snapshot once the game gets
// long), each the index of the step among those legal when it was played.
// Undo and saved games replay from it.
static const uint8_t HIST = sizeof(((Record *)0)->m);
static uint8_t hist[HIST];
static uint8_t nh;
static uint8_t movesDone;            // complete moves among them
static eng::Snap base;
static bool baseStart;

static Event q[8];
static uint8_t qHead, qCount;

static Event &push(uint8_t type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0) {
    if (qCount == 8) { qHead = (uint8_t)((qHead + 1) & 7); qCount--; }   // never stall: drop the oldest
    Event &e = q[(qHead + qCount++) & 7];
    memset(&e, 0, sizeof e);
    e.type = type; e.a = a; e.b = b; e.c = c;
    return e;
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

static bool isHuman(bool black) { return setup.mode == TWO_PLAYER || black == (setup.humanBlack != 0); }

static void sync() {
    for (uint8_t s = 0; s < 64; s++) board[s] = eng::pieceAt(s);
    plies = eng::ply();
}

static void toBase() {
    if (baseStart) eng::newGame(setup.rules); else eng::restore(base, setup.rules);
}

// Replay from the base until `moves` moves are complete, then (the rest of
// a multiple jump) on to step `upTo` at least. Returns the steps played.
static uint8_t replay(uint8_t moves, uint8_t upTo) {
    toBase();
    uint8_t i = 0, m = 0;
    while (i < nh && (m < moves || i < upTo))
        if (eng::play(hist[i++])) m++;
    movesDone = m;
    return i;
}

// Fold the older half of the moves into the base snapshot (between moves).
static void rebase() {
    uint8_t all = nh, k = replay((uint8_t)(movesDone / 2), 0), rest = movesDone;
    eng::snapshot(base);
    baseStart = false;
    memmove(hist, hist + k, (size_t)(all - k));
    nh = (uint8_t)(all - k);
    replay(0xFF, nh);
    (void)rest;
}

void start(const Setup &s) {
    setup = s;
    eng::seed(s.seed);
    eng::newGame(s.rules);
    baseStart = true;
    nh = 0;
    movesDone = 0;
    hopN = 0;
    qHead = qCount = 0;
    result = PLAYING;
    abortReq = false;
    lastFrom = lastTo = 0xFF;
    sync();
    push(EV_START);
    phase = WAIT;
}

static void finish(Result r, Reason why) {
    result = r;
    reason = why;
    phase = OVER;
    push(EV_OVER, r, why);
}

// Between moves: the verdicts. True if the game goes on.
static bool judge() {
    bool black = eng::blackToMove();
    switch (eng::status()) {
        case eng::LOST_NO_PIECES:  finish(black ? WHITE_WINS : BLACK_WINS, BY_CAPTURE); return false;
        case eng::LOST_BLOCKED:    finish(black ? WHITE_WINS : BLACK_WINS, BY_BLOCK); return false;
        case eng::DRAW_40:         finish(DRAW_40, BY_RULE); return false;
        case eng::DRAW_REPETITION: finish(DRAW_REPETITION, BY_RULE); return false;
        default: return true;
    }
}

#if defined(CHSIM) || defined(CHTEST)
void startAt(const Setup &s, const char *cells, bool black) {
    start(s);
    eng::setup(cells, black, s.rules);
    eng::snapshot(base);             // the position becomes the base: undo stops here
    baseStart = false;
    sync();
    qHead = qCount = 0;
    push(EV_START);
    judge();
}
#endif

bool active() { return phase != OFF && phase != OVER; }
bool blackToMove() { return eng::blackToMove(); }
bool humanToMove() { return phase == HUMAN; }
bool cpuThinking() { return inThink; }
uint8_t chainSq() { return eng::chainSq(); }
bool mustJump() { return eng::chainSq() != eng::NONE || ((setup.rules & eng::R_FORCED) && eng::canJump()); }

static void doStep(uint8_t index) {
    eng::Step s;
    if (!eng::stepAt(index, s)) return;
    bool human = phase == HUMAN;
    if (eng::chainSq() == eng::NONE) {
        if (nh > HIST - 14) rebase();           // room for the longest multiple jump
        hopN = 0;
        moveFrom = s.from;
    }
    Event &e = push(EV_HOP, s.from, s.to);
    e.piece = board[s.from];
    e.capSq = s.cap;
    if (s.cap != eng::NONE) { e.captured = board[s.cap]; e.hop = ++hopN; }
    bool done = eng::play(index);
    hist[nh++] = index;
    sync();
    if (board[s.to] != e.piece) e.flags |= H_CROWN;
    lastFrom = moveFrom; lastTo = s.to;
    if (!done) { phase = human ? HUMAN : CPU; return; }
    e.flags |= H_LAST;
    movesDone++;
    if (judge()) phase = WAIT;
    else if (reason != BY_RULE) e.flags |= H_FINAL;
}

void update(bool stageBusy) {
    switch (phase) {
        case WAIT:
            if (stageBusy || qCount) break;
            {
                bool black = eng::blackToMove();
                bool human = isHuman(black);
                push(EV_TURN, black, human, mustJump());
                phase = human ? HUMAN : CPU;
            }
            break;
        case CPU:
            if (stageBusy || qCount) break;
            if (eng::chainSq() == eng::NONE) push(EV_THINK);
            inThink = true;
            cpuStep = eng::think(LEVEL[setup.level]);
            inThink = false;
            if (abortReq || cpuStep < 0) {
                abortReq = false;
                phase = WAIT;          // whatever asked for the abort decides what's next
                break;
            }
            {
                eng::Step s;
                eng::stepAt((uint8_t)cpuStep, s);
                push(EV_PICK, s.from, s.to, eng::chainSq() != eng::NONE).captured = s.cap != eng::NONE;
            }
            phase = CPU_PICKED;
            break;
        case CPU_PICKED:
            if (stageBusy || qCount) break;
            phase = CPU;
            doStep((uint8_t)cpuStep);
            break;
        default:
            break;
    }
}

uint8_t movesFrom(uint8_t from, uint8_t *to, uint8_t *capture) {
    if (phase != HUMAN) return 0;
    eng::Step s[16];
    uint8_t n = eng::stepsFrom(from, s, 16);
    for (uint8_t i = 0; i < n; i++) { to[i] = s[i].to; capture[i] = s[i].cap != eng::NONE; }
    return n;
}

bool play(uint8_t from, uint8_t to) {
    if (phase != HUMAN) return false;
    int16_t i = eng::indexOf(from, to);
    if (i < 0) return false;
    doStep((uint8_t)i);
    return true;
}

// Moves to take back so that a human is to move again. A multiple jump
// half made is taken back first (and is all, if it is your own).
static bool partial() { return eng::chainSq() != eng::NONE; }
static int8_t undoCount() {
    bool cpuToMove = setup.mode == VS_CPU && !isHuman(eng::blackToMove());
    uint8_t k = setup.mode == TWO_PLAYER ? !partial() : cpuToMove ? 1 : partial() ? 0 : 2;
    return movesDone >= k ? (int8_t)k : (int8_t)-1;
}

bool canUndo() {
    if (inThink || phase == OFF) return false;
    int8_t k = undoCount();
    return k > 0 || (k == 0 && partial());
}

bool undo() {
    if (!canUndo()) return false;
    nh = replay((uint8_t)(movesDone - undoCount()), 0);
    sync();
    hopN = 0;
    lastFrom = lastTo = 0xFF;
    result = PLAYING;
    qHead = qCount = 0;
    judge();
    push(EV_START);
    phase = WAIT;
    return true;
}

void resign() {
    if (!active()) return;
    bool black = setup.mode == TWO_PLAYER ? eng::blackToMove() : setup.humanBlack != 0;
    finish(black ? WHITE_WINS : BLACK_WINS, BY_RESIGNATION);
}

void abortThink() {
    if (!inThink) return;
    abortReq = true;
    eng::abort();
}

// ---------------------------------------------------------------------------
// Saved games
// ---------------------------------------------------------------------------
void save(Record &r) {
    r.base = base;
    r.baseIsStart = baseStart;
    r.mode = setup.mode; r.humanBlack = setup.humanBlack; r.level = setup.level; r.rules = setup.rules;
    r.n = nh;
    memcpy(r.m, hist, nh);
}

bool load(const Record &r) {
    if (r.n > HIST || r.mode > TWO_PLAYER || r.level >= LEVELS || r.rules > eng::R_ALL) return false;
    Setup s = {r.mode, r.humanBlack, r.level, r.rules, setup.seed};
    start(s);
    base = r.base;
    baseStart = r.baseIsStart != 0;
    // Replay, checking every step is legal where it is played.
    toBase();
    for (uint8_t i = 0; i < r.n; i++) {
        if (r.m[i] >= eng::stepCount()) { start(s); return false; }
        if (eng::play(r.m[i])) movesDone++;
        hist[i] = r.m[i];
    }
    nh = r.n;
    sync();
    hopN = 0;
    qHead = qCount = 0;
    if (!judge()) return false;
    push(EV_START);
    phase = WAIT;
    return true;
}

}  // namespace match
