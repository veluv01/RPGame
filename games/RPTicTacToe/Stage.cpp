// The play screen (Stage.h): the felt or the iso table, the marks, the
// gloves, the bars, and the show for what the match reports. It redraws
// only what moved (render(), at the end).
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
#include <RPGame.h>
#include <string.h>
#include "Stage.h"
#include "ChipArt.h"
#include "Iso.h"
#include "Text.h"
#include "Remap.h"
#include "Fx.h"
#include "Sounds.h"
#include "src/assets/Assets.h"

namespace stage {

static const int BOARD_Y0 = 12, BOARD_Y1 = 116;         // between the two bars
static const int HOME_X = 116 << 4, HOME_Y = 56 << 4;   // where the dealer's glove waits
static const int HOLD = 14;             // iso: how high a glove holds its piece over the felt
static uint8_t hover = HOLD, dropFrom = HOLD;   // over a piece: higher, clear of it

static int16_t gx, gy, tx, ty;          // the glove's fingertip, Q4
static bool gloveOn, gloveCpu, moving, reaching, dirty;
static bool moved, wasMoving;           // only the glove or the cursor changed
static int16_t bandLo = 12, bandHi = 116;   // iso: rows the glove, its piece and the cursor were drawn in
static uint16_t clockSig;               // BLITZ: what its clock showed
static uint8_t alertT, busyT, strikeT, tossT, tossWho, quipT, lastPhase;
static uint16_t catT;
static uint8_t spinT;                   // the held piece's turn, in ticks
static bool mapView;                    // the strategy map instead of the iso table
static uint8_t introT;                  // iso: the board dropping onto the carpet
static uint8_t dropT, dropCell = NONE;  // iso: a piece falling onto its pad
static Sfx landSfx;
static const char *landText;
static uint8_t landColour;
static uint32_t lastSig;
static const char *quip;
static char note[24];

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
struct Geo { int16_t x0, y0; uint8_t cw, r; };

static Geo geo(const Board &b) {
    Geo g;
    g.cw = b.n == 9 ? 24 : (b.w == 5 ? 18 : (b.w == 7 ? 14 : 11));
    g.r = b.n == 9 ? 8 : (b.w == 5 ? 6 : (b.w == 7 ? 5 : 3));
    int gap = (b.flags & F_ULTIMATE) ? 2 : 0;
    g.x0 = (int16_t)((128 - b.w * g.cw - gap) / 2 + 1);
    g.y0 = (int16_t)(BOARD_Y0 + 1 + (104 - b.h * g.cw - gap) / 2 - (b.n == 9 ? 3 : 0));   // 3x3: clear of the stocks
    return g;
}

__attribute__((noinline)) bool canIso(const Board &b) { return iso::fits(b); }
__attribute__((noinline)) bool isoOn(const Board &b) { return iso::fits(b) && !mapView; }
void toggleView() { mapView = !mapView; dirty = true; bandLo = 12; bandHi = 116; }

// iso: the board is lowered (camY px, gliding to camT) when a held piece
// would otherwise rise out of the top of the room.
static int8_t camY, camT;

static int introLift() { return introT * introT / 5 - camY; }

__attribute__((noinline)) static void cellPos(const Board &b, uint8_t cell, int &cx, int &cy) {
    if (isoOn(b)) { iso::cellPos(iso::view(b, introLift()), cell, cx, cy); return; }
    int x = cell % b.w, y = cell / b.w;
    Geo g = geo(b);
    bool u = (b.flags & F_ULTIMATE) != 0;
    cx = g.x0 + x * g.cw + (u ? x / 3 : 0) + (g.cw - 1) / 2;
    cy = g.y0 + y * g.cw + (u ? y / 3 : 0) + (g.cw - 1) / 2;
}

static uint8_t radius(const Board &b) {
    if (isoOn(b)) return b.n == 9 ? 6 : 4;
    return geo(b).r;
}

// ---------------------------------------------------------------------------
// Marks
// ---------------------------------------------------------------------------
static void drawX(int cx, int cy, int r, uint8_t c) {
    int t = r / 4, w = r < 5 ? 2 : 2 * t + 1;            // small marks: 2 px strokes
    for (int d = -r; d <= r; d++) {
        gfx_hline(cx + d - t, cy + d, w, c);
        gfx_hline(cx - d - t, cy + d, w, c);
    }
}

void mark(int cx, int cy, int r, uint8_t sym, uint8_t colour) {
    if (sym == 1) {
        if (r >= 6) drawX(cx + 1, cy + 1, r, INK);
        drawX(cx, cy, r, colour ? colour : RED);
        return;
    }
    uint8_t c = colour ? colour : CYAN;
    if (r < 5) {
        gfx_circle(cx, cy, r, c);
        gfx_circle(cx, cy, r - 1, c);
        return;
    }
    gfx_fillCircle(cx + 1, cy + 1, r, INK);
    gfx_fillCircle(cx, cy, r, colour ? colour : BLUE);
    gfx_circle(cx, cy, r - 1, c);
    gfx_fillCircle(cx, cy, r - 2 - r / 4, FELT);
    gfx_circle(cx, cy, r - 2 - r / 4, INK);
}

// ---------------------------------------------------------------------------
// The felt
// ---------------------------------------------------------------------------
// The flat map: the iso table's board seen from above - a wooden frame with
// gold trim and its shadow on the felt, felt pads set into the wood (in
// shadow along the top and left, catching the light bottom and right), and
// gold inlay between the pads of the smaller boards.
static void drawGrid(const Board &b) {
    Geo g = geo(b);
    bool u = (b.flags & F_ULTIMATE) != 0;
    int W = b.w * g.cw + (u ? 2 : 0) - 1, H = b.h * g.cw + (u ? 2 : 0) - 1;
    int inset = g.cw >= 24 ? 2 : (g.cw >= 14 ? 1 : 0);
    bool inlay = g.cw >= 14;
    dither(g.x0, g.y0 + H + 3, W + 5, 3, INK, 0);
    dither(g.x0 + W + 3, g.y0, 3, H + 3, INK, 0);
    gfx_fillRect(g.x0 - 3, g.y0 - 3, W + 6, H + 6, WOOD);
    gfx_rect(g.x0 - 3, g.y0 - 3, W + 6, H + 6, GOLD);
    gfx_hline(g.x0 - 2, g.y0 + H + 1, W + 4, WINE);
    gfx_vline(g.x0 + W + 1, g.y0 - 2, H + 4, WINE);
    int s = g.cw - 1 - 2 * inset;
    for (int y = 0; y < b.h; y++)
        for (int x = 0; x < b.w; x++) {
            int px = g.x0 + x * g.cw + (u ? x / 3 : 0) + inset, py = g.y0 + y * g.cw + (u ? y / 3 : 0) + inset;
            gfx_fillRect(px, py, s, s, FELT);
            gfx_hline(px, py, s, FELT_DK);
            gfx_vline(px, py, s, FELT_DK);
            gfx_hline(px + 1, py + s - 1, s - 1, FELT_LT);
            gfx_vline(px + s - 1, py + 1, s - 1, FELT_LT);
        }
    for (int i = 1; i < b.w; i++) {
        int x = g.x0 + i * g.cw + (u ? i / 3 : 0) - 1;
        bool heavy = u && i % 3 == 0;
        if (inlay || heavy) gfx_vline(x, g.y0, H, GOLD);
        if (heavy) gfx_vline(x - 1, g.y0, H, GOLD);
    }
    for (int i = 1; i < b.h; i++) {
        int y = g.y0 + i * g.cw + (u ? i / 3 : 0) - 1;
        bool heavy = u && i % 3 == 0;
        if (inlay || heavy) gfx_hline(g.x0, y, W, GOLD);
        if (heavy) gfx_hline(g.x0, y - 1, W, GOLD);
    }
}

static void drawMarks(const Match &m) {
    const Board &b = m.b;
    uint8_t r = radius(b);
    // VANISH: the mark that goes next, for the side about to move.
    uint8_t fading = NONE;
    if ((b.flags & F_VANISH) && !b.result && b.qn[b.turn] == 3) fading = b.q[b.turn][0];
    for (uint8_t i = 0; i < b.n; i++) {
        uint8_t c = b.cell[i];
        if (!c) continue;
        if ((b.flags & F_DARK) && topOf(c) == 2 && !(b.seen >> i & 1)) continue;   // not found yet
        int cx, cy;
        cellPos(b, i, cx, cy);
        if (topOf(c) == 3) {                             // MINES: a crater
            gfx_fillCircle(cx, cy, 6, INK);
            gfx_circle(cx, cy, 6, WINE);
            drawX(cx, cy, 2, WOOD);
            continue;
        }
        bool hot = i == fading;
        if (!(b.flags & F_ULTIMATE)) for (uint8_t j = 0; j < 5; j++) if (b.win[j] == i) hot = true;
        if (b.flags & F_GOBBLE)
            iso::stand(iso::chipArt(false, levelOf(c)), cx, cy + 3, 0, topOf(c) == 2 ? (hot ? RM_BLUEHOT : RM_BLUE) : (hot ? RM_HOT : RM_ID), false);
        else mark(cx, cy, r, topOf(c), hot ? FX_A : 0);
    }
    if (b.flags & F_ULTIMATE) {
        Geo g = geo(b);
        for (uint8_t s = 0; s < 9; s++) {
            int x = g.x0 + s % 3 * 34, y = g.y0 + s / 3 * 34;
            if (b.small[s]) {
                dither(x, y, 32, 32, FELT_DK, 0);
                if (b.small[s] < 3) mark(x + 16, y + 16, 12, b.small[s]);
            } else if (s == b.must && m.phase == Phase::Human) gfx_rect(x - 1, y - 1, 34, 34, FX_B);
        }
    }
}

// The winning line, drawn out from one end.
static void drawStrike(const Board &b) {
    if (!strikeT || b.win[0] == NONE || (b.flags & F_WRAP)) return;
    uint8_t last = 4;
    while (b.win[last] == NONE) last--;
    int ax, ay, bx, by;
    cellPos(b, b.win[0], ax, ay);
    cellPos(b, b.win[last], bx, by);
    int dx = bx - ax, dy = by - ay;
    if (isoOn(b)) {                                      // through the pieces' middles
        int up = b.n == 9 ? 12 : 7, ex = dx / (2 * last), ey = dy / (2 * last);
        ax -= ex; ay -= ey + up; bx += ex; by += ey - up;
    } else {
        int r = radius(b);
        if (b.flags & F_ULTIMATE) r = 12;
        int ex = dx ? (dx > 0 ? r : -r) : 0, ey = dy ? (dy > 0 ? r : -r) : 0;
        ax -= ex; ay -= ey; bx += ex; by += ey;
    }
    int p = strikeT > 16 ? 16 : strikeT;
    bx = ax + (bx - ax) * p / 16;
    by = ay + (by - ay) * p / 16;
    static const int8_t OFF[5][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}, {0, 0}};
    for (int i = 0; i < 5; i++)
        gfx_line(ax + OFF[i][0], ay + OFF[i][1], bx + OFF[i][0], by + OFF[i][1], i == 4 ? WHITE : FX_A);
}

