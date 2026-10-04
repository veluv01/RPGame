// The table in motion: the glove and what it carries, every card in flight,
// the deal, undo, auto-play and the cascade, and drawing it all (see
// Stage.h).
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library)
#include <RPGame.h>
#include <string.h>
#include "Stage.h"
#include "Layout.h"
#include "CardArt.h"
#include "Fx.h"
#include "Sounds.h"
#include "src/assets/Assets.h"

namespace stage {

using namespace lay;
using art::SW;
using art::SH;

int32_t bank;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum : uint8_t { F_DOWN = 1, F_TURN = 2, F_PUFF = 4 };   // lands face down; turns over on the way; puffs as it lands
struct Fly { int16_t x0, y0; uint8_t card, pile, idx, T, flags; int8_t t; };
static Fly flies[28];                           // T = 0: free

static State st = PLAY;
static uint8_t curPile = TAB, depth = 1;        // where the glove points; how many cards of a column
static uint8_t heldSrc = NO_PILE, heldN;        // the run the glove carries
static int16_t gx16, gy16;                      // the glove's fingertip, Q4
static uint8_t tapT, denyT, sinceA = 255, autoT;
static uint8_t flipT[7];                        // a column's top card turning up
static Klondike before;                         // for undo
static bool hasUndo, redraw;

// The cascade.
static struct {
    int16_t x, y, vx, vy;                       // Q4
    uint8_t card, idx, active, nStamp, dirty;
    struct { int16_t x, y; uint8_t card; } stamp[3];
} cas;

// ---------------------------------------------------------------------------
// Where things go
// ---------------------------------------------------------------------------
// How far apart a column's cards sit: as dealt while they fit, squeezed when
// they don't, and over the status line before the ranks stop being readable.
static void pitch(const Klondike &k, uint8_t col, int &pd, int &pu) {
    int d = k.down[col], u = k.nTab[col] - d - 1;
    pd = DOWN_PITCH;
    pu = UP_PITCH;
    int room = STATUS_Y - SH - TAB_Y;
    if (u < 1 || d * pd + u * pu <= room) return;
    pd = 2;
    pu = (room - d * pd) / u;
    if (pu < 6) pu = (room + 128 - STATUS_Y - d * pd) / u;
    if (pu > UP_PITCH) pu = UP_PITCH;
}

static void cardPos(const Klondike &k, uint8_t p, int idx, int &x, int &y) {
    y = TOP_Y;
    if (p == STOCK) x = colX(0);
    else if (p == WASTE) {
        int j = idx - (k.nWaste - (k.fan ? k.fan : 1));
        x = colX(1) + (j < 0 ? 0 : j > 2 ? 2 : j) * FAN_DX;
    } else if (p < TAB) x = colX(3 + p - FOUND);
    else {
        uint8_t col = (uint8_t)(p - TAB);
        int pd, pu, d = k.down[col];
        pitch(k, col, pd, pu);
        x = colX(col);
        y = TAB_Y + (idx < d ? idx * pd : d * pd + (idx - d) * pu);
    }
}

static uint8_t inFlight(uint8_t p) {
    uint8_t n = 0;
    for (auto &f : flies) if (f.T && f.pile == p) n++;
    return n;
}

static bool busy() {
    for (auto &f : flies) if (f.T) return true;
    return false;
}

static void fly(int x0, int y0, uint8_t card, uint8_t pile, uint8_t idx, uint8_t T, uint8_t flags = 0, int delay = 0) {
    for (auto &f : flies) {
        if (f.T) continue;
        f.x0 = (int16_t)x0; f.y0 = (int16_t)y0;
        f.card = card; f.pile = pile; f.idx = idx; f.T = T; f.flags = flags; f.t = (int8_t)-delay;
        return;
    }
}

// The held run rides on the fingertip: the finger is under its last card.
static void heldPos(int i, int &x, int &y) {
    x = (gx16 >> 4) - 8;
    y = (gy16 >> 4) + 2 - SH - (heldN - 1 - i) * UP_PITCH;
}

// ---------------------------------------------------------------------------
// Setting up
// ---------------------------------------------------------------------------
static void clearMotion() {
    memset(flies, 0, sizeof flies);
    memset(flipT, 0, sizeof flipT);
    heldSrc = NO_PILE;
    hasUndo = false;
    tapT = denyT = 0;
    sinceA = 255;
    redraw = true;
    fx::clear();
}

void deal(const Klondike &k) {
    clearMotion();
    st = DEALING;
    curPile = TAB; depth = 1;
    gx16 = (int16_t)(64 << 4); gy16 = (int16_t)(150 << 4);
    // Row by row, as a dealer would: one to each column, then again from the second...
    int delay = 20;
    for (uint8_t row = 0; row < 7; row++)
        for (uint8_t col = row; col < 7; col++) {
            fly(colX(0), TOP_Y, k.tab[col][row], (uint8_t)(TAB + col), row, 9, row < col ? F_DOWN : F_TURN, delay);
            delay += 3;
        }
    audio::sfx(Sfx::Shuffle);
}

void resume() {
    clearMotion();
    st = PLAY;
}

void invalidate() { redraw = true; }
State state() { return st; }
uint8_t cursor() { return curPile; }
uint8_t holding() { return heldSrc == NO_PILE ? 0 : heldN; }
void point(uint8_t pile, uint8_t d) { if (pile < PILES) { curPile = pile; depth = d ? d : 1; } }

static void landed(const Klondike &k, const Fly &f) {
    if (f.flags & F_PUFF) {
        // CHChess's puff, lighter: a ring of grey and white spreading from
        // the middle of the card.
        int x, y;
        cardPos(k, f.pile, f.idx, x, y);
        for (int i = 0; i < 12; i++) {
            // From the card's edge outward, so it shows against the felt.
            int a = i * 256 / 12 + 10, c = fx::isin(a + 64), sn = fx::isin(a);
            fx::spawn(fx::DUST, x + 8 + ((c * 9) >> 8), y + 11 + ((sn * 12) >> 8), (c * 24) >> 8, (sn * 24) >> 8, 20,
                      i & 1 ? WHITE : SILVER);
        }
    }
    if (f.pile >= FOUND && f.pile < TAB) {
        int x, y;
        cardPos(k, f.pile, f.idx, x, y);
        fx::burst(fx::SPARK, x + 8, y + 11, 7, 30, rankOf(f.card) == RK ? FX_A : GOLD);
        if (k.scoring != NO_SCORE) fx::floatText(k.scoring == VEGAS ? "+$5" : "+10", x + 2, y + SH - 2, GOLD);
        if (!audio::playing()) audio::blip((uint16_t)(1800 + rankOf(f.card) * 150), 40);   // never over an effect
        if (rankOf(f.card) == RK) {
            static const char *const SUIT[4] = {"CLUBS!", "DIAMONDS!", "SPADES!", "HEARTS!"};
            fx::banner(SUIT[suitOf(f.card)], redCard(f.card) ? fx::B_RED : fx::B_CYAN, 70, 44);
            audio::sfx(Sfx::Suit);
        }
    }
}

// ---------------------------------------------------------------------------
// The player's moves
// ---------------------------------------------------------------------------
static void deny() { audio::sfx(Sfx::Deny); denyT = 1; }

static void remember(const Klondike &k) { before = k; hasUndo = true; }

static void afterMove(const Klondike &k) {
    if (k.won() || k.autoReady()) { st = AUTO; autoT = 0; hasUndo = false; }
}

// n cards from src to dst, flown from the glove (held) or from where they lie.
static void play(Klondike &k, uint8_t src, uint8_t n, uint8_t dst, bool held, uint8_t T) {
    int x[13], y[13];
    uint8_t from = (uint8_t)(k.count(src) - n);
    for (uint8_t i = 0; i < n; i++) {
        if (held) heldPos(i, x[i], y[i]);
        else cardPos(k, src, from + i, x[i], y[i]);
    }
    uint8_t to = k.dest(src, dst);
    if (k.move(src, n, dst)) {
        flipT[src - TAB] = 1;
        audio::sfx(Sfx::Flip);
    }
    uint8_t at = (uint8_t)(k.count(to) - n);
    for (uint8_t i = 0; i < n; i++)
        fly(x[i], y[i], k.card(to, (uint8_t)(at + i)), to, (uint8_t)(at + i), T, held && to >= TAB && !i ? F_PUFF : 0);
    heldSrc = NO_PILE;
    afterMove(k);
}

static void putBack(const Klondike &k) {
    uint8_t from = (uint8_t)(k.count(heldSrc) - heldN);
    for (uint8_t i = 0; i < heldN; i++) {
        int x, y;
        heldPos(i, x, y);
        fly(x, y, k.card(heldSrc, (uint8_t)(from + i)), heldSrc, (uint8_t)(from + i), 5);
    }
    heldSrc = NO_PILE;
    audio::sfx(Sfx::Back);
}

static void drawStock(Klondike &k) {
    if (!k.nStock && !k.canRecycle()) { deny(); return; }
    remember(k);
    uint8_t n = k.draw();
    if (n == 0xFF) {                                     // the waste goes back under
        memset(flies, 0, sizeof flies);
        fly(colX(1), TOP_Y, 0, STOCK, (uint8_t)(k.nStock - 1), 8, F_DOWN);
        audio::sfx(Sfx::Whoosh);
        return;
    }
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (uint8_t)(k.nWaste - n + i);
        fly(colX(0), TOP_Y, k.deck[idx], WASTE, idx, 7, F_TURN, i * 3);
    }
    audio::sfx(Sfx::Flip);
}

