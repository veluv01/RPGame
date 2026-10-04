// The play screen's presentation (Stage.h): turns the game's events into
// motion - hops, climbs, a snake's meal, the dice, the camera - and
// draws it.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and BoardView)
#include <string.h>
#include <RPGame.h>
#include <Arduino.h>
#include "config.h"
#include "Stage.h"
#include "Game.h"
#include "BoardView.h"
#include "Fx.h"
#include "Sounds.h"
#include "src/assets/Assets.h"

namespace stage {

using namespace board;
using namespace game;
using namespace layout;

// Cherries, banana, apple, strawberry.
const uint8_t SEAT_COLOUR[4] = {RED, GOLD, FELT_LT, SKIN};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool fast, demo, wide;               // QUICK pace; the title's backdrop; the player's overview

// The game as shown: it trails the rules by whatever is still in the air.
static uint8_t sPos[SEATS];                 // NOBODY: its token is in the air, or inside a snake
static int16_t sNum[SEATS], numTo[SEATS];   // the HUD's counters roll towards their targets
static uint8_t hudFlash[SEATS];

static uint8_t turn;
static bool humanTurn, glove;
static uint8_t focus;                       // the square the camera attends to
static uint8_t tapT;                        // the glove's tap
static uint8_t holdT;                       // frames the stage keeps the game waiting
static uint8_t beatT;                       // the heartbeat, a roll from home
static uint8_t lastHuman = NOBODY;          // for the hand-over between players
static bool over, overDone;
static uint8_t overT, winner;

// A token on the move: hopping square to square (past 100, there and back),
// or thrown in one spinning arc (bumped; spat out).
struct Mover { uint8_t seat, from, to, steps, i, t, on, jump; };
static Mover mv;

// The dice, in screen space: tumbling in for diceFrames(), then at rest.
static uint8_t diceA, diceB, diceT;
static bool diceAgain;
// ARCADE: the bar with the two dice and where each would take you.
static bool barOn;
static uint8_t sel, pickT;                  // the die chosen; a CPU's choice on show

// A ladder being climbed, or a snake's meal.
enum : uint8_t { LADDER, SNAKE };
struct Ride { uint8_t on, kind, link, seat, phase, t, T; };
static Ride ride;
static uint8_t snap[LINKS - LADDERS];       // jaws snapping at a token going by
static uint8_t boomT, boomAt;               // the bite's blast: two rings of chunks flying out from a square

// The plate's words: two parts, each its own colour.
static char plText[28];
static uint8_t plSplit, plC[2], plT;

// Camera: eased (world, Q4) towards its aim. The whip zoom: board::zoom
// steps towards zoomTo, one step each frame drawn.
static int32_t cx16, cy16;
static int16_t aimX, aimY;
static uint8_t zoomTo = 5;
static bool zoomDrawn = true;
// The glove's tip, world, Q4.
static int32_t gx16, gy16;

// A CPU's turn goes by at the QUICK pace, seen from above: the show is for
// your own.
static bool quick() { return !demo && (fast || !humanTurn); }
static uint8_t restZoom() { return quick() || wide ? 5 : 10; }
static uint8_t hopFrames() { return mv.jump ? 22 : quick() ? 4 : 7; }
static uint8_t diceFrames() { return quick() ? 14 : (sPos[turn] != NOBODY && sPos[turn] >= 94) ? 44 : 30; }

static const uint8_t RM_CPU[16] = {0, 1, 2, 3, 4, 5, 6, 7, RED, WINE, 10, 11, 12, 13, 14, 15};   // a CPU's red-cuffed glove

// The title's backdrop plays in silence, with no lettering.
static void snd(Sfx s) { if (!demo) audio::sfx(s); }
static void blip(int hz, int ms) { if (!demo) audio::blip((uint16_t)hz, (uint16_t)ms); }
// (Not named banner: called with an fx:: style, fx::banner would be found
// too, and win the overload.)
// Words floating up from something on the board: kept clear of the HUD and
// the screen's edges.
static void note(const char *text, int x, int y, uint8_t colour) {
    fx::floatText(text, x < 16 ? 16 : x > 112 ? 112 : x, y < 24 ? 24 : y, colour);
}
static void call(const char *text, fx::BannerStyle s, int frames) { if (!demo) fx::banner(text, s, 40, (uint8_t)frames); }

// ---------------------------------------------------------------------------
// Geometry and the camera
// ---------------------------------------------------------------------------

// Where a seat's token rests on a square (its middle, world): side by side
// with whoever else is there.
static void slot(uint8_t n, uint8_t seat, int &x, int &y) {
    uint8_t there = 0, idx = 0;
    for (uint8_t p = 0; p < st.players; p++)
        if (sPos[p] == n) { if (p == seat) idx = there; there++; }
    centre(n, x, y);
    if (sPos[seat] == n) x += (2 * idx - (there - 1)) * 2;
}

// The square a mover's hop i ends on.
static uint8_t hopSquare(uint8_t i) {
    int n = mv.from + i;
    return (uint8_t)(n > LAST ? 2 * LAST - n : n);
}

static void moverPos(int &x, int &y, int &z) {
    int ax, ay, bx, by, T = hopFrames();
    centre(mv.jump ? mv.from : hopSquare(mv.i), ax, ay);
    centre(mv.jump ? mv.to : hopSquare((uint8_t)(mv.i + 1)), bx, by);
    int e = fx::ease(mv.jump ? fx::IN_OUT : fx::LINEAR, mv.t, T);
    x = ax + (((bx - ax) * e) >> 8);
    y = ay + (((by - ay) * e) >> 8);
    z = ((mv.jump ? 14 : 5) * fx::isin(mv.t * 128 / T)) >> 8;
}

static int linkLength(uint8_t k) {
    int ax, ay, bx, by;
    centre(LINK[k].from, ax, ay);
    centre(LINK[k].to, bx, by);
    bx -= ax; by -= ay;
    if (bx < 0) bx = -bx;
    return (by < 0 ? -by : by) + bx / 2;            // near enough
}

// Rows at the foot of the screen under the bar or the plate: the board may
// scroll up from behind them.
static int covered() { return barOn ? 21 : plT ? 13 : 0; }

// Frame a world point as the zoom the camera is heading for allows.
static void aimAt(int x, int y) {
    int hx = 320 / zoomTo, hy = 295 / zoomTo, below = covered() * 5 / zoomTo;
    if (x < hx) x = hx;
    if (x > 128 - hx) x = 128 - hx;
    if (y > 128 - hy + below) y = 128 - hy + below;
    if (y < TOP + hy) y = TOP + hy;
    aimX = (int16_t)x; aimY = (int16_t)y;
}

// A square; at rest, with the stretch of board ahead of it in view (where
// the dice might take you).
static void aimSquare(uint8_t n) {
    int x, y, ax, ay;
    centre(n, x, y);
    if (!over && !ride.on) {
        centre((uint8_t)(n + 3 > LAST ? LAST : n + 3), ax, ay);
        x = (x + ax) / 2; y = (y + ay) / 2;
    }
    aimAt(x, y);
}

static void placeCamera() { setCamera((int)(cx16 >> 4), (int)(cy16 >> 4), covered()); }

static void snapCamera() {
    cx16 = aimX << 4; cy16 = aimY << 4;
    placeCamera();
}

static void stepCamera() {
    int32_t dx = (aimX << 4) - cx16, dy = (aimY << 4) - cy16;
    cx16 += dx >> 2; cy16 += dy >> 2;
    if (dx > -16 && dx < 16) cx16 = aimX << 4;
    if (dy > -16 && dy < 16) cy16 = aimY << 4;
    placeCamera();
}

// ---------------------------------------------------------------------------
// Public controls
// ---------------------------------------------------------------------------
void setFast(bool on) { fast = on; }
void setDemo(bool on) { demo = on; }
void setOverview(bool on) { wide = on; if (!ride.on && !mv.on) zoomTo = restZoom(); }
bool overview() { return wide; }
bool overShown() { return overDone; }
bool picking() { return barOn && humanTurn && diceT > diceFrames() && phase() == P_PICK && !pickT; }
uint8_t pickSel() { return sel; }
void setPick(uint8_t which) { sel = which; }

void lookAt(uint8_t n, uint8_t z) {
    board::zoom = zoomTo = z;
    focus = n;
    aimSquare(n);
    snapCamera();
}

bool busy() {
    return mv.on || holdT || (diceT && diceT <= diceFrames()) || ride.on || pickT || board::zoom != zoomTo;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
static void say(const char *a, uint8_t ca, const char *b, uint8_t cb) {
    char *p = fmtStr(plText, a);
    plSplit = (uint8_t)(p - plText);
    fmtStr(p, b);
    plC[0] = ca; plC[1] = cb;
    plT = 1;
}

static void sync() {
    for (uint8_t p = 0; p < SEATS; p++) {
        sPos[p] = st.pos[p];
        sNum[p] = numTo[p] = st.pos[p];
    }
    memset(&mv, 0, sizeof mv);
    memset(&ride, 0, sizeof ride);
    memset(snap, 0, sizeof snap);
    boomT = 0;
    fx::clear();
    diceT = holdT = pickT = plT = overT = 0;
    over = overDone = glove = barOn = wide = false;
    lastHuman = NOBODY;
    turn = st.cur;
    humanTurn = isHuman(turn);
    lookAt(st.pos[st.cur], 5);
}

void reset() {
    sync();
    glove = phase() == P_ROLL;
    zoomTo = restZoom();
}

static uint8_t humans() {
    uint8_t n = 0;
    for (uint8_t p = 0; p < st.players; p++) n += isHuman(p);
    return n;
}

// The middle of a seat's token on screen (for sparks and floating words).
static void tokenScreen(uint8_t seat, int &x, int &y) {
    slot(sPos[seat] == NOBODY ? st.pos[seat] : sPos[seat], seat, x, y);
    x = sx(x); y = sy(y);
}

static void onTurn(const Event &e) {
    turn = e.a;
    humanTurn = e.b != 0;
    glove = true;
    barOn = false;
    if (!e.c) diceT = 0;
    focus = sPos[turn] == NOBODY ? st.pos[turn] : sPos[turn];
    zoomTo = restZoom();
    plT = 0;
    if (!humanTurn) { holdT = 10; return; }
    // Several humans: the handheld changes hands.
    if (humans() > 1 && lastHuman != turn) {
        char name[10], *p = fmtStr(name, "PLAYER ");
        *p++ = (char)('1' + turn);
        *p = 0;
        call(name, fx::B_GOLD, 50);
        holdT = 30;
    }
    lastHuman = turn;
    // A roll from home, the plate says what it takes.
    char need[12];
    need[0] = 0;
    if (focus >= 94) fmtInt(fmtStr(need, "  NEED "), LAST - focus);
    say(e.c ? "ROLL AGAIN!" : "A: ROLL", e.c ? FX_B : WHITE, need, GOLD);
    if (!e.c) snd(Sfx::Turn);
    audio::led(audio::LED_BLINK);
}

static void startRide(const Event &e, uint8_t kind) {
    ride.on = 1; ride.kind = kind; ride.seat = e.a; ride.phase = 0; ride.t = 0;
    ride.link = (uint8_t)linkAt(e.b);
    focus = e.b;
}

static void onEvent(const Event &e) {
    int x, y;
    char buf[8];
    switch (e.type) {
        case EV_START: sync(); break;
        case EV_TURN: onTurn(e); break;
        case EV_DICE:
            diceA = e.a; diceB = e.b; diceAgain = e.c != 0;
            diceT = 1;
            glove = false;
            plT = 0;
            barOn = diceB != 0 && !demo;
            sel = 0;
            snd(Sfx::Dice);
            break;
        case EV_PICK:
            sel = e.b;
            pickT = fast ? 12 : 24;
            tapT = 1;
            snd(Sfx::Select);
            break;
        case EV_MOVE:
            memset(&mv, 0, sizeof mv);
            mv.seat = e.a; mv.from = e.b; mv.to = e.c; mv.steps = (uint8_t)e.amount; mv.on = 1;
            sPos[e.a] = NOBODY;
            barOn = false;
            if (diceB) diceT = 0;                        // the bar takes its dice with it
            plT = 0;
            zoomTo = restZoom();
            break;
        case EV_LADDER: startRide(e, LADDER); break;
        case EV_SNAKE: startRide(e, SNAKE); break;
        case EV_BUMP:
            tokenScreen(e.a, x, y);
            call("BUMP!", fx::B_CYAN, 50);
            fx::shake(8, 2);
            fx::burst(fx::STAR, x, y, 12, 40, SEAT_COLOUR[e.a]);
            fmtInt(buf, e.c - e.b);
            note(buf, x, y - 8, RED);
            snd(Sfx::Bump);
            memset(&mv, 0, sizeof mv);
            mv.seat = e.a; mv.from = e.b; mv.to = e.c; mv.on = mv.jump = 1;
            sPos[e.a] = NOBODY;
            numTo[e.a] = e.c;
            hudFlash[e.a] = 16;
            zoomTo = 5;                                  // to see it go
            break;
        case EV_OVER:
            over = true;
            overT = 0;
            winner = e.a;
            glove = barOn = false;
            diceT = 0;
            break;
    }
}

void begin() {}

// ---------------------------------------------------------------------------
// Per tick
// ---------------------------------------------------------------------------

// Up a ladder: a flourish at its foot, the climb rung by rung with the
// camera alongside, the top.
static void climb() {
    const Link &l = LINK[ride.link];
    uint8_t p = ride.seat;
    int x, y;
    char buf[8];
    switch (ride.phase) {
        case 0:
            if (ride.t == 1) {
                tokenScreen(p, x, y);
                fx::burst(fx::STAR, x, y, 10, 36, GOLD);
                snd(Sfx::Ladder);
            }
            if (ride.t < 12) break;
            ride.phase = 1; ride.t = 0;
            ride.T = (uint8_t)(linkLength(ride.link) * (quick() ? 3 : 5) / 4);
            sPos[p] = NOBODY;
            break;
        case 1:
            sNum[p] = numTo[p] = (int16_t)(l.from + (l.to - l.from) * ride.t / ride.T);
            if (ride.t % 4 == 0) blip(1300 + 2200 * ride.t / ride.T, 30);
            if (ride.t < ride.T) break;
            ride.phase = 2; ride.t = 0;
            sPos[p] = l.to;
            sNum[p] = numTo[p] = l.to;
            hudFlash[p] = 16;
            focus = l.to;
            tokenScreen(p, x, y);
            fmtInt(fmtStr(buf, "+"), l.to - l.from);
            note(buf, x, y - 8, GOLD);
            fx::burst(fx::STAR, x, y, 12, 40, SEAT_COLOUR[p]);
            snd(Sfx::Climb);
            audio::led(audio::LED_TRIPLE);
            if (l.to - l.from >= 30) {
                call("BIG CLIMB!", fx::B_RAINBOW, 60);
                fx::fountain(fx::CONFETTI, 64, 90, 16);
                ride.t = (uint8_t)(-30);                 // a moment longer
            }
            break;
        case 2:
            // At the top: now the camera pulls back, to show how far that was.
            if ((int8_t)ride.t < (quick() ? 8 : 24)) break;
            ride.phase = 3; ride.t = 0;
            zoomTo = 5;
            break;
        default:
            if (ride.t < (quick() ? 8 : 40) || board::zoom != 5) break;
            ride.on = 0;
            break;
    }
}

// Down a snake: it sees you, it eats you, you go all the way down inside
// it, and it spits you out at its tail.
static void swallow() {
    const Link &l = LINK[ride.link];
    uint8_t p = ride.seat;
    int x, y;
    char buf[8];
    switch (ride.phase) {
        case 0:
            if (ride.t == 1) {
                tokenScreen(p, x, y);
                note("!", x, y - sized(8), RED);
                snd(Sfx::Hiss);
            }
            if (ride.t < (quick() ? 10 : 20)) break;
            ride.phase = 1; ride.t = 0;
            tokenScreen(p, x, y);
            sPos[p] = NOBODY;
            // The bite: everything goes off at once.
            boomT = 1; boomAt = l.from;
            fx::shake(8, 3);
            pal::flash(light, 0xA12, 4);
            fx::burst(fx::SPARK, x, y, 16, 70, RED);
            fx::burst(fx::STAR, x, y, 12, 48, WHITE);
            fx::burst(fx::SPARK, x, y, 10, 30, SEAT_COLOUR[p]);
            fx::burst(fx::DUST, x, y, 8, 44, WHITE);
            call("CHOMP!", fx::B_RED, 50);
            snd(Sfx::Chomp);
            break;
        case 1:
            if (ride.t < 16) break;
            ride.phase = 2; ride.t = 0;
            ride.T = (uint8_t)(linkLength(ride.link) * (quick() ? 3 : 5) / 4 + 10);
            numTo[p] = l.to;                             // the counter tumbles down
            hudFlash[p] = ride.T;
            break;
        case 2:
            if (ride.t % 3 == 0) blip(2600 - 1700 * ride.t / ride.T, 24);
            if (ride.t < ride.T) break;
            ride.phase = 3; ride.t = 0;
            focus = l.to;
            memset(&mv, 0, sizeof mv);                   // out, head over heels
            mv.seat = p; mv.from = mv.to = l.to; mv.on = mv.jump = 1;
            tokenScreen(p, x, y);
            fmtInt(buf, l.to - l.from);
            note(buf, x, y - 8, RED);
            fx::burst(fx::DUST, x, y, 10, 30, WHITE);
            fx::shake(3, 1);
            snd(Sfx::Spit);
            break;
        case 3:
            // Down on the board again: now the camera pulls back, to show
            // how far that was.
            if (mv.on || ride.t < 30) break;
            ride.phase = 4; ride.t = 0;
            zoomTo = 5;
            break;
        default:
            if (ride.t < (quick() ? 8 : 40) || board::zoom != 5) break;
            ride.on = 0;
            break;
    }
}

static void landed() {
    int x, y;
    uint8_t p = mv.seat;
    mv.on = 0;
    sPos[p] = mv.to;
    sNum[p] = numTo[p] = mv.to;
    focus = mv.to;
    tokenScreen(p, x, y);
    fx::burst(fx::DUST, x, y + sized(4), 8, zoomed(20), WHITE);
    fx::shake(3, 1);
    snd(Sfx::Land);
    if (mv.jump) return;
    holdT = quick() ? 4 : 10;
    if (mv.from + mv.steps == LAST + 1) { call("SO CLOSE!", fx::B_CYAN, 50); holdT = quick() ? 20 : 36; }
    // Next door to a snake's head, and not on it.
    if (linkAt(mv.to) < 0 && (linkAt((uint8_t)(mv.to + 1)) >= LADDERS || linkAt((uint8_t)(mv.to - 1)) >= LADDERS))
        note("PHEW!", x, y - sized(8), CYAN);
}

void update() {
    Event e;
    // A new game cuts in on anything still showing; everything else waits its turn.
    while (peekEvent(e) && (!busy() || e.type == EV_START)) {
        popEvent(e);
        onEvent(e);
    }

    if (holdT) holdT--;
    if (pickT) pickT--;
    if (plT && plT < 8) plT++;
    if (tapT && ++tapT > 12) tapT = 0;
    for (auto &s : snap) if (s) s--;
    if (boomT && ++boomT > 22) boomT = 0;
    for (uint8_t p = 0; p < SEATS; p++) {
        if (hudFlash[p]) hudFlash[p]--;
        int d = numTo[p] - sNum[p];
        if (d) sNum[p] = (int16_t)(sNum[p] + (d / 6 ? d / 6 : (d > 0 ? 1 : -1)));
    }

    // The whip zoom: one step per frame drawn; going out, the camera jumps
    // with each step rather than drifting between them.
    bool stepOut = false;
    if (board::zoom != zoomTo && zoomDrawn) {
        stepOut = board::zoom > zoomTo;
        board::zoom = (uint8_t)(board::zoom < zoomTo ? board::zoom + 1 : board::zoom - 1);
        zoomDrawn = false;
    }

    // The dice tumble in; where they stop, a knock, and the call if they
    // earn another roll.
    if (diceT && diceT <= diceFrames() && ++diceT > diceFrames()) {
        fx::shake(3, 1);
        snd(Sfx::Land);
        holdT = quick() ? 8 : 16;
        if (diceAgain) {
            call(diceB ? "DOUBLES!" : "SIX!", fx::B_CYAN, 50);
            snd(Sfx::Doubles);
            holdT = quick() ? 20 : 36;
        }
    }

    // A roll from home: your heart, while the dice wait.
    if (glove && humanTurn && !busy() && sPos[turn] != NOBODY && sPos[turn] >= 94) {
        if (++beatT >= 44) beatT = 0;
        if (beatT == 0) snd(Sfx::Tick);
        if (beatT == 9) snd(Sfx::Tock);
    }

    if (mv.on && ++mv.t >= hopFrames()) {
        mv.t = 0;
        if (mv.jump) landed();
        else {
            uint8_t at = hopSquare(++mv.i);
            bool back = mv.from + mv.i > LAST;
            sNum[mv.seat] = numTo[mv.seat] = at;
            blip(back ? 2300 - 120 * (mv.from + mv.i - LAST) : 1500 + 110 * mv.i, 26);
            if (mv.i == mv.steps) landed();
            else if (at == LAST) {                       // off the end wall, and back
                int x, y;
                centre(LAST, x, y);
                note("BOUNCE!", sx(x) + 8, sy(y) + sized(10), CYAN);
                fx::shake(4, 1);
                snd(Sfx::Deny);
            } else {
                int k = linkAt(at);                      // past a snake: it has a go
                if (k >= LADDERS) { snap[k - LADDERS] = 12; blip(900, 14); }
            }
        }
    }

    if (ride.on) {
        ride.t++;
        if (ride.kind == LADDER) climb(); else swallow();
    }

    // The end: the winner, and the party.
    if (over && !busy() && overT < 255) {
        overT++;
        if (overT == 1) {
            char buf[12];
            if (humans() == 1 && isHuman(winner)) fmtStr(buf, "YOU WIN!");
            else { buf[0] = 'P'; buf[1] = (char)('1' + winner); fmtStr(buf + 2, " WINS!"); }
            call(buf, fx::B_RAINBOW, 160);
            if (!demo) {
                fx::fountain(fx::CONFETTI, 40, 90, 20);
                fx::fountain(fx::COIN, 88, 90, 14);
            }
            snd(isHuman(winner) || !humans() ? Sfx::Win : Sfx::Lose);
            audio::led(audio::LED_PARTY);
            focus = LAST;
            zoomTo = fast && !demo ? 5 : 10;
        }
        if (overT == (demo ? 120 : 200)) overDone = true;
    }

    // What the camera frames: the token on the move, the square you are
    // choosing, else the square in focus.
    if (mv.on && !mv.jump) {
        int x, y, z;
        moverPos(x, y, z);
        aimAt(x, y);
    } else if (picking()) {
        int x, y, ax, ay;
        uint8_t via;
        landing(st.pos[turn], die(sel), &via);
        centre(st.pos[turn], x, y);
        centre(via, ax, ay);
        aimAt((x + ax) / 2, (y + ay) / 2);
    } else if (ride.on && ride.phase == (ride.kind == SNAKE ? 2 : 1)) {
        // Down the snake with whoever is inside it, or up the ladder.
        int x, y, ax, ay, e = ride.kind == SNAKE ? fx::ease(fx::IN_OUT, ride.t, ride.T) : ride.t * 256 / ride.T;
        centre(LINK[ride.link].from, x, y);
        centre(LINK[ride.link].to, ax, ay);
        aimAt(x + (((ax - x) * e) >> 8), y + (((ay - y) * e) >> 8));
    } else aimSquare(focus);
    if (stepOut) snapCamera();
    stepCamera();

    // The glove glides to the token whose turn it is.
    int x, y;
    slot(sPos[turn] == NOBODY ? st.pos[turn] : sPos[turn], turn, x, y);
    y -= 6;
    gx16 += ((x << 4) - gx16) >> 1;
    gy16 += ((y << 4) - gy16) >> 1;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// A token with its middle at world (x, y), lifted off the board, turned.
static void drawToken(uint8_t seat, int x, int y, int lift, uint8_t spin) {
    const uint8_t *a = TOKEN[seat];
    int s = zscale(), px = sx(x), py = sy(y);
    if (px < -24 || px > 152 || py < -24 || py > 152) return;
    if (lift) gfx_fillEllipse(px, py + sized(5), sized(4), sized(1), INK);       // its shadow stays on the board
    py -= zoomed(lift);
    if (spin) spriteRot(a, a[0] / 2, a[1] / 2, px, py, spin, s, RM_ID);
    else sprite4(a, px - ((a[0] * s) >> 9), py - ((a[1] * s) >> 9), RM_ID, s);
}

static void drawTokens(uint32_t frame) {
    // Whoever's turn it is on top.
    for (uint8_t i = 1; i <= st.players; i++) {
        uint8_t p = (uint8_t)((turn + i) % st.players);
        int x, y, lift = 0;
        if (sPos[p] == NOBODY) continue;
        slot(sPos[p], p, x, y);
        if (ride.on && ride.seat == p && ride.kind == SNAKE && !ride.phase) x += ride.t & 2 ? 1 : -1;    // it has seen the snake
        if (over && p == winner && overT) {
            lift = (fx::isin((int)(frame * 6) & 127) * 4) >> 8;                    // the winner's dance
        }
        drawToken(p, x, y, lift, 0);
    }
    if (mv.on) {
        int x, y, z;
        moverPos(x, y, z);
        drawToken(mv.seat, x, y, z, mv.jump ? (uint8_t)(mv.t * 256 / hopFrames()) : 0);
    }
    if (ride.on && ride.kind == LADDER && ride.phase == 1) {
        int ax, ay, bx, by;
        centre(LINK[ride.link].from, ax, ay);
        centre(LINK[ride.link].to, bx, by);
        drawToken(ride.seat, ax + (bx - ax) * ride.t / ride.T, ay + (by - ay) * ride.t / ride.T,
                  (ride.t & 7) < 4 ? 1 : 0, 0);
    }
}

static void drawLinks(uint32_t frame) {
    for (uint8_t i = 0; i < LADDERS; i++)
        drawLadder(i, ride.on && ride.kind == LADDER && ride.link == i ? (ride.phase == 1 ? ride.t * 256 / ride.T : ride.phase ? 256 : 0) : 0);
    dbg::prof(4);
    for (uint8_t i = 0; i < LINKS - LADDERS; i++) {
        Pose p;
        p.phase = (uint8_t)((frame >> 3) * 14);
        p.amp = 36;
        p.open = snap[i] != 0;
        p.bulge = -1;
        p.bulgeColour = 0;
        if (ride.on && ride.kind == SNAKE && ride.link == LADDERS + i) {
            p.open = ride.phase < 2;
            if (ride.phase == 2) {
                // Its meal goes down, and it thrashes.
                p.bulge = (int16_t)(fx::ease(fx::IN_OUT, ride.t, ride.T));
                p.bulgeColour = SEAT_COLOUR[ride.seat];
                p.amp = 70;
                p.phase = (uint8_t)(frame * 10);
            }
        }
        drawSnake(i, p, frame);
    }
}

// Whose turn it is: an arrow bobbing over its token.
static void drawGlove(uint32_t frame) {
    if (!glove || mv.on || over || ride.on || barOn) return;
    int x = sx((int)(gx16 >> 4)), y = sy((int)(gy16 >> 4)) - sized(1) + ((fx::isin((int)(frame >> 3) * 40) * 2) >> 8);
    int n = big() ? 5 : 3;
    for (int i = 0; i <= n; i++) gfx_hline(x - i - 1, y - i - 1, 2 * i + 3, INK);
    gfx_hline(x - n - 1, y - n - 2, 2 * n + 3, INK);
    for (int i = 0; i < n; i++) gfx_hline(x - i, y - i - 2, 2 * i + 1, humanTurn ? FX_B : SILVER);
}

// ---------------------------------------------------------------------------
// HUD: everyone's square along the top; a plate at the foot of the screen.
// ---------------------------------------------------------------------------
static void plate(const char *a, uint8_t ca, const char *b, uint8_t cb, int y, int grow) {
    int wa = text35Width(a), tw = wa + (*b ? text35Width(b) + 1 : 0);
    int pw = ((tw + 8) * grow) >> 8;
    if (pw < 6) return;
    fillRound(64 - pw / 2, y, pw, 11, 2, NAVY);
    roundRect(64 - pw / 2, y, pw, 11, 2, GOLD);
    if (grow < 256) return;
    text35(64 - tw / 2, y + 3, a, ca);
    text35(64 - tw / 2 + wa + 1, y + 3, b, cb);
}

static void drawHud() {
    gfx_fillRect(0, 0, 128, 9, INK);
    gfx_hline(0, 9, 128, GOLD);
    for (uint8_t p = 0; p < st.players; p++) {
        char buf[8];
        int x = p * 32;
        bool lit = p == turn && !over;
        if (lit) gfx_fillRect(x, 0, 32, 9, NAVY);
        gfx_fillRect(x + 2, 2, 5, 5, hudFlash[p] & 2 ? WHITE : SEAT_COLOUR[p]);
        buf[0] = 'P'; buf[1] = (char)('1' + p); buf[2] = '~';
        fmtInt(buf + 3, sNum[p]);
        text35(x + 9, 2, buf, hudFlash[p] & 2 ? SEAT_COLOUR[p] : lit ? FX_B : WHITE);
    }
    if (st.players < SEATS) {                       // room for the game being played
        const char *m = st.mode == ARCADE ? "ARCADE" : "CLASSIC";
        text35(127 - text35Width(m), 2, m, SILVER);
    }
    if (plT && !barOn && !over && !diceT && !wide) {
        char a[28];
        memcpy(a, plText, plSplit);
        a[plSplit] = 0;
        plate(a, plC[0], plText + plSplit, plC[1], 116, plT < 8 ? fx::ease(fx::OUT_BACK, plT, 8) : 256);
    }
}

// ---------------------------------------------------------------------------
// The dice, and the ARCADE bar
// ---------------------------------------------------------------------------
static void drawDie(int x, int y, uint8_t face, uint8_t angle) {
    static const uint16_t PIPS[6] = {0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};     // a 3 x 3 of pips, bit = 3 * row + column
    if (angle) spriteRot(DIE, 6, 6, x, y, angle, 256, RM_ID);
    else sprite4(DIE, x - 6, y - 6, RM_ID);
    int cs = fx::isin(angle + 64), sn = fx::isin(angle);
    for (int i = 0; i < 9; i++) {
        if (!(PIPS[face - 1] >> i & 1)) continue;
        int dx = (i % 3 - 1) * 3, dy = (i / 3 - 1) * 3;
        gfx_fillRect(x + ((dx * cs - dy * sn + 128) >> 8) - 1, y + ((dx * sn + dy * cs + 128) >> 8) - 1, 2, 2, INK);
    }
}

// What moving by a die would do: the words, and their colour.
static uint8_t preview(uint8_t d, char *buf) {
    uint8_t via, to = landing(st.pos[turn], d, &via);
    if (to == LAST) { fmtStr(buf, "WIN!"); return FX_B; }
    if (to != via) { fmtInt(fmtStr(buf, to > via ? "UP TO " : "DOWN TO "), to); return to > via ? GOLD : RED; }
    if (to != 1)
        for (uint8_t r = 0; r < st.players; r++)
            if (r != turn && sPos[r] == to) {
                char *p = fmtStr(buf, "BUMP P");
                *p++ = (char)('1' + r);
                *p = 0;
                return CYAN;
            }
    fmtInt(fmtStr(buf, "TO "), to);
    return WHITE;
}

static void drawDice(uint32_t frame) {
    if (!diceT || over) return;
    int T = diceFrames(), t = diceT > T ? T : diceT;
    int drop = ((256 - fx::ease(fx::OUT_BOUNCE, t, T)) * 44) >> 8;
    bool down = t >= T, choice = down && diceA != diceB;
    if (barOn) {
        gfx_fillRect(0, 107, 128, 21, NAVY);
        gfx_hline(0, 106, 128, GOLD);
    }
    for (int k = 0; k < (diceB ? 2 : 1); k++) {
        // In from the right, bouncing, spinning down to rest: one die in
        // the corner, or two in the bar.
        int rx = diceB ? 12 + k * 64 : 108, ry = diceB ? 117 : 108;
        bool on = choice && k == sel;
        if (barOn && on) {
            fillRound(2 + k * 64, 109, 60, 17, 3, INK);
            roundRect(2 + k * 64, 109, 60, 17, 3, pickT ? WHITE : (frame & 8) ? FX_B : GOLD);
        }
        int x = rx + (T - t) * (3 + k), y = ry - drop;
        uint8_t face = t < T ? (uint8_t)((t / 3 + k * 2) % 6 + 1) : (k ? diceB : diceA);
        if (!barOn) gfx_fillEllipse(x, ry + 7, 6, 2, INK);
        drawDie(x, y, face, (uint8_t)((T - t) * (k ? 19 : -23)));
        if (barOn && down) {
            char buf[12];
            uint8_t c = preview(face, buf);
            text35(22 + k * 64, 115, buf, choice && !on ? SILVER : c);
        }
    }
    // The glove on the die chosen: yours, or the CPU's as it taps.
    if (barOn && choice && (humanTurn || pickT)) {
        int bob = tapT ? (tapT < 6 ? tapT : 12 - tapT) / 2 : (fx::isin((int)(frame >> 3) * 40) * 2) >> 8;
        sprite4(HAND, 12 + sel * 64 - HAND_TIP + 12, 96 + bob, humanTurn ? RM_ID : RM_CPU);
    }
}

// The squares the dice offer: where the die chosen leads (in red, flashing,
// if that is into a snake), an arc of dots marching there from your token,
// and the other die's square.
static void drawMarks(uint32_t frame) {
    if (!barOn || diceT <= diceFrames() || diceA == diceB || mv.on || sPos[turn] == NOBODY) return;
    for (uint8_t k = 0; k < 2; k++) {
        uint8_t via, to = landing(sPos[turn], k ? diceB : diceA, &via);
        if (k != sel) { mark(via, SILVER); continue; }
        bool snake = to < via, blink = (frame & 8) != 0;
        uint8_t c = snake ? (blink ? RED : WHITE) : (blink ? WHITE : FX_A);
        mark(via, c);
        if (to != via) mark(to, snake ? c : FX_A);
        int ax, ay, bx, by;
        centre(sPos[turn], ax, ay);
        centre(via, bx, by);
        ax = sx(ax); ay = sy(ay); bx = sx(bx); by = sy(by);
        int n = big() ? 16 : 10, d = big() ? 2 : 1, h = zoomed(11);
        for (int i = 1; i < n; i++) {
            if ((i + (int)(frame >> 3)) & 1) continue;
            int x = ax + (bx - ax) * i / n, y = ay + (by - ay) * i / n - 4 * h * i * (n - i) / (n * n);
            gfx_fillRect(x - d + 1, y - d + 1, 2 * d, 2 * d, INK);
            gfx_fillRect(x - d, y - d, 2 * d - 1 + (d & 1), 2 * d - 1 + (d & 1), snake ? RED : c);
        }
    }
}

// A still scene is not redrawn: the frame is flushed again, so palette
// effects keep moving at 60 Hz, and the snakes wriggle at 7.5 Hz.
static uint32_t lastSig;

static uint32_t signature(uint32_t frame, uint32_t ui) {
    int lo, hi;
    bool rolling = false;
    for (uint8_t p = 0; p < SEATS; p++) rolling |= sNum[p] != numTo[p] || hudFlash[p];
    for (uint8_t s : snap) rolling |= s != 0;
    if (fx::activeRows(lo, hi) || mv.on || rolling || ride.on || boomT || (over && overT) || tapT ||
        (diceT && diceT <= diceFrames()) || (plT && plT < 8))
        return frame;
    if (cx16 != (int32_t)aimX << 4 || cy16 != (int32_t)aimY << 4) return frame;
    uint32_t h = 2166136261u;
    uint32_t v[] = {
        (uint32_t)camX, (uint32_t)camY, board::zoom, focus, turn, glove, (uint32_t)(gx16 >> 4), (uint32_t)(gy16 >> 4),
        frame >> 3, plT, diceT, barOn, sel, pickT, over, ui,
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
    dbg::profStart();
    drawBoard();
    dbg::prof(0);
    drawMarks(frame);
    drawLinks(frame);
    dbg::prof(1);
    drawTokens(frame);
    if (!demo) {
        drawGlove(frame);
        drawHud();
        drawDice(frame);
    }
    dbg::prof(2);
    if (boomT) {
        // Chunks in two rings, the inner one slower and turned half a step.
        static const uint8_t HOT[4] = {RED, WHITE, GOLD, WHITE};
        int x, y, e = fx::ease(fx::OUT_CUBIC, boomT, 22);
        centre(boomAt, x, y);
        x = sx(x); y = sy(y);
        for (int i = 0; i < 24; i++) {
            int a = i * 256 / 12 + (i < 12 ? 0 : 11), r = (zoomed(i < 12 ? 34 : 20) * e) >> 8;
            int d = sized(boomT < 8 ? 3 : boomT < 16 ? 2 : 1);
            gfx_fillRect(x + ((fx::isin(a + 64) * r) >> 8) - d / 2, y + ((fx::isin(a) * r) >> 8) - d / 2, d, d, HOT[(i + boomT / 2) & 3]);
        }
    }
    fx::drawParticles((uint8_t)(board::zoom * 2 / 5));
    fx::drawFloats();
    fx::drawBanner();
    fx::applyShake(10, 127);
    dbg::prof(3);
    return true;
}

}  // namespace stage