// ---------------------------------------------------------------------------
// The side panels of the 3x3 tables
// ---------------------------------------------------------------------------
static void chipPile(int cx, int base, int pitch, uint8_t n, uint8_t lifted) {
    for (uint8_t i = 0; i < n; i++) {
        bool up = i >= n - lifted;
        art::chip(cx, base - i * pitch - (up ? 5 : 0), up ? 1 : 2, i == n - 1 || i == n - lifted - 1);
    }
}

// AUCTION: each side's chips, as they stand (during the show, as bid).
static uint8_t chipsOf(const Match &m, uint8_t s, uint8_t &lift) {
    const Board &b = m.b;
    bool bidding = m.phase == Phase::Bid || m.phase == Phase::BidShow;
    lift = !bidding ? 0 : (s ? (m.phase == Phase::BidShow ? m.bidC : 0) : m.bidP);
    uint8_t n = b.chips[s];
    if (m.phase == Phase::BidShow) {
        uint8_t paid = b.turn ? m.bidC : m.bidP;
        n = (uint8_t)(b.turn == s ? n + paid : n - paid);
    }
    return n;
}

static bool turnOf(const Match &m, uint8_t s) {
    return !m.b.result && m.b.turn == s && m.phase >= Phase::Human && m.phase <= Phase::Settle;
}