static void undo(Klondike &k) {
    if (!hasUndo) { deny(); return; }
    uint16_t secs = k.secs;
    uint8_t sub = k.sub;
    k = before;
    k.secs = secs; k.sub = sub;                          // the clock does not run backwards
    clearMotion();
    audio::sfx(Sfx::Whoosh);
}

static void steer(const Klondike &k, uint8_t rep) {
    static const uint8_t TOP_COL[6] = {0, 1, 3, 4, 5, 6};
    static const uint8_t ABOVE[7] = {STOCK, WASTE, WASTE, FOUND, FOUND + 1, FOUND + 2, FOUND + 3};
    uint8_t was = curPile, wasDepth = depth;
    bool top = curPile < TAB;
    if (rep & (LEFT_BUTTON | RIGHT_BUTTON)) {
        uint8_t n = top ? 6 : 7, i = (uint8_t)(top ? curPile : curPile - TAB);
        i = (uint8_t)((i + (rep & LEFT_BUTTON ? n - 1 : 1)) % n);
        curPile = (uint8_t)(top ? i : TAB + i);
        depth = 1;
    }
    if (rep & UP_BUTTON) {
        if (!top) {
            uint8_t col = (uint8_t)(curPile - TAB);
            if (heldSrc == NO_PILE && depth < k.faceUp(col)) depth++;
            else { curPile = ABOVE[col]; depth = 1; }
        }
    }
    if (rep & DOWN_BUTTON) {
        if (top) curPile = (uint8_t)(TAB + TOP_COL[curPile]);
        else if (depth > 1) depth--;
    }
    if (curPile != was || depth != wasDepth) audio::sfx(Sfx::Cursor);
    if (curPile >= TAB) {                               // an undo can shorten the run under the glove
        uint8_t up = k.faceUp((uint8_t)(curPile - TAB));
        if (depth > up) depth = up ? up : 1;
    }
}

