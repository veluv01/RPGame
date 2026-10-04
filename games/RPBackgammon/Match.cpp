// A match's flow (see Match.h): the turns, the cube, the CPU thinking a slice
// a tick, the events for the stage, and saving and setting up positions.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed
#include <string.h>
#include "Match.h"
#include "Net.h"
#include "Cube.h"

namespace match {

// Positions the CPU weighs per tick. A tick has ~8 ms to spare while the
// previous frame goes out; with the network and the move generator in SRAM
// this should keep it well inside that on the board (an estimate, ~60 us a
// position: to be measured there, tools/scripts/device_think.txt).
static const uint16_t QUANTUM = 64;
static const uint8_t MAX_CUBE = 6;  // 64

bg::Board board;
Setup setup;
uint8_t winner, how, reason, points;
uint8_t score[2], cube, cubeOwner = CENTRE;
bool crawford;
uint16_t turns;
bool autoPlay, beavers;

enum Phase : uint8_t {
    OFF,
    OPENING,        // waiting for the opening roll to be thrown
    DEALT,          // a turn's dice are on the table already (the opening roll, a restored game)
    TURN,           // hand the turn to `sideNow`
    ROLL,           // a human's turn: waiting for the throw (or a double)
    CPU_ROLL,       // the CPU's: it may double, then throws
    DOUBLED,        // a human has been doubled: take, pass or beaver
    CPU_ANSWER,     // the CPU has been doubled
    BEAVERED,       // a human's double has been beavered: take, or raccoon
    CPU_RACCOON,    // the CPU's has
    HUMAN,          // moving checkers
    CONFIRM,        // all played: pick up the dice
    NO_MOVE,        // nothing can be played: the dice go back once that has been shown
    CPU_THINK,
    CPU_PLAY,
    BETWEEN,        // a game is over, the match goes on
    OVER,           // the match is over
};
static Phase phase;
static uint8_t sideNow;
static bg::Turn turn;
static uint8_t group[4];            // per step played: 1 if it began a move (a take-back undoes whole moves)
static bg::Board turnBoard;         // the position as the turn began
static uint8_t rolled[2];
static bool hasRoll;
static bool crawfordDone;           // the Crawford game has been played (or is being)
static bool nextCrawford;
static bg::Rng dice, cpuRng;
static uint64_t cpuRngAtTurn;
static ai::Play cpuPlay;
static uint8_t cpuStep;
static uint8_t offer;               // the value on the table while a double is answered (log2)

#ifdef MATCH_SCRIPTED
static char stacked[40];
static uint8_t stackedAt;
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

bool isHuman(uint8_t s) { return setup.mode == TWO_PLAYER || s == bg::WHITE; }
bool active() { return phase != OFF && phase != BETWEEN && phase != OVER; }
bool between() { return phase == BETWEEN; }
bool matchOver() { return phase == OVER; }
uint8_t side() { return sideNow; }
uint8_t die(uint8_t i) { return rolled[i & 1]; }
const bg::Board &turnStart() { return turnBoard; }
bool humanToRoll() { return phase == OPENING || phase == ROLL; }
bool humanToMove() { return phase == HUMAN; }
bool humanToConfirm() { return phase == CONFIRM; }
bool humanToAnswer() { return phase == DOUBLED || phase == BEAVERED; }
bool beavered() { return phase == BEAVERED; }
bool cpuThinking() { return phase == CPU_THINK; }
bool cubeLive() { return setup.length > 1; }
bool postCrawford() { return crawfordDone && !crawford; }

static uint8_t throwDie() {
#ifdef MATCH_SCRIPTED
    if (stacked[stackedAt]) return (uint8_t)(stacked[stackedAt++] - '0');
#endif
    return dice.die();
}

// A game from the starting position.
static void newGame() {
    bg::reset(board);
    qHead = qCount = 0;
    turns = 0;
    cube = 0;
    cubeOwner = CENTRE;
    crawford = nextCrawford;
    nextCrawford = false;
    sideNow = bg::WHITE;
    hasRoll = false;
    push(EV_START);
    phase = OPENING;
}

static void begin(const Setup &s) {
    setup = s;
    if (setup.length < 1) setup.length = 1;
    dice.seed(s.seed, 1);
    cpuRng.seed(s.seed, 2);
    score[0] = score[1] = 0;
    winner = how = reason = points = 0;
    crawford = crawfordDone = nextCrawford = false;
    newGame();
}

void start(const Setup &s) {
#ifdef MATCH_SCRIPTED
    stacked[0] = 0; stackedAt = 0;
#endif
    begin(s);
}

void nextGame() {
    if (phase == BETWEEN) newGame();
}

// The game is over: its points to the winner, and the match goes on or not.
static void finish(uint8_t w, uint8_t h, uint8_t why, uint8_t pts) {
    winner = w; how = h; reason = why; points = pts;
    score[w] = (uint8_t)(score[w] + pts);
    if (crawford) crawford = false;
    else if (!crawfordDone && setup.length > 1 && score[w] + 1 == setup.length && score[w ^ 1] + 1 < setup.length) {
        // A side has reached match point for the first time: the next game
        // is the Crawford game, with no doubling.
        nextCrawford = crawfordDone = true;
    }
    phase = score[w] >= setup.length ? OVER : BETWEEN;
    push(EV_OVER, w, h, why, pts);
}

// The roll allows one play only (in cpuPlay): nothing for a human to decide.
static bool onlyPlay() {
    bg::Plays g;
    bg::Board b = board;
    uint8_t n = 0;
    bg::begin(g, b, sideNow, rolled[0], rolled[1]);
    while (bg::next(g, b)) {
        if (++n > 1) return false;
        cpuPlay.n = g.depth;
        memcpy(cpuPlay.from, g.from, 4);
        memcpy(cpuPlay.die, g.die, 4);
    }
    return true;
}

// The turn's dice are known: what can be done with them?
static void dealt(uint8_t a, uint8_t b) {
    rolled[0] = a; rolled[1] = b;
    hasRoll = true;
    turnBoard = board;
    cpuRngAtTurn = cpuRng.state;
    bg::beginTurn(turn, board, sideNow, a, b);
    push(EV_ROLL, a, b, turn.need, sideNow);
    if (!turn.need) phase = NO_MOVE;
    else if (isHuman(sideNow) && !(autoPlay && onlyPlay())) phase = HUMAN;
    else if (isHuman(sideNow)) { cpuStep = 0; phase = CPU_PLAY; }
    else {
        push(EV_THINK);
        ai::start(board, sideNow, a, b, setup.level, cpuRng);
        phase = CPU_THINK;
    }
}

static void pickup() {
    push(EV_PICKUP, sideNow);
    turns++;
    sideNow ^= 1;
    hasRoll = false;
    turnBoard = board;
    phase = TURN;
}

// One step of the turn onto the board and into the queue; true if the game
// goes on.
static bool stepNow(uint8_t from, uint8_t die, bool more, bool first) {
    bool hit = bg::playStep(turn, board, sideNow, from, die);
    group[turn.steps - 1] = first;
    push(EV_STEP, from, bg::landing(from, die), (uint8_t)((hit ? F_HIT : 0) | (more ? F_MORE : 0)), die);
    uint8_t w;
    uint8_t h = bg::result(board, w);
    if (!h) return true;
    finish(w, h, BY_PLAY, (uint8_t)(cubeValue() * h));
    return false;
}

void roll() {
    if (phase == OPENING) {
        // A die each; the higher starts, and plays the two as its roll.
        uint8_t a = throwDie(), b = throwDie();
        push(EV_OPENING, a, b);
        if (a == b) return;
        sideNow = a > b ? bg::WHITE : bg::RED;
        rolled[0] = a; rolled[1] = b;
        hasRoll = true;
        turnBoard = board;
        cpuRngAtTurn = cpuRng.state;
        phase = DEALT;
    } else if (phase == ROLL) {
        uint8_t a = throwDie(), b = throwDie();
        dealt(a, b);
    }
}

// ---------------------------------------------------------------------------
// The cube
// ---------------------------------------------------------------------------
static bool mayDouble(uint8_t s) {
    return cubeLive() && !crawford && cube < MAX_CUBE && (cubeOwner == CENTRE || cubeOwner == s);
}

static bool beaverOk() { return beavers && offer < MAX_CUBE; }
bool canBeaver() { return beaverOk() && humanToAnswer(); }

bool canDouble() { return phase == ROLL && mayDouble(sideNow); }

static void doubled(uint8_t s) {
    offer = (uint8_t)(cube + 1);
    push(EV_DOUBLE, s, offer);
    phase = isHuman(s ^ 1) ? DOUBLED : CPU_ANSWER;
}

void offerDouble() {
    if (canDouble()) doubled(sideNow);
}

// The answer is settled: the cube is `owner`'s at the value offered, and
// the side on roll throws.
static void settle(uint8_t owner) {
    cube = offer;
    cubeOwner = owner;
    push(EV_TAKE, owner, cube);
    phase = isHuman(sideNow) ? ROLL : CPU_ROLL;
}

// The doubled side (the one not on roll) takes.
static void taken() { settle(sideNow ^ 1); }

// It beavers: takes, and redoubles at once, keeping the cube. The doubler
// may raccoon (redouble again, and the cube is its own), but cannot pass.
static void beaverNow() {
    push(EV_BEAVER, sideNow ^ 1, ++offer);
    if (offer < MAX_CUBE) phase = isHuman(sideNow) ? BEAVERED : CPU_RACCOON;
    else taken();
}

static void raccoonNow() {
    push(EV_RACCOON, sideNow, ++offer);
    settle(sideNow);
}

static void passed() {
    finish(sideNow, 1, BY_PASS, (uint8_t)cubeValue());
}

void take() { if (phase == DOUBLED || phase == BEAVERED) taken(); }
void pass() { if (phase == DOUBLED) passed(); }
void beaver() { if (phase == DOUBLED && canBeaver()) beaverNow(); }
void raccoon() { if (phase == BEAVERED && canBeaver()) raccoonNow(); }

// The chance (Q16) that side s wins the game from here, s about to roll: the
// network's view of the position after the other side's move.
static uint32_t chanceOnRoll(uint8_t s) {
    return 65535u - net::chance(net::eval(board, s ^ 1));
}

static int need(uint8_t s) { return setup.length - score[s]; }

// ---------------------------------------------------------------------------
void update(bool stageBusy) {
    if (phase == CPU_THINK) {
        // Thinking goes on under whatever the stage is showing.
        if (!ai::step(QUANTUM) || stageBusy || qCount) return;
        ai::chosen(cpuPlay);
        cpuStep = 0;
        phase = CPU_PLAY;
        return;
    }
    if (stageBusy || qCount) return;
    switch (phase) {
        case DEALT:
            push(EV_TURN, sideNow, isHuman(sideNow), 1);
            dealt(rolled[0], rolled[1]);
            break;
        case TURN:
            push(EV_TURN, sideNow, isHuman(sideNow), 0);
            phase = isHuman(sideNow) ? ROLL : CPU_ROLL;
            break;
        case OPENING:
        case ROLL:
            // Nothing to decide (no double to offer): the dice are thrown.
            if (autoPlay && (phase == OPENING || !mayDouble(sideNow))) roll();
            break;
        case CPU_ROLL: {
            uint8_t s = sideNow;
            if (mayDouble(s) && cube::wantsDouble(need(s), need(s ^ 1), cubeValue(), cubeOwner == s, postCrawford(),
                                                   chanceOnRoll(s), cube::gammonish(board, s))) {
                doubled(s);
                break;
            }
            uint8_t a = throwDie(), b = throwDie();
            dealt(a, b);
            break;
        }
        case CPU_ANSWER: {
            uint8_t s = sideNow ^ 1;                // the CPU, doubled
            uint32_t p = 65535u - chanceOnRoll(sideNow);
            if (!cube::wantsTake(need(s), need(s ^ 1), cubeValue(), postCrawford(), p)) passed();
            else if (beaverOk() && p >= cube::BEAVER_AT) beaverNow();
            else taken();
            break;
        }
        case CPU_RACCOON:
            // Beavered: the CPU redoubles again if it still likes its game.
            if (offer < MAX_CUBE && chanceOnRoll(sideNow) >= cube::BEAVER_AT) raccoonNow();
            else taken();
            break;
        case NO_MOVE:
            pickup();
            break;
        case CPU_PLAY:
            if (cpuStep < cpuPlay.n) {
                uint8_t f = cpuPlay.from[cpuStep], d = cpuPlay.die[cpuStep];
                // The same checker travelling on with the next die: one move.
                bool first = !cpuStep || cpuPlay.from[cpuStep] != bg::landing(cpuPlay.from[cpuStep - 1], cpuPlay.die[cpuStep - 1]);
                bool more = cpuStep + 1 < cpuPlay.n && f > d && cpuPlay.from[cpuStep + 1] == f - d;
                cpuStep++;
                stepNow(f, d, more, first);
            } else pickup();
            break;
        default:
            break;
    }
}

uint8_t targetsFrom(uint8_t from, bg::Target *out) {
    if (phase != HUMAN) return 0;
    return bg::targets(turn, board, sideNow, from, out);
}

bool play(uint8_t from, const bg::Target &t) {
    if (phase != HUMAN) return false;
    uint8_t at = from;
    for (uint8_t k = 0; k < t.n; k++) {
        if (!bg::stepAllowed(turn, board, sideNow, at, t.die[k])) return k != 0;
        if (!stepNow(at, t.die[k], k + 1 < t.n, k == 0)) return true;       // the game is over
        at = bg::landing(at, t.die[k]);
    }
    if (bg::turnDone(turn)) phase = CONFIRM;
    return true;
}

bool canTakeBack() { return (phase == HUMAN || phase == CONFIRM) && turn.steps; }

bool takeBack() {
    if (!canTakeBack()) return false;
    for (;;) {
        uint8_t k = (uint8_t)(turn.steps - 1), f = turn.from[k], d = turn.die[k];
        bool first = group[k] != 0;
        push(EV_UNDO, f, bg::landing(f, d), (uint8_t)((turn.hit[k] ? F_HIT : 0) | (first ? 0 : F_MORE)), d);
        bg::takeBack(turn, board, sideNow);
        if (first) break;
    }
    phase = HUMAN;
    return true;
}

void confirm() {
    if (phase == CONFIRM) pickup();
}

uint8_t steps(uint8_t *from, uint8_t *die, uint8_t *hit) {
    memcpy(from, turn.from, turn.steps);
    memcpy(die, turn.die, turn.steps);
    memcpy(hit, turn.hit, turn.steps);
    return turn.steps;
}

void resign() {
    if (!active()) return;
    uint8_t loser = setup.mode == TWO_PLAYER ? sideNow : (uint8_t)bg::WHITE, w = loser ^ 1;
    finish(w, 1, BY_RESIGNATION, (uint8_t)cubeValue());
}

// ---------------------------------------------------------------------------
// Saved games
// ---------------------------------------------------------------------------
enum : uint8_t { S_OPENING, S_TO_ROLL, S_ROLLED };

// The match's state, copied byte for byte into the record and back.
static uint8_t *const PART[] = {
    (uint8_t *)&setup, score, &cube, &cubeOwner, (uint8_t *)&crawford, (uint8_t *)&crawfordDone,
    &sideNow, rolled, (uint8_t *)&turns, (uint8_t *)&dice.state, (uint8_t *)&cpuRng.state,
};
static const uint8_t PART_N[] = {sizeof setup, 2, 1, 1, 1, 1, 1, 2, 2, 8, 8};
static_assert(sizeof setup + 27 == sizeof(Record::vars), "Record::vars holds the parts");

// Between games, the screens begin the next game before saving (nextGame),
// so a saved game is never between two.
void save(Record &r) {
    memset(&r, 0, sizeof r);
    // With the dice thrown: the turn as it began, and the CPU's generator then.
    uint64_t now = cpuRng.state;
    if (hasRoll) cpuRng.state = cpuRngAtTurn;
    uint8_t *v = r.vars;
    for (uint8_t i = 0; i < sizeof PART_N; i++) { memcpy(v, PART[i], PART_N[i]); v += PART_N[i]; }
    cpuRng.state = now;
    const bg::Board &b = hasRoll ? turnBoard : board;
    for (uint8_t i = 0; i < 26; i++) r.pts[i] = (uint8_t)(b.n[0][i] | (b.n[1][i] << 4));
    r.state = phase == OPENING ? S_OPENING : hasRoll ? S_ROLLED : S_TO_ROLL;
}

bool load(const Record &r) {
    bg::Board b = {};
    for (uint8_t i = 0; i < 26; i++) { b.n[0][i] = r.pts[i] & 15; b.n[1][i] = r.pts[i] >> 4; }
    bg::recount(b);
    start(setup);
    const uint8_t *v = r.vars;
    for (uint8_t i = 0; i < sizeof PART_N; i++) { memcpy(PART[i], v, PART_N[i]); v += PART_N[i]; }
    uint8_t w;
    bool ok = bg::valid(b) && !bg::result(b, w) && setup.mode <= TWO_PLAYER && setup.level < LEVELS &&
              setup.length && score[0] < setup.length && score[1] < setup.length && cube <= MAX_CUBE &&
              cubeOwner <= CENTRE && sideNow <= 1 && r.state <= S_ROLLED &&
              (r.state != S_ROLLED || (rolled[0] - 1u < 6u && rolled[1] - 1u < 6u));
    if (!ok) { setup.length = 1; start(setup); return false; }
    board = turnBoard = b;
    cpuRngAtTurn = cpuRng.state;
    hasRoll = r.state == S_ROLLED;
    phase = hasRoll ? DEALT : r.state == S_TO_ROLL ? TURN : OPENING;
    return true;
}

#ifdef MATCH_SCRIPTED
// "w 6:5 8:3 13:5 24:2 r 6:5 8:3 13:5 24:2": each side's points (its own
// numbering, 25 = the bar) and how many; the rest of its fifteen are off.
bool startPosition(const Setup &s, const char *spec, uint8_t sideToRoll) {
    begin(s);
    memset(&board, 0, sizeof board);
    uint8_t sd = 0;
    for (const char *p = spec; *p;) {
        if (*p == 'w' || *p == 'r') { sd = *p == 'r'; p++; continue; }
        if (*p < '0' || *p > '9') { p++; continue; }
        uint8_t pt = 0, n = 0;
        while (*p >= '0' && *p <= '9') pt = (uint8_t)(pt * 10 + (*p++ - '0'));
        if (*p == ':') p++;
        while (*p >= '0' && *p <= '9') n = (uint8_t)(n * 10 + (*p++ - '0'));
        if (pt > 25) return false;
        board.n[sd][pt] = n;
    }
    for (uint8_t k = 0; k < 2; k++) {
        uint8_t sum = 0;
        for (uint8_t i = 1; i <= 25; i++) sum += board.n[k][i];
        if (sum > bg::CHECKERS) return false;
        board.n[k][0] = (uint8_t)(bg::CHECKERS - sum);
    }
    bg::recount(board);
    if (!bg::valid(board)) return false;
    turnBoard = board;
    sideNow = sideToRoll;
    phase = TURN;
    return true;
}

void stackDice(const char *digits) {
    uint8_t n = 0;
    for (; *digits && n < sizeof stacked - 1; digits++)
        if (*digits >= '1' && *digits <= '6') stacked[n++] = *digits;
    stacked[n] = 0;
    stackedAt = 0;
}

void setScore(uint8_t length, uint8_t white, uint8_t red, uint8_t cubeLog, uint8_t owner, bool crawfordGame) {
    setup.length = length;
    score[0] = white; score[1] = red;
    cube = cubeLog; cubeOwner = owner;
    crawford = crawfordGame;
    crawfordDone = crawfordGame || white + 1 >= setup.length || red + 1 >= setup.length;
    push(EV_START);
}
#endif

}  // namespace match