static const char *whoName(const Match &m, uint8_t s) { return m.two ? (s ? "P2" : "P1") : (s ? "HIM" : "YOU"); }

// BLITZ's clock and score: big digits under a label.
static void bigNumber(int x, int y, const char *label, int value, uint8_t c, bool right) {
    char buf[4];
    *fmtInt(buf, value) = 0;
    text35(right ? x - text35Width(label) : x, y, label, SILVER);
    text35x2(right ? x - 2 * text35Width(buf) : x, y + 8, buf, c);
}

// The corners of the 3x3 tables (both views): who is who at the top, and at
// the foot what each side has on the table - the stakes, the chips to bid,
// the pieces in hand.
static void drawSides(const Match &m, const Casino &c) {
    const Board &b = m.b;
    for (uint8_t s = 0; s < 2; s++) {
        int x = s ? 124 : 4, dir = s ? -1 : 1;
        const char *who = whoName(m, s);
        text35(s ? x - text35Width(who) : x, 15, who, turnOf(m, s) ? FX_B : SILVER);
        uint8_t sym = (b.flags & F_SAME) ? 1 : (uint8_t)(s + 1);
        if (b.flags & F_GOBBLE) iso::stand(iso::chipArt(false, 1), x + dir * 9, 34, 0, s ? RM_BLUE : RM_ID, false);
        else if (!(b.flags & F_WILD)) iso::stand(iso::art(false, sym), x + dir * 9, 39, 0, RM_ID, false);
        if (b.flags & F_GOBBLE) {                        // the chips still in hand, small to large
            static const int8_t AT[3] = {5, 18, 35};
            for (uint8_t l = 0; l < 3; l++) {
                int cx = x + dir * AT[l];
                if (s == b.turn && l == m.size && m.phase == Phase::Human && (!s || m.two))
                    gfx_fillRect(cx - 4 - l, 115, 9 + 2 * l, 1, FX_B);
                for (uint8_t k = 0; k < b.stock[s][l]; k++)
                    iso::stand(iso::chipArt(false, l), cx, 112 - 3 * k, 0, s ? RM_BLUE : RM_ID, !k);
            }
        } else if (b.flags & F_AUCTION) {
            uint8_t lift, n = chipsOf(m, s, lift);
            chipPile(x + dir * 7, 110, 2, n, lift);
            char buf[4];
            *fmtInt(buf, n) = 0;
            text35(x + dir * 16 - (s ? text35Width(buf) : 0), 108, buf, WHITE);
        }
    }
    if (b.flags & F_BLITZ) {
        bigNumber(4, 92, "TIME", (m.clock + 59) / 60, m.clock < 600 ? RED : WHITE, false);
        bigNumber(124, 92, "WON", m.wins, GOLD, true);
        if (m.phase == Phase::Human) {                   // the shot clock, draining
            int w = 26 * m.shot / SHOT_TICKS;
            gfx_fillRect(4, 112, w, 2, m.shot < 60 ? RED : FX_B);
        }
    }
}

