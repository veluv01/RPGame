#pragma GCC optimize("Os")   // cold code: size over speed (the tile loop lives in Tile.cpp)
// The play screen's presentation (Stage.h): the view and its zoom, the
// glove, pairs in flight, the sparrow, the HUD; render() skips still frames.
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "Stage.h"
#include "Tile.h"
#include "Fx.h"
#include "Sounds.h"
#include "MahjongBoard.h"
#include "Nav.h"
#include "src/assets/Assets.h"

namespace stage {

using board::NONE;

// The pile's corner on screen: 15 tiles of 8 across, 8 of 12 down, between
// the HUD and the plate. Even, so every tile is drawn at an even x.
static const int OX = 4, OY = 14;
static const int PLATE_Y = 116;
static const int PILE_Y0 = 10, PILE_Y1 = 116;        // the rows the pile is drawn in

// How a tile looks: face, emboss, edge, then its body - the side (the tile's
// thickness) and the backing. Every tile is white: the glove only stops on
// the ones you can take, so nothing needs greying out.
#ifndef CHMJ_BODY_SIDE
#define CHMJ_BODY_SIDE SKIN       // ivory; GOLD is a gilded look
#endif
static const uint8_t BODY_SIDE = CHMJ_BODY_SIDE, BODY_BACK = WOOD;
static const tile::Style FREE    = {WHITE, SKIN, WOOD, BODY_SIDE, BODY_BACK};
static const tile::Style GLINT   = {FX_B, GOLD, WOOD, BODY_SIDE, BODY_BACK};
static const tile::Style WHITE_OUT   = {WHITE, WHITE, WHITE, WHITE, WHITE};
static const tile::Style BACKS   = {FELT_LT, FELT_LT, WOOD, BODY_SIDE, BODY_BACK};

// The view. Close up, tiles are drawn up to twice the size (vw, the tile's
// width on screen, 8..16 px) around a focus point that follows the glove;
// a zoom steps vw a frame at a time. Pile coordinates are board::px/py.
static int vw = tile::W, wantW = tile::W;
static int32_t fx16, fy16;           // the focus (Q4, pile coordinates)
static int vFx, vFy, vSx, vSy;       // this frame's: focus, and where it is on screen
static int pMinX, pMaxX, pMinY, pMaxY;   // the pile's extent

enum Phase : uint8_t { PLAY, SHUFFLING, DROPPING, RESHUFFLE };
static uint8_t phase;
static uint16_t phaseT;
static bool quick, easy;            // easy: the faces with numbers

static uint8_t cur = NONE, sel = NONE;
static uint32_t gen;                 // counts changes to the pile (for the redraw check)

// The glove: glides to the tile under the cursor (Q4), bobs, taps on A. Left
// alone for a moment it draws back to the foot of the table, off the pile,
// so it hides no tile while you look for a pair; the cursor's tile stays
// raised and outlined.
static int32_t gx16, gy16;
static bool gloveSet;
static uint8_t tapT, idleT;
static const uint8_t IDLE_FRAMES = 50;
static const int REST_X = 117, REST_Y = 129;
static uint8_t leftShown;            // the HUD's count (it does not flicker while a deal is worked out)

// A pair taken: the two tiles fly together, flash, and are gone.
struct Mover { int16_t x0, y0, x1, y1; uint8_t tile; };
static Mover mv[2];
static uint8_t mvT, mvN;             // mvN: the flight's frames, 0 = none in flight
static const uint8_t HIT = 4;        // frames they flash white for, together
static int16_t pairX, pairY;         // where they meet (pile coordinates)

typedef uint8_t Set[(board::MAX_TILES + 7) / 8];
static Set glint;                    // tiles that shimmer: just freed, put back or shuffled
static uint8_t glintT;
static Set wasFree;
static uint8_t hintA = NONE, hintB = NONE, hintT;
static int32_t shown;                // the chips on the HUD, rolling towards board::chips
static uint8_t winT, stuckT;         // the cleared and the no-moves sequences
// A cleared table: a sparrow (the 1 of bamboo's bird) visits it. It flies
// in and lands, idles, hops and pecks, turns and looks about, walks, eats,
// flicks its tail and flies off; a button shoos it away sooner. Each act is
// one of its animations (tools/art/bird.txt) played through a sequence of
// its frames, a few ticks a frame, moving it along as it goes.
struct Act { uint8_t anim, ticks, reps; int8_t dx; const uint8_t *seq; uint8_t len; };
static const uint8_t SQ_LAND[] = {5, 4, 3, 2, 1, 0}, SQ_IDLE[] = {0, 1, 2, 3}, SQ_HOP[] = {0, 1, 2, 3},
                     SQ_PECK[] = {0, 1}, SQ_LOOK[] = {0, 1, 2, 3, 4}, SQ_WALK[] = {0, 1, 0, 2},
                     SQ_EAT[] = {0, 1, 2, 3, 4, 4, 4, 4, 5}, SQ_FLICK[] = {0, 1, 2, 3, 4, 5, 4, 6},
                     SQ_TAKEOFF[] = {0, 1, 2, 3, 4, 5}, SQ_FLY[] = {0, 1};
#define ACT(a, t, r, dx, sq) { a, t, r, dx, sq, (uint8_t)sizeof(sq) }
enum : uint8_t { A_IN, A_LAND, A_IDLE, A_HOP, A_PECK, A_LOOK, A_WALK, A_EAT, A_FLICK, A_TAKEOFF, A_OUT, A_DONE };
static const Act ACTS[A_DONE] = {
    ACT(B_FLY, 4, 0, 0, SQ_FLY),             // in: flown along a curve (updateBird)
    ACT(B_TAKEOFF, 4, 1, 0, SQ_LAND),        // landing: the take-off backwards
    ACT(B_IDLE, 7, 2, 0, SQ_IDLE),
    ACT(B_HOP, 5, 2, 10, SQ_HOP),            // dx: Q4 a tick, the way it faces
    ACT(B_PECK, 5, 4, 0, SQ_PECK),
    ACT(B_IDLE3, 7, 1, 0, SQ_LOOK),          // turned round first
    ACT(B_WALK, 6, 2, 6, SQ_WALK),
    ACT(B_EAT, 5, 1, 0, SQ_EAT),
    ACT(B_IDLE2, 6, 1, 0, SQ_FLICK),
    ACT(B_TAKEOFF, 4, 1, 0, SQ_TAKEOFF),
    ACT(B_FLY, 4, 0, 0, SQ_FLY),             // out: until off the screen
};
static const int GROUND = 104;               // where its feet are
static uint8_t birdAct = A_DONE, birdStep;   // the act, and how many frames into it
static uint8_t birdTick;
static bool birdRight;                       // facing right (drawn mirrored)
static int16_t birdX16, birdY16;             // its beak's side, and its feet (Q4)
static int16_t birdFromX, birdFromY;
static bool birdOn() { return birdAct != A_DONE; }
static char ann[14];                 // a call-out on the plate
static uint8_t annT;
static const uint8_t ANN_FRAMES = 60;
static uint8_t streakWas;            // the streak before the pair in flight

static const uint8_t DROP_FRAMES = 60, FALL = 6;     // the deal: tiles land over a second
// The tile lying squarely on each tile (NONE: none). Such a tile, with its
// side, hides the face of the one under it completely, so only that one's
// side is drawn: 56 of the turtle's 144, to begin with.
static uint8_t over[board::MAX_TILES];

static inline bool has(const uint8_t *s, uint8_t i) { return (s[i >> 3] >> (i & 7)) & 1; }
static inline void mark(uint8_t *s, uint8_t i) { s[i >> 3] |= (uint8_t)(1u << (i & 7)); }

// Pile coordinates -> screen, for this frame's view. At 1x simply OX + px.
static inline int VX(int px) { return vSx + (((px - vFx) * vw) >> 3); }
static inline int VY(int py) { return vSy + (((py - vFy) * vw) >> 3); }
static inline int sx(uint8_t i) { return VX(board::px(i)); }
static inline int sy(uint8_t i) { return VY(board::py(i)); }
static inline int vh() { return vw * 3 / 2; }        // a tile's height on screen
static inline int vt() { return vw >= 16 ? 2 : 1; }   // its body bands' thickness
static inline int zs(int n) { return (n * vw) >> 3; } // a pile distance on screen

static void viewSetup() {
    vFx = (int)(fx16 >> 4);
    vFy = (int)(fy16 >> 4);
    // At 1x the focus stays where it is; at 2x it is mid-screen.
    vSx = OX + vFx + (((64 - OX - vFx) * (vw - tile::W)) >> 3);
    vSy = OY + vFy + (((63 - OY - vFy) * (vw - tile::W)) >> 3);
}

void setQuick(bool on) { quick = on; }
void setFaces(bool numbers) { easy = numbers; gen++; }

// Face f in the chosen set; the classic one has its own art for close up.
static tile::Face faceOf(uint8_t f) {
#if !CHMJ_LEAN
    if (easy) return tile::Face{TILE_CELL_EASY[f], TILE_INK_EASY[f], nullptr, 0};
#endif
    return tile::Face{TILE_CELL_CLASSIC[f], TILE_INK_CLASSIC[f], TILE_CELL_BIG[f], TILE_INK_BIG[f]};
}
uint8_t cursor() { return cur; }
uint8_t selected() { return sel; }
void invalidate();

static bool gloveOn() { return phase == PLAY && cur != NONE && !mvN && !winT; }

// How far a tile is raised: picked up, or under the glove.
static int liftOf(uint8_t i) { return zs(i == sel ? 3 : 1); }

// Where the fingertip belongs (screen): on top of the cursor's tile, or at rest.
static void gloveTarget(int32_t &tx, int32_t &ty) {
    if (idleT >= IDLE_FRAMES) { tx = REST_X << 4; ty = REST_Y << 4; return; }
    tx = (int32_t)(sx(cur) + vw / 2) << 4;
    ty = (int32_t)(sy(cur) - liftOf(cur)) << 4;
}

// Where the camera looks: the glove's tile (the pile's middle without one),
// kept far enough in that a close-up shows no bare felt beyond the pile.
static void focusTarget(int32_t &tx, int32_t &ty) {
    int x = cur != NONE ? board::px(cur) + 4 : (pMinX + pMaxX) / 2;
    int y = cur != NONE ? board::py(cur) + 6 : (pMinY + pMaxY) / 2;
    if (pMaxX - pMinX <= 64) x = (pMinX + pMaxX) / 2;
    else if (x < pMinX + 32) x = pMinX + 32;
    else if (x > pMaxX - 32) x = pMaxX - 32;
    if (pMaxY - pMinY <= 52) y = (pMinY + pMaxY) / 2;
    else if (y < pMinY + 26) y = pMinY + 26;
    else if (y > pMaxY - 26) y = pMaxY - 26;
    tx = (int32_t)x << 4;
    ty = (int32_t)y << 4;
}

void setZoom(bool close) {
    int w = close ? 2 * tile::W : tile::W;
    if (w != wantW) audio::sfx(close ? Sfx::ZoomIn : Sfx::ZoomOut);
    wantW = w;
}
bool zoomed() { return vw != tile::W; }

bool busy() { return phase != PLAY || mvN || winT || stuckT || board::dealing(); }
bool clearedShown() { return winT > 150 && !birdOn(); }
bool stuckShown() { return stuckT > 70; }

static void announce(const char *text) {
    strncpy(ann, text, sizeof ann - 1);
    ann[sizeof ann - 1] = 0;
    annT = 1;
}

// The cursor belongs on a free tile: the nearest, if its own has gone.
static void fixCursor() {
    uint8_t list[board::MAX_FREE], n = board::freeList(list);
    if (!n) { cur = NONE; return; }
    if (cur != NONE && board::isFree(cur)) return;
    cur = cur == NONE ? list[n - 1] : nav::nearest(list, n, cur);
    idleT = 0;
}

static void findCovers() {
    pMinX = pMinY = 999;
    pMaxX = pMaxY = -999;
    for (uint8_t i = 0; i < board::count; i++) {
        const board::Pos &p = board::pos[i];
        if (board::px(i) < pMinX) pMinX = board::px(i);
        if (board::px(i) + tile::W + 2 > pMaxX) pMaxX = board::px(i) + tile::W + 2;
        if (board::py(i) < pMinY) pMinY = board::py(i);
        if (board::py(i) + tile::H + 2 > pMaxY) pMaxY = board::py(i) + tile::H + 2;
        over[i] = NONE;
        for (uint8_t j = (uint8_t)(i + 1); j < board::count; j++) {
            const board::Pos &q = board::pos[j];
            if (q.x2 == p.x2 && q.y2 == p.y2 && q.z == p.z + 1) { over[i] = j; break; }
        }
    }
}

static void reset() {
    phaseT = 0;
    cur = sel = NONE;
    gloveSet = false;
    tapT = idleT = mvN = mvT = glintT = hintT = winT = stuckT = annT = 0;
    birdAct = A_DONE;
    memset(glint, 0, sizeof glint);
    gen++;
    fx::clear();
    fx::setFloor(PLATE_Y - 4);
    invalidate();
}

// A new table: the camera starts on it, at the zoom it is set to.
static void settleView() {
    focusTarget(fx16, fy16);
    vw = wantW;
    viewSetup();
}

void begin() {}

void deal(uint8_t layout, uint32_t seed) {
    reset();
    board::layout = layout;
    board::dealBegin(seed);
    findCovers();
    settleView();
    shown = 0;
    phase = SHUFFLING;
    audio::sfx(Sfx::Shuffle);
}

void resume() {
    reset();
    findCovers();
    shown = board::chips;
    phase = PLAY;
    if (board::isFree(board::mark)) cur = board::mark;     // where the glove was
    fixCursor();
    settleView();
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
void hop(int ux, int uy) {
    uint8_t list[board::MAX_FREE], n = board::freeList(list);
    if (busy() || cur == NONE) return;
    idleT = 0;
    uint8_t t = nav::step(list, n, cur, ux, uy);
    if (t == NONE) { audio::sfx(Sfx::Deny); return; }
    cur = t;
    audio::sfx(Sfx::Cursor);
}

static void floatMoney(int32_t v, int x, int y, uint8_t colour) {
    char buf[10], *p = buf;
    if (v > 0) *p++ = '+';
    fmtMoney(p, v);
    if (x < 14) x = 14;
    if (x > 114) x = 114;
    fx::floatText(buf, x, y < 14 ? 14 : y, colour);
}

static void takePair(uint8_t a, uint8_t b) {
    // Where they are now (pile coordinates): the picked one floating, the
    // other under the glove.
    int ax = board::px(a), ay = board::py(a) - 3, bx = board::px(b), by = board::py(b) - 1;
    memset(wasFree, 0, sizeof wasFree);
    for (uint8_t i = 0; i < board::count; i++) if (board::isFree(i)) mark(wasFree, i);
    streakWas = board::streakT ? board::streak : 0;
    if (!board::match(a, b)) return;
    gen++;
    // They meet half way, side by side.
    int mx = ((ax + bx) / 2) & ~1, my = (ay + by) / 2 - 4;
    if (my < 0) my = 0;
    bool aLeft = ax <= bx;
    mv[0] = Mover{(int16_t)ax, (int16_t)ay, (int16_t)(mx + (aLeft ? -4 : 4)), (int16_t)my, a};
    mv[1] = Mover{(int16_t)bx, (int16_t)by, (int16_t)(mx + (aLeft ? 4 : -4)), (int16_t)my, b};
    pairX = (int16_t)(mx + 4);
    pairY = (int16_t)(my + 6);
    mvT = 0;
    mvN = quick ? 8 : 14;
    sel = NONE;
    hintT = 0;
    memset(glint, 0, sizeof glint);
    for (uint8_t i = 0; i < board::count; i++) if (board::isFree(i) && !has(wasFree, i)) mark(glint, i);
}

// The pair meets: the flash, the sparks, the chips (on screen, where it is).
static void land() {
    int hitX = VX(pairX), hitY = VY(pairY);
    fx::burst(fx::SPARK, hitX, hitY, 10, 36, GOLD);
    fx::burst(fx::STAR, hitX, hitY, 4, 24, WHITE);
    fx::fountain(fx::COIN, hitX, hitY, (uint8_t)(1 + board::streak));
    fx::shake(4, 1);
    floatMoney(board::lastPay(), hitX, hitY - 10, GOLD);
    audio::sfx((Sfx)((uint8_t)Sfx::Match1 + board::streak - 1));
    if (board::streak > 1 && board::streak > streakWas) {         // a step up: call it out
        char buf[14], *p = fmtStr(buf, "STREAK X");
        fmtInt(p, board::streak);
        announce(buf);
    }
    glintT = 30;
    gen++;
    fixCursor();
}

bool hinted(uint8_t &a, uint8_t &b) {
    if (!hintT || !board::present(hintA) || !board::present(hintB)) return false;
    a = hintA;
    b = hintB;
    return true;
}

bool take(uint8_t a, uint8_t b) {
    if (busy() || !board::canMatch(a, b)) return false;
    cur = b;
    takePair(a, b);
    return true;
}

void press() {
    if (busy() || cur == NONE) return;
    idleT = 0;
    tapT = 12;
    if (sel == cur) { sel = NONE; audio::sfx(Sfx::Drop); }
    else if (sel != NONE && board::canMatch(sel, cur)) takePair(sel, cur);
    else { sel = cur; audio::sfx(Sfx::Pick); }          // pick up (this one instead)
}

bool undo() {
    uint8_t a, b;
    int32_t had = board::chips;
    if (phase != PLAY || mvN || winT || !board::undo(a, b)) { audio::sfx(Sfx::Deny); return false; }
    gen++;
    sel = NONE;
    hintT = stuckT = idleT = 0;
    cur = board::isFree(a) ? a : b;
    fixCursor();
    memset(glint, 0, sizeof glint);
    mark(glint, a);
    mark(glint, b);
    glintT = 30;
    if (board::chips != had) floatMoney(board::chips - had, sx(a) + vw / 2, sy(a), RED);
    audio::sfx(Sfx::Undo);
    return true;
}

void back() {
    if (busy()) return;
    idleT = 0;
    if (sel != NONE) { sel = NONE; audio::sfx(Sfx::Drop); }
    else undo();
}

void hint() {
    uint8_t a = NONE, b = NONE;
    if (busy()) return;
    // A twin of the tile in hand, if it has one free; else any pair.
    if (sel != NONE) {
        uint8_t list[board::MAX_FREE], n = board::freeList(list);
        for (uint8_t i = 0; i < n && b == NONE; i++) if (board::canMatch(sel, list[i])) { a = sel; b = list[i]; }
    }
    if (b == NONE && !board::hint(a, b)) { audio::sfx(Sfx::Deny); return; }
    int32_t had = board::chips;
    board::spend(board::HINT_COST);
    hintA = a; hintB = b;
    hintT = 150;
    idleT = 0;
    cur = a == sel ? b : a;
    if (board::chips != had) floatMoney(board::chips - had, 64, 20, RED);
    audio::sfx(Sfx::Hint);
}

bool shuffle() {
    if (phase != PLAY || mvN || winT || !board::shuffleBegin()) { audio::sfx(Sfx::Deny); return false; }
    phase = RESHUFFLE;
    phaseT = 0;
    sel = NONE;
    hintT = stuckT = 0;
    audio::sfx(Sfx::Shuffle);
    return true;
}

static void nextAct() {
    birdAct++;
    birdStep = birdTick = 0;
    if (birdAct == A_LOOK) {
        // Turn round: the beak goes to the other end.
        birdRight = !birdRight;
        birdX16 = (int16_t)(birdX16 + ((BIRD[B_IDLE3].w - 1) << 4) * (birdRight ? 1 : -1));
    }
    if (birdAct == A_LAND) audio::sfx(Sfx::Chirp);
    if (birdAct == A_TAKEOFF) audio::sfx(Sfx::ZoomIn);
}

static void updateBird() {
    if (!birdOn()) return;
    const Act &a = ACTS[birdAct];
    if (++birdTick >= a.ticks) {
        birdTick = 0;
        birdStep++;
        if (birdAct == A_PECK && (birdStep & 1)) audio::sfx(Sfx::Cursor);    // tap, tap
    }
    int dir = birdRight ? 1 : -1;
    if (birdAct == A_IN) {
        // A swoop down from the corner to the middle of the table.
        static const int IN_T = 72;
        int t = birdStep * a.ticks + birdTick, e = fx::ease(fx::OUT_CUBIC, t, IN_T);
        birdX16 = (int16_t)(birdFromX + ((((70 << 4) - birdFromX) * e) >> 8));
        birdY16 = (int16_t)(birdFromY + ((((GROUND << 4) - birdFromY) * e) >> 8));
        if (t >= IN_T) nextAct();
        return;
    }
    if (birdAct == A_OUT) {
        birdX16 = (int16_t)(birdX16 + 34 * dir);
        birdY16 = (int16_t)(birdY16 - 20);
        if (birdY16 < (-30 << 4) || birdX16 < (-30 << 4) || birdX16 > (160 << 4)) birdAct = A_DONE;
        return;
    }
    birdX16 = (int16_t)(birdX16 + a.dx * dir);
    if (birdAct == A_TAKEOFF && birdStep >= 3) birdY16 = (int16_t)(birdY16 - 8);
    if (birdStep >= a.len * a.reps) nextAct();
}

// The sparrow's frame now, and where to draw it.
static const uint8_t *birdFrame(int &x, int &y) {
    const Act &a = ACTS[birdAct];
    const BirdAnim &an = BIRD[a.anim];
    const uint8_t *spr = an.frames[a.seq[birdStep % a.len]];
    int ax = birdX16 >> 4, ay = birdY16 >> 4;
    x = birdRight ? ax - an.w + 1 : ax;      // the beak at ax, whichever way it faces
    y = ay - an.h + 1;
    return spr;
}

static void drawBird() {
    if (!birdOn()) return;
    int x, y;
    const uint8_t *d = birdFrame(x, y);
    int w = d[0], h = d[1];
    // Its shadow on the felt, while it is down.
    if (birdAct != A_IN && birdAct != A_OUT) {
        int sw = BIRD[ACTS[birdAct].anim].w - 6;
        int cx = (birdX16 >> 4) + (birdRight ? -(sw / 2 + 3) : sw / 2 + 3);
        gfx_hline(cx - sw / 2, GROUND + 1, sw, FELT_DK);
        gfx_hline(cx - sw / 2 + 2, GROUND + 2, sw - 4, FELT_DK);
    }
    // span4, mirrored when it faces right.
    d += 2;
    for (int j = 0; j < h; j++) {
        uint8_t n = *d++;
        int px = 0;
        for (uint8_t i = 0; i < n; i++) {
            uint8_t b = *d++;
            int len = (b >> 4) + 1;
            if ((b & 15) != 15) gfx_hline(birdRight ? x + w - px - len : x + px, y + j, len, b & 15);
            px += len;
        }
    }
}

// Shoo: away it goes (a button, while it is here).
void shoo() {
    if (!birdOn() || birdAct == A_IN || birdAct >= A_TAKEOFF) return;
    birdAct = A_TAKEOFF;
    birdStep = birdTick = 0;
    audio::sfx(Sfx::ZoomIn);
}

void update(bool playing) {
    phaseT++;
    if (tapT) tapT--;
    if (idleT < 255) idleT++;
    if (!board::dealing()) leftShown = board::left;
    if (glintT) glintT--;
    if (hintT) hintT--;
    if (annT && ++annT > ANN_FRAMES) annT = 0;
    if (shown != board::chips) {
        int32_t d = board::chips - shown, s = d / 5;
        shown += s ? s : (d > 0 ? 1 : -1);
    }
    switch (phase) {
        case SHUFFLING:
            // The deal is worked out a few pairs a frame, under the rattle.
            if (board::dealStep(6) && phaseT >= (quick ? 2 : 30)) {
                phase = quick ? PLAY : DROPPING;
                phaseT = 0;
                gen++;
                fixCursor();
            }
            break;
        case DROPPING:
            if (phaseT % 5 == 1) audio::sfx(Sfx::Clack);
            if (phaseT > DROP_FRAMES + FALL) { phase = PLAY; gen++; }
            break;
        case RESHUFFLE:
            if (board::dealStep(6) && phaseT >= (quick ? 12 : 40)) {
                phase = PLAY;
                gen++;
                if (board::shuffleFailed()) {
                    announce("NO SHUFFLE");
                    audio::sfx(Sfx::Deny);
                } else {
                    memset(glint, 0xFF, sizeof glint);
                    glintT = 30;
                    int32_t d = board::chips - shown;
                    if (d) floatMoney(d, 64, 20, RED);
                    audio::sfx(Sfx::Select);
                }
                cur = NONE;
                fixCursor();
            }
            break;
        default:
            if (playing && !board::cleared() && !mvN) board::tick();
            if (mvN) {
                mvT++;
                if (mvT == mvN) land();
                if (mvT >= mvN + HIT) mvN = 0;
            } else if (board::cleared()) {
                if (!winT) {
                    // The table is cleared: the jackpot.
                    fx::banner("MAHJONG!", fx::B_RAINBOW, 54, 170);
                    fx::fountain(fx::CONFETTI, 36, 96, 16);
                    fx::fountain(fx::CONFETTI, 92, 96, 16);
                    fx::fountain(fx::COIN, 64, 80, 12);
                    floatMoney(board::bonus, 64, 80, GOLD);
                    audio::sfx(Sfx::Jackpot);
                    audio::led(audio::LED_PARTY);
                }
                if (winT < 255) winT++;
                if (winT == 60 || winT == 110) fx::fountain(fx::COIN, winT == 60 ? 30 : 98, 90, 10);
                if (winT == 30) {
                    // Here it comes, from the right.
                    birdAct = A_IN; birdStep = birdTick = 0; birdRight = false;
                    birdFromX = 132 << 4; birdFromY = 30 << 4;
                }
            } else if (board::stuck()) {
                if (!stuckT) {
                    fx::banner("NO MOVES", fx::B_RED, 58, 80);
                    fx::shake(8, 2);
                    audio::sfx(Sfx::Stuck);
                    sel = NONE;
                }
                if (stuckT < 255) stuckT++;
            } else stuckT = 0;
            break;
    }
    updateBird();
    // The camera: the zoom steps (a whip, as CHChess's), the focus eases
    // towards the glove's tile.
    if (vw != wantW) vw += vw < wantW ? 2 : -2;
    int32_t tx, ty;
    focusTarget(tx, ty);
    fx16 += (tx - fx16) >> 2;
    fy16 += (ty - fy16) >> 2;
    if (tx - fx16 > -4 && tx - fx16 < 4) fx16 = tx;
    if (ty - fy16 > -4 && ty - fy16 < 4) fy16 = ty;
    viewSetup();
    // The glove glides to its tile.
    if (cur != NONE) {
        gloveTarget(tx, ty);
        if (!gloveSet) { gx16 = tx; gy16 = ty; gloveSet = true; }
        int32_t dx = tx - gx16, dy = ty - gy16;
        gx16 += dx / 2;
        gy16 += dy / 2;
        if (dx > -2 && dx < 2) gx16 = tx;
        if (dy > -2 && dy < 2) gy16 = ty;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void drawTable() {
    gfx_fillRect(0, 10, 128, 118, FELT);
    if (vw != tile::W) { dither(0, 113, 128, 15, FELT_DK, 1); return; }     // close up: no table edges
    dither(0, 12, 128, 2, FELT_DK, 0);
    dither(0, 113, 128, 15, FELT_DK, 1);
    dither(0, 14, 2, 99, FELT_DK, 0);
    dither(126, 14, 2, 99, FELT_DK, 0);
}

static void drawTile(uint8_t i, int x, int y, const tile::Style &st) {
    tile::draw(faceOf(board::face[i]), x, y, st, vw);
}

static bool onScreen(int x, int y) {
    int t = 2 * vt();
    return x < GFX_W && x + vw + t > 0 && y < PILE_Y1 && y + vh() + t > PILE_Y0;
}

// A tile held up off the pile: it floats `lift` px above where it lay, with
// its shadow under it, and wears an outline.
static void drawLifted(uint8_t i, uint8_t outline) {
    int x = sx(i), y = sy(i), lift = liftOf(i), t = vt();
    gfx_fillRect(x + 2 * t, y + 2 * t, vw, vh(), BODY_BACK);
    drawTile(i, x, y - lift, FREE);
    gfx_rect(x - 1, y - lift - 1, vw + 2, vh() + 2, outline);
}

static void outlineTile(uint8_t i, uint8_t c) {
    gfx_rect(sx(i) - 1, sy(i) - 1, vw + 2, vh() + 2, c);
}

static void drawPile(uint32_t frame) {
    if (phase == SHUFFLING) return;                      // nothing on the table yet
    uint8_t n = board::count;
    bool glove = gloveOn();
    int t = vt();
    tile::setClip(PILE_Y0, PILE_Y1);
    // The bottom layer's shadow on the felt, down and right.
    if (phase == PLAY)
        for (uint8_t i = 0; i < n && board::pos[i].z == 0; i++)
            if (board::present(i)) gfx_fillRect(sx(i) + 3 * t, sy(i) + 3 * t, vw + t, vh() + t, FELT_DK);
    for (uint8_t i = 0; i < n; i++) {
        if (!board::present(i)) continue;
        int x = sx(i), y = sy(i);
        if (phase == RESHUFFLE) {
            // Face down and rattling about while they are dealt again.
            uint32_t h = (i * 2654435761u) ^ ((frame >> 2) * 40503u);
            x += (int)((h >> 8) & 2) - 2 + 2 * (int)((h >> 12) & 1);
            y += (int)((h >> 16) % 3) - 1;
            if (onScreen(x, y)) tile::draw(faceOf(TILE_BACK), x, y, BACKS, vw);
            continue;
        }
        if (phase == DROPPING) {
            int age = (int)phaseT - i * DROP_FRAMES / n;
            if (age < 0) continue;
            if (age < FALL) y -= zs((FALL - age) * (FALL - age));
        }
        if (!onScreen(x, y)) continue;
        if (i == sel || (i == cur && glove)) {
            if (phase == PLAY) continue;                     // drawn raised, afterwards
        }
        uint8_t top = over[i];
        if (phase == PLAY && top != NONE && board::present(top) && top != sel && !(top == cur && glove)) {
            static const tile::Face BODY = {nullptr, 0, nullptr, 0};
            tile::draw(BODY, x, y, FREE, vw);                // its face is hidden: just its body
            continue;
        }
        bool lit = glintT && has(glint, i);
        drawTile(i, x, y, lit ? GLINT : FREE);
    }
    if (phase == PLAY) {
        // The twins of the tile in hand, and a hint.
        if (sel != NONE) {
            uint8_t list[board::MAX_FREE], k = board::freeList(list);
            for (uint8_t j = 0; j < k; j++)
                if (list[j] != cur && board::canMatch(sel, list[j])) outlineTile(list[j], (frame >> 3) & 1 ? CYAN : WHITE);
        }
        if (hintT && board::present(hintA) && board::present(hintB)) {
            uint8_t c = (frame >> 2) & 1 ? GOLD : RED;
            if (hintA != cur && hintA != sel) outlineTile(hintA, c);
            if (hintB != cur && hintB != sel) outlineTile(hintB, c);
        }
        if (sel != NONE && sel != cur) drawLifted(sel, FX_A);
        if (glove) drawLifted(cur, cur == sel ? FX_A : FX_B);
    }
    tile::setClip(0, GFX_H);
}

static void drawMovers() {
    if (!mvN) return;
    tile::setClip(PILE_Y0 - 8, PILE_Y1);
    for (auto &m : mv) {
        int t = mvT < mvN ? mvT : mvN;
        int e = fx::ease(fx::IN_OUT, t, mvN);
        int x = VX(m.x0 + (((m.x1 - m.x0) * e) >> 8)), y = VY(m.y0 + (((m.y1 - m.y0) * e) >> 8));
        y -= zs((fx::isin(t * 128 / mvN) * 6) >> 8);         // a little hop
        if (vw == tile::W) x &= ~1;
        tile::Face f = faceOf(board::face[m.tile]);
        if (mvT >= mvN) { f.inks = f.bigInks = 0x11; tile::draw(f, x, y, WHITE_OUT, vw); }   // all white
        else tile::draw(f, x, y, FREE, vw);
    }
    tile::setClip(0, GFX_H);
}

static void drawGlove(uint32_t frame) {
    if (!gloveOn() || !gloveSet) return;
    int x = (int)(gx16 >> 4), y = (int)(gy16 >> 4);
    int bob = (fx::isin((int)(frame >> 3) * 40) * 2) >> 8;
    if (tapT) bob = (tapT < 6 ? tapT : 12 - tapT) / 2 + 1;
    // As big as the tiles, unless it is resting off the pile.
    int scale = idleT >= IDLE_FRAMES ? 256 : vw * 32;
    sprite4(HAND, x - ((HAND_TIP * scale) >> 8), y - ((HAND[1] * scale) >> 8) + zs(bob), RM_ID, scale);
}

// Words in their colours on a rounded plate centred at y: grow (Q8) is the
// plate's width so far, and word k shows from frame 6 + 3k, dropping in.
static void plate(const char *const *w, const uint8_t *c, uint8_t n, int y, int grow, int t) {
    int tw = 0;
    for (uint8_t i = 0; i < n; i++) tw += text35Width(w[i]) + (w[i][0] ? 1 : 0);
    if (tw) tw--;
    int pw = ((tw + 8) * grow) >> 8;
    if (pw < 6) return;
    fillRound(64 - pw / 2, y, pw, 11, 2, NAVY);
    roundRect(64 - pw / 2, y, pw, 11, 2, GOLD);
    int x = 64 - tw / 2;
    for (uint8_t i = 0; i < n; i++) {
        int d = t - 6 - 3 * i;
        if (d >= 0) text35s(x, y + 3 - (d < 3 ? 3 - d : 0), w[i], c[i]);
        x += text35Width(w[i]) + (w[i][0] ? 1 : 0);
    }
}

static const char *const SUIT[3] = {"DOTS ", "BAMBOO ", "CHARACTER "};
static const char *const HONOUR[7] = {"EAST WIND", "SOUTH WIND", "WEST WIND", "NORTH WIND",
                                      "RED DRAGON", "GREEN DRAGON", "WHITE DRAGON"};

static void drawHud(uint32_t frame) {
    gfx_fillRect(0, 0, 128, 9, INK);
    gfx_hline(0, 9, 128, GOLD);
    char buf[16], *p = fmtInt(buf, phase == SHUFFLING ? board::count : leftShown);
    fmtStr(p, " TILES");
    text35(3, 2, buf, WHITE);
    // The chips, and the streak they are being paid at.
    p = fmtMoney(buf, shown);
    bool hot = board::streak > 1 && board::streakT;
    char x[4] = {' ', 'X', (char)('0' + board::streak), 0};
    int w = text35Width(buf) + (hot ? 12 : 0), cx = 68 - w / 2;
    cx += text35(cx, 2, buf, GOLD);
    if (hot) text35(cx, 2, x, FX_B);
    uint16_t s = board::secs();
    if (s > 5999) s = 5999;
    p = fmtInt(buf, s / 60);
    *p++ = ':';
    *p++ = (char)('0' + s % 60 / 10);
    *p++ = (char)('0' + s % 10);
    *p = 0;
    text35(126 - text35Width(buf), 2, buf, WHITE);
    // How long the streak has left.
    if (board::streakT) gfx_fillRect(0, 10, (board::streakT * 64 / board::STREAK_FRAMES + 1) * 2, 2, CYAN);

    if (annT) {
        // A call-out: the plate springs open, then the words drop in.
        int t = annT, grow = t < 8 ? fx::ease(fx::OUT_BACK, t, 8) : t > ANN_FRAMES - 8 ? (ANN_FRAMES - t) * 32 : 256;
        const char *w[1] = {ann};
        static const uint8_t C[1] = {FX_B};
        plate(w, C, 1, PLATE_Y, grow, t);
    } else if (phase == SHUFFLING || phase == RESHUFFLE) {
        static const char *const DOTS[4] = {"SHUFFLING", "SHUFFLING.", "SHUFFLING..", "SHUFFLING..."};
        const char *w[1] = {DOTS[(frame >> 3) & 3]};
        static const uint8_t C[1] = {WHITE};
        plate(w, C, 1, phase == SHUFFLING ? 58 : PLATE_Y, 256, 99);
    } else if (gloveOn()) {
        // What the glove is on - and whether it pairs with the tile in hand.
        uint8_t f = board::face[cur];
        char num[2] = {(char)('1' + f % 9), 0};
        const char *w[3] = {"", "", ""};
        static const uint8_t C[3] = {WHITE, GOLD, FX_B};
        if (f < 27) { w[0] = SUIT[f / 9]; w[1] = num; }
        else if (f < board::FLOWER) w[0] = HONOUR[f - 27];
        else w[0] = f < board::SEASON ? "FLOWER" : "SEASON";
        if (sel != NONE && sel != cur && board::canMatch(sel, cur)) w[2] = " PAIR!";
        plate(w, C, 3, PLATE_Y, 256, 99);
    }
}

// A still scene is not redrawn: the frame is flushed again, so palette
// effects keep moving at 60 Hz; the glove's bob steps at 7.5 Hz.
static uint32_t lastSig;

static uint32_t signature(uint32_t frame, uint32_t ui) {
    int lo, hi;
    if (fx::activeRows(lo, hi) || mvN || phase != PLAY || tapT || annT || vw != wantW || birdOn()) return frame;
    int32_t ftx, fty;
    focusTarget(ftx, fty);
    if (ftx != fx16 || fty != fy16) return frame;
    if (cur != NONE) {
        int32_t tx, ty;
        gloveTarget(tx, ty);
        if (gx16 != tx || gy16 != ty) return frame;
    }
    uint32_t h = 2166136261u;
    uint32_t v[] = {
        cur, sel, gen, frame >> 3, hintT ? frame >> 2 : 0, (uint32_t)shown, board::secs(), board::streak, leftShown,
        (uint32_t)(board::streakT * 64 / board::STREAK_FRAMES), glintT != 0, winT != 0, stuckT != 0, ui, easy,
        (uint32_t)vw, (uint32_t)fx16, (uint32_t)fy16,
    };
    for (uint32_t x : v) h = (h ^ x) * 16777619u;
    return h;
}

void invalidate() { lastSig = 0; }

bool render(uint32_t frame, uint32_t ui) {
    uint32_t sig = signature(frame, ui);
    if (sig == lastSig) return false;
    lastSig = sig;
    drawTable();
    drawPile(frame);
    drawMovers();
    drawHud(frame);
    drawGlove(frame);
    fx::drawParticles();
    drawBird();
    fx::drawFloats();
    fx::drawBanner();
    fx::applyShake(12, 127);
    return true;
}

#if CHGAME_DEBUG
// Device render profile (debug Y command): microseconds per section.
void profile(uint32_t *us) {
    uint32_t t = micros();
    drawTable();          us[0] = micros() - t; t = micros();
    drawPile(8);          us[1] = micros() - t; t = micros();
    drawHud(8);
    drawGlove(8);         us[2] = micros() - t; t = micros();
    fx::drawParticles();
    fx::drawFloats();
    fx::drawBanner();     us[3] = micros() - t;
    invalidate();
}
#endif

}  // namespace stage