static void input(Klondike &k, uint8_t pressed, uint8_t rep) {
    steer(k, rep);
    if (sinceA < 255) sinceA++;
    if (!(pressed & (A_BUTTON | B_BUTTON | SELECT_BUTTON))) return;
    // A press never waits for cards in the air: they land at once.
    for (auto &f : flies) if (f.T) { f.T = 0; landed(k, f); }
    if (pressed & SELECT_BUTTON) {
        if (heldSrc != NO_PILE) putBack(k);
        else undo(k);
        return;
    }
    if (pressed & B_BUTTON) {
        if (heldSrc != NO_PILE) putBack(k);
        else drawStock(k);
        return;
    }
    if (!(pressed & A_BUTTON)) return;
    tapT = 1;
    if (heldSrc == NO_PILE) {
        if (curPile == STOCK) { drawStock(k); return; }
        if (!k.count(curPile)) { deny(); return; }
        heldSrc = curPile;
        heldN = curPile >= TAB ? depth : 1;
        depth = 1;
        sinceA = 0;
        audio::sfx(Sfx::Pick);
        return;
    }
    if (curPile == heldSrc) {
        // A second press in the same spot: straight to the foundation if it
        // came quickly (the double-click) and the card goes; else back down.
        if (sinceA < 20 && k.canMove(heldSrc, heldN, FOUND)) { remember(k); play(k, heldSrc, 1, FOUND, true, 8); }
        else putBack(k);
        return;
    }
    if (!k.canMove(heldSrc, heldN, curPile)) { deny(); return; }
    remember(k);
    play(k, heldSrc, heldN, curPile, true, 5);
    audio::sfx(Sfx::Deal);
}

