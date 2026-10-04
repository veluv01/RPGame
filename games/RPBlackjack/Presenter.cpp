// The play screen's presentation (Presenter.h): Round's events become
// cards and chips in flight, sounds and the dealer's speech; the table is
// redrawn a band at a time, only where something changed.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
#include <RPGame.h>
#include <string.h>
#include <stdio.h>
#include "Presenter.h"
#include "Fx.h"
#include "Round.h"
#include "Layout.h"
#include "Table.h"
#include "CardArt.h"
#include "Sounds.h"

namespace present {

using namespace lay;

enum { FLIGHT = 12, FLIP = 8 };

struct CardView { int16_t x, y, sx, sy; uint8_t t, flip; bool live, up; };
static CardView views[3][12];

struct Ghost { int16_t x, y; uint8_t t; };
static Ghost ghosts[12];

enum FlyKind : uint8_t { CHIP_IN, CHIP_OUT, STACK_TO_TRAY, STACK_TO_PLAYER };
struct Fly { int16_t x0, y0, x1, y1; int16_t t; uint8_t T, denom, seat, kind; int32_t value; };
static Fly flies[16];

static int32_t dispBet[3];          // 0 = insurance, 1..2 = hands
static int32_t shown = 500, pending = 0;
static uint8_t purseFlash = 0;
static uint8_t collectT[3];         // countdown to sweeping a won stack to the player

static uint8_t bubLine = 0xFF, bubChars = 0, bubLen = 0, bubHold = 0;
static uint8_t face = F_NORMAL, blinkT = 90, blinking = 0, look = 1;
static uint8_t shuffleT = 0, peekT = 0;

// ---------------------------------------------------------------------------
// Layout of hands and stacks
// ---------------------------------------------------------------------------
static bool sideways(const Round &r, uint8_t seat, uint8_t i) {
    if (!seat) return false;
    const Hand &h = r.hands[seat - 1];
    bool splitAces = h.fromSplit && cardRank(h.cards[0]) == 0;
    return (h.doubled || (splitAces && h.count == 2)) && i == h.count - 1 && i > 0;
}

static void handGeom(const Round &r, uint8_t seat, int &x0, int &y, int &pitch) {
    const Hand &h = seat ? r.hands[seat - 1] : r.dealer;
    int n = h.count ? h.count : 1;
    int lastW = (seat && sideways(r, seat, (uint8_t)(n - 1))) ? 26 : CARD_W;
    int cx, maxW, lo = 2, hi;
    if (!seat) { cx = DEALER_CX; maxW = 100; hi = 102; y = DEALER_CARDS_Y; }
    else if (r.nHands == 1) { cx = PLAYER_CX; maxW = PLAYER_MAX_X - 4; hi = PLAYER_MAX_X; y = PLAYER_CARDS_Y; }
    else {
        cx = seat == 1 ? SPLIT_CX0 : SPLIT_CX1; maxW = 46; y = PLAYER_CARDS_Y;
        hi = seat == 1 ? 48 : PLAYER_MAX_X;
        lo = seat == 1 ? 2 : 50;
        bool playerTurn = r.phase == Phase::PlayHand || r.phase == Phase::DoubleUp || r.phase == Phase::Bust ||
                          r.phase == Phase::SplitCards;
        if (playerTurn && r.active != seat - 1) y += 3;
    }
    pitch = PITCH;
    if (n > 1 && lastW + PITCH * (n - 1) > maxW) pitch = (maxW - lastW) / (n - 1);
    if (pitch < 5) pitch = 5;
    int W = lastW + pitch * (n - 1);
    x0 = cx - W / 2;
    if (x0 + W > hi) x0 = hi - W;
    if (x0 < lo) x0 = lo;
}

static void cardTarget(const Round &r, uint8_t seat, uint8_t i, int &x, int &y) {
    int x0, y0, pitch;
    handGeom(r, seat, x0, y0, pitch);
    x = x0 + i * pitch;
    y = y0;
    if (sideways(r, seat, i)) { x -= 3; y += 4; }
}

static void stackPos(const Round &r, uint8_t seat, int &x, int &y) {
    if (seat == 0) { x = INS_CX; y = INS_CY; return; }
    x = r.nHands == 2 ? (seat == 1 ? BET_CX - 7 : BET_CX + 7) : BET_CX;
    y = BET_CY - 2;
}

// ---------------------------------------------------------------------------
// Actors
// ---------------------------------------------------------------------------
static void fly(int x0, int y0, int x1, int y1, uint8_t denom, uint8_t seat, int32_t value, uint8_t kind,
                int delay = 0, uint8_t T = 14) {
    for (auto &f : flies) {
        if (f.T) continue;
        f.x0 = (int16_t)x0; f.y0 = (int16_t)y0; f.x1 = (int16_t)x1; f.y1 = (int16_t)y1;
        f.t = (int16_t)-delay; f.T = T; f.denom = denom; f.seat = seat; f.kind = kind; f.value = value;
        return;
    }
    // Pool full: apply the effect immediately.
    if (kind == CHIP_IN) dispBet[seat] += value;
    if (kind == STACK_TO_PLAYER) pending -= value;
}

static void chipsIn(const Round &r, uint8_t seat, int32_t amount, int fromX, int fromY, int stagger) {
    int x, y;
    stackPos(r, seat, x, y);
    static const int32_t V[5] = {1, 5, 10, 25, 100};
    int k = 0;
    while (amount > 0 && k < 8) {
        int d = art::chipDenom(amount);
        if (k == 7) {                                    // last flight carries the rest
            fly(fromX, fromY, x, y, (uint8_t)d, seat, amount, CHIP_IN, k * stagger);
            break;
        }
        fly(fromX + k * 3, fromY, x, y, (uint8_t)d, seat, V[d], CHIP_IN, k * stagger);
        amount -= V[d];
        k++;
    }
}

static void sweep(const Round &r, uint8_t seat, uint8_t kind) {
    if (dispBet[seat] <= 0) return;
    int x, y;
    stackPos(r, seat, x, y);
    int tx = kind == STACK_TO_TRAY ? TRAY_X + TRAY_W / 2 : 8;
    int ty = kind == STACK_TO_TRAY ? RAIL_Y + RAIL_H + 10 : 140;
    fly(x, y, tx, ty, 0, seat, dispBet[seat], kind, 0, kind == STACK_TO_TRAY ? 16 : 18);
    dispBet[seat] = 0;
    audio::sfx(Sfx::Whoosh);
}

static int seatCx(const Round &r, uint8_t seat) {
    int x0, y, pitch;
    handGeom(r, seat, x0, y, pitch);
    const Hand &h = seat ? r.hands[seat - 1] : r.dealer;
    return x0 + (CARD_W + pitch * (h.count ? h.count - 1 : 0)) / 2;
}

// ---------------------------------------------------------------------------
// Events -> motion, sound, speech
// ---------------------------------------------------------------------------
static uint8_t exprFor(uint8_t f) {
    static const uint8_t M[5] = {table::E_NORMAL, table::E_ANGRY, table::E_RAISED, table::E_SMILE, table::E_SURPRISED};
    return f < 5 ? M[f] : table::E_NORMAL;
}

void reset(const Round &r) {
    memset(views, 0, sizeof views);
    memset(ghosts, 0, sizeof ghosts);
    memset(flies, 0, sizeof flies);
    memset(dispBet, 0, sizeof dispBet);
    memset(collectT, 0, sizeof collectT);
    shown = r.purse; pending = 0;
    bubLine = 0xFF; face = F_NORMAL; shuffleT = 0; peekT = 0;
    fx::clear();
    invalidate();
}

void dismissBubble() { bubHold = 0; bubChars = bubLen; bubLine = 0xFF; }
int32_t shownPurse() { return shown; }

void onEvents(Round &r) {
    Event e;
    char buf[12];
    while (r.popEvent(e)) {
        switch (e.type) {
            case Ev::Deal: {
                CardView &v = views[e.a][e.b];
                v.live = true; v.t = 0; v.flip = 0; v.up = false;
                v.sx = v.x = (int16_t)((SHOE_X - 4) << 4);        // slides out from under the shoe,
                v.sy = v.y = (int16_t)((RAIL_Y + RAIL_H + 1) << 4); // below the rail: the wall band stays clean
                audio::sfx(Sfx::Deal);
                break;
            }
            case Ev::Reveal: audio::sfx(Sfx::Reveal); break;
            case Ev::Shuffle:
                shuffleT = r.decks() > 1 ? 70 : 20;
                audio::sfx(r.decks() > 1 ? Sfx::Shuffle : Sfx::Whoosh);
                break;
            case Ev::BetAdd:
                if (e.b == 0xFF) {
                    chipsIn(r, e.a, e.amount, 20, 132, 2);          // last hand's bet, again
                } else {                                            // from the chip button
                    int x, y; stackPos(r, e.a, x, y);
                    fly(9 + e.b * 17, 118, x, y, e.b, e.a, e.amount, CHIP_IN);
                }
                audio::sfx(Sfx::Chip);
                break;
            case Ev::BetRemove: {
                int x, y; stackPos(r, e.a, x, y);
                dispBet[e.a] -= e.amount;
                if (dispBet[e.a] < 0) dispBet[e.a] = 0;
                fly(x, y, 20, 136, (uint8_t)art::chipDenom(e.amount), e.a, e.amount, CHIP_OUT);
                audio::sfx(Sfx::Chip);
                break;
            }
            case Ev::Insure:
                chipsIn(r, 0, e.amount, 30, 132, 3);
                audio::sfx(Sfx::Chip);
                break;
            case Ev::Split:
                views[2][0] = views[1][1];
                views[1][1].live = false;
                chipsIn(r, 2, e.amount, 30, 132, 3);
                fx::burst(fx::SPARK, 48, PLAYER_CARDS_Y + 10, 10, 40, FX_A);
                audio::sfx(Sfx::Split);
                break;
            case Ev::Double:
                chipsIn(r, e.a, e.amount, 30, 132, 3);
                audio::sfx(Sfx::Double);
                break;
            case Ev::Bust: {
                int cx = seatCx(r, e.a);
                fx::banner("BUST!", fx::B_RED, PLAYER_CARDS_Y + 8);
                fx::shake(12, 3);
                fx::burst(fx::SPARK, cx, PLAYER_CARDS_Y + 12, 12, 48, RED);
                pal::flash(WHITE, 0xFBB, 6);
                sweep(r, e.a, STACK_TO_TRAY);
                fmtMoney(buf, -(int32_t)e.amount);
                fx::floatText(buf, BET_CX, BET_CY - 18, RED);
                audio::sfx(Sfx::Bust);
                break;
            }
            case Ev::Natural: {
                int cx = seatCx(r, e.a);
                fx::banner("BLACKJACK!", fx::B_RAINBOW, 64, 90);
                fx::fountain(fx::CONFETTI, cx, PLAYER_CARDS_Y, 24);
                fx::burst(fx::STAR, cx, PLAYER_CARDS_Y + 10, 10, 50, FX_A);
                audio::sfx(Sfx::Blackjack);
                audio::led(audio::LED_TRIPLE);
                break;
            }
            case Ev::Hand21:
                fx::banner("21!", fx::B_GOLD, PLAYER_CARDS_Y + 8, 50);
                fx::burst(fx::SPARK, seatCx(r, e.a), PLAYER_CARDS_Y + 12, 10, 40, GOLD);
                audio::sfx(Sfx::Reveal);
                break;
            case Ev::PeekStart:
                peekT = 48;
                break;
            case Ev::PeekEnd:
                peekT = 0;
                if (e.a) {
                    fx::banner("DEALER BJ", fx::B_RED, 62, 70);
                    fx::shake(10, 2);
                    audio::sfx(Sfx::Lose);
                }
                break;
            case Ev::Settle: {
                int x, y; stackPos(r, e.a, x, y);
                int32_t stake = r.hands[e.a - 1].bet;
                int32_t win = e.amount - stake;
                if (e.b == R_WIN || e.b == R_BLACKJACK) {
                    pending += e.amount;
                    chipsIn(r, e.a, win, TRAY_X + 6, RAIL_Y + RAIL_H + 2, 3);
                    collectT[e.a] = 40;
                    fmtMoney(fmtStr(buf, "+"), win);
                    fx::floatText(buf, x, y - 18, GOLD);
                    if (e.b == R_BLACKJACK) {
                        fx::banner("3 TO 2!", fx::B_RAINBOW, PLAYER_CARDS_Y + 8, 70);
                        audio::sfx(Sfx::Blackjack);
                    } else {
                        fx::banner("WIN!", fx::B_GOLD, PLAYER_CARDS_Y + 8, 60);
                        audio::sfx(Sfx::Win);
                    }
                    fx::fountain(fx::CONFETTI, seatCx(r, e.a), PLAYER_CARDS_Y + 4, 14);
                    audio::led(audio::LED_BLINK);
                } else if (e.b == R_PUSH) {
                    pending += e.amount;
                    collectT[e.a] = 30;
                    fx::banner("PUSH", fx::B_CYAN, PLAYER_CARDS_Y + 8, 50);
                    audio::sfx(Sfx::Push);
                } else {
                    sweep(r, e.a, STACK_TO_TRAY);
                    fmtMoney(buf, -stake);
                    fx::floatText(buf, x, y - 18, RED);
                    audio::sfx(Sfx::Lose);
                }
                break;
            }
            case Ev::Insurance:
                if (e.b) {
                    pending += e.amount;
                    chipsIn(r, 0, e.amount - dispBet[0], TRAY_X + 6, RAIL_Y + RAIL_H + 2, 3);
                    collectT[0] = 36;
                    audio::sfx(Sfx::Insurance);
                    fx::floatText("INSURED", INS_CX - 8, INS_CY - 14, CYAN);
                } else {
                    sweep(r, 0, STACK_TO_TRAY);
                }
                break;
            case Ev::DealerBust:
                fx::banner("BUST!", fx::B_GOLD, DEALER_CARDS_Y + 12, 60);
                fx::burst(fx::SPARK, seatCx(r, 0), DEALER_CARDS_Y + 12, 12, 40, GOLD);
                break;
            case Ev::Say:
                bubLine = e.a;
                bubChars = 0;
                bubLen = (uint8_t)strlen(r.lineText(e.a));
                bubHold = 100;
                face = e.b;
                break;
            case Ev::Cursor: audio::sfx(Sfx::Cursor); break;
            case Ev::Deny: audio::sfx(Sfx::Deny); break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Per-frame animation
// ---------------------------------------------------------------------------
void update(const Round &r) {
    bool anyFlight = false;
    int lookX = -1;
    for (uint8_t s = 0; s < 3; s++) {
        const Hand &h = s ? r.hands[s - 1] : r.dealer;
        for (uint8_t i = 0; i < 12; i++) {
            CardView &v = views[s][i];
            if (!v.live) continue;
            if (i >= h.count || (s == 2 && r.nHands < 2)) {     // cleared: sweep away
                for (auto &g : ghosts) if (!g.t) { g.x = v.x; g.y = v.y; g.t = 14; break; }
                v.live = false;
                continue;
            }
            int tx, ty;
            cardTarget(r, s, i, tx, ty);
            tx <<= 4; ty <<= 4;
            if (v.t < FLIGHT) {
                v.t++;
                int e = fx::ease(fx::OUT_CUBIC, v.t, FLIGHT);
                v.x = (int16_t)(v.sx + (((tx - v.sx) * e) >> 8));
                v.y = (int16_t)(v.sy + (((ty - v.sy) * e) >> 8) - ((fx::isin(v.t * 128 / FLIGHT) * 3) >> 4));
                if (v.y < ((RAIL_Y + RAIL_H) << 4)) v.y = (int16_t)((RAIL_Y + RAIL_H) << 4);   // stay under the rail
                anyFlight = true;
                lookX = v.x >> 4;
                if (v.t == FLIGHT) {
                    fx::burst(fx::DUST, (tx >> 4) + 11, (ty >> 4) + 27, 4, 20, FELT_LT);
                }
            } else {
                v.x = (int16_t)(v.x + (tx - v.x) / 3);
                v.y = (int16_t)(v.y + (ty - v.y) / 3);
                if (v.x != tx && (tx - v.x < 3 && v.x - tx < 3)) v.x = (int16_t)tx;
                if (v.y != ty && (ty - v.y < 3 && v.y - ty < 3)) v.y = (int16_t)ty;
            }
            bool wantUp = !(s == 0 && i == 1 && !r.holeShown);
            if (v.t >= FLIGHT && !v.flip && v.up != wantUp) { v.flip = 1; audio::sfx(Sfx::Flip); }
            if (v.flip) {
                v.flip++;
                if (v.flip == FLIP / 2) v.up = wantUp;
                if (v.flip > FLIP) v.flip = 0;
            }
        }
    }
    for (auto &g : ghosts) if (g.t) { g.t--; g.x -= 56; }          // swept off to the left

    for (auto &f : flies) {
        if (!f.T) continue;
        f.t++;
        if (f.t >= f.T) {
            if (f.kind == CHIP_IN) { dispBet[f.seat] += f.value; }
            if (f.kind == STACK_TO_PLAYER) { pending -= f.value; purseFlash = 24; audio::sfx(Sfx::Coin); }
            f.T = 0;
        }
    }
    for (uint8_t s = 0; s < 3; s++) {
        if (!collectT[s]) continue;
        bool landing = false;
        for (auto &f : flies) if (f.T && f.seat == s && f.kind == CHIP_IN) landing = true;
        if (landing) continue;
        if (--collectT[s] == 0) sweep(r, s, STACK_TO_PLAYER);
    }

    // Purse rolls toward the money that has actually come home.
    int32_t target = r.purse - pending;
    int32_t d = target - shown;
    if (d) {
        int32_t step = d / 5;
        if (!step) step = d > 0 ? 1 : -1;
        shown += step;
        // The counter's ticks (and the typewriter's, below) never cut off an effect.
        if (d > 0 && (shown & 3) == 0 && !audio::playing()) audio::blip(3000 + (uint16_t)((shown * 7) & 511), 8);
    }
    if (purseFlash) purseFlash--;

    // Speech bubble typewriter, dealer's face.
    if (bubLine != 0xFF) {
        if (bubChars < bubLen) {
            bubChars++;
            if ((bubChars & 1) && !audio::playing()) audio::blip((uint16_t)(1900 + (bubChars * 97) % 700), 12);
        } else if (bubHold) {
            bubHold--;
        } else {
            bubLine = 0xFF;
            face = F_NORMAL;
        }
    }
    if (blinking) blinking--;
    else if (--blinkT == 0) { blinking = 6; blinkT = (uint8_t)fx::rndRange(90, 220); }
    look = lookX < 0 ? 1 : (lookX < 30 ? 0 : (lookX > 40 ? 2 : 1));
    if (peekT) { peekT--; look = 2; if ((peekT % 12) == 11) audio::sfx(Sfx::Peek); }
    if (shuffleT) shuffleT--;
    (void)anyFlight;
    fx::update();
}

bool busy() {
    for (uint8_t s = 0; s < 3; s++)
        for (uint8_t i = 0; i < 12; i++) {
            const CardView &v = views[s][i];
            if (v.live && (v.t < FLIGHT || v.flip)) return true;
        }
    for (auto &f : flies) if (f.T) return true;
    for (uint8_t s = 0; s < 3; s++) if (collectT[s]) return true;
    return shuffleT > 0;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void speechBubble(const char *src, int typed) {
    int x = BUBBLE_X, y = BUBBLE_Y, w = BUBBLE_W, h = BUBBLE_H;
    panel(x, y, w, h, 4, WHITE, INK);
    // Tail toward the dealer's mouth.
    for (int i = 0; i < 5; i++) {
        gfx_hline(x - 5 + i, y + 22 + i, 6 - i, WHITE);
        gfx_pixel(x - 6 + i, y + 22 + i, INK);
    }
    gfx_vline(x, y + 21, 4, WHITE);
    // Typewriter: each line is centred on its full width, then only the
    // characters typed so far are drawn.
    int lines = 1;
    for (const char *p = src; *p; p++) if (*p == '\n') lines++;
    int ty = y + h / 2 - (lines * 7) / 2 + 1;
    for (const char *p = src; *p;) {
        const char *e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        char line[20];
        int n = len < 19 ? len : 19;
        memcpy(line, p, n);
        line[n] = 0;
        int lx = x + w / 2 - text35Width(line) / 2;
        int show = typed < n ? (typed > 0 ? typed : 0) : n;
        line[show] = 0;
        text35(lx, ty, line, INK);
        typed -= len + 1;
        ty += 7;
        if (!e) break;
        p = e + 1;
    }
}

static void bubble(const Round &r) { speechBubble(r.lineText(bubLine), bubChars); }

static void badgeFor(const Round &r, uint8_t seat) {
    const Hand &h = seat ? r.hands[seat - 1] : r.dealer;
    if (!h.count || r.opt.totals) return;
    // Only count cards that have landed face up, so the badge never spoils.
    Hand vis; vis.clear();
    for (uint8_t i = 0; i < h.count; i++) {
        const CardView &v = views[seat][i];
        if (v.live && v.t >= FLIGHT && v.up && !v.flip) vis.cards[vis.count++] = h.cards[i];
    }
    if (!vis.count) return;
    char buf[8];
    uint8_t bg = INK, fg = WHITE;
    int best = vis.best(), hard = vis.hard();
    if (hard > 21) { strcpy(buf, "BUST"); bg = RED; fg = WHITE; }
    else if (seat && vis.count == h.count && h.natural()) { strcpy(buf, "BJ"); bg = FX_A; fg = INK; }
    else if (best == 21) { strcpy(buf, "21"); bg = GOLD; fg = INK; }
    else if (best != hard) { char *p = fmtInt(buf, hard); *p++ = '/'; fmtInt(p, best); }
    else fmtInt(buf, best);
    int x0, y, pitch;
    handGeom(r, seat, x0, y, pitch);
    int w = art::badgeWidth(buf);
    int bx = x0 - w - 2, by = y + 8;
    if (seat && r.nHands == 2) { bx = x0 + 1; by = y - 12; }          // split: above each hand
    else if (bx < 1) {                                   // no room on the left: try the right
        int right = x0 + CARD_W + pitch * (h.count - 1) + 3;
        if (seat == 0 && right + w < SHOE_X + 20) bx = right;
        else { bx = 1; by = y + 1; }
    }
    if (seat && r.nHands == 2 && seat - 1 == r.active &&
        (r.phase == Phase::PlayHand || r.phase == Phase::DoubleUp)) bg = bg == INK ? NAVY : bg;
    art::badge(bx, by, buf, bg, fg);
}

static void drawHand(const Round &r, uint8_t seat) {
    const Hand &h = seat ? r.hands[seat - 1] : r.dealer;
    bool dim = seat && r.nHands == 2 && seat - 1 != r.active &&
               (r.phase == Phase::PlayHand || r.phase == Phase::DoubleUp || r.phase == Phase::Bust);
    int x0 = 128, x1 = 0, y0 = 128, y1 = 0;
    for (uint8_t i = 0; i < h.count && i < 12; i++) {
        const CardView &v = views[seat][i];
        if (!v.live) continue;
        int x = v.x >> 4, y = v.y >> 4;
        if (seat == 0 && i == 1 && peekT) y -= (peekT > 10 && peekT < 40) ? 3 : 1;   // lift to peek
        bool top = i == h.count - 1 || (i + 1 < h.count && !views[seat][i + 1].live);
        if (v.t >= FLIGHT && seat && sideways(r, seat, i) && !v.flip && v.up) {
            art::cardSideways(x, y, h.cards[i]);
        } else {
            int w = CARD_W;
            if (v.flip) {
                int half = FLIP / 2;
                w = v.flip <= half ? CARD_W * (half - v.flip + 1) / (half + 1) : CARD_W * (v.flip - half) / (half + 1);
                if (w < 2) w = 2;
            }
            art::card(x, y, h.cards[i], v.up, w, top || v.t < FLIGHT);
        }
        if (x < x0) x0 = x;
        if (x + 28 > x1) x1 = x + 28;
        if (y < y0) y0 = y;
        if (y + 29 > y1) y1 = y + 29;
    }
    if (dim && x1 > x0) art::dimCard(x0, y0, x1 - x0, y1 - y0);
}

// ---------------------------------------------------------------------------
// Band-level redraw. The framebuffer survives between frames, and most of
// the table is still most of the time, so each band (wall 0..45, felt
// 46..111) is redrawn only when the state it shows has changed or something
// moving has touched it, this frame or the last. Every frame is still
// flushed, so palette animation (FX_A/FX_B, fades) keeps running.
// ---------------------------------------------------------------------------
struct Sig {
    uint32_t h = 2166136261u;
    void add(int32_t v) { h = (h ^ (uint32_t)v) * 16777619u; }
};

static uint32_t sigWall = 0, sigFelt = 0;
static int16_t prevLo = 999, prevHi = -1;
static bool forceAll = true;

void invalidate() { forceAll = true; }

static int flyY(const Fly &f) {
    int e = fx::ease(fx::OUT_CUBIC, f.t, f.T);
    int y = f.y0 + (((f.y1 - f.y0) * e) >> 8) - ((fx::isin(f.t * 128 / f.T) * 10) >> 8);
    int top = RAIL_Y + RAIL_H + 1 + (f.kind >= STACK_TO_TRAY ? 8 : 1);   // never over the rail
    return y < top ? top : y;
}

static void movingRows(int &lo, int &hi) {
    fx::activeRows(lo, hi);
    auto span = [&](int a, int b) { if (a < lo) lo = a; if (b > hi) hi = b; };
    for (uint8_t s = 0; s < 3; s++)
        for (uint8_t i = 0; i < 12; i++) {
            const CardView &v = views[s][i];
            if (v.live && (v.t < FLIGHT || v.flip)) span(v.y >> 4, (v.y >> 4) + 30);
        }
    for (auto &g : ghosts) if (g.t) span(g.y >> 4, (g.y >> 4) + 30);
    for (auto &f : flies) if (f.T && f.t >= 0) { int y = flyY(f); span(y - (f.kind >= STACK_TO_TRAY ? 8 : 1), y + 5); }
    if (peekT) span(DEALER_CARDS_Y - 4, DEALER_CARDS_Y + 30);
}

static int16_t curLo = 999, curHi = -1;

bool rowsMoving(int a, int b) {
    return (curHi >= curLo && curLo <= b && curHi >= a) || (prevHi >= prevLo && prevLo <= b && prevHi >= a);
}

bool render(const Round &r, uint32_t frame) {
    (void)frame;
    uint8_t expr = exprFor(face);
    if (bubLine != 0xFF && bubChars < bubLen && ((frame >> 2) & 1)) expr = table::E_TALK;
    if (blinking) expr = table::E_BLINK;

    Sig w;
    w.add(expr); w.add(look); w.add(bubLine); w.add(bubChars); w.add(shown);
    w.add(dispBet[1] + dispBet[2]); w.add(purseFlash ? 1 + ((purseFlash >> 2) & 1) : 0);
    w.add(r.shoeLeftPercent()); w.add(shuffleT); w.add(r.opt.dealer);
    Sig f;
    f.add((int32_t)r.phase); f.add(r.nHands); f.add(r.active); f.add(r.opt.rules); f.add(r.holeShown);
    for (uint8_t s = 0; s < 3; s++) {
        f.add(dispBet[s]);
        const Hand &h = s ? r.hands[s - 1] : r.dealer;
        f.add(h.count);
        for (uint8_t i = 0; i < h.count && i < 12; i++) {
            const CardView &v = views[s][i];
            f.add(v.live | (v.up << 1) | (v.flip << 2) | (h.cards[i] << 8) | (v.t << 16));
            f.add(v.x | (v.y << 16));
        }
    }

    int lo, hi;
    movingRows(lo, hi);
    bool moving = hi >= lo, wasMoving = curHi >= curLo;           // cur* = last frame here
    auto touched = [&](int a, int b) {
        return (moving && lo <= b && hi >= a) || (wasMoving && curLo <= b && curHi >= a);
    };
    bool drawWall = forceAll || w.h != sigWall || touched(0, RAIL_Y + RAIL_H - 1);
    bool drawFelt = forceAll || f.h != sigFelt || touched(RAIL_Y + RAIL_H, TRIM_Y);
    forceAll = false;
    sigWall = w.h; sigFelt = f.h;
    prevLo = curLo; prevHi = curHi;
    curLo = (int16_t)lo; curHi = (int16_t)hi;

    dbg::profStart();
    if (drawWall) {
        table::wall(frame);
        table::dealer(expr, look, r.opt.dealer != 0);
        if (bubLine != 0xFF) bubble(r);
        else table::plaque(shown, dispBet[1] + dispBet[2], purseFlash);
        table::shoe(r.shoeLeftPercent(), shuffleT);
        table::rail(frame);
    }
    dbg::prof(0);
    if (drawFelt) {
        gfx_fillRect(0, RAIL_Y + RAIL_H, 128, TRIM_Y - RAIL_Y - RAIL_H, FELT);
        table::felt(r);
        dbg::prof(1);
        if (r.phase == Phase::InitBet) gfx_ellipse(BET_CX, BET_CY, BET_RX + 1, BET_RY + 1, FX_B);
        drawHand(r, 0);
        badgeFor(r, 0);
        dbg::prof(2);
        for (uint8_t s = 1; s <= r.nHands; s++) { drawHand(r, s); badgeFor(r, s); }
        dbg::prof(3);
        for (uint8_t s = 0; s < 3; s++) {
            if (dispBet[s] <= 0) continue;
            int x, y; stackPos(r, s, x, y);
            art::chipStack(x, y, dispBet[s], 9);
        }
        gfx_hline(0, TRIM_Y, 128, GOLD);
        dbg::prof(4);
    }
    return drawWall || drawFelt;
}

void overlay() {
    for (auto &fl : flies) {
        if (!fl.T || fl.t < 0) continue;
        int e = fx::ease(fx::OUT_CUBIC, fl.t, fl.T);
        int x = fl.x0 + (((fl.x1 - fl.x0) * e) >> 8);
        int y = flyY(fl);
        if (fl.kind == CHIP_IN || fl.kind == CHIP_OUT) art::chip(x, y, fl.denom, true);
        else art::chipStack(x, y, fl.value, 4);
    }
    for (auto &g : ghosts) if (g.t) art::card(g.x >> 4, g.y >> 4, 0, false);

    dbg::prof(5);
    fx::drawParticles();
    fx::drawFloats();
    dbg::prof(6);
    fx::drawBanner();
    dbg::prof(7);
    fx::applyShake(0, TRIM_Y - 1);
    dbg::prof(8);
}

}  // namespace present