// ---------------------------------------------------------------------------
// The bars
// ---------------------------------------------------------------------------
static const char *status(const Match &m) {
    const Board &b = m.b;
    switch (m.phase) {
        case Phase::Over: return quipT ? quip : "A: AGAIN    B: TABLES";
        case Phase::Toss: return "THE COIN DECIDES";
        case Phase::Bid: return "UP/DOWN: BID   A: LOCK IN";
        case Phase::BidShow: return note;
        case Phase::Human:
            if (quipT) return quip;
            if (b.flags & F_GOBBLE) return "A: PLACE    B: SIZE";
            if (b.flags & F_WILD) return "A: X     B: O";
            if (b.flags & F_MISERE) return "DON'T MAKE THREE";
            if (b.flags & F_DARK) return "FEEL YOUR WAY";
            if (m.two) return b.turn ? "PLAYER 2" : "PLAYER 1";
            if (canIso(b) && b.left == b.n) return "YOUR MOVE   SELECT: VIEW";
            return "YOUR MOVE";
        case Phase::Think: case Phase::Reach: return quipT ? quip : "THE DEALER THINKS";
        default: return quipT ? quip : "";
    }
}

static void drawBars(const Match &m, const Casino &c) {
    gfx_fillRect(0, 0, 128, 11, INK);
    gfx_hline(0, 11, 128, GOLD);
    text35(3, 3, MODE_NAME[m.mode], GOLD);
    char buf[12];
    if (m.two) {                                         // the score, not the money
        char *p = fmtInt(fmtStr(buf, "P1 "), m.score[0]);
        *fmtStr(fmtInt(fmtStr(p, " - "), m.score[1]), " P2") = 0;
    } else {
        *fmtMoney(buf, c.purse) = 0;
        text35(125 - text35Width(buf), 3, buf, WHITE);
        *fmtMoney(fmtStr(buf, "BET "), ANTES[c.ante]) = 0;
    }
    text35(m.two ? 125 - text35Width(buf) : 64 - text35Width(buf) / 2 + 6, 3, buf, SILVER);
    gfx_fillRect(0, 117, 128, 11, INK);
    gfx_hline(0, 116, 128, GOLD);
    const char *s = status(m);
    text35(64 - text35Width(s) / 2, 120, s, quipT && s == quip ? WHITE : FELT_LT);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
__attribute__((noinline)) static void say(const char *s) { quip = s; quipT = 110; }

static const uint8_t *heldArt(const Match &m, uint8_t side, const uint8_t *&rm);

__attribute__((noinline)) static void tip(const Match &m, uint8_t cell, uint8_t side, int16_t &x, int16_t &y) {
    const Board &b = m.b;
    int cx, cy;
    cellPos(b, cell, cx, cy);
    x = (int16_t)(cx << 4);
    const uint8_t *rm, *a = heldArt(m, side, rm);
    // Over a piece that's showing, the held one rises clear of its top, so it
    // reads as above it rather than through it.
    uint8_t on = b.cell[cell], sym = topOf(on);
    int lift = HOLD;
    if (a && on && sym != 3 && !((b.flags & F_DARK) && sym == 2 && !(b.seen >> cell & 1))) {
        int over = iso::height((b.flags & F_GOBBLE) ? iso::chipArt(b.n == 9, levelOf(on)) : iso::art(b.n == 9, sym)) + 4;
        if (over > lift) lift = over;
    }
    hover = (uint8_t)lift;
    if (a) {
        cy -= lift + iso::height(a) - 3;                 // pinching the top of the piece it holds
        int top = cy - 16 - camY;                        // the glove's top, the board unlowered
        camT = (int8_t)(top < 13 ? 13 - top : 0);
        cy += camT - camY;                               // where it will be once the board is down
    }
    y = (int16_t)((cy - (isoOn(b) ? 1 : radius(b) / 2)) << 4);
}

void reset(const Match &m) {
    (void)m;
    gloveOn = moving = reaching = false;
    bandLo = 12; bandHi = 116;
    alertT = busyT = strikeT = tossT = quipT = 0;
    catT = 0;
    dropT = 0; dropCell = NONE;
    camY = camT = 0;
    lastPhase = 0xFF;
    lastSig = 0;
    dirty = true;
    fx::clear();
}

void invalidate() { dirty = true; bandLo = 12; bandHi = 116; }

bool busy() { return busyT || tossT || introT || dropT || (reaching && moving); }

void paid(int32_t net) {
    char buf[10], *p = buf;
    if (net > 0) *p++ = '+';
    *fmtMoney(p, net) = 0;
    if (net) fx::floatText(buf, 64, 24, net > 0 ? GOLD : RED);
}

void onEvents(const Match &m) {
    const Board &b = m.b;
    for (uint8_t i = 0; i < m.nEv; i++) {
        const Event &e = m.ev[i];
        int cx = 64, cy = 64;
        if (e.type == EV_MOVE) moved = true;
        else dirty = true;
        switch (e.type) {
            case EV_START:
                strikeT = 0;
                dropT = 0; dropCell = NONE;
    camY = camT = 0;
                if (isoOn(b)) introT = 18;
                break;
            case EV_TOSS:
                tossT = 1; tossWho = e.a;
                audio::sfx(Sfx::Coin);
                break;
            case EV_BID: {
                char *p = fmtInt(fmtStr(note, "YOU "), m.bidP);
                *fmtInt(fmtStr(p, "   HIM "), m.bidC) = 0;
                fx::floatText(e.a ? "HIS GO" : "YOURS!", 64, 60, e.a ? RED : GOLD);
                audio::sfx(e.a ? Sfx::Deny : Sfx::Chip);
                busyT = 50;
                break;
            }
            case EV_MOVE: audio::sfx(Sfx::Cursor); break;
            case EV_ARG: audio::sfx(Sfx::Tick); break;
            case EV_DENY: audio::sfx(Sfx::Deny); alertT = 10; break;
            case EV_TIMEOUT: fx::floatText("TOO SLOW", 64, 60, RED); break;
            case EV_BOOM:
                cellPos(b, e.a, cx, cy);
                fx::floatText("BOOM!", cx, cy - 10, GOLD);
                fx::burst(fx::SPARK, cx, cy, 16, 50, GOLD);
                fx::shake(8, 2);
                pal::flash(FELT, 0xFFF, 3);
                audio::sfx(Sfx::Boom);
                busyT = 20;
                break;
            case EV_BUMP:
                cellPos(b, e.a, cx, cy);
                fx::floatText("BUMP!", cx, cy - 12, WHITE);
                fx::burst(fx::STAR, cx, cy, 8, 30, CYAN);
                audio::sfx(Sfx::Deny);
                break;
            case EV_PLACE: {
                busyT = 6;
                if ((b.flags & F_DARK) && e.b && !b.result) {       // nothing to see
                    say(QUIPS[QUIP_DARK]);
                    audio::sfx(Sfx::Tock);
                    break;
                }
                uint8_t need = (uint8_t)(b.k - b.run);
                landText = nullptr; landColour = WHITE; landSfx = Sfx::Place;
                if (b.result && b.win[0] != NONE) { landText = "TOE!"; landColour = FX_A; }
                else if (need == 1) { landText = "TAC"; landSfx = Sfx::Tac; }
                else if (need == 2 && b.n == 9) { landText = "TIC"; landSfx = Sfx::Tic; }
                dropCell = e.a;
                if (isoOn(b)) { dropT = 12; busyT = 14; gloveOn = false; dropFrom = hover; break; }
                dropT = 1;                                    // the map: it lands at once
                if (!e.b && !b.result && !m.two) {       // the dealer has opinions
                    uint32_t r = fx::rnd();
                    if (b.n == 9 && b.left == 8 && !(b.flags & F_BLITZ))
                        say(QUIPS[e.a == 4 ? QUIP_CENTRE : ((e.a & 1) ? QUIP_EDGE : QUIP_CORNER)]);
                    else if (r % 7 == 0 && !(b.flags & F_BLITZ)) say(QUIPS[QUIP_ANY + (r >> 8) % QUIP_ANY_N]);
                }
                if (dropT == 1) busyT = 6;
                break;
            }
            case EV_GONE:
                cellPos(b, e.a, cx, cy);
                fx::burst(fx::SPARK, cx, cy, 10, 30, FX_A);
                audio::sfx(Sfx::Poof);
                break;
            case EV_SMALL:
                cellPos(b, smallCentre(e.a), cx, cy);
                fx::burst(fx::STAR, cx, cy, 12, 40, FX_B);
                audio::sfx(b.small[e.a] == 1 ? Sfx::Win : Sfx::Lose);
                break;
            case EV_BOARD:
                strikeT = 1;
                if (e.a == R_P0) { fx::floatText("+1", 64, 60, GOLD); audio::sfx(Sfx::Win); fx::fountain(fx::COIN, 64, 110, 6); }
                else if (e.a == R_P1) { fx::floatText("-5 SEC", 64, 60, RED); audio::sfx(Sfx::Lose); }
                else { fx::floatText("CAT!", 64, 60, CYAN); audio::sfx(Sfx::Meow); }
                break;
            case EV_OVER: {
                bool mis = (b.flags & F_MISERE) != 0;
                if (!(b.flags & F_BLITZ)) strikeT = 1;
                if (b.flags & F_BLITZ) {
                    *fmtStr(fmtInt(note, m.wins), m.wins == 1 ? " BOARD" : " BOARDS") = 0;
                    fx::banner(note, e.a == R_P0 ? fx::B_RAINBOW : fx::B_RED, 64, 110);
                } else if (m.two && e.a != R_DRAW) fx::banner(e.a == R_P0 ? "P1 WINS!" : "P2 WINS!", fx::B_RAINBOW, 64, 110);
                else if (e.a == R_P0) fx::banner(mis ? "HE MADE 3!" : "YOU WIN!", fx::B_RAINBOW, 64, 110);
                else if (e.a == R_P1) fx::banner(mis ? "OOPS! THREE" : "HOUSE WINS", fx::B_RED, 64, 110);
                else { fx::banner("CAT'S GAME", fx::B_CYAN, 56, 110); catT = 1; }
                if (e.a == R_P0 || (m.two && e.a == R_P1)) {
                    audio::sfx(Sfx::BigWin);
                    audio::led(audio::LED_PARTY);
                    fx::shake(10, 2);
                    fx::fountain(fx::COIN, 40, 112, 8);
                    fx::fountain(fx::CONFETTI, 88, 112, 14);
                } else audio::sfx(e.a == R_P1 ? Sfx::Lose : Sfx::Meow);
                if (!m.two) say(QUIPS[QUIP_OVER + (e.a - 1) * 2 + (fx::rnd() & 1)]);
                busyT = 40;
                break;
            }
        }
    }
}

// A piece meets the felt: dust, its call-out and its sound.
__attribute__((noinline)) static void land(const Board &b) {
    int cx, cy;
    cellPos(b, dropCell, cx, cy);
    bool big = !isoOn(b) || b.n == 9;
    fx::burst(fx::DUST, cx, cy, big ? 8 : 5, big ? 30 : 20, isoOn(b) ? FELT_LT : SKIN);
    if (landText) fx::floatText(landText, cx, cy - (isoOn(b) ? (big ? 30 : 20) : 8), landColour);
    if (isoOn(b) && big) fx::shake(3, 1);
    audio::sfx(landSfx);
}

void update(const Match &m) {
    const Board &b = m.b;
    fx::update();
    if (introT && !--introT) {                           // the board lands: thump
        fx::shake(6, 2);
        fx::burst(fx::DUST, 64, 104, 12, 40, SILVER);
        audio::sfx(Sfx::Place);
    }
    if (dropT && --dropT == (isoOn(b) ? 7 : 0) && dropCell != NONE) land(b);
    if (!(++spinT & 3) && gloveOn && b.n == 9 && isoOn(b) && !(b.flags & F_GOBBLE)) moved = true;   // the next spin frame
    if (m.phase != Phase::Human && m.phase != Phase::Reach && m.phase != Phase::Settle) camT = 0;
    if (!isoOn(b)) camT = camY = 0;
    if (camY != camT) {                                  // the board glides
        int d = camT - camY;
        camY = (int8_t)(camY + (d > 2 ? (d + 1) / 2 : (d < -2 ? (d - 1) / 2 : d)));
        dirty = true;
    }
    if (!dropT) dropCell = NONE;
    if (busyT) busyT--;
    if (alertT) alertT--;
    if (quipT && !--quipT) dirty = true;
    if (strikeT && strikeT < 17) strikeT++;
    if (tossT && ++tossT > 44) {
        tossT = 0;
        fx::floatText(tossWho ? (m.two ? "P2!" : "HIS GO") : (m.two ? "P1!" : "YOURS!"), 64, 46, tossWho ? CYAN : SKIN);
    }
    if (catT && ++catT > 150) catT = 0;

    // The glove: yours rides the cursor; the dealer's comes from his side.
    Phase p = m.phase;
    bool was = gloveOn && !gloveCpu;
    reaching = p == Phase::Reach;
    if (p == Phase::Human) {
        bool second = b.turn != 0;                       // two players: the red cuff
        if (gloveCpu != second) was = false;
        gloveOn = true; gloveCpu = second;
        tip(m, m.cur, b.turn, tx, ty);
        if (!was) { gx = tx; gy = ty; }
    } else if (p == Phase::Think) {
        if (!gloveOn || !gloveCpu) { gx = HOME_X; gy = HOME_Y; }
        gloveOn = gloveCpu = true;
        tx = HOME_X; ty = HOME_Y;
    } else if (p == Phase::Reach && !(b.flags & F_DARK)) {   // in the dark his hand isn't seen
        gloveOn = gloveCpu = true;
        tip(m, m.pend.cell, 1, tx, ty);
    } else if (p != Phase::Settle) gloveOn = false;
    int dx = tx - gx, dy = ty - gy;
    moving = dx > 12 || dx < -12 || dy > 12 || dy < -12;
    if (moving) { gx = (int16_t)(gx + dx / 3); gy = (int16_t)(gy + dy / 3); }
    else { gx = tx; gy = ty; }

    uint32_t sig = (uint32_t)p | ((uint32_t)m.size << 12) | ((uint32_t)m.bidP << 16) | ((uint32_t)gloveOn << 24);
    if (sig != lastSig) { lastSig = sig; dirty = true; }
}

// ---------------------------------------------------------------------------
// The iso table
// ---------------------------------------------------------------------------
// The piece a glove carries in iso (null: none) and its colours.
__attribute__((noinline)) static const uint8_t *heldArt(const Match &m, uint8_t side, const uint8_t *&rm) {
    const Board &b = m.b;
    rm = RM_ID;
    if (!isoOn(b) || (b.flags & F_WILD) || b.result) return nullptr;
    bool big = b.n == 9;
    if (b.flags & F_GOBBLE) {
        uint8_t l = side == 0 || m.two ? m.size : m.pend.arg;
        if (side) rm = RM_BLUE;
        return iso::chipArt(big, l);
    }
    return iso::art(big, (b.flags & F_SAME) ? 1 : (uint8_t)(side + 1));
}

static bool inLine(const Board &b, uint8_t cell) {
    for (uint8_t j = 0; j < 5; j++) if (b.win[j] == cell) return true;
    return false;
}

static void drawIso(const Match &m, const Casino &c, uint32_t frame) {
    const Board &b = m.b;
    iso::View v = iso::view(b, introLift());
    iso::drawRoom(NAVY, BLUE);
    drawSides(m, c);
    iso::drawBoard(v);
    if (m.phase == Phase::Human) iso::padBorder(v, m.cur, b.n == 9 ? 3 : 2, FX_B, FX_B, 0);
    else if (!b.result && b.last != NONE && b.turn == 0 && !m.two && !(b.flags & F_DARK) && !dropT)
        iso::padBorder(v, b.last, b.n == 9 ? 3 : 2, SILVER, FELT_DK, 0);   // where the dealer just went
    uint8_t fading = NONE;
    if ((b.flags & F_VANISH) && !b.result && b.qn[b.turn] == 3) fading = b.q[b.turn][0];
    bool hop = b.result && fx::bannerActive();
    // The pad a held piece hangs over: a piece there is in its shadow.
    const uint8_t *hrm;
    uint8_t under = NONE;
    if (gloveOn && !moving && (m.phase == Phase::Human || m.phase == Phase::Reach) &&
        heldArt(m, m.phase == Phase::Human ? b.turn : 1, hrm))
        under = m.phase == Phase::Human ? m.cur : m.pend.cell;
    // Back to front: the far corner's diagonal first.
    for (int s = 0; s <= 2 * (v.n - 1); s++) {
        for (int u = 0; u < v.n; u++) {
            int w = s - u;
            if (w < 0 || w >= v.n) continue;
            uint8_t cell = (uint8_t)(w * v.n + u), cv = b.cell[cell];
            if (!cv) continue;
            uint8_t sym = topOf(cv);
            if ((b.flags & F_DARK) && sym == 2 && !(b.seen >> cell & 1)) continue;   // not found yet
            int cx, cy;
            iso::cellPos(v, cell, cx, cy);
            if (sym == 3) {                              // MINES: a scorched hole in the felt
                gfx_fillEllipse(cx, cy, v.hh, v.hh / 2, WINE);
                gfx_fillEllipse(cx, cy, v.hh - 1, v.hh / 2 - 1, INK);
                gfx_hline(cx - 2, cy - 1, 4, WOOD);
                continue;
            }
            bool hot = cell == fading || inLine(b, cell);
            int lift = 0;
            if (cell == dropCell && dropT) lift = (dropFrom * (256 - fx::bounce(12 - dropT, 12))) >> 8;
            else if (hop && inLine(b, cell)) {
                int a = fx::isin((int)(frame * 12) - cx * 2);
                if (a > 0) lift = (a * (v.big ? 6 : 4)) >> 8;
            }
            const uint8_t *rm = hot ? RM_HOT : RM_ID;
            bool shaded = cell == under;
            if (shaded) rm = RM_SHADE;
            if (b.flags & F_GOBBLE) {
                if (sym == 2) rm = shaded ? RM_BLUESHADE : (hot ? RM_BLUEHOT : RM_BLUE);
                iso::stand(iso::chipArt(v.big, levelOf(cv)), cx, cy, lift, rm);
            } else iso::stand(iso::art(v.big, sym), cx, cy, lift, rm);
        }
    }
}

// ---------------------------------------------------------------------------
// The rows of what moves on its own: the glove, the piece it holds, the
// cursor's pad - and BLITZ's clocks.
static void movingRows(const Match &m, int &lo, int &hi) {
    const Board &b = m.b;
    lo = 128; hi = 0;
    if (gloveOn) {
        const uint8_t *rm, *held = heldArt(m, m.phase == Phase::Human ? b.turn : 1, rm);
        lo = (gy >> 4) - 16;
        hi = (gy >> 4) + (held ? iso::height(held) + 2 : 2);
    }
    if (m.phase == Phase::Human || m.phase == Phase::Reach) {   // its pad (and the held piece's shadow)
        int cx, cy, h = b.n == 9 ? 10 : 6;
        cellPos(b, m.phase == Phase::Human ? m.cur : m.pend.cell, cx, cy);
        if (cy - h < lo) lo = cy - h;
        if (cy + h > hi) hi = cy + h;
    }
    if (b.flags & F_BLITZ) { if (lo > 90) lo = 90; hi = 116; }
}

bool render(const Match &m, const Casino &c, uint32_t frame) {
    const Board &b = m.b;
    int lo, hi;
    uint16_t clk = (b.flags & F_BLITZ) && m.phase != Phase::Over ? (uint16_t)((m.clock + 59) / 60 * 64 + m.shot * 26 / SHOT_TICKS) : 0;
    bool full = dirty || busyT || tossT || catT || alertT || (strikeT && strikeT < 17) || introT || dropT ||
                fx::activeRows(lo, hi) || fx::bannerActive();
    bool motion = moving || wasMoving || moved || clk != clockSig;
    wasMoving = moving;
#ifdef CHSIM_FORCE_FULL
    full = full || motion;                                  // the redraw check's reference build (rpgame redraw)
#endif
    if (!full && !motion) return false;
    dirty = moved = false;
    clockSig = clk;
    // In iso a full redraw costs most of a frame; when only the glove or the
    // cursor moved, just the rows they were and are in are drawn again.
    int nlo, nhi;
    movingRows(m, nlo, nhi);
    bool band = !full && isoOn(b);
    if (band) {
        lo = nlo < bandLo ? nlo : bandLo;
        hi = nhi > bandHi ? nhi : bandHi;
        if (lo < 12) lo = 12;
        if (hi > 116) hi = 116;
        if (lo < hi) gfx_setClip(0, lo, GFX_W, hi - lo);   // the library and RPGfx draw inside it
    }
    bandLo = (int16_t)nlo; bandHi = (int16_t)nhi;

    if (isoOn(b)) drawIso(m, c, frame);
    else {
        iso::drawRoom(FELT_DK, FELT);                    // the felt, darker away from the light
        drawGrid(b);
        drawMarks(m);
        if (b.n == 9) drawSides(m, c);
    }
    drawStrike(b);

    if (isoOn(b)) {
    } else if (!b.result && b.last != NONE && b.turn == 0 && !m.two && b.n > 9 && !(b.flags & F_DARK)) {
        int cx, cy;                                      // where the dealer just went
        cellPos(b, b.last, cx, cy);
        int h = (geo(b).cw - 1) / 2 - 1;
        gfx_rect(cx - h, cy - h, 2 * h + 1, 2 * h + 1, SILVER);
    }
    if (m.phase == Phase::Human && !isoOn(b)) {         // the cursor's cell
        int cx, cy;
        uint8_t at = m.cur;
        if ((b.flags & F_GRAVITY) && rules::drop(b, at) != NONE) at = rules::drop(b, at);   // where it will land
        cellPos(b, at, cx, cy);
        {
            int h = (geo(b).cw - 1) / 2;
            gfx_rect(cx - h, cy - h, 2 * h + 1, 2 * h + 1, FX_B);
            if (h > 6) gfx_rect(cx - h + 1, cy - h + 1, 2 * h - 1, 2 * h - 1, FX_B);
        }
    }
    if (tossT) {                                         // the coin, turning over as it flies
        int cy = 70 - ((fx::isin(tossT * 128 / 44) * 34) >> 8);
        int ry = tossT > 36 ? 9 : 1 + (((fx::isin(tossT * 26) < 0 ? -fx::isin(tossT * 26) : fx::isin(tossT * 26)) * 8) >> 8);
        gfx_fillEllipse(64, cy, 9, ry, WOOD);
        gfx_fillEllipse(64, cy, 8, ry > 1 ? ry - 1 : ry, GOLD);
        if (tossT > 36) mark(64, cy, 4, (uint8_t)(tossWho + 1), tossWho ? BLUE : RED);
    }
    if (catT) sprite4((catT & 8) ? CAT1 : CAT2, (int)catT - 18, isoOn(b) ? 105 : 104, RM_ID);
    fx::drawParticles(2);
    const uint8_t *hrm, *held = gloveOn && (m.phase == Phase::Human || m.phase == Phase::Think || m.phase == Phase::Reach)
                                        ? heldArt(m, m.phase == Phase::Human ? b.turn : 1, hrm) : nullptr;
    if (held) {
        int hx = gx >> 4, hy = (gy >> 4) + iso::height(held) - 2;
        uint8_t at = m.phase == Phase::Human ? m.cur : m.pend.cell;
        if (!moving && (m.phase == Phase::Human || m.phase == Phase::Reach) && !b.cell[at]) {
            int cx, cy;                                  // its shadow on the felt (on a piece: the piece is shaded)
            cellPos(b, at, cx, cy);
            iso::shadow(held, cx, cy, hover);
        }
        bool mirror;
        const uint8_t *art = iso::spin(held, (uint8_t)((spinT >> 2) & 3), mirror);
        iso::stand(art, hx, hy, 0, hrm, false, mirror);
    }
    if (gloveOn) {
        const uint8_t *rm = alertT ? RM_ALERT : (gloveCpu ? RM_CPU : RM_ID);
        sprite4(HAND, (gx >> 4) - HAND_TIP, (gy >> 4) - 15, rm);
    }
    fx::drawFloats();
    fx::drawBanner();
    fx::applyShake(BOARD_Y0, BOARD_Y1 - 1);
    if (band) { gfx_resetClip(); return true; }
    drawBars(m, c);
    (void)frame;
    return true;
}

}  // namespace stage