// ---------------------------------------------------------------------------
// The cascade
// ---------------------------------------------------------------------------
static void cascadeBegin() {
    memset(&cas, 0, sizeof cas);
}

static bool cascadeTick() {
    if (!cas.active) {
        if (cas.idx >= 52) return false;
        // Kings first, round the four foundations, as Windows did it.
        uint8_t s = (uint8_t)(cas.idx & 3);
        cas.card = makeCard((uint8_t)(12 - (cas.idx >> 2)), s);
        cas.x = (int16_t)(colX(3 + s) << 4);
        cas.y = (int16_t)(TOP_Y << 4);
        int v = 10 + (int)(fx::rnd() % 36);
        cas.vx = (int16_t)(fx::rnd() & 1 ? v : -v);
        cas.vy = (int16_t)(-(int)(fx::rnd() % 40));
        cas.active = 1;
        cas.idx++;
        cas.dirty |= (uint8_t)(1 << s);
    }
    cas.x = (int16_t)(cas.x + cas.vx);
    cas.y = (int16_t)(cas.y + cas.vy);
    cas.vy = (int16_t)(cas.vy + 3);
    const int floor = (128 - SH) << 4;
    if (cas.y > floor) {
        cas.y = (int16_t)floor;
        cas.vy = (int16_t)(-(cas.vy * 13) / 16);
        if (cas.vy < -6 && !audio::playing()) audio::blip((uint16_t)(1200 - cas.vy * 12), 12);
    }
    if (cas.nStamp < 3) cas.nStamp++;
    auto &s = cas.stamp[cas.nStamp - 1];
    s.x = (int16_t)(cas.x >> 4); s.y = (int16_t)(cas.y >> 4); s.card = cas.card;
    if (cas.x < -(SW << 4) || cas.x > (128 << 4)) cas.active = 0;
    return true;
}

static void cascadeDraw() {
    for (uint8_t s = 0; s < 4; s++) {
            if (!((cas.dirty >> s) & 1)) continue;
            // The card that left uncovers the one under it.
            int left = 13 - (cas.idx + 3 - s) / 4, x = colX(3 + s);
            if (left > 0) art::small(x, TOP_Y, makeCard((uint8_t)(left - 1), s), true);
            else { gfx_fillRect(x, TOP_Y, SW + 1, SH + 1, FELT); art::slot(x, TOP_Y, s); }
        }
    cas.dirty = 0;
    art::flat = true;
    for (uint8_t i = 0; i < cas.nStamp; i++) art::small(cas.stamp[i].x, cas.stamp[i].y, cas.stamp[i].card, true);
    art::flat = false;
    cas.nStamp = 0;
}

