// The play screen in motion (see Stage.h): Match's events become dice,
// checkers and the cube in flight, the gloves, the camera's close-ups, the
// HUD and the plate; and drawing it all.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (hot pixel loops live in Draw/Table)
#include <string.h>
#include <RPGame.h>
#include <Arduino.h>
#include "config.h"
#include "Stage.h"
#include "Match.h"
#include "Notation.h"
#include "Ai.h"
#include "Table.h"
#include "Fx.h"
#include "Sounds.h"
#include "src/assets/Assets.h"

namespace stage {

using namespace table;
using bg::BAR;
using bg::OFF;

// ---------------------------------------------------------------------------
// Colour remaps for the art (its neutral tones -> a side: Assets.h; effects here)
// ---------------------------------------------------------------------------
static const uint8_t RM_CPU[16] = {0, 1, 2, 3, 4, 5, 6, 7, RED, WINE, 10, 11, 12, 13, 14, 15};   // Red's glove: a red cuff
static const uint8_t RM_HIT[16] = {INK, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE,
                                   WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE};   // struck: a white flash
static const uint8_t RM_PREY[16] = {INK, RED, RED, RED, RED, RED, RED, RED,
                                    RED, RED, RED, RED, RED, RED, RED, RED};         // about to be: a red one
static const uint8_t RM_ALERT[16] = {0, RED, 2, 3, 4, WINE, 6, 7, RED, WINE, 10, 11, 12, 13, 14, 15};   // your glove, denied

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bg::Board shown;              // checkers standing still
static uint8_t turnSide;
static bool humanTurn, thinking, over, overDone, fast, glove;
static bool opened;                  // the opening roll is done: someone's turn has begun
static uint8_t cur = AT_DICE, sel = 0xFF, nTgt;
static bg::Target tgt[4];
static uint8_t cpuSpot = AT_DICE;    // where the CPU's glove is
static uint8_t intent = 0xFF;        // ... and where the checker it carries is going
static bool intentHit;

// A checker in the air: one step of a move (or of a take-back).
struct Mover { uint8_t on, side, from, to, die, flags, t, T, undo; int16_t x0, y0, x1, y1; };
static Mover mv;
static match::Event pend;            // the step being shown
static bool chained;                 // the step before it was the same checker's

// A blot knocked off its point, on its way to the bar; and the cube flying.
struct Flyer { uint8_t on, side, t, T; int16_t x0, y0, x1, y1; };
static Flyer fly, cubeFly;

// The dice: thrown in from their side's edge, tumbling; at rest; slid across
// (the opening roll's two to whoever won it); two more popping up for a
// double; picked up at the turn's end.
enum : uint8_t { D_OFF, D_REST, D_THROW, D_SLIDE, D_POP, D_LEAVE };
struct Die { uint8_t mode, face, side, used, t, T, delay; int16_t x0, y0, x1, y1; };
static Die dice[4];
static bool settle;                  // a roll is on its way: when the dice are still, what it means
static bool opening;                 // ... the opening roll
static uint8_t rollNeed;

// The cube: where it stands, and a double on offer (it floats over the bar,
// turning over to the new value, until the answer).
static uint8_t cubeLog, cubeOwn = match::CENTRE, offerLog, cubeTurn;
static bool offered;
static const uint8_t TURN_T = 16;

// The CPU carrying a checker: the glove goes to it, taps, and takes it.
static bool picking;
static uint8_t pickT, tapT, thinkT;
static bool tock;

static uint8_t holdT;                // frames the stage keeps the game waiting
static bool waitPress;               // the last word of the game stays up until a button
static uint8_t overT, partyT;        // ... and a won match's party
const char *opponentName = "CPU";

// The glove's fingertip, gliding (world, Q4).
static int32_t gx16, gy16;

// The whip zoom: table::zoom steps towards zoomTo, one step each frame drawn
// (a slow frame never bunches two), about the focus; when the moment is
// over the camera holds (outWait) until its dust has cleared, then pulls back.
static uint8_t zoomTo = 5;
static bool outWait, zoomDrawn = true;
static int16_t focusX = CX, focusY = CY;
static uint8_t slowF = 1;            // a hit plays out twice as slowly, to sell it
static bool blocked;                 // the checker under the glove has no move
static uint8_t denyT;                // ... and A was pressed on it

// The last move in words on the plate, popping up word by word.
static const char *annW[5];
static uint8_t annC[5], annN, annT;
static char annBuf[12];
static const uint8_t ANN_FRAMES = 100;

// The plate above it: the other side's last play during your roll, a hint,
// or the coach's word on your play.
enum : uint8_t { ADV_NONE, ADV_LAST, ADV_HINT, ADV_VERDICT };
static uint8_t adv, advCol, advT;
static char advBuf[44];
static uint8_t hintTo[4], nHint;
// This turn's steps, for the notation of the whole play.
static uint8_t stFrom[4], stDie[4], stHit[4], nSt;

// ---------------------------------------------------------------------------
// Where things are
// ---------------------------------------------------------------------------
static bool diceMoving() {
    for (auto &d : dice) if (d.mode > D_REST) return true;
    return false;
}

static uint8_t diceCount() {
    uint8_t n = 0;
    for (auto &d : dice) n += d.mode != D_OFF;
    return n;
}

// The cube's place: in the air over the bar while a double is on offer.
static void cubeXY(int &x, int &y) {
    cubeAt(offered ? match::CENTRE : cubeOwn, x, y);
}

void spotXY(uint8_t spot, int &x, int &y) {
    uint8_t s = turnSide;
    if (spot == AT_DICE) {                                   // just above them
        dieAt(s, 0, 1, x, y);
        x += DIE / 2; y -= 1;
        return;
    }
    if (spot == AT_CUBE) {
        cubeXY(x, y);
        x += CUBE / 2; y -= 1;
        return;
    }
    uint8_t n = shown.n[s][spot];
    if (spot == OFF) { checkerAt(s, OFF, n, 0, x, y); x += CHIP / 2; y += 1; return; }
    // Holding a checker: where it would be set down. Otherwise the top one.
    if (sel != 0xFF && spot != sel) checkerAt(s, spot, n, (uint8_t)(n + 1), x, y);
    else checkerAt(s, spot, n ? (uint8_t)(n - 1) : 0, n ? n : 1, x, y);
    x += CHIP / 2; y += CHIP / 2;
}

// The hand reaches a spot from the middle of the board, so that it never
// covers the stack it points at: from below (fingertip up) for the far side.
static bool fromBelow(uint8_t spot) {
    if (spot == AT_DICE) return false;
    if (spot == AT_CUBE) return !offered && cubeOwn == bg::RED;
    if (spot == BAR || spot == OFF) return turnSide == bg::RED;
    return whites(turnSide, spot) > 12;
}

static void whip(int x, int y) {
    if (fast) return;
    if (zoomTo != 10) audio::sfx(Sfx::Whoosh);
    zoomTo = 10;
    focusX = (int16_t)x; focusY = (int16_t)y;
}

// ---------------------------------------------------------------------------
// Public controls
// ---------------------------------------------------------------------------
uint8_t cursor() { return cur; }
void setCursor(uint8_t spot) { cur = spot; }
void setFast(bool on) { fast = on; }
uint8_t selected() { return humanTurn ? sel : 0xFF; }
void setBlocked(bool b) { blocked = b; }
void deny() {
    denyT = 24;
    audio::sfx(Sfx::Deny);
}

void select(uint8_t from, const bg::Target *t, uint8_t n) {
    sel = from;
    nTgt = n;
    memcpy(tgt, t, n * sizeof tgt[0]);
    audio::sfx(Sfx::Lift);
}

void deselect() {
    sel = 0xFF;
    nTgt = 0;
}

bool waiting() { return waitPress; }
void acknowledge() {
    waitPress = false;
    fx::holdBanner(false);
}

void advise(const char *text, uint8_t colour, const uint8_t *to, uint8_t n) {
    fmtStr(advBuf, text);
    advCol = colour;
    adv = to ? ADV_HINT : ADV_VERDICT;
    advT = 0;
    nHint = n;
    if (to) memcpy(hintTo, to, n);
}

void quiet() {
    if (adv != ADV_LAST) adv = ADV_NONE;
    nHint = 0;
}

bool busy() {
    return mv.on || fly.on || cubeFly.on || cubeTurn || holdT || picking || settle || diceMoving() || outWait ||
           waitPress || zoom != zoomTo;
}

bool ready() {
    match::Event e;
    return !busy() && !match::peekEvent(e);
}
bool overShown() { return overDone; }

// ---------------------------------------------------------------------------
// The plate's words
// ---------------------------------------------------------------------------
static void addWord(const char *w, uint8_t c) { if (annN < 5) { annW[annN] = w; annC[annN++] = c; } }

static void say(const char *w, uint8_t c, const char *w2 = nullptr, uint8_t c2 = 0) {
    annN = 0;
    annT = 1;
    addWord(w, c);
    if (w2) addWord(w2, c2);
}

// ---------------------------------------------------------------------------
// Moments
// ---------------------------------------------------------------------------
// The longest run of points `s` holds (two or more on each).
static uint8_t prime(const bg::Board &b, uint8_t s) {
    uint8_t best = 0, run = 0;
    for (uint8_t p = 1; p <= 24; p++) {
        run = b.n[s][p] >= 2 ? (uint8_t)(run + 1) : 0;
        if (run > best) best = run;
    }
    return best;
}

// All six home points held, and the other side has a checker on the bar.
static bool closedOut(const bg::Board &b, uint8_t s) {
    for (uint8_t p = 1; p <= 6; p++) if (b.n[s][p] < 2) return false;
    return b.n[s ^ 1][BAR] != 0;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
// Dust kicked out from under something of radius r set down with its middle
// at world (x, y): the felt's own colour, so it blends in.
static void puff(int x, int y, int r, uint8_t n) {
    int cx = sx(x), cy = sy(y), rr = zoomed(r), sp = zoomed(26);
    for (uint8_t i = 0; i < n; i++) {
        int a = i * 256 / n + fx::rndRange(0, 16);
        int c = fx::isin(a + 64), q = fx::isin(a);
        fx::spawn(fx::DUST, cx + ((c * rr) >> 8), cy + ((q * rr) >> 8), (c * sp) >> 8, (q * sp) >> 9,
                  (uint8_t)fx::rndRange(16, 30), FELT_LT);
    }
}

// A die thrown in from its player's end of the table (the home board's).
static int edgeX(uint8_t side) { return (side == bg::WHITE) != mirror ? 150 : -34; }

static void throwDie(uint8_t i, uint8_t side, uint8_t face, uint8_t slot, uint8_t slots, uint8_t delay) {
    Die &d = dice[i];
    int x, y;
    dieAt(side, slot, slots, x, y);
    d.mode = D_THROW; d.face = face; d.side = side; d.used = 0;
    d.t = 0; d.T = fast ? 22 : 34; d.delay = delay;
    d.x0 = (int16_t)edgeX(side);
    d.y0 = (int16_t)(side == bg::WHITE ? 100 : 16);
    d.x1 = (int16_t)x; d.y1 = (int16_t)y;
}

static void moveDie(uint8_t i, uint8_t mode, int x, int y, uint8_t T) {
    Die &d = dice[i];
    d.x0 = d.x1; d.y0 = d.y1;
    d.x1 = (int16_t)x; d.y1 = (int16_t)y;
    d.mode = mode; d.t = 0; d.T = T; d.delay = 0;
}

static void onStart() {
    shown = match::board;
    memset(dice, 0, sizeof dice);
    mv.on = fly.on = cubeFly.on = 0;
    holdT = 0; picking = false; thinking = false; tapT = 0; waitPress = false; settle = false;
    offered = false; cubeTurn = 0;
    cubeLog = match::cube; cubeOwn = match::cubeOwner;
    fx::clear();
    over = overDone = false;
    overT = partyT = 0;
    intent = 0xFF;
    deselect();
    zoom = zoomTo = 5;
    setCamera(CX, CY);
    outWait = false;
    annT = 0;
    adv = ADV_NONE; nHint = 0; nSt = 0;
    turnSide = match::side();
    humanTurn = match::isHuman(turnSide);
    opened = false;
    glove = true;
    cur = cpuSpot = AT_DICE;
    focusX = CX; focusY = CY;
    int x, y;
    spotXY(AT_DICE, x, y);
    gx16 = x << 4; gy16 = y << 4;
    if (match::crawford) say("CRAWFORD GAME:", GOLD, " NO DOUBLING", SILVER);
}

static void onOpening(uint8_t a, uint8_t b) {
    throwDie(0, bg::WHITE, a, 0, 1, 0);
    throwDie(1, bg::RED, b, 0, 1, 6);
    dice[2].mode = dice[3].mode = D_OFF;
    settle = opening = true;
    tapT = 1;
    audio::sfx(Sfx::Rattle);
}

static void onTurn(uint8_t side, bool human, bool dealt) {
    turnSide = side;
    humanTurn = human;
    opened = true;
    thinking = false;
    intent = 0xFF;
    deselect();
    glove = true;
    chained = false;
    nSt = 0;
    nHint = 0;
    if (adv != ADV_LAST) adv = ADV_NONE;
    cur = cpuSpot = AT_DICE;
    if (dealt) return;
    if (human) audio::sfx(Sfx::Turn);
    else holdT = fast ? 8 : 24;          // the CPU's glove comes over to its dice first
}

static void onRoll(uint8_t a, uint8_t b, uint8_t need, uint8_t side) {
    rollNeed = need;
    if (adv == ADV_LAST) adv = ADV_NONE;
    int x, y;
    if (dice[0].mode == D_REST && dice[1].mode == D_REST) {
        // The opening roll's two dice go to the side that won it.
        for (uint8_t i = 0; i < 2; i++) { dieAt(side, i, 2, x, y); moveDie(i, D_SLIDE, x, y, 16); }
        audio::sfx(Sfx::Whoosh);
    } else {
        throwDie(0, side, a, 0, 2, 0);
        throwDie(1, side, b, 1, 2, 4);
        dice[2].mode = dice[3].mode = D_OFF;
        tapT = 1;
        audio::sfx(Sfx::Rattle);
        spotXY(AT_DICE, x, y);
        whip(x, y);
    }
    settle = true;
    opening = false;
}

// The dice have come to rest: what the roll means.
static void settled() {
    settle = false;
    int x, y;
    if (opening) {
        uint8_t a = dice[0].face, b = dice[1].face;
        if (a == b) {
            // The same: both are thrown again.
            say("TIE: ROLL AGAIN", WHITE);
            audio::sfx(Sfx::NoMove);
            holdT = 50;
            for (uint8_t i = 0; i < 2; i++) moveDie(i, D_LEAVE, dice[i].x0, dice[i].y0, 14);
            return;
        }
        bool two = match::setup.mode == match::TWO_PLAYER;
        if (two) say(a > b ? "WHITE" : "RED", a > b ? WHITE : RED, " STARTS", GOLD);
        else say(a > b ? "YOU" : opponentName, a > b ? WHITE : RED, a > b ? " START" : " STARTS", GOLD);
        holdT = fast ? 20 : 50;
        return;
    }
    holdT = fast ? 6 : 20;
    if (dice[0].face == dice[1].face && rollNeed) {
        // A double: each die counts twice. Two more appear beside them.
        uint8_t s = dice[0].side;
        for (uint8_t i = 0; i < 4; i++) {
            dieAt(s, i, 4, x, y);
            if (i < 2) { moveDie(i, D_SLIDE, x, y, 10); continue; }
            dice[i] = dice[0];
            dice[i].x1 = (int16_t)x; dice[i].y1 = (int16_t)y;
            moveDie(i, D_POP, x, y, 12);
        }
        spotXY(AT_DICE, x, y);
        fx::banner("DOUBLES!", fx::B_RAINBOW, 36, 60);
        fx::burst(fx::STAR, sx(x), sy(y), 10, 34, GOLD);
        audio::sfx(Sfx::Doubles);
        holdT = fast ? 20 : 44;
    }
    if (!rollNeed) {
        // On the bar against a closed board: it dances.
        fx::banner(shown.n[dice[0].side][BAR] ? "DANCE!" : "NO MOVES", fx::B_RED, 36, 70);
        audio::sfx(Sfx::NoMove);
        denyT = 24;
        holdT = 70;
    }
    outWait = zoomTo > 5;
}

static void useDie(uint8_t face, bool used) {
    // Playing: the first unplayed die of that number; taking back: the last played.
    for (uint8_t k = 0; k < 4; k++) {
        Die &d = dice[used ? k : 3 - k];
        if (d.mode != D_OFF && d.face == face && d.used != used) { d.used = used; return; }
    }
}

// The step in `pend` takes off.
static void launch() {
    uint8_t s = turnSide, from = pend.a, to = pend.b;
    bool hit = (pend.c & match::F_HIT) != 0;
    uint8_t n = shown.n[s][from], m = shown.n[s][to];
    int x, y;
    checkerAt(s, from, (uint8_t)(n - 1), n, x, y);
    mv.x0 = (int16_t)x; mv.y0 = (int16_t)y;
    checkerAt(s, to, m, (uint8_t)(m + 1), x, y);
    mv.x1 = (int16_t)x; mv.y1 = (int16_t)y;
    int dx = mv.x1 - mv.x0, dy = mv.y1 - mv.y0;
    int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    // The big moments, close up: a hit, and the last checker off.
    bool last = to == OFF && shown.n[s][OFF] == bg::CHECKERS - 1;
    slowF = 1;
    if ((hit || last) && !fast) {
        whip(x + CHIP / 2, y + CHIP / 2);
        if (hit) slowF = 2;
    }
    mv.on = 1; mv.side = s; mv.from = from; mv.to = to; mv.die = pend.d; mv.flags = pend.c;
    mv.undo = 0; mv.t = 0;
    mv.T = (uint8_t)((fast ? 8 + dist / 14 : 12 + dist / 9) * slowF);
    intent = to;
    intentHit = hit;
    useDie(pend.d, true);
    audio::sfx(Sfx::Lift);
}

static void onStep(const match::Event &e) {
    thinking = false;
    pend = e;
    if (nSt < 4) { stFrom[nSt] = e.a; stDie[nSt] = e.d; stHit[nSt++] = (e.c & match::F_HIT) != 0; }
    nHint = 0;
    if (adv == ADV_HINT) adv = ADV_NONE;
    bool wasChained = chained;
    chained = (e.c & match::F_MORE) != 0;
    if (humanTurn) { deselect(); launch(); return; }
    if (wasChained) { launch(); return; }            // the same checker travels on
    // The CPU's glove goes to the checker first.
    picking = true;
    pickT = 0;
    cpuSpot = e.a;
}

static void onUndo(const match::Event &e) {
    uint8_t s = turnSide, from = e.a, to = e.b;
    uint8_t n = shown.n[s][to];
    int x, y;
    checkerAt(s, to, (uint8_t)(n ? n - 1 : 0), n, x, y);
    mv.x0 = (int16_t)x; mv.y0 = (int16_t)y;
    bg::undoStep(shown, s, from, e.d, (e.c & match::F_HIT) != 0);
    n = shown.n[s][from];
    checkerAt(s, from, (uint8_t)(n - 1), n, x, y);
    mv.x1 = (int16_t)x; mv.y1 = (int16_t)y;
    mv.on = 1; mv.side = s; mv.from = from; mv.to = to; mv.die = e.d; mv.flags = e.c;
    mv.undo = 1; mv.t = 0; mv.T = 9;
    if (nSt) nSt--;
    useDie(e.d, false);
    deselect();
    annT = 0;
    if (adv != ADV_LAST) adv = ADV_NONE;
    nHint = 0;
    audio::sfx(Sfx::Whoosh);
}

static void land() {
    mv.on = 0;
    if (mv.undo) { audio::sfx(Sfx::Cursor); return; }
    uint8_t s = mv.side;
    bool more = (mv.flags & match::F_MORE) != 0;
    uint8_t primeWas = prime(shown, s);
    bool closedWas = closedOut(shown, s);
    bool hit = bg::doStep(shown, s, mv.from, mv.die);
    int cx = sx(mv.x1 + CHIP / 2), cy = sy(mv.y1 + CHIP / 2);
    intent = 0xFF;
    if (mv.to == OFF) {
        fx::burst(fx::SPARK, cx, cy, 8, 28, GOLD);
        audio::sfx(Sfx::Coin);
    } else if (hit) {
        // The blot is knocked off its point and over to the bar.
        uint8_t o = s ^ 1, nb = shown.n[o][BAR];
        int x, y;
        checkerAt(o, BAR, (uint8_t)(nb - 1), nb, x, y);
        fly.on = 1; fly.side = o; fly.t = 0; fly.T = (uint8_t)(26 * slowF);
        fly.x0 = mv.x1; fly.y0 = mv.y1;
        fly.x1 = (int16_t)x; fly.y1 = (int16_t)y;
        fx::burst(fx::SPARK, cx, cy, 12, 36, GOLD);
        fx::burst(fx::STAR, cx, cy, 4, 24, WHITE);
        fx::banner("HIT!", fx::B_RED, 30, 50);
        audio::sfx(Sfx::Hit);
        audio::led(audio::LED_BLINK);
    } else {
        audio::sfx(Sfx::Land);
        // A point made (the second checker on it): a twinkle.
        if (shown.n[s][mv.to] == 2 && mv.to != BAR) fx::burst(fx::STAR, cx, cy, 5, 22, GOLD);
    }
    // The moments players wait for: a full prime, the home board closed
    // with a checker on the bar.
    if (!closedWas && closedOut(shown, s)) {
        fx::banner("CLOSED OUT!", fx::B_RAINBOW, 36, 90);
        fx::fountain(64, 70, 14);
        audio::sfx(Sfx::Doubles);
        holdT = 40;
    } else if (primeWas < 6 && prime(shown, s) >= 6) {
        fx::banner("PRIME!", fx::B_GOLD, 36, 80);
        fx::burst(fx::STAR, cx, cy, 12, 40, GOLD);
        audio::sfx(Sfx::Doubles);
        holdT = 36;
    }
    puff(mv.x1 + CHIP / 2, mv.y1 + CHIP / 2, CHIP / 2, more ? 5 : 9);
    if (!more) holdT = (uint8_t)(holdT > 12 ? holdT : humanTurn ? 2 : fast ? 5 : 12);
    if (!humanTurn || hit) {
        uint8_t h = hit;
        notate(annBuf, &mv.from, &mv.die, &h, 1);
        say(annBuf, humanTurn ? WHITE : turnSide == bg::WHITE ? WHITE : RED, hit ? " HIT!" : nullptr, RED);
    }
    if (zoomTo > 5 && !fly.on) outWait = true;
}

static void onPickup(uint8_t side) {
    for (uint8_t i = 0; i < 4; i++)
        if (dice[i].mode != D_OFF) moveDie(i, D_LEAVE, edgeX(dice[i].side), dice[i].y1, 12);
    thinking = false;
    tapT = 1;
    cpuSpot = AT_DICE;
    // The whole play, in the notation, for the other side to read.
    notate(advBuf, stFrom, stDie, stHit, nSt);
    adv = ADV_LAST;
    advCol = side == bg::WHITE ? WHITE : RED;
    nHint = 0;
    audio::sfx(Sfx::Pickup);
}

// The cube rises from its place, turns over to the value offered, and floats
// over the bar until the answer.
static void flyCube(int x0, int y0, int x1, int y1, uint8_t T) {
    cubeFly.on = 1; cubeFly.t = 0; cubeFly.T = T;
    cubeFly.x0 = (int16_t)x0; cubeFly.y0 = (int16_t)y0; cubeFly.x1 = (int16_t)x1; cubeFly.y1 = (int16_t)y1;
}

static void onDouble(uint8_t side, uint8_t log) {
    int x0, y0, x1, y1;
    cubeXY(x0, y0);
    offered = true;
    offerLog = log;
    cubeTurn = 1;
    cubeXY(x1, y1);
    flyCube(x0, y0, x1, y1, 14);
    fx::banner("DOUBLE!", fx::B_GOLD, 30, 70);
    audio::sfx(Sfx::Whoosh);
    whip(x1 + CUBE / 2, y1);
    cpuSpot = AT_CUBE;
    tapT = 1;
    (void)side;
    holdT = 30;
}

// A beaver or a raccoon: the floating cube turns over again, to twice that.
static void onRedouble(uint8_t type, uint8_t log) {
    offerLog = log;
    cubeTurn = 1;
    fx::banner(type == match::EV_BEAVER ? "BEAVER!" : "RACCOON!", fx::B_GOLD, 30, 70);
    audio::sfx(Sfx::Doubles);
    holdT = 40;
}

static void onTake(uint8_t taker, uint8_t log) {
    int x0, y0, x1, y1;
    cubeXY(x0, y0);
    offered = false;
    cubeLog = log;
    cubeOwn = taker;
    cubeXY(x1, y1);
    flyCube(x0, y0, x1, y1, 18);
    focusX = (int16_t)(x1 + CUBE / 2); focusY = (int16_t)y1;      // the camera goes with it
    fx::banner("TAKE!", fx::B_WHITE, 30, 50);
    audio::sfx(Sfx::Select);
    holdT = 24;
}

static void onOver(uint8_t winner, uint8_t how, uint8_t reason) {
    over = true;
    overT = 0;
    humanTurn = thinking = false;
    glove = false;
    bool vsCpu = match::setup.mode == match::VS_CPU, matchWon = match::matchOver() && match::cubeLive();
    bool cheer = !vsCpu || winner == bg::WHITE;         // a human won
    static const char *const HOW[2] = {"GAMMON!", "BACKGAMMON!"};
    const char *text;
    if (reason == match::BY_RESIGNATION) text = vsCpu ? "YOU RESIGN" : "RESIGNED";
    else if (reason == match::BY_PASS) text = cheer ? "PASSED!" : "YOU PASS";
    else if (how > 1 && cheer) text = HOW[how - 2];
    else text = vsCpu ? (cheer ? "YOU WIN!" : "YOU LOSE") : winner == bg::WHITE ? "WHITE WINS!" : "RED WINS!";
    if (matchWon) text = cheer ? "MATCH!" : "MATCH LOST";
    bool played = reason != match::BY_RESIGNATION;
    fx::banner(text, !played ? fx::B_WHITE : cheer ? fx::B_RAINBOW : fx::B_RED, 36, 170);
    fx::holdBanner(waitPress = true);
    holdT = 70;                                          // PRESS A once it has sunk in
    offered = false;
    if (cheer && played) {
        audio::sfx(how > 1 || matchWon ? Sfx::Gammon : Sfx::Win);
        audio::led(audio::LED_PARTY);
        fx::fountain(40, 96, 20);
        fx::fountain(88, 96, 20);
        if (matchWon) partyT = 1;
    } else {
        audio::sfx(Sfx::Lose);
    }
    outWait = zoomTo > 5;
}

void begin() {}

// ---------------------------------------------------------------------------
// Per tick
// ---------------------------------------------------------------------------
static void moverPos(int &x, int &y, uint8_t &lift) {
    int e = fx::ease(fx::IN_OUT, mv.t, mv.T);
    x = mv.x0 + (((mv.x1 - mv.x0) * e) >> 8);
    y = mv.y0 + (((mv.y1 - mv.y0) * e) >> 8);
    // Lift, carry, set down.
    int up = mv.undo ? 3 : 6, ramp = 4 * slowF, left = mv.T - mv.t;
    lift = (uint8_t)(mv.t < ramp ? mv.t * up / ramp : left < ramp ? left * up / ramp : up);
}

// Something flying in an arc, up towards the eye and back down.
static void arcPos(const Flyer &f, int height, int &x, int &y, uint8_t &lift) {
    int e = fx::ease(fx::OUT_CUBIC, f.t, f.T);
    x = f.x0 + (((f.x1 - f.x0) * e) >> 8);
    y = f.y0 + (((f.y1 - f.y0) * e) >> 8);
    lift = (uint8_t)((fx::isin(f.t * 128 / f.T) * height) >> 8);
}

// A die on its way: where, how high, how far turned, and the face showing.
static void diePos(const Die &d, int &x, int &y, uint8_t &lift, uint8_t &turn, uint8_t &face) {
    x = d.x1; y = d.y1;
    lift = turn = 0;
    face = d.face;
    if (d.mode <= D_REST) return;
    int t = d.delay ? 0 : d.t, left = d.T - t;
    int e = fx::ease(d.mode == D_THROW ? fx::OUT_CUBIC : fx::IN_OUT, t, d.T);
    if (d.mode != D_POP) {
        x = d.x0 + (((d.x1 - d.x0) * e) >> 8);
        y = d.y0 + (((d.y1 - d.y0) * e) >> 8);
    }
    switch (d.mode) {
        case D_THROW: {
            // Three hops, each lower; spinning down to square; any face until the last.
            int hop = fx::isin(t * 384 / d.T);
            if (hop < 0) hop = -hop;
            lift = (uint8_t)((hop * left * 10 / d.T) >> 8);
            turn = (uint8_t)(left * 11);
            if (left > 5) face = (uint8_t)(1 + (d.face + (t / 3) * 5 + (d.x1 & 3)) % 6);
            break;
        }
        case D_POP:   lift = (uint8_t)(left * 8 / d.T); break;
        case D_LEAVE: lift = (uint8_t)(t < 6 ? t : 6); break;
        default: break;
    }
}

void update() {
    match::Event e;
    // A new position (a new game, a restored one) cuts in on anything still
    // showing; everything else waits its turn.
    while (match::peekEvent(e) && (!busy() || e.type == match::EV_START)) {
        match::popEvent(e);
        switch (e.type) {
            case match::EV_START:   onStart(); break;
            case match::EV_OPENING: onOpening(e.a, e.b); break;
            case match::EV_TURN:    onTurn(e.a, e.b != 0, e.c != 0); break;
            case match::EV_ROLL:    onRoll(e.a, e.b, e.c, e.d); break;
            case match::EV_THINK:   thinking = true; thinkT = 0; holdT = fast ? 10 : 40; break;
            case match::EV_STEP:    onStep(e); break;
            case match::EV_UNDO:    onUndo(e); break;
            case match::EV_PICKUP:  onPickup(e.a); break;
            case match::EV_DOUBLE:  onDouble(e.a, e.b); break;
            case match::EV_TAKE:    onTake(e.a, e.b); break;
            case match::EV_BEAVER:
            case match::EV_RACCOON: onRedouble(e.type, e.b); break;
            case match::EV_OVER:    onOver(e.a, e.b, e.c); break;
        }
    }

    if (holdT) holdT--;
    if (denyT) denyT--;
    if (tapT && ++tapT > 12) tapT = 0;
    if (advT < 255) advT++;
    if (annT && (!waitPress || annT < 40) && ++annT > ANN_FRAMES) annT = 0;
    if (outWait && !fx::particles() && !holdT && !fly.on && !mv.on && !cubeFly.on && !offered) { outWait = false; zoomTo = 5; }
    if (zoom != zoomTo && zoomDrawn) {
        zoom = (uint8_t)(zoom < zoomTo ? zoom + 1 : zoom - 1);
        zoomDrawn = false;
    }
    // The glove's checker: its outline fades black/white (HOVER); holding one,
    // the points it can go to shimmer (TARGETS).
    pal::setMode(sel != 0xFF ? pal::TARGETS : glove ? pal::HOVER : pal::CASINO);

    // Dice.
    for (auto &d : dice) {
        if (d.mode <= D_REST) continue;
        if (d.delay) { d.delay--; continue; }
        if (++d.t < d.T) continue;
        if (d.mode == D_THROW) {                         // down: a puff of felt
            puff(d.x1 + DIE / 2, d.y1 + DIE / 2, DIE / 2 + 1, 6);
        }
        d.mode = d.mode == D_LEAVE ? D_OFF : D_REST;
    }
    if (settle && !diceMoving()) settled();

    // Checkers, and the cube.
    if (mv.on && ++mv.t >= mv.T) land();
    if (fly.on && ++fly.t >= fly.T) {
        fly.on = 0;
        puff(fly.x1 + CHIP / 2, fly.y1 + CHIP / 2, CHIP / 2, 9);
        audio::sfx(Sfx::Land);
        holdT = (uint8_t)(holdT > 16 ? holdT : 16);
        if (zoomTo > 5) outWait = true;
    }
    if (cubeTurn && ++cubeTurn > TURN_T) cubeTurn = 0;
    if (cubeFly.on && ++cubeFly.t >= cubeFly.T) {
        cubeFly.on = 0;
        if (!offered) {                                  // set down
            puff(cubeFly.x1 + CUBE / 2, cubeFly.y1 + CUBE / 2, CUBE / 2, 8);
            audio::sfx(Sfx::Land);
            if (zoomTo > 5) outWait = true;
        }
    }
    if (picking) {
        // As a player would: the glove rests on the checker, taps it, and
        // carries it off.
        uint8_t rest = fast ? 6 : 16;
        pickT++;
        if (pickT == rest) { tapT = 1; audio::sfx(Sfx::Select); }
        if (pickT == rest + 8) { picking = false; launch(); }
    }
    if (thinking) {
        // The glove wanders over the checkers the CPU is weighing, and for a
        // long think a soft clock ticks.
        thinkT++;
        if (!(thinkT & 15)) { uint8_t c = ai::considering(); if (c) cpuSpot = c; }
        if (thinkT >= 120) { thinkT = 0; audio::sfx((tock = !tock) ? Sfx::Tock : Sfx::Tick); }
    }
    if (partyT && partyT < 255) {
        partyT++;
        if (!(partyT & 31)) { fx::fountain(20 + fx::rndRange(0, 88), 110, 12); audio::led(audio::LED_BLINK); }
    }

    // The glove glides to where it points: your cursor; the CPU's checker, or
    // the one in its hand.
    int x, y;
    uint8_t lift;
    if (mv.on && !humanTurn) { moverPos(x, y, lift); x += CHIP / 2; y += CHIP / 2; }
    else spotXY(humanTurn ? cur : cpuSpot, x, y);
    gx16 += ((x << 4) - gx16) >> 1;
    gy16 += ((y << 4) - gy16) >> 1;

    // The camera: on the dice as they land, the checker about to hit, the
    // blot on its way to the bar.
    if (fly.on) { arcPos(fly, 14, x, y, lift); focusX = (int16_t)(x + CHIP / 2); focusY = (int16_t)(y + CHIP / 2); }
    if (zoom == 5) setCamera(CX, CY);
    else setCamera(camX + (focusX - camX) / 2, camY + (focusY - camY) / 2);

    if (over && overT < 255) overT++;
    if (overT > 60 && !waitPress) overDone = true;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
// Blots the picked-up checker would hit, on the way or at the end: in the
// mover's numbering.
static bool prey(uint8_t point, bool &underGlove) {
    for (uint8_t i = 0; i < nTgt; i++) {
        uint8_t at = sel;
        for (uint8_t k = 0; k < tgt[i].n; k++) {
            at = bg::landing(at, tgt[i].die[k]);
            if (at == point && ((tgt[i].hits >> k) & 1)) { underGlove = tgt[i].to == cur; return true; }
        }
    }
    return false;
}

static void drawTargets(uint32_t frame) {
    if (intent != 0xFF) tint(turnSide, intent, intentHit ? RED : CYAN, false);
    // A hint: where its checkers go, blinking gold.
    if (nHint && ((frame >> 4) & 1)) for (uint8_t i = 0; i < nHint; i++) tint(turnSide, hintTo[i], GOLD, false);
    if (sel == 0xFF) return;
    for (uint8_t i = 0; i < nTgt; i++) {
        bool hits = (tgt[i].hits >> (tgt[i].n - 1)) & 1;
        // The one the glove is on blinks between dithered and solid.
        tint(turnSide, tgt[i].to, hits ? FX_B : FX_A, humanTurn && tgt[i].to == cur && ((frame >> 4) & 1));
    }
}

static void drawCheckers(uint32_t frame, bool plain) {
    for (uint8_t s = 0; s < 2; s++) {
        const uint8_t *base = CHECKER_REMAP[s];
        for (uint8_t k = 0; k < shown.n[s][OFF]; k++) drawOff(s, k);
        for (uint8_t p = 1; p <= BAR; p++) {
            uint8_t n = shown.n[s][p];
            if (!n) continue;
            // The top checker may be away: in the air, or on its way here.
            uint8_t vis = n;
            if (mv.on && mv.side == s && mv.from == p) vis--;
            if (fly.on && fly.side == s && p == BAR) vis--;
            for (uint8_t k = 0; k < vis; k++) {
                int x, y;
                checkerAt(s, p, k, n, x, y);
                if (sy(y) > 118 || sy(y + CHIP) < 10) continue;     // under the HUD
                const uint8_t *rm = base;
                uint8_t hl[16], edge = 0xFF, lift = 0;
                bool top = k == n - 1;
                if (top && !plain) {
                    // Outlines: picked up, the rainbow; under the glove, fading
                    // black to white (FX_A in the palette's HOVER mode). A blot
                    // the picked-up checker would hit flashes white - red if
                    // the glove is where it would be hit from.
                    bool mine = s == turnSide, under = false;
                    uint8_t spot = humanTurn ? cur : cpuSpot;
                    if (mine && p == sel) { edge = fx::RAIN[(frame >> 3) % 5]; lift = 4; }
                    else if (mine && glove && sel == 0xFF && p == spot && !mv.on) edge = FX_A;
                    else if (!mine && sel != 0xFF && prey((uint8_t)(25 - p), under) && (frame & 8)) rm = under ? RM_PREY : RM_HIT;
                    if (edge != 0xFF) {
                        memcpy(hl, base, 16);
                        hl[INK] = edge;
                        rm = hl;
                    }
                }
                drawChecker(x, y, rm, lift);
            }
        }
    }
    if (plain) return;
    int x, y;
    uint8_t lift;
    if (mv.on) {
        moverPos(x, y, lift);
        // Into the tray: it turns on its edge as it arrives.
        if (mv.to == OFF && !mv.undo && mv.T - mv.t < 4) drawOff(mv.side, shown.n[mv.side][OFF]);
        else if (mv.to == OFF && mv.undo && mv.t < 3) drawOff(mv.side, shown.n[mv.side][OFF]);
        else drawChecker(x, y, CHECKER_REMAP[mv.side], lift);
    }
    if (fly.on) {
        arcPos(fly, 14, x, y, lift);
        drawChecker(x, y, fly.t < 4 * slowF ? RM_HIT : CHECKER_REMAP[fly.side], lift);
    }
}

static void drawDice() {
    for (auto &d : dice) {
        if (d.mode == D_OFF || (d.mode != D_REST && d.delay)) continue;
        int x, y;
        uint8_t lift, turn, face;
        diePos(d, x, y, lift, turn, face);
        drawDie(x, y, face, d.used ? DIE_USED[d.side] : DIE_REMAP[d.side], turn, lift);
    }
}

static void drawCube(uint32_t frame) {
    if (!match::cubeLive() || match::crawford) return;
    int x, y;
    uint8_t lift = 0;
    if (cubeFly.on) arcPos(cubeFly, offered ? 10 : 12, x, y, lift);
    else cubeXY(x, y);
    if (offered && !cubeFly.on) lift = (uint8_t)(8 + ((fx::isin((int)frame * 4) * 2) >> 8));   // floating
    // Turning over to the value offered: the old face until it is up.
    uint8_t log = offered ? offerLog : cubeLog;
    if (cubeTurn && cubeTurn < TURN_T / 2 && offered) log = (uint8_t)(log - 1);
    // In the middle and never turned, it shows 64, as on a real board.
    table::drawCube(x, y, (uint16_t)(log || offered || cubeOwn != match::CENTRE ? 1u << log : 64u), offered ? FX_B : WHITE, lift);
}

static void drawGlove(uint32_t frame) {
    if (!glove) return;
    int x = sx((int)(gx16 >> 4)), y = sy((int)(gy16 >> 4));
    int bob = (fx::isin((int)(frame >> 3) * 40) * 2) >> 8;
    if (tapT) bob = (tapT < 6 ? tapT : 12 - tapT) / 2;
    bob = zoomed(bob);
    const uint8_t *rm = denyT & 4 ? RM_ALERT : turnSide == bg::RED ? RM_CPU : RM_ID;
    uint8_t spot = humanTurn ? cur : mv.on ? mv.to : cpuSpot;
    if (fromBelow(spot)) sprite4(HAND, x - zoomed(HAND_TIP), y + 1 - bob, rm, zscale(), SPR_FLIP_V);   // turned over
    else sprite4(HAND, x - zoomed(HAND_TIP), y - zoomed(HAND[1]) + bob - 1, rm, zscale());
}

// ---------------------------------------------------------------------------
// HUD: whose turn, the match and the pip counts; and a plate at the foot of
// the screen naming what the glove is on, or the last move.
// ---------------------------------------------------------------------------

// Words in their colours on a rounded plate: grow (Q8) is the plate's width
// so far, and word k shows from frame 6 + 3k, dropping in.
static void plate(const char *const *w, const uint8_t *c, uint8_t n, int y, int grow, int t) {
    int tw = 0;
    for (uint8_t i = 0; i < n; i++) tw += text35Width(w[i]) + 1;
    tw--;
    int pw = ((tw + 9) * grow) >> 8;
    if (pw < 6) return;
    if (pw > 128) pw = 128;
    fillRound(64 - pw / 2, y, pw, 10, 2, NAVY);
    roundRect(64 - pw / 2, y, pw, 10, 2, GOLD);
    int x = 64 - tw / 2;
    for (uint8_t i = 0; i < n; i++) {
        int d = t - 6 - 3 * i;
        if (d >= 0) text35(x, y + 3 - (d < 3 ? 3 - d : 0), w[i], c[i]);
        x += text35Width(w[i]) + 1;
    }
}

static void drawHud(uint32_t frame) {
    gfx_fillRect(0, 0, 128, 9, INK);
    gfx_hline(0, 9, 128, GOLD);
    gfx_fillRect(0, 119, 128, 9, INK);
    gfx_hline(0, 118, 128, GOLD);
    bool vsCpu = match::setup.mode == match::VS_CPU, mine = turnSide == bg::WHITE;
    bool toRoll = match::humanToRoll(), toConfirm = match::humanToConfirm();
    char who[20], *p;
    if (over) fmtStr(who, "GAME OVER");
    else if (!opened) fmtStr(who, "OPENING ROLL");
    else if (offered) fmtStr(who, "DOUBLE OFFERED");
    else if (vsCpu && !mine) {
        // The CPU by name, with dots while it thinks.
        p = fmtStr(who, opponentName);
        if (thinking) for (uint32_t k = 0; k < ((frame >> 4) & 3); k++) *p++ = '.', *p = 0;
    } else {
        p = fmtStr(who, vsCpu ? "YOUR" : mine ? "WHITE:" : "RED:");
        fmtStr(p, toRoll ? " ROLL" : " MOVE");
    }
    text35(3, 2, who, thinking ? FX_B : vsCpu || mine || !opened ? WHITE : RED);
    // Pips to go, each side's; and in a match, the score and its length.
    char num[12];
    fmtInt(num, bg::pips(shown, bg::RED));
    int x = 126 - text35Width(num);
    text35(x, 2, num, RED);
    gfx_pixel(x - 2, 4, GOLD);
    fmtInt(num, bg::pips(shown, bg::WHITE));
    x -= 3 + text35Width(num);
    text35(x, 2, num, WHITE);
    if (match::cubeLive()) {
        // The match: the score, and what it is played to.
        p = fmtInt(num, match::score[0]); *p++ = '-';
        p = fmtInt(p, match::score[1]); *p++ = '/';
        fmtInt(p, match::setup.length);
        text35(x - 6 - text35Width(num), 2, num, GOLD);
    }

    const int py = 118;
    static const char *const ROLL[1] = {"PRESS A TO ROLL"};
    static const char *const DONE[3] = {"A DONE", "   ", "B BACK"};
    static const uint8_t PROMPT[3] = {WHITE, WHITE, SILVER};
    if (annT) {
        // The last move: the plate springs open, then the words drop in.
        int t = annT, grow = t < 8 ? fx::ease(fx::OUT_BACK, t, 8) : t > ANN_FRAMES - 8 ? (ANN_FRAMES - t) * 32 : 256;
        plate(annW, annC, annN, py, grow, t);
    } else if (over || busy()) {
    } else if (toRoll && humanTurn) {
        if (cur == AT_CUBE) {
            char d[16];
            fmtInt(fmtStr(d, "A DOUBLE TO "), 2 << cubeLog);
            const char *w[1] = {d};
            plate(w, PROMPT, 1, py, 256, 99);
        } else if (match::canDouble()) {
            // The cube is beside the dice: toward the bar.
            bool left = (turnSide == bg::WHITE) != mirror;
            const char *w[3] = {"A ROLL", "   ", left ? "< DOUBLE" : "DOUBLE >"};
            plate(w, PROMPT, 3, py, 256, 99);
        } else if (frame & 32) plate(ROLL, PROMPT, 1, py, 256, 99);
    } else if (toConfirm && humanTurn) {
        plate(DONE, PROMPT, 3, py, 256, 99);
    } else if (humanTurn && cur < AT_DICE && match::humanToMove()) {
        // What the glove is on: a point, or where the picked-up checker would go.
        char a[6], b[6];
        auto name = [](uint8_t q, char *buf) -> const char * {
            return q == BAR ? "BAR" : q == OFF ? "OFF" : (fmtInt(buf, q), buf);
        };
        const char *w[4] = {name(sel != 0xFF ? sel : cur, a), sel != 0xFF ? "/" : cur == BAR ? "" : " POINT",
                            sel != 0xFF ? name(cur, b) : "", blocked ? " NO MOVES" : ""};
        uint8_t c[4] = {WHITE, SILVER, GOLD, (uint8_t)(denyT & 4 ? RED : SILVER)};
        if (sel != 0xFF) for (uint8_t i = 0; i < nTgt; i++)
            if (tgt[i].to == cur && tgt[i].hits) { w[3] = " HIT!"; c[3] = RED; }
        plate(w, c, 4, py, 256, 99);
    }
    // The plate above: the other side's play, a hint, the coach.
    bool showAdv = adv == ADV_LAST ? toRoll && humanTurn && opened && !busy()
                 : adv == ADV_HINT ? advT < 240 && (match::humanToMove() || toConfirm)
                 : adv == ADV_VERDICT ? toConfirm : false;
    if (showAdv && !over) {
        const char *w[1] = {advBuf};
        uint8_t c[1] = {advCol};
        plate(w, c, 1, 105, adv == ADV_LAST ? 256 : advT < 8 ? fx::ease(fx::OUT_BACK, advT, 8) : 256, 99);
    }
    if (waitPress && holdT < 20 && (frame & 32)) {           // blinking
        static const char *const PRESS[1] = {"PRESS A"};
        plate(PRESS, PROMPT, 1, 100, 256, 99);
    }
}

void renderScene(uint32_t frame) {
    drawBoard();
    drawCheckers(frame, true);
}

// A still scene is not redrawn: the frame is flushed again, so palette
// effects keep moving at 60 Hz, and the bob and the blinks step at 7.5 Hz,
// so an idle board costs an eighth of the frames.
static uint32_t lastSig;

static uint32_t signature(uint32_t frame, uint32_t ui) {
    int lo, hi;
    if (fx::activeRows(lo, hi) || mv.on || fly.on || cubeFly.on || cubeTurn || offered || partyT || diceMoving() ||
        zoom != zoomTo || zoom != 5)
        return frame;
    uint32_t h = 2166136261u;
    uint32_t v[] = {
        cur, sel, cpuSpot, intent, humanTurn, thinking, picking, glove, turnSide, opened, (uint32_t)(gx16 >> 4),
        (uint32_t)(gy16 >> 4), frame >> 3, nTgt, over, tapT, annT, ui, waitPress, denyT, holdT != 0,
        (uint32_t)match::humanToRoll() | match::humanToConfirm() << 1 | match::canDouble() << 2, blocked,
        (uint32_t)(dice[0].used | dice[1].used << 1 | dice[2].used << 2 | dice[3].used << 3), diceCount(),
        adv, (uint32_t)(advT < 8 ? advT : 8), advT < 240, nHint, cubeLog, cubeOwn,
    };
    for (uint32_t x : v) h = (h ^ x) * 16777619u;
    return h;
}

void invalidate() { lastSig = 0; }

bool render(uint32_t frame, uint32_t ui) {
    zoomDrawn = true;                            // the zoom may take its next step
    uint32_t sig = signature(frame, ui);
    if (sig == lastSig) return false;
    lastSig = sig;
    drawBoard(10, 118);                          // (the HUD covers the rest)
    drawTargets(frame);
    drawCheckers(frame, false);
    drawDice();
    drawCube(frame);
    drawGlove(frame);
    drawHud(frame);
    fx::drawParticles((uint8_t)((2 * zoom + 2) / 5));
    fx::drawBanner();
    return true;
}

#if CHGAME_DEBUG
// Device render profile (debug Y command): microseconds per section,
// averaged over 8 draws of the current scene.
static void profBoard(uint32_t f) { drawBoard(10, 118); drawTargets(f); }
static void profCheckers(uint32_t f) { drawCheckers(f, false); }
static void profDice(uint32_t f) { drawDice(); drawCube(f); drawGlove(f); }
static void profFx(uint32_t) { fx::drawParticles(2); fx::drawBanner(); }
void profile(uint32_t *us) {
    static void (*const PART[5])(uint32_t) = {profBoard, profCheckers, profDice, drawHud, profFx};
    for (int k = 0; k < 5; k++) {
        uint32_t t = micros();
        for (int i = 0; i < 8; i++) PART[k](0);
        us[k] = (micros() - t) / 8;
    }
    invalidate();
}
#endif

}  // namespace stage
