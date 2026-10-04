// The play screen in motion (Stage.h): match events become tiles in the
// air, the glove, the camera, the HUD and the firecrackers.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (hot pixel loops live in the library's Draw)
#include <string.h>
#include <RPGame.h>
#include <Arduino.h>
#include "config.h"
#include "Stage.h"
#include "Match.h"
#include "Table.h"
#include "Layout.h"
#include "Fx.h"
#include "Sounds.h"
#include "src/assets/Assets.h"

#if CHGAME_DEBUG && defined(CHSIM)
uint64_t sim_hostNanos();
#endif

namespace stage {

using namespace table;
using dom::NONE;

// ---------------------------------------------------------------------------
// Colour remaps
// ---------------------------------------------------------------------------
static const uint8_t RM_HIT[16] = {INK, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE,
                                   WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE};   // lit: a white flash
static const uint8_t RM_ALERT[16] = {0, RED, 2, 3, 4, WINE, 6, 7, RED, WINE, 10, 11, 12, 13, 14, 15};   // the glove, denied

constexpr int RACK_Y = 99;          // the hand's tiles (13 x 25), standing in the rack
constexpr int OPP_Y = 11;           // the other hand's backs (4 x 7)
constexpr int BONE_X = 110;         // the boneyard's count, on the far rack

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static uint8_t shownN;               // tiles of the line on the felt (layout holds the same)
static uint32_t gone;                // ... blown off it at the round's end (a bit per play)
static uint32_t handShown[2];        // tiles standing in each hand
static uint8_t endsShown;            // what the line's ends add up to
static uint8_t view;                 // whose hand is on the near rack
static bool hidden;                  // ... face down (two players: until the hand-over is answered)
static uint8_t turnSide;
static bool humanTurn, setTurn, over, overDone, fast;
static uint8_t cur = NONE;           // the tile of the hand under the glove
static uint8_t selTile = NONE, selArms[dom::ARMS], nSel, selAt;     // choosing an end for it
static uint8_t hintTile = NONE, hintArm, hintT;
static int16_t shownScore[2], goalScore[2];     // the HUD's scores count up to what has been shown
static uint8_t tick;

// A tile in the air: from a hand to the line, or from the boneyard to a hand.
enum : uint8_t { M_PLAY, M_DRAW };
struct Mover { uint8_t on, kind, side, tile, arm, pts, t, T; int16_t x0, y0; layout::Placed to; };
static Mover mv;

// A tile blown off the line, spinning away (CHChess's captured piece).
struct Flyer { uint8_t on, v, t; int8_t vx, vy, spin; int16_t x, y, gy; };     // gy: the felt under it
static Flyer fly[6];

static bool dealing;
static uint8_t dealT;
static uint8_t holdT;                // frames the stage keeps the game waiting
static bool waitPress;               // ... until a button
static uint8_t handTo = NONE;        // the hand-over: who the handheld goes to

// The round's end: the call, the fuse along the line, the points.
enum : uint8_t { E_NONE, E_CALL, E_FUSE, E_TALLY, E_HOLD };
static uint8_t endPhase, endWinner, endWhy, endPts;
static uint16_t endT;
static bool cheer, boomed;
static uint8_t popAt[dom::TILES], fuseT, lastAt, popped;

// The plate over the foot of the felt, and a number floating up.
static char sayBuf[28];
static uint8_t sayCol, sayT;
static char floatBuf[6];
static uint8_t floatT, floatCol;
static int16_t floatX, floatY;

static bool glove, gloveBelow;
static int32_t gx16 = 64 << 4, gy16 = 90 << 4;      // the fingertip, gliding (screen, Q4)
static uint8_t denyT;

// The camera: close up (zoom 10) on the focus - the tile just played, the
// end the glove's tile fits, the fuse - gliding after it; or the whole table
// (zoom 5) while the player asks for it. table::zoom steps towards zoomTo,
// one step each frame drawn (a slow frame never bunches two).
static uint8_t zoomTo = 10;
static bool overview, zoomDrawn = true;
static int16_t focusX = CX, focusY = CY;

// ---------------------------------------------------------------------------
// Where things are
// ---------------------------------------------------------------------------
// The top-left of a tile of the side's hand, mask being the tiles standing in it.
static void slotXY(uint8_t side, uint32_t mask, uint8_t tile, int &x, int &y) {
    int n = dom::count(mask), i = dom::count(mask & ((1u << tile) - 1));
    if (side == view) {
        // Nine fit side by side; more overlap.
        int pitch = n <= 9 ? 14 : 113 / (n - 1);
        x = 64 - ((n - 1) * pitch + 13) / 2 + i * pitch;
        y = RACK_Y;
    } else {
        int pitch = n <= 13 ? 5 : 60 / (n - 1);
        x = 3 + i * pitch;
        y = OPP_Y;
    }
}

static void centreOf(const layout::Placed &p, int &x, int &y) {
    int w, h;
    placedBox(p, x, y, w, h);
    x += w / 2; y += h / 2;
}

static void focusOn(int x, int y) { focusX = (int16_t)x; focusY = (int16_t)y; }

static void say(const char *text, uint8_t colour, uint8_t frames = 90) {
    fmtStr(sayBuf, text);
    sayCol = colour;
    sayT = frames;
}

static const char *nameOf(uint8_t side) {
    if (match::setup.mode == match::VS_CPU) return side ? "CPU" : "YOU";
    return side ? "P2" : "P1";
}

// ---------------------------------------------------------------------------
// Public controls
// ---------------------------------------------------------------------------
uint8_t cursor() { return cur; }
void setCursor(uint8_t tile) { cur = tile; }
void setFast(bool on) { fast = on; }
void setOverview(bool on) {
    if (on != overview) audio::sfx(Sfx::Whoosh);
    overview = on;
}
bool choosing() { return selTile != NONE; }
uint8_t chosen() { return selArms[selAt]; }
void deny() {
    denyT = 24;
    audio::sfx(Sfx::Deny);
}

void choose(uint8_t tile, const uint8_t *arms, uint8_t n) {
    selTile = tile;
    nSel = n;
    selAt = 0;
    memcpy(selArms, arms, n);
    hintTile = NONE;
    audio::sfx(Sfx::Lift);
}

void chooseStep(int d) {
    selAt = (uint8_t)((selAt + nSel + d) % nSel);
    audio::sfx(Sfx::Cursor);
}

void unchoose() { selTile = NONE; }

void hint(uint8_t tile, uint8_t arm) {
    hintTile = tile;
    hintArm = arm;
    hintT = 0;
    cur = tile;
    audio::sfx(Sfx::Coin);
}

bool waiting() { return waitPress; }
void acknowledge() {
    waitPress = false;
    fx::holdBanner(false);
    if (handTo != NONE) {            // the handheld has changed hands
        view = handTo;
        handTo = NONE;
        hidden = false;
        sayT = 0;
        audio::sfx(Sfx::Turn);
    }
}

void hurry() {
    if (endPhase == E_FUSE) fuseT = 254;
}

bool busy() {
    return mv.on || dealing || holdT || waitPress || (over && !overDone);
}

bool ready() {
    match::Event e;
    return !busy() && !match::peekEvent(e);
}
bool overShown() { return overDone; }

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
// Dust kicked out from under something set down with its middle at screen
// (cx, cy): the felt's own colour, so it blends in.
static void puff(int cx, int cy, uint8_t n) {
    int rr = zoomed(5), sp = zoomed(26);
    for (uint8_t i = 0; i < n; i++) {
        int a = i * 256 / n + fx::rndRange(0, 16);
        int c = fx::isin(a + 64), q = fx::isin(a);
        fx::spawn(fx::DUST, cx + ((c * rr) >> 8), cy + ((q * rr) >> 8), (c * sp) >> 8, (q * sp) >> 9,
                  (uint8_t)fx::rndRange(16, 30), FELT_LT);
    }
}

static void reset() {
    memset(&mv, 0, sizeof mv);
    memset(fly, 0, sizeof fly);
    gone = 0;
    over = overDone = false;
    endPhase = E_NONE;
    dealing = waitPress = false;
    holdT = sayT = floatT = hintT = denyT = endsShown = 0;
    handTo = NONE;
    cur = hintTile = NONE;
    unchoose();
    glove = false;
    zoom = zoomTo = 10;
    overview = false;
    focusOn(CX, CY);
    setCamera(CX, CY);
    fx::clear();
    for (uint8_t s = 0; s < 2; s++) shownScore[s] = goalScore[s] = (int16_t)match::score[s];
    turnSide = match::round.turn;
    humanTurn = false;
    bool two = match::setup.mode == match::TWO_PLAYER;
    if (!two) view = 0;
    hidden = two;
}

void snap() {
    reset();
    layout::rebuild(match::round, match::round.n);
    shownN = layout::n;
    handShown[0] = match::round.hand[0];
    handShown[1] = match::round.hand[1];
    endsShown = dom::endSum(match::round);
}

static void onStart(bool restored) {
    if (restored) { snap(); return; }
    reset();
    layout::reset();
    shownN = 0;
    handShown[0] = handShown[1] = 0;
}

static void onTurn(uint8_t side, bool human, bool set) {
    turnSide = side;
    setTurn = set;
    humanTurn = human && !set;
    unchoose();
    hintTile = NONE;
    if (humanTurn && match::setup.mode == match::TWO_PLAYER && (view != side || hidden)) {
        // Two players: the rack turns face down until the next player has the handheld.
        hidden = true;
        handTo = side;
        waitPress = true;
        say(side ? "PLAYER 2: PRESS A" : "PLAYER 1: PRESS A", GOLD, 255);
        return;
    }
    if (humanTurn) audio::sfx(Sfx::Turn);
    else if (!set) holdT = fast ? 6 : 22;       // the CPU takes a moment
}

static void onPlay(const match::Event &e) {
    uint8_t s = e.a;
    int x, y;
    slotXY(s, handShown[s], e.b, x, y);
    bool rack = s == view;
    mv.on = 1; mv.kind = M_PLAY; mv.side = s; mv.tile = e.b; mv.arm = e.c; mv.pts = e.d;
    mv.x0 = (int16_t)(x + (rack ? 6 : 2)); mv.y0 = (int16_t)(y + (rack ? 12 : 3));
    mv.t = 0; mv.T = fast ? 12 : 20;
    handShown[s] &= ~(1u << e.b);
    layout::plan(e.b, e.c, mv.to);
    unchoose();
    hintTile = NONE;
    audio::sfx(Sfx::Lift);
}

static void onDraw(const match::Event &e) {
    uint8_t s = e.a;
    int x, y;
    slotXY(s, handShown[s] | (1u << e.b), e.b, x, y);
    mv.on = 1; mv.kind = M_DRAW; mv.side = s; mv.tile = e.b;
    mv.x0 = (int16_t)x; mv.y0 = (int16_t)y;
    mv.t = 0; mv.T = fast ? 6 : 10;
    if (!match::isHuman(s)) say("CPU DRAWS", SILVER, 40);
}

static void onPass(uint8_t s) {
    char buf[20];
    fmtStr(fmtStr(buf, nameOf(s)), s || match::setup.mode == match::TWO_PLAYER ? " PASSES" : " PASS");
    say(buf, RED, 80);
    audio::sfx(Sfx::Knock);
    holdT = fast ? 30 : 60;
}

static void onRound(const match::Event &e) {
    over = true;
    overDone = false;
    endPhase = E_CALL;
    endT = 0;
    endWinner = e.a; endWhy = e.b; endPts = e.c;
    humanTurn = false;
    hidden = false;
    unchoose();
    hintTile = NONE;
    cheer = endWinner < 2 && (match::setup.mode == match::TWO_PLAYER || endWinner == 0);
    if (endWhy == dom::DOMINO) {
        fx::banner("DOMINO!", cheer ? fx::B_RAINBOW : fx::B_RED, 44, 64);
        audio::sfx(cheer ? Sfx::Win : Sfx::Lose);
        if (cheer) audio::led(audio::LED_TRIPLE);
    } else {
        fx::banner("BLOCKED!", fx::B_WHITE, 44, 64);
        audio::sfx(Sfx::Knock);
    }
}

void begin() {}

// ---------------------------------------------------------------------------
// A tile set down
// ---------------------------------------------------------------------------
static void floatUp(int x, int y, int v, uint8_t colour) {
    char *p = floatBuf;
    *p++ = '+';
    fmtInt(p, v);
    floatX = (int16_t)x; floatY = (int16_t)y;
    floatCol = colour;
    floatT = 1;
}

static void scored(uint8_t s, uint8_t pts, int wx, int wy) {
    static const char *const CALL[4] = {"FIVE!", "TEN!", "FIFTEEN!", "TWENTY!"};
    char num[6];
    const char *text = pts <= 20 ? CALL[pts / 5 - 1] : (fmtStr(fmtInt(num, pts), "!"), num);
    bool mine = match::isHuman(s);
    goalScore[s] = (int16_t)(goalScore[s] + pts);
    fx::banner(text, !mine ? fx::B_RED : pts >= 15 ? fx::B_RAINBOW : fx::B_GOLD, 44, 60);
    int cx = sx(wx), cy = sy(wy);
    fx::burst(fx::SPARK, cx, cy, 10, 36, mine ? GOLD : RED);
    fx::burst(fx::STAR, cx, cy, (uint8_t)(2 + pts / 5 * 2), 28, WHITE);
    floatUp(cx - 6, cy - 12, pts, mine ? GOLD : RED);
    audio::sfx(pts >= 15 ? Sfx::Score : Sfx::Coin);
    audio::led(audio::LED_BLINK);
    holdT = fast ? 24 : 50;
}

static void land() {
    mv.on = 0;
    if (mv.kind == M_DRAW) {
        handShown[mv.side] |= 1u << mv.tile;
        audio::blip(2600, 16);
        holdT = fast ? 3 : 8;
        return;
    }
    layout::add(mv.tile, mv.arm);
    shownN = layout::n;
    endsShown = dom::endSum(match::round);
    int x, y;
    centreOf(mv.to, x, y);
    focusOn(x, y);
    setCamera(x, y);
    audio::sfx(Sfx::Land);
    fx::shake(3, 1);
    puff(sx(x), sy(y), 7);
    holdT = humanTurn ? 4 : fast ? 6 : 14;
    if (setTurn) say(dom::isDouble(mv.tile) ? "HIGHEST DOUBLE LEADS" : "HEAVIEST TILE LEADS", GOLD);
    else if (dom::count(handShown[mv.side]) == 1) {
        // One tile from going out: said aloud, as at the table.
        char buf[20];
        fmtStr(fmtStr(buf, nameOf(mv.side)), ": LAST TILE!");
        say(buf, match::isHuman(mv.side) ? GOLD : RED, 70);
    }
    if (mv.pts) scored(mv.side, mv.pts, x, y);
}

// Where the tile in the air is: its middle on screen, how far turned, its
// size (Q8), and how high over the felt (pixels: y + lift is the ground).
static void moverPos(int &x, int &y, uint8_t &angle, int &scale, int &lift) {
    int e = fx::ease(fx::IN_OUT, mv.t, mv.T), tx, ty;
    centreOf(mv.to, tx, ty);
    tx = sx(tx); ty = sy(ty);
    x = mv.x0 + (((tx - mv.x0) * e) >> 8);
    lift = (fx::isin(mv.t * 128 / mv.T) * 10) >> 8;
    y = mv.y0 + (((ty - mv.y0) * e) >> 8) - lift;
    // Out of the hand upright, turning to lie as it will; from the far rack, growing.
    int s0 = mv.side == view ? 256 : 80, s1 = zscale() / 2;
    scale = s0 + (((s1 - s0) * e) >> 8);
    angle = mv.to.upright() ? 0 : (uint8_t)((64 * (256 - e)) >> 8);
}

// ---------------------------------------------------------------------------
// The round's end: the line goes off like a string of firecrackers, from
// the last tile played back to the first and out along the other arms.
// ---------------------------------------------------------------------------
static void lightFuse() {
    const uint8_t *play = match::round.play;
    uint8_t pos[dom::TILES], cnt[dom::ARMS] = {0, 0, 0, 0};
    pos[0] = 0;
    for (uint8_t i = 1; i < shownN; i++) pos[i] = ++cnt[play[i] >> 5];
    uint8_t last = (uint8_t)(shownN - 1), arm = last ? play[last] >> 5 : 0xFF, from = pos[last];
    lastAt = 0;
    for (uint8_t i = 0; i < shownN; i++) {
        uint8_t d = !i ? from : (play[i] >> 5) == arm ? (uint8_t)(from - pos[i]) : (uint8_t)(from + pos[i]);
        // Crackers speed up as the fuse burns; a lost round is just swept away.
        uint8_t at = 1;
        if (!cheer) at = (uint8_t)(1 + 2 * d);
        else for (uint8_t k = 0; k < d; k++) at = (uint8_t)(at + (k < 11 ? 14 - k : 3) / (fast ? 2 : 1));
        popAt[i] = at;
        if (at > lastAt) lastAt = at;
    }
    fuseT = popped = 0;
    boomed = false;
    endPhase = E_FUSE;
}

static void pop(uint8_t i) {
    gone |= 1u << i;
    popped++;
    int x, y;
    centreOf(layout::at[i], x, y);
    focusOn(x, y);                                          // the camera goes along the fuse
    x = sx(x); y = sy(y);
    if (!cheer) {
        puff(x, y, 5);
        audio::blip((uint16_t)(900 - 12 * popped), 12);
        return;
    }
    // Blown off its place: up, over and away, spinning.
    Flyer *f = &fly[0];
    for (auto &g : fly) {
        if (!g.on) { f = &g; break; }
        if (g.t > f->t) f = &g;                              // (none free: the one longest gone)
    }
    f->on = 1; f->v = layout::at[i].v; f->t = 0;
    f->x = (int16_t)(x << 4); f->y = (int16_t)(y << 4); f->gy = (int16_t)(y + 3);
    f->vx = (int8_t)fx::rndRange(-30, 31); f->vy = (int8_t)fx::rndRange(-58, -30);
    f->spin = (int8_t)((fx::rnd() & 1 ? 1 : -1) * fx::rndRange(7, 15));
    fx::burst(fx::SPARK, x, y, 6, 36, fx::RAIN[popped % 5]);
    fx::burst(fx::STAR, x, y, 2, 24, WHITE);
    for (uint8_t k = 0; k < 2; k++)                         // smoke
        fx::spawn(fx::DUST, x + fx::rndRange(-3, 4), y, fx::rndRange(-8, 9), fx::rndRange(-18, -8), 26, SILVER);
    fx::shake(3, 1);
    audio::blip((uint16_t)(1200 + 70 * popped), 24);
    if (popAt[i] == lastAt && !boomed) {
        // The last of them: the big one.
        boomed = true;
        fx::burst(fx::SPARK, x, y, 16, 52, GOLD);
        fx::burst(fx::STAR, x, y, 8, 40, WHITE);
        fx::shake(12, 3);
        audio::sfx(Sfx::Boom);
        audio::led(audio::LED_BLINK);
    }
}

static bool flying() {
    for (auto &f : fly) if (f.on) return true;
    return false;
}

// The points the round's end is worth, onto the score; and the match's last word.
static void tally() {
    if (endWinner < 2 && endPts) {
        goalScore[endWinner] = (int16_t)(goalScore[endWinner] + endPts);
        floatUp(endWinner ? 96 : 12, 26, endPts, GOLD);
        audio::sfx(Sfx::Coin);
    }
}

static void lastWord() {
    if (!match::matchOver()) { overDone = true; endPhase = E_NONE; return; }
    uint8_t w = match::score[1] > match::score[0];
    bool vsCpu = match::setup.mode == match::VS_CPU, won = !vsCpu || !w;
    fx::banner(vsCpu ? (won ? "YOU WIN!" : "YOU LOSE") : w ? "P2 WINS!" : "P1 WINS!", won ? fx::B_RAINBOW : fx::B_RED, 56, 170);
    fx::holdBanner(waitPress = true);
    holdT = 70;                                          // PRESS A once it has sunk in
    cheer = won;
    if (won) {
        audio::sfx(Sfx::Match);
        audio::led(audio::LED_PARTY);
        fx::fountain(40, 96, 20);
        fx::fountain(88, 96, 20);
    } else audio::sfx(Sfx::Lose);
    endPhase = E_HOLD;
    endT = 0;
}

// ---------------------------------------------------------------------------
// Per tick
// ---------------------------------------------------------------------------
void update() {
    match::Event e;
    // A new round (or a restored one) cuts in on anything still showing;
    // everything else waits its turn.
    while (match::peekEvent(e) && (!busy() || e.type == match::EV_START)) {
        match::popEvent(e);
        switch (e.type) {
            case match::EV_START: onStart(e.a != 0); break;
            case match::EV_DEAL:  dealing = true; dealT = 0; break;
            case match::EV_TURN:  onTurn(e.a, e.b != 0, e.c != 0); break;
            case match::EV_PLAY:  onPlay(e); break;
            case match::EV_DRAW:  onDraw(e); break;
            case match::EV_PASS:  onPass(e.a); break;
            case match::EV_ROUND: onRound(e); break;
        }
    }

    tick++;
    if (holdT) holdT--;
    if (denyT) denyT--;
    if (sayT && sayT < 255) sayT--;
    if (hintT < 255) hintT++;
    if (floatT && ++floatT > 50) floatT = 0;
    zoomTo = overview ? 5 : 10;
    if (zoom != zoomTo && zoomDrawn) {
        zoom = (uint8_t)(zoom < zoomTo ? zoom + 1 : zoom - 1);
        zoomDrawn = false;
    }
    pal::setMode(selTile != NONE ? pal::TARGETS : pal::CASINO);

    // The deal: a tile to each hand in turn, clicking down.
    if (dealing && ++dealT >= (fast ? 2 : 4)) {
        dealT = 0;
        uint32_t left[2] = {match::round.hand[0] & ~handShown[0], match::round.hand[1] & ~handShown[1]};
        uint8_t s = dom::count(handShown[0]) <= dom::count(handShown[1]) ? 0 : 1;
        if (!left[s]) s ^= 1;
        if (left[s]) {
            handShown[s] |= left[s] & (0u - left[s]);
            audio::blip((uint16_t)(1800 + 60 * dom::count(handShown[s])), 10);
        } else {
            dealing = false;
            holdT = fast ? 6 : 16;
        }
    }

    // The scores count up.
    for (uint8_t s = 0; s < 2; s++) {
        int d = goalScore[s] - shownScore[s];
        if (d <= 0 || (tick & 1)) continue;
        shownScore[s] = (int16_t)(shownScore[s] + (d > 20 ? 5 : 1));
        audio::blip((uint16_t)(2000 + 12 * (shownScore[s] & 63)), 8);
    }

    if (mv.on && ++mv.t >= mv.T) land();
    for (auto &f : fly) {
        if (!f.on) continue;
        f.t++;
        f.x = (int16_t)(f.x + f.vx); f.y = (int16_t)(f.y + f.vy);
        if (f.vy < 100) f.vy = (int8_t)(f.vy + 3);
        if (f.t > 44 || f.x < -20 * 16 || f.x > 148 * 16 || f.y > 140 * 16) f.on = 0;
    }

    if (endPhase) {
        endT++;
        switch (endPhase) {
            case E_CALL:
                if (endT < (fast ? 40 : 70)) break;
                if (endWinner < 2 && shownN) lightFuse();
                else { endPhase = E_TALLY; endT = 0; }
                break;
            case E_FUSE:
                if (fuseT < 255) fuseT++;
                for (uint8_t i = 0; i < shownN; i++) {
                    if ((gone >> i) & 1) continue;
                    if (popAt[i] <= fuseT) pop(i);
                    else if (cheer && popAt[i] - fuseT < 6 && (tick & 1)) {
                        // The fuse has reached it: it spits.
                        int x, y;
                        centreOf(layout::at[i], x, y);
                        fx::spawn(fx::SPARK, sx(x), sy(y), fx::rndRange(-20, 21), fx::rndRange(-30, -6), 10, tick & 2 ? GOLD : WHITE);
                    }
                }
                if (popped >= shownN && !flying()) { endPhase = E_TALLY; endT = 0; }
                break;
            case E_TALLY:
                if (endT == 8) tally();
                if (endT > (fast ? 30 : 60) && shownScore[0] == goalScore[0] && shownScore[1] == goalScore[1]) lastWord();
                break;
            case E_HOLD:
                if (!waitPress) { overDone = true; endPhase = E_NONE; }
                else if (!(endT % 22) && cheer) {
                    // Fireworks over the table until the button.
                    int x = fx::rndRange(14, 114), y = fx::rndRange(26, 80);
                    fx::burst(fx::SPARK, x, y, 14, 46, fx::RAIN[(endT / 22) % 5]);
                    fx::burst(fx::STAR, x, y, 5, 26, WHITE);
                    audio::blip((uint16_t)fx::rndRange(500, 900), 30);
                    audio::led(audio::LED_BLINK);
                }
                break;
        }
    }

    // The glove: on the tile of your hand, on the end you are choosing, on
    // the boneyard when there is nothing to play.
    int x = 64, y = 96;
    bool show = false, below = false;
    if (!over && humanTurn && !mv.on && !dealing && !waitPress && !hidden) {
        if (selTile != NONE) {
            layout::Placed p;
            layout::plan(selTile, selArms[selAt], p);
            centreOf(p, x, y);
            x = sx(x); y = sy(y);
            below = y < 54;
            show = true;
        } else if (match::humanToDraw()) {
            x = BONE_X + 2; y = OPP_Y + 8;
            show = below = true;
        } else if (cur != NONE && ((handShown[view] >> cur) & 1)) {
            slotXY(view, handShown[view], cur, x, y);
            x += 6; y -= 4;
            show = true;
        }
    }
    if (show && (!glove || below != gloveBelow)) { gx16 = x << 4; gy16 = y << 4; }     // (it does not fly in)
    glove = show;
    gloveBelow = below;
    gx16 += ((x << 4) - gx16) >> 1;
    gy16 += ((y << 4) - gy16) >> 1;

    // The camera goes to the tile on its way down, the end being chosen,
    // or the first end the tile under the glove fits; else it stays on the
    // last thing that happened.
    if (mv.on && mv.kind == M_PLAY) { centreOf(mv.to, x, y); focusOn(x, y); }
    else if (show && shownN && !match::humanToDraw()) {
        uint8_t arm = 0xFF, fits = selTile != NONE ? 0 : dom::armsFor(match::round, cur);
        if (selTile != NONE) arm = selArms[selAt];
        else for (uint8_t a = dom::ARMS; a-- > 0;) if ((fits >> a) & 1) arm = a;
        uint8_t dir, v;
        if (arm != 0xFF) {
            layout::endOf(arm, x, y, dir, v);
            focusOn(X0 + x * UNIT, Y0 + y * UNIT);
        }
    }
    if (zoom == 5) setCamera(CX, CY);
    else {
        // (Gliding: a quarter of the way each tick, the last pixels one at a time.)
        int dx = focusX - camX, dy = focusY - camY;
        setCamera(camX + (dx / 4 ? dx / 4 : (dx > 0) - (dx < 0)), camY + (dy / 4 ? dy / 4 : (dy > 0) - (dy < 0)));
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void drawLine(uint32_t frame, bool plain) {
    // Back to front: the edge a tile shows below its face goes under the
    // face of the tile in front of it.
    uint8_t order[dom::TILES], n = 0;
    for (uint8_t i = 0; i < shownN; i++) {
        if (!onScreen(layout::at[i])) continue;
        uint8_t k = n++;
        while (k && layout::at[order[k - 1]].y > layout::at[i].y) { order[k] = order[k - 1]; k--; }
        order[k] = i;
    }
    for (uint8_t k = 0; k < n; k++) {
        uint8_t i = order[k];
        if ((gone >> i) & 1) drawScorch(layout::at[i]);
        else drawShadow(layout::at[i]);
    }
    for (uint8_t k = 0; k < n; k++) {
        uint8_t i = order[k];
        if ((gone >> i) & 1) continue;
        // About to go off: lit.
        bool lit = endPhase == E_FUSE && cheer && popAt[i] - fuseT < 5;
        drawPlaced(layout::at[i], lit ? FX_B : tileFace(), INK);
    }
    if (plain) return;
    // The pips open at each end, in figures; lit where the tile under the glove fits.
    if (shownN && !over && zoom == zoomTo && selTile == NONE && !(mv.on && mv.kind == M_PLAY)) {
        bool pick = humanTurn && !hidden && cur != NONE && match::humanToPlay();
        uint8_t fits = pick ? dom::armsFor(match::round, cur) : 0;
        int x, y;
        uint8_t dir, v;
        bool sides = match::round.spinner && layout::endOf(dom::E, x, y, dir, v) && layout::endOf(dom::W, x, y, dir, v);
        for (uint8_t a = 0; a < (sides ? dom::ARMS : dom::N); a++) drawTag(a, ((fits >> a) & 1) && ((frame >> 3) & 1));
    }
    // Where the tile in hand may go; where the hint would put it.
    layout::Placed p;
    for (uint8_t k = 0; k < nSel && selTile != NONE; k++) {
        layout::plan(selTile, selArms[k], p);
        drawGhost(p, k == selAt ? FX_A : FX_B, k == selAt && ((frame >> 3) & 1));
    }
    if (hintTile != NONE && selTile == NONE && hintT < 240 && ((frame >> 4) & 1)) {
        layout::plan(hintTile, hintArm, p);
        drawGhost(p, GOLD, false);
    }
}

// The tiles of a hand, small and face up, along the far rack (the round is over).
static void drawRevealed(uint32_t mask) {
    int n = dom::count(mask), pitch = n <= 11 ? 11 : 114 / (n - 1), x = 2;
    for (uint8_t t = 0; t < dom::TILES; t++) {
        if (!((mask >> t) & 1)) continue;
        drawTile(x, 11, dom::lo(t), dom::hi(t), true, 2, 5, tileFace(), INK, 2);
        x += pitch;
    }
}

static void drawHud(uint32_t) {
    gfx_fillRect(0, 0, 128, 9, INK);
    gfx_hline(0, 9, 128, GOLD);
    char buf[16], *p;
    for (uint8_t s = 0; s < 2; s++) {
        // Each side's score on a plaque, the edge of the one to play pulsing.
        p = fmtStr(buf, nameOf(s));
        *p++ = ' ';
        fmtInt(p, shownScore[s]);
        bool turn = !over && turnSide == s && !dealing;
        int w = text35Width(buf), x = s ? 124 - w : 4;
        fillRound(x - 3, 0, w + 6, 9, 2, NAVY);
        roundRect(x - 3, 0, w + 6, 9, 2, turn ? FX_B : WOOD);
        text35(x, 2, buf, shownScore[s] != goalScore[s] ? GOLD : s ? SILVER : WHITE);
    }
    fmtInt(fmtStr(buf, "TO "), match::target());
    text35(64 - text35Width(buf) / 2, 2, buf, GOLD);

    // The far rack: the other hand's backs, what the ends add up to, the
    // boneyard. A wooden rail, its front edge catching the lamp.
    gfx_fillRect(0, 10, 128, 9, WOOD);
    gfx_hline(0, 18, 128, GOLD);
    uint8_t opp = view ^ 1;
    if (over && endPhase != E_CALL) {
        // The round is over: what the other hand held, face up on a deeper rack.
        gfx_fillRect(0, 19, 128, 14, WOOD);
        gfx_hline(0, 33, 128, GOLD);
        gfx_hline(0, 34, 128, INK);
        drawRevealed(handShown[opp]);
        return;
    }
    for (uint8_t t = 0; t < dom::TILES; t++) {
        if (!((handShown[opp] >> t) & 1)) continue;
        int x, y;
        slotXY(opp, handShown[opp], t, x, y);
        drawBack(x, y, 4, 7);
    }
    if (match::round.game == dom::FIVES && shownN && !over) {
        // The count: as it would stand with the tile in hand on the end chosen.
        uint8_t sum = endsShown;
        bool live = selTile != NONE;
        if (live) {
            dom::Round c = match::round;
            dom::place(c, c.turn, selTile, selArms[selAt]);
            sum = dom::endSum(c);
        }
        fmtInt(fmtStr(buf, "ENDS "), sum);
        bool five = sum && sum % 5 == 0;
        text35(70, 12, buf, live ? (five ? FX_B : SILVER) : five ? GOLD : WHITE);
    }
    if (!over) {
        drawBack(BONE_X, OPP_Y, 4, 7);
        fmtInt(buf, dom::count(match::round.bone));
        text35(BONE_X + 6, 12, buf, WHITE);
    }
}

// Your rack: a wooden tray along the near edge of the table, the tiles
// standing in its channel behind a front lip.
static void drawRack(uint32_t frame) {
    gfx_hline(0, BOT, 128, GOLD);                       // the back rail, lit along its top
    gfx_fillRect(0, BOT + 1, 128, 3, WOOD);
    gfx_hline(0, BOT + 4, 128, INK);
    gfx_fillRect(0, BOT + 5, 128, 124 - BOT - 5, WOOD);  // the channel, in shade at the back
    gfx_fillRect(0, BOT + 5, 128, 2, WINE);
    dither(0, BOT + 7, 128, 3, WINE, 0);
    uint32_t hand = handShown[view];
    bool pick = humanTurn && !over && !hidden && match::humanToPlay() && !mv.on;
    for (uint8_t pass = 0; pass < 2; pass++)
        for (uint8_t t = 0; t < dom::TILES; t++) {
            if (!((hand >> t) & 1)) continue;
            // The tile under the glove is drawn last, over its neighbours.
            bool up = pick && (t == cur || t == selTile);
            if (up != (pass == 1)) continue;
            int x, y;
            slotXY(view, hand, t, x, y);
            if (hidden) { drawBack(x, y, 13, 25); continue; }
            uint8_t face = tileFace(), edge = INK;
            if (pick && !dom::armsFor(match::round, t)) face = tileDim();   // fits nowhere
            if (t == hintTile && hintT < 240 && ((frame >> 4) & 1)) face = GOLD;
            if (up) { y -= t == selTile ? 5 : 3; edge = t == selTile ? FX_A : FX_B; }
            gfx_vline(x + 13, y + 3, RACK_Y + 22 - y, INK);       // its shadow on the channel
            drawTile(x, y, dom::lo(t), dom::hi(t), true, 3, 5, face, edge);
        }
    gfx_hline(0, 124, 128, GOLD);                       // the front lip
    gfx_fillRect(0, 125, 128, 2, WOOD);
    gfx_hline(0, 127, 128, INK);
}

// The shadows on the felt of the tiles in the air: under everything but the line.
static void drawAirShadows() {
    gfx_setClip(0, TOP, 128, BOT - TOP);
    int x, y, scale, lift;
    uint8_t angle;
    if (mv.on && mv.kind == M_PLAY) {
        moverPos(x, y, angle, scale, lift);
        int big = (scale * 25) >> 8, small = (scale * 13) >> 8;
        bool across = angle > 32;
        drawLift(x, y + lift, across ? big : small, across ? small : big, lift + (256 - fx::ease(fx::IN_OUT, mv.t, mv.T)) / 16);
    }
    for (auto &f : fly) {
        if (!f.on) continue;
        int h = f.gy - (f.y >> 4);
        if (h > 0) drawLift(f.x >> 4, f.gy, 12, 8, h);
    }
    gfx_resetClip();
}

static void drawAir() {
    if (mv.on) {
        int x, y, scale, lift;
        uint8_t angle;
        if (mv.kind == M_DRAW) {
            // A back sliding along from the boneyard.
            int e = fx::ease(fx::OUT_CUBIC, mv.t, mv.T);
            x = BONE_X + (((mv.x0 - BONE_X) * e) >> 8);
            y = OPP_Y + (((mv.y0 - OPP_Y) * e) >> 8);
            drawBack(x, y, 4, 7);
        } else {
            moverPos(x, y, angle, scale, lift);
            spinTile(mv.to.first(), mv.to.second(), mv.to.upright(), x, y, angle, scale, RM_ID);
        }
    }
    for (auto &f : fly) {
        if (!f.on) continue;
        // (Towards the eye: up to a third bigger.)
        int grow = 256 + f.t * 6;
        spinTile((f.v >> 3) & 7, f.v & 7, f.v >> 7, f.x >> 4, f.y >> 4, (uint8_t)(f.t * f.spin),
                 (zscale() / 2 * (grow > 340 ? 340 : grow)) >> 8, f.t < 4 ? RM_HIT : RM_ID);
    }
}

static void drawGlove(uint32_t frame) {
    if (!glove) return;
    int x = (int)(gx16 >> 4), y = (int)(gy16 >> 4);
    int bob = (fx::isin((int)(frame >> 3) * 40) * 2) >> 8;
    const uint8_t *rm = denyT & 4 ? RM_ALERT : RM_ID;
    if (gloveBelow) sprite4(HAND, x - HAND_TIP, y + 1 - bob, rm, 256, SPR_FLIP_V);    // turned over
    else sprite4(HAND, x - HAND_TIP, y - HAND[1] + bob - 1, rm, 256);
}

// A line of the 3x5 font on a rounded plate.
static void plate(const char *s, uint8_t c, int y) {
    int tw = text35Width(s), pw = tw + 9;
    fillRound(64 - pw / 2, y, pw, 10, 2, NAVY);
    roundRect(64 - pw / 2, y, pw, 10, 2, GOLD);
    text35(64 - tw / 2, y + 3, s, c);
}

static void drawWords(uint32_t frame) {
    const int py = BOT - 12;
    if (sayT == 255) { if (frame & 32) plate(sayBuf, sayCol, py); }
    else if (sayT) { if (sayT > 6 || (sayT & 2)) plate(sayBuf, sayCol, py); }
    else if (waitPress && holdT < 20) { if (frame & 32) plate("PRESS A", WHITE, py); }
    else if (humanTurn && !over && !busy() && match::humanToDraw()) { if (frame & 32) plate("NO PLAY: A DRAWS", WHITE, py); }
    if (floatT) {
        int y = floatY - floatT / 3;
        text35x2(floatX + 1, y + 1, floatBuf, INK);
        text35x2(floatX, y, floatBuf, floatT > 40 && (floatT & 2) ? WHITE : floatCol);
    }
}

// A still scene is not redrawn: the frame is flushed again, so palette
// effects keep moving at 60 Hz, and the bob and the blinks step at 7.5 Hz,
// so an idle table costs an eighth of the frames.
static uint32_t lastSig;

static uint32_t signature(uint32_t frame, uint32_t ui) {
    int lo, hi;
    if (fx::activeRows(lo, hi) || mv.on || flying() || dealing || endPhase == E_FUSE || floatT || zoom != zoomTo)
        return frame;
    uint32_t h = 2166136261u;
    uint32_t v[] = {
        cur, selTile, selAt, hintTile, (uint32_t)(hintT < 240), humanTurn, glove, turnSide, (uint32_t)(gx16 >> 4),
        (uint32_t)(gy16 >> 4), frame >> 3, over, ui, waitPress, denyT, holdT != 0, hidden, view, shownN, gone,
        handShown[0], handShown[1], (uint32_t)shownScore[0], (uint32_t)shownScore[1], sayT, endPhase,
        (uint32_t)match::humanToPlay() | match::humanToDraw() << 1, match::round.bone, zoom, (uint32_t)camX,
        (uint32_t)camY,
    };
    for (uint32_t x : v) h = (h ^ x) * 16777619u;
    return h;
}

void invalidate() { lastSig = 0; }

bool render(uint32_t frame, uint32_t ui) {
    zoomDrawn = true;                            // the zoom may take its next step
    uint32_t sig = signature(frame, ui);
#ifndef CHDM_NOSKIP
    if (sig == lastSig) return false;
#endif
    lastSig = sig;
    drawFelt();
    drawLine(frame, false);
    drawAirShadows();
    drawHud(frame);
    drawRack(frame);
    drawAir();
    drawGlove(frame);
    drawWords(frame);
    fx::drawParticles((uint8_t)((2 * zoom + 2) / 5));
    fx::drawBanner();
    fx::applyShake(10, GFX_H - 1);       // all under the scoreboard: the racks go with the felt
    return true;
}

#if CHGAME_DEBUG
// Render profile (debug Y command): per section, averaged over 64 draws of
// the current scene - microseconds on the board; in the simulator, host
// nanoseconds (scale by chdrive's `cal` ratio / 1000 for a device estimate).
#ifdef CHSIM
static uint32_t clockNow() { return (uint32_t)::sim_hostNanos(); }
#else
static uint32_t clockNow() { return micros(); }
#endif
static void profFelt(uint32_t) { drawFelt(); }
static void profLine(uint32_t f) { drawLine(f, false); }
static void profHud(uint32_t f) { drawHud(f); }
static void profRack(uint32_t f) { drawRack(f); }
static void profRest(uint32_t f) { drawAirShadows(); drawAir(); drawGlove(f); drawWords(f); }
void profile(uint32_t *us) {
    static void (*const PART[5])(uint32_t) = {profFelt, profLine, profHud, profRack, profRest};
    for (int k = 0; k < 5; k++) {
        uint32_t t = clockNow();
        for (int i = 0; i < 64; i++) PART[k](0);
        us[k] = (clockNow() - t) / 64;
    }
    invalidate();
}
#endif

}  // namespace stage