// ---------------------------------------------------------------------------
// Per-frame
// ---------------------------------------------------------------------------
void update(Klondike &k, uint8_t pressed, uint8_t rep, bool paused) {
    for (auto &f : flies) {
        if (!f.T) continue;
        if (f.t == 0 && st == DEALING) audio::sfx(Sfx::Deal);
        if (++f.t < (int8_t)f.T) continue;
        f.T = 0;
        landed(k, f);
    }
    for (auto &t : flipT) if (t && ++t > 8) t = 0;
    if (tapT && ++tapT > 12) tapT = 0;
    if (denyT && ++denyT > 12) denyT = 0;
    fx::update();

    switch (st) {
        case DEALING:
            if (!busy()) st = PLAY;
            break;
        case PLAY:
            if (paused) break;
            k.tick();
            input(k, pressed, rep);
            break;
        case AUTO:
            if (k.won()) {
                if (!busy() && !fx::particles() && !fx::bannerActive()) {
                    st = CASCADE;
                    fx::clear();                          // nothing may be left to freeze in the picture
                    cascadeBegin();
                    redraw = true;
                }
            } else if (++autoT >= 5) {
                autoT = 0;
                uint8_t p = k.autoSource();
                if (p == NO_PILE) st = PLAY;
                else play(k, p, 1, FOUND, false, 9);
            }
            break;
        case CASCADE:
            if (pressed || !cascadeTick()) st = DONE;
            break;
        case DONE:
            break;
    }

    // The glove glides to what it points at, from below: a fingertip on
    // the bottom edge of the pile's top card, the rest of it clear of the
    // cards (the shimmering edges say how much of a column is taken).
    // Carrying, the run rides on it a little off where it would land.
    int x = 64, y = 150;
    if (st == PLAY) {
        int n = k.count(curPile);
        if (heldSrc != NO_PILE) {
            cardPos(k, curPile, curPile == heldSrc ? n - heldN : (curPile <= WASTE && n ? n - 1 : n), x, y);
            x += 10;
            y += (heldN - 1) * UP_PITCH + SH;
        } else {
            cardPos(k, curPile, n ? n - 1 : 0, x, y);
            x += 8;
            y += SH - 2;
        }
        if (y > 120) y = 120;
    }
    gx16 = (int16_t)(gx16 + (((x << 4) - gx16) >> 1));
    gy16 = (int16_t)(gy16 + (((y << 4) - gy16) >> 1));
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static int squash(int t, int n) {            // a flip, t in 0..n: full, edge-on at n/2, full
    int half = n / 2;
    int w = t <= half ? SW * (half - t + 1) / (half + 1) : SW * (t - half) / (half + 1);
    return w < 2 ? 2 : w;
}

static void drawStatus(const Klondike &k) {
    gfx_hline(0, STATUS_Y, 128, GOLD);
    gfx_fillRect(0, STATUS_Y + 1, 128, 127 - STATUS_Y, NAVY);
    char b[24];
    int y = STATUS_Y + 2;
    if (k.scoring == VEGAS) {
        fmtMoney(fmtStr(b, "BANK "), bank + k.score);
        text35(2, y, b, bank + k.score < 0 ? SKIN : GOLD);
    } else if (k.scoring == STANDARD) {
        fmtInt(fmtStr(b, "SCORE "), k.score);
        text35(2, y, b, GOLD);
    }
    if (k.timed) fmtTime(fmtStr(b, "TIME "), k.secs);
    else fmtInt(fmtStr(b, "MOVES "), k.moves);
    text35(126 - text35Width(b), y, b, WHITE);
}

static void drawTable(const Klondike &k, bool glove, uint32_t frame) {
    art::clock = (uint8_t)(frame >> 4);
    gfx_fillRect(0, 0, 128, STATUS_Y, FELT);
    drawStatus(k);
    bool holding = heldSrc != NO_PILE, pick = glove && !holding;

    // The top row.
    for (uint8_t p = STOCK; p < TAB; p++) {
        int n = k.count(p) - inFlight(p) - (p == heldSrc ? 1 : 0), x, y;
        uint8_t edge = (pick && p == curPile) || (holding && k.canMove(heldSrc, heldN, p)) ? FX_B : INK;
        cardPos(k, p, 0, x, y);
        if (n <= 0) {
            if (p == WASTE) continue;
            art::slot(x, y, p >= FOUND ? (uint8_t)(p - FOUND) : 0xFF);
            if (edge != INK) roundRect(x, y, SW, SH, 2, edge);
            if (p == STOCK) {                           // go round again, or not
                if (k.canRecycle()) roundRect(x + 4, y + 7, 9, 9, 4, FELT_LT);
                else if (!k.nStock) text35x2(x + 6, y + 6, "X", RED);
            }
            continue;
        }
        if (p == STOCK) {
            if (n > 1) art::small(x - 1, y - 1, 0, false);
            art::small(x, y, 0, false, SW, SH, edge);
        } else if (p == WASTE) {
            int first = k.nWaste - (k.fan ? k.fan : 1) - 1;
            for (int i = first < 0 ? 0 : first; i < n; i++) {
                cardPos(k, p, i, x, y);
                art::small(x, y, k.deck[i], true, SW, SH, i == n - 1 ? edge : (uint8_t)INK);
            }
        } else {
            art::small(x, y, makeCard((uint8_t)(n - 1), (uint8_t)(p - FOUND)), true, SW, SH, edge);
        }
    }

    // The columns.
    for (uint8_t col = 0; col < 7; col++) {
        uint8_t p = (uint8_t)(TAB + col);
        int n = k.nTab[col] - inFlight(p) - (p == heldSrc ? heldN : 0), x, y;
        bool target = holding && k.canMove(heldSrc, heldN, p);
        if (n <= 0) {
            art::slot(colX(col), TAB_Y);
            if (target || (pick && p == curPile)) roundRect(colX(col), TAB_Y, SW, SH, 2, FX_B);
            continue;
        }
        int pd, pu;
        pitch(k, col, pd, pu);
        for (int i = 0; i < n; i++) {
            cardPos(k, p, i, x, y);
            bool up = i >= k.down[col], last = i == n - 1;
            uint8_t edge = (last && target) || (pick && p == curPile && i >= n - depth) ? FX_B : INK;
            int w = SW;
            if (last && flipT[col]) { w = squash(flipT[col], 8); up = flipT[col] > 4; }
            art::small(x, y, k.tab[col][i], up, w, last ? SH : (up ? pu : pd), edge);
        }
    }

    // Cards in the air, and in hand.
    for (auto &f : flies) {
        if (!f.T || f.t < 0) continue;
        int x, y;
        cardPos(k, f.pile, f.idx, x, y);
        int e = fx::ease(fx::OUT_CUBIC, f.t, f.T);
        x = f.x0 + (((x - f.x0) * e) >> 8);
        y = f.y0 + (((y - f.y0) * e) >> 8);
        bool up = !(f.flags & F_DOWN);
        int w = SW;
        if (f.flags & F_TURN) { w = squash(f.t, f.T); up = f.t > f.T / 2; }
        art::small(x, y, f.card, up, w);
    }
    if (holding) {
        uint8_t from = (uint8_t)(k.count(heldSrc) - heldN);
        for (uint8_t i = 0; i < heldN; i++) {
            int x, y;
            heldPos(i, x, y);
            art::small(x, y, k.card(heldSrc, (uint8_t)(from + i)), true, SW, i == heldN - 1 ? SH : UP_PITCH, FX_A);
        }
    }
    if (glove) {
        int bob = holding ? 0 : (int)((frame >> 5) & 1);
        if (tapT) bob = -(tapT < 6 ? tapT : 12 - tapT) / 2;
        int dx = denyT ? (fx::isin(denyT * 64) * 3) >> 8 : 0;
        sprite4(HAND, (gx16 >> 4) - HAND_TIP + dx, (gy16 >> 4) + bob);
    }
    fx::drawParticles();
    fx::drawFloats();
    fx::drawBanner();
}

void render(const Klondike &k, uint32_t frame) {
    if (st >= CASCADE) {
        // The table one last time, then only the stamps: nothing is wiped.
        if (redraw) drawTable(k, false, frame);
        redraw = false;
        cascadeDraw();
        return;
    }
    drawTable(k, st == PLAY, frame);
}

}  // namespace stage
