// A game's flow: turns, the CPU's thinking a slice a tick, and choosing the
// dealer's lines from what each move changed (see Game.h).
#pragma GCC optimize("Os", "no-ipa-sra")
#include <string.h>
#include "Game.h"

// How much the CPU thinks in one tick. On the board it searches until a few
// milliseconds have gone (a frame is 16.7, and has to be drawn too); in the
// simulator and the host tests, where time does not pass while code runs, a
// fixed number of positions, so a scripted run is the same every time.
#if defined(__riscv) && !defined(CHSIM)
#include <Arduino.h>
#define CHF4_AI_SLICE_US 5000
static bool think() {
    uint32_t t0 = micros();
    do {
        if (ai::step(16)) return true;
    } while (micros() - t0 < CHF4_AI_SLICE_US);
    return false;
}
#else
static bool think() { return ai::step(150); }
#endif

namespace game {

using c4::Bits;
using c4::count;
using c4::immediate;

c4::Board board;
Setup setup;
uint8_t winner = c4::NOBODY;
uint8_t four[4];
char said[taunt::LINE_MAX];
uint8_t lastKind = 0xFF;

static uint8_t turn;
static bool playing, thinking, found, spoke;
static uint16_t thinkT, idleT;
static uint8_t quiet;               // moves since the dealer last spoke
static uint8_t pre;                 // the line he has ready for his move (a taunt::Kind), NONE, or SAID
static uint8_t once;                // lines said once a game (bits)
enum : uint8_t { ONCE_FORCED = 1, ONCE_LOSING = 2, ONCE_DRAWISH = 4, ONCE_IDLE = 8 };
constexpr uint8_t NONE = 0xFF, SAID = 0xFE;
static c4::Rng rng;

// --- Events ---------------------------------------------------------------
static Event queue[8];
static uint8_t qHead, qTail;

static void push(uint8_t type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0) {
    Event &e = queue[qTail];
    e.type = type; e.a = a; e.b = b; e.c = c;
    qTail = (uint8_t)((qTail + 1) & 7);
}

bool popEvent(Event &e) {
    if (qHead == qTail) return false;
    e = queue[qHead];
    qHead = (uint8_t)((qHead + 1) & 7);
    return true;
}

// --- The dealer's lines ---------------------------------------------------
static void say(uint8_t kind, bool wait, uint8_t number = 0) {
    uint8_t face = taunt::pick(kind, rng, said, number);
    lastKind = kind;
    quiet = 0;
    spoke = true;
    push(EV_SAY, face, wait);
}

// Small talk: only now and then.
static bool chatty() { return quiet >= 3 && rng.below(3) == 0; }

static bool vsCpu() { return setup.mode == VS_CPU; }
static bool human(uint8_t s) { return !vsCpu() || s == YOU; }

static void turnTo(uint8_t s) {
    turn = s;
    thinking = found = false;
    thinkT = idleT = 0;
    push(EV_TURN, s, human(s));
}

static void begin(const Setup &s) {
    setup = s;
    rng.s = s.seed | 1;
    winner = c4::NOBODY;
    playing = true;
    qHead = qTail = 0;
    quiet = 0; once = 0; pre = NONE; spoke = false;
    lastKind = NONE;
    taunt::reset();
}

void start(const Setup &s) {
    begin(s);
    c4::reset(board);
    push(EV_START);
    say(vsCpu() ? taunt::START : taunt::P2_START, false);
    turnTo(s.first & 1);
}

static void finish(uint8_t who) {
    winner = who;
    playing = thinking = false;
    uint8_t kind = who == c4::NOBODY ? taunt::DRAWN : !vsCpu() ? taunt::P2_WON : who == YOU ? taunt::HE_LOST : taunt::HE_WON;
    uint8_t face = taunt::pick(kind, rng, said);
    lastKind = kind;
    push(EV_OVER, who, face);
}

// A disc goes in; then what it changed. `mine` and `theirs` are each side's
// immediate wins as they stood before it.
static void move(uint8_t s, uint8_t col) {
    Bits mine = immediate(board, s), theirs = immediate(board, s ^ 1);
    uint8_t row = c4::play(board, s, col);
    push(EV_DROP, s, col, row);
    quiet++;
    spoke = false;
    if (c4::fourThrough(board.side[s], col, row, four)) { finish(s); return; }
    if (c4::full(board)) { finish(c4::NOBODY); return; }
    Bits now = immediate(board, s), their = immediate(board, s ^ 1);
    bool blocked = theirs && !their;
    if (!vsCpu()) {
        if (mine) say(taunt::P2_MISSED, false);
        else if (count(now) >= 2) say(taunt::P2_DOUBLE, false);
        else if (blocked) say(taunt::P2_BLOCK, false);
        else if (now && chatty()) say(taunt::P2_THREAT, false);
        else if (quiet >= 6 && rng.below(4) == 0) say(taunt::P2_BANTER, false);
    } else if (s == YOU) {
        pre = NONE;
        if (their) {
            // He wins next move: he says so as he plays it.
            // (Not a slip of yours if he had it coming: two ways to win, or
            // a win he has already announced.)
            pre = count(theirs) >= 2 || (once & ONCE_FORCED) ? taunt::WIN_NOW : (theirs & their) ? taunt::UNBLOCKED : taunt::GIFT;
        } else if (mine) say(taunt::MISSED, false);
        else if (count(now) >= 2) say(taunt::DOUBLE, false);
        else if (blocked) say(taunt::BLOCKED, false);
        else if (board.n <= 2 && col == 3 && rng.below(2)) say(taunt::OPEN_CENTRE, false);
        else if (board.n <= 2 && (col == 0 || col == 6)) say(taunt::OPEN_EDGE, false);
        else if (board.n >= 34 && !(once & ONCE_DRAWISH) &&
                 !c4::winning(board.side[0], board.side[0] | board.side[1]) &&
                 !c4::winning(board.side[1], board.side[0] | board.side[1])) {
            once |= ONCE_DRAWISH;
            say(taunt::DRAWISH, false);
        }
    } else {
        if (count(now) >= 2) say(taunt::TRAP, false);
        else if (now && chatty()) say(taunt::THREAT, false);
    }
    turnTo(s ^ 1);
}

// The CPU has chosen: what does he say as he plays? True if he says it
// (and the game waits for him to finish).
static bool preamble() {
    int16_t sc = ai::score();
    if (pre != NONE) { say(pre, true); return true; }
    if (spoke) return false;                                // he has only just had his say
    if (count(immediate(board, YOU)) == 1) { say(taunt::MUST_BLOCK, true); return true; }
    if (ai::winning(sc) && !(once & ONCE_FORCED)) {
        once |= ONCE_FORCED;
        say(taunt::FORCED, true, ai::movesToWin(sc));
        return true;
    }
    if (ai::losing(sc) && !(once & ONCE_LOSING)) {
        once |= ONCE_LOSING;
        say(taunt::LOSING, true);
        return true;
    }
    if (!chatty()) return false;
    say(sc > 12 ? taunt::AHEAD : sc < -12 ? taunt::BEHIND : taunt::BANTER, true);
    return true;
}

void update(bool busy) {
    if (!playing) return;
    if (human(turn)) {
        if (vsCpu() && ++idleT == 900 && !(once & ONCE_IDLE)) {
            once |= ONCE_IDLE;
            say(taunt::IDLE, false);
        }
        return;
    }
    if (!thinking) {
        ai::start(board, turn, setup.level, rng);
        thinking = true;
    }
    if (thinkT < 0xFFFF) thinkT++;
    if (!found) found = think();
    // He takes a moment even over the obvious, and never plays over his own
    // words or a disc still falling.
    if (!found || thinkT < (setup.quick ? 12 : 40) || busy) return;
    if (pre != SAID) {
        bool talks = preamble();
        pre = SAID;                                         // ... or he had nothing to say
        if (talks) return;
    }
    pre = NONE;
    move(turn, ai::chosen());
}

bool active() { return playing; }
uint8_t side() { return turn; }
bool humanToMove() { return playing && human(turn); }
bool cpuThinking() { return playing && !human(turn); }
uint8_t thinkColumn() { return ai::considering(); }

bool drop(uint8_t col) {
    if (!humanToMove() || !c4::canPlay(board, col)) return false;
    move(turn, col);
    return true;
}

void resign() {
    if (!humanToMove()) return;
    memset(four, 0xFF, sizeof four);
    finish(turn ^ 1);
}

void save(Record &r) {
    r.board = board;
    r.setup = setup;
    r.turn = turn;
    r.flags = once;
}

bool load(const Record &r) {
    // Trust nothing: the heights must match the discs, the sides not overlap.
    Bits occ = r.board.side[0] | r.board.side[1];
    if ((r.board.side[0] & r.board.side[1]) || (occ & ~c4::BOARD) || r.turn > 1 || r.setup.mode > TWO_PLAYER ||
        r.setup.level >= LEVELS) return false;
    uint8_t n = 0;
    for (uint8_t c = 0; c < c4::COLS; c++) {
        uint8_t h = r.board.h[c];
        for (uint8_t w = 0; w < 7; w++)
            if (((occ & c4::cell(c, w)) != 0) != (w < h)) return false;
        if (h > c4::ROWS) return false;
        n = (uint8_t)(n + h);
    }
    if (n != r.board.n || n >= c4::CELLS || c4::hasFour(r.board.side[0]) || c4::hasFour(r.board.side[1])) return false;
    begin(r.setup);
    rng.s ^= (uint32_t)n * 2654435761u;
    board = r.board;
    once = r.flags;
    push(EV_START);
    turnTo(r.turn);
    return true;
}

bool startPosition(const Setup &s, const char *moves) {
    begin(s);
    c4::reset(board);
    uint8_t t = s.first & 1;
    for (; *moves >= '1' && *moves <= '7'; moves++, t ^= 1) {
        uint8_t col = (uint8_t)(*moves - '1');
        if (!c4::canPlay(board, col)) return false;
        c4::play(board, t, col);
    }
    if (c4::hasFour(board.side[0]) || c4::hasFour(board.side[1]) || c4::full(board)) return false;
    push(EV_START);
    turnTo(t);
    return true;
}

}  // namespace game
