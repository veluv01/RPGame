// The play screen (Stage.h): the table's events turned into cards and chips
// in flight, sounds and narration, and the drawing of the table, redrawn only
// when something on it changed.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library)
#include <RPGame.h>
#include <string.h>
#include "Stage.h"
#include "Layout.h"
#include "CardArt.h"
#include "Bar.h"
#include "Hand.h"
#include "Fx.h"
#include "Sounds.h"
#include "src/assets/Assets.h"

namespace stage {

using namespace lay;
using art::CARD_W;
using art::MINI_W;

enum { FLIGHT = 12, FLIP = 8, STAGGER = 4, BOARD_STAGGER = 9, ANN_FRAMES = 150, ACT_FRAMES = 80 };
enum : uint8_t { BOARD = 4, BURN = 5 };

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
struct CardView { int16_t x, y, sx, sy; int8_t t; uint8_t flip, live, up, hold; };
static CardView views[5][7];         // seats 0..3, 4: the board
static uint8_t dealClock;

struct Ghost { int16_t x, y, sx, sy; uint8_t t; };   // a card on its way to the muck
static Ghost ghosts[12];

enum FlyKind : uint8_t { TO_BET, TO_POT, TO_SEAT, BACK };
struct Fly { int16_t x0, y0, x1, y1; int8_t t; uint8_t T, kind, seat; int32_t value; };
static Fly flies[16];
static int32_t dispBet[4], betTarget[4], dispPot, dispStack[4];
static uint8_t stackFlash[4];

static uint8_t lastAct[4], actT[4], drewN[4];
static uint8_t acting = 0xFF;        // whose turn it is
static uint8_t joinT[4];

static char annText[44];             // the narration: words split by '|'
static uint8_t annCol[8], annN, annT;

static uint8_t hiSeat = 0xFF, hiHole, hiBoard;    // the winning five
static int16_t gx16, gy16, bx16, by16;            // the glove; the dealer button
static uint8_t tapT, shuffleT, thinkT;
static bool forceDraw = true;

// ---------------------------------------------------------------------------
// Where things go
// ---------------------------------------------------------------------------
static bool community(const Table &t) { return t.game == HOLDEM || t.game == OMAHA; }

static void slot(const Table &t, uint8_t s, uint8_t i, int &x, int &y) {
    if (s == BOARD) { x = boardX(i); y = BOARD_Y; return; }
    uint8_t g = t.game, n = VARIANTS[g].hole;
    if (s == YOU) {
        static const uint8_t PITCH[GAMES] = {24, 16, 17, 11};
        uint8_t p = PITCH[g];
        x = 45 - (CARD_W + p * (n - 1)) / 2 + i * p;
        y = HAND_Y;
        if (g == STUD && ((t.seats[YOU].up >> i) & 1) && t.seats[YOU].up != 0x7F) y -= 3;
        if (t.phase == Phase::DrawHuman && ((t.drawMask >> i) & 1)) y -= 5;
        if (hiSeat == YOU && ((hiHole >> i) & 1)) y -= 2;
        return;
    }
    int bx = blockX(s);
    y = MINI_Y + (hiSeat == s && ((hiHole >> i) & 1) ? -2 : 0);
    if (g == STUD) {
        // Down cards tucked at the left, each up card showing its rank and
        // suit; all seven fanned out evenly once the hand is shown.
        static const uint8_t OFF[7] = {0, 3, 10, 17, 24, 31, 6};
        x = bx + 1 + (t.seats[s].up == 0x7F ? i * 5 : OFF[i]);
        return;
    }
    static const uint8_t MP[GAMES] = {11, 7, 8, 0};
    x = bx + 21 - (MINI_W + MP[g] * (n - 1)) / 2 + i * MP[g];
}

static void seatPos(uint8_t s, int &x, int &y) {
    if (s == YOU) { x = PANEL_X + 8; y = PANEL_Y + 9; }
    else { x = blockX(s) + 6; y = PLATE_Y + 3; }
}
static void betPos(uint8_t s, int &x, int &y) {
    if (s == YOU) { x = PANEL_X + 6; y = PANEL_Y + 20; }
    else { x = blockX(s) + 6; y = BET_Y + 3; }
}
static void potPos(const Table &t, int &x, int &y) {
    if (community(t)) { x = 70; y = POT_Y + 3; }
    else { x = 42; y = 63; }
}
static void buttonPos(uint8_t s, int &x, int &y) {
    if (s == YOU) { x = PANEL_X + 29; y = PANEL_Y + 5; }
    else { x = blockX(s) + 37; y = BET_Y + 3; }
}

static bool wantUp(const Table &t, uint8_t s, uint8_t i) {
    return s == BOARD || s == YOU || ((t.seats[s].up >> i) & 1);
}

const char *streetName(const Table &t) {
    switch (t.phase) {
        case Phase::Showdown: case Phase::Award: return "SHOWDOWN";
        case Phase::Draw: case Phase::DrawThink: case Phase::DrawHuman: return "DRAW";
        default: break;
    }
    static const char *const COMMUNITY[4] = {"PRE-FLOP", "FLOP", "TURN", "RIVER"};
    static const char *const STUD_ST[5] = {"3RD ST", "4TH ST", "5TH ST", "6TH ST", "7TH ST"};
    if (community(t)) return COMMUNITY[t.street & 3];
    if (t.game == STUD) return STUD_ST[t.street < 5 ? t.street : 4];
    return t.street ? "FINAL" : "PRE-DRAW";
}

static const char *nameOf(const Table &t, uint8_t s) {
    return s == YOU ? "YOU" : art::SEAT_NAME[t.seats[s].colour % 6];
}

// ---------------------------------------------------------------------------
// The narration plate
// ---------------------------------------------------------------------------
static char *annP;
static void annBegin() { annP = annText; annN = 0; annT = 0; }
static void annWord(const char *w, uint8_t c) {
    if (annN >= 8 || annP + strlen(w) + 2 > annText + sizeof annText) return;
    if (annN) *annP++ = '|';
    annP = fmtStr(annP, w);
    annCol[annN++] = c;
}
static void annMoney(int32_t v, uint8_t c) { char b[12]; fmtMoney(b, v); annWord(b, c); }

// ---------------------------------------------------------------------------
// Actors
// ---------------------------------------------------------------------------
static void ghost(int x, int y) {
    for (auto &g : ghosts)
        if (!g.t) { g.sx = g.x = (int16_t)(x << 4); g.sy = g.y = (int16_t)(y << 4); g.t = 1; return; }
}

static void muck(uint8_t s, uint8_t mask) {
    for (uint8_t i = 0; i < 7; i++) {
        CardView &v = views[s][i];
        if (!v.live || !((mask >> i) & 1)) continue;
        ghost(v.x >> 4, v.y >> 4);
        v.live = 0;
    }
}

static void fly(int x0, int y0, int x1, int y1, uint8_t kind, uint8_t seat, int32_t value, int delay, uint8_t T = 14) {
    for (auto &f : flies) {
        if (f.T) continue;
        f.x0 = (int16_t)x0; f.y0 = (int16_t)y0; f.x1 = (int16_t)x1; f.y1 = (int16_t)y1;
        f.t = (int8_t)-delay; f.T = T; f.kind = kind; f.seat = seat; f.value = value;
        return;
    }
    // Pool full: land it now.
    if (kind == TO_BET) dispBet[seat] += value;
    if (kind == TO_POT) dispPot += value;
}

// Chips from a seat to its bet spot (a few, staggered), or into the pot.
static void chipsOut(const Table &t, uint8_t s, int32_t amount, bool toPot) {
    if (amount <= 0) return;
    int x0, y0, x1, y1;
    seatPos(s, x0, y0);
    if (toPot) potPos(t, x1, y1); else betPos(s, x1, y1);
    int32_t part = amount / 3 > 0 ? amount / 3 : amount;
    for (int k = 0; k < 3 && amount > 0; k++) {
        int32_t v = k == 2 || part >= amount ? amount : part;
        fly(x0, y0, x1, y1, toPot ? TO_POT : TO_BET, s, v, k * 3);
        amount -= v;
    }
}

static void newHand() {
    for (uint8_t s = 0; s < 5; s++) muck(s, 0x7F);
    memset(dispBet, 0, sizeof dispBet);
    memset(betTarget, 0, sizeof betTarget);
    memset(actT, 0, sizeof actT);
    memset(drewN, 0, sizeof drewN);
    dispPot = 0;
    hiSeat = 0xFF;
    acting = 0xFF;
    dealClock = 6;                      // the muck clears first
}

void reset() {
    memset(views, 0, sizeof views);
    memset(ghosts, 0, sizeof ghosts);
    memset(flies, 0, sizeof flies);
    newHand();
    memset(dispStack, 0, sizeof dispStack);
    memset(joinT, 0, sizeof joinT);
    annN = 0; annT = ANN_FRAMES;
    bx16 = (int16_t)((PANEL_X + 29) << 4); by16 = (int16_t)((PANEL_Y + 5) << 4);
    fx::clear();
    forceDraw = true;
}

void invalidate() { forceDraw = true; }

// ---------------------------------------------------------------------------
// Events -> motion, sound, words
// ---------------------------------------------------------------------------
static void announceWin(const Table &t, const Event &e) {
    uint8_t s = e.a;
    bool you = s == YOU;
    int x, y;
    seatPos(s, x, y);
    int px, py;
    potPos(t, px, py);
    fly(px, py, x, y, TO_SEAT, s, e.amount, 0, 18);
    dispPot -= e.amount;
    if (dispPot < 0) dispPot = 0;
    annBegin();
    annWord(nameOf(t, s), you ? GOLD : WHITE);
    char buf[24];
    if (e.c == 0xFF) {                                    // everyone else folded
        annWord(you ? " WIN " : " WINS ", SILVER);
        annMoney(e.amount, GOLD);
    } else {
        annWord(you ? " WIN: " : " WINS: ", SILVER);
    }
    if (!you) {
        fmtMoney(fmtStr(buf, "+"), e.amount);
        fx::floatText(buf, x + 14, y + 12, GOLD);
    }
    if (e.c != 0xFF) {
        // The winning five: lift them and edge them in the rainbow.
        const Seat &p = t.seats[s];
        uint32_t sc = hand::bestFive(p.cards, p.n, t.board, t.nBoard, VARIANTS[t.game].exactTwo, hiHole, hiBoard);
        hiSeat = s;
        hand::describe(buf, sc);
        if (text35Width(annText) + text35Width(buf) > 118) fmtStr(buf, hand::catName(e.c));
        annWord(buf, CYAN);
        if (e.b == 0) {                                   // the main pot: the big moment
            if (e.c >= hand::QUADS) {
                char ban[16];
                fmtStr(fmtStr(ban, e.c == hand::STRAIGHT_FLUSH && hand::topRank(sc) == RA ? "ROYAL FLUSH" :
                                   hand::catName(e.c)), e.c == hand::STRAIGHT_FLUSH ? "" : "!");
                fx::banner(ban, fx::B_RAINBOW, 60, 110);
                fx::fountain(fx::CONFETTI, 40, 74, 18);
                fx::fountain(fx::CONFETTI, 88, 74, 18);
                audio::led(audio::LED_PARTY);
            } else if (e.c >= hand::STRAIGHT) {
                char ban[16];
                fmtStr(fmtStr(ban, hand::catName(e.c)), "!");
                fx::banner(ban, fx::B_GOLD, 60, 80);
            } else if (you) {
                fx::banner("YOU WIN!", fx::B_GOLD, 60, 70);
            }
        }
    }
    if (you) {
        fmtMoney(fmtStr(buf, "+"), e.amount);
        fx::floatText(buf, PANEL_X + 17, PANEL_Y - 4, GOLD);
        fx::fountain(fx::COIN, PANEL_X + 17, PANEL_Y + 6, 10);
        audio::sfx(e.c != 0xFF && e.c >= hand::STRAIGHT ? Sfx::BigWin : Sfx::Win);
        if (e.c < hand::QUADS) audio::led(audio::LED_TRIPLE);
    } else {
        audio::sfx(t.live(YOU) && e.c != 0xFF ? Sfx::Lose : Sfx::Coin);
    }
}

static const char *const VERB3[] = {"", " FOLDS", " CHECKS", " CALLS ", " BETS ", " RAISES TO ", " GOES ALL IN ",
                                    " SMALL BLIND ", " BIG BLIND ", " ANTES ", " BRINGS IN ", " COMPLETES TO ", ""};
static const char *const VERB2[] = {"", " FOLD", " CHECK", " CALL ", " BET ", " RAISE TO ", " GO ALL IN ",
                                    " SMALL BLIND ", " BIG BLIND ", " ANTE ", " BRING IN ", " COMPLETE TO ", ""};

void onEvents(Table &t) {
    Event e;
    while (t.popEvent(e)) {
        uint8_t s = e.a;
        switch (e.type) {
            case Ev::Shuffle:
                audio::sfx(Sfx::Shuffle);
                shuffleT = 30;
                dealClock = 32;                   // the riffle, then the deal
                break;
            case Ev::Button:
                newHand();
                break;
            case Ev::Join:
                joinT[s] = 24;
                dispStack[s] = e.amount;
                annBegin();
                annWord(art::SEAT_NAME[e.b % 6], art::SEAT_COLOUR[e.b % 6]);
                annWord(" SITS DOWN", SILVER);
                audio::sfx(Sfx::Coin);
                break;
            case Ev::Bust: {
                int x, y;
                seatPos(s, x, y);
                fx::burst(fx::SPARK, x + 14, y + 2, 14, 40, RED);
                annBegin();
                annWord(nameOf(t, s), WHITE);
                annWord(" IS BUSTED", RED);
                audio::sfx(Sfx::Bust);
                break;
            }
            case Ev::Post:
                lastAct[s] = e.b; actT[s] = ACT_FRAMES;
                if (e.b == A_ANTE) chipsOut(t, s, e.amount, true);
                else { chipsOut(t, s, e.amount, false); betTarget[s] += e.amount; }
                if (e.b == A_BRINGIN) {
                    annBegin();
                    annWord(nameOf(t, s), WHITE);
                    annWord(s == YOU ? VERB2[A_BRINGIN] : VERB3[A_BRINGIN], SILVER);
                    annMoney(e.amount, GOLD);
                }
                audio::sfx(Sfx::Chip);
                break;
            case Ev::Deal: {
                if (s == BURN) { ghost(DECK_X, DECK_Y); break; }
                CardView &v = views[s][e.b];
                v.live = 1; v.flip = 0; v.up = 0;
                v.t = (int8_t)-dealClock;
                v.hold = (s == BOARD && e.b >= 3) ? 14 : 0;   // the turn and the river: a beat before they turn
                v.sx = v.x = (int16_t)(DECK_X << 4);
                v.sy = v.y = (int16_t)(DECK_Y << 4);
                dealClock = (uint8_t)(dealClock + (s == BOARD ? BOARD_STAGGER : STAGGER));
                break;
            }
            case Ev::Street:
                break;
            case Ev::Turn:
                acting = s;
                if (s == YOU && !t.demo && t.phase != Phase::Draw) audio::sfx(Sfx::Turn);
                break;
            case Ev::Action: {
                uint8_t a = e.b;
                lastAct[s] = a; actT[s] = ACT_FRAMES;
                acting = 0xFF;
                int32_t add = e.amount - betTarget[s];
                if (add > 0) { chipsOut(t, s, add, false); betTarget[s] = e.amount; }
                annBegin();
                annWord(nameOf(t, s), s == YOU ? GOLD : WHITE);
                annWord((s == YOU ? VERB2 : VERB3)[a], a == A_FOLD || a == A_CHECK ? SILVER : CYAN);
                if (a >= A_CALL && a <= A_ALLIN) annMoney(e.amount, GOLD);
                int x, y;
                seatPos(s, x, y);
                switch (a) {
                    case A_FOLD:
                        if (s != YOU) muck(s, 0x7F);
                        audio::sfx(Sfx::Fold);
                        break;
                    case A_CHECK:
                        fx::burst(fx::DUST, x + 16, y + 6, 6, 18, FELT_LT);
                        audio::sfx(Sfx::Knock);
                        break;
                    case A_CALL: audio::sfx(Sfx::Chip); break;
                    case A_ALLIN:
                        fx::banner("ALL IN!", fx::B_RED, 60, 60);
                        fx::shake(10, 2);
                        fx::burst(fx::SPARK, x + 14, y + 4, 12, 44, GOLD);
                        audio::sfx(Sfx::AllIn);
                        break;
                    default: audio::sfx(Sfx::Raise); break;
                }
                break;
            }
            case Ev::Return: {
                int x0, y0, x1, y1;
                betPos(s, x0, y0);
                seatPos(s, x1, y1);
                dispBet[s] -= e.amount; betTarget[s] -= e.amount;
                if (dispBet[s] < 0) dispBet[s] = 0;
                fly(x0, y0, x1, y1, BACK, s, e.amount, 0);
                break;
            }
            case Ev::Collect: {
                int px, py;
                potPos(t, px, py);
                for (uint8_t k = 0; k < SEATS; k++) {
                    if (betTarget[k] <= 0) continue;
                    int x, y;
                    betPos(k, x, y);
                    fly(x, y, px, py, TO_POT, k, betTarget[k], k * 2, 16);
                    dispBet[k] = 0; betTarget[k] = 0;
                }
                dealClock = (uint8_t)(dealClock < 18 ? 18 : dealClock);   // sweep first, then deal
                audio::sfx(Sfx::Slide);
                break;
            }
            case Ev::Discard:
                muck(s, e.b);
                drewN[s] = e.c;
                lastAct[s] = A_DRAW; actT[s] = ACT_FRAMES;
                acting = 0xFF;
                annBegin();
                annWord(nameOf(t, s), s == YOU ? GOLD : WHITE);
                if (e.c) {
                    char b[12];
                    fmtInt(fmtStr(b, s == YOU ? " DRAW " : " DRAWS "), e.c);
                    annWord(b, CYAN);
                } else annWord(s == YOU ? " STAND PAT" : " STANDS PAT", GOLD);
                audio::sfx(Sfx::Whoosh);
                break;
            case Ev::Reveal:
                if (s != YOU) {
                    char b[24];
                    hand::describe(b, t.score(s));
                    annBegin();
                    annWord(nameOf(t, s), WHITE);
                    annWord(" SHOWS ", SILVER);
                    annWord(b, CYAN);
                }
                break;
            case Ev::Win:
                announceWin(t, e);
                break;
            case Ev::HandEnd:
                acting = 0xFF;
                if (!t.seats[YOU].stack && !t.demo) {
                    fx::banner("BUSTED!", fx::B_RED, 96, 90);
                    fx::shake(14, 3);
                    pal::flash(WHITE, 0xFBB, 6);
                    audio::sfx(Sfx::Bust);
                }
                break;
            case Ev::Rebuy:
                annBegin();
                annWord("YOU REBUY ", SILVER);
                annMoney(e.amount, GOLD);
                dispStack[YOU] = 0;
                audio::sfx(Sfx::Coin);
                break;
            case Ev::Cursor: audio::sfx(Sfx::Cursor); tapT = 1; break;
            case Ev::Deny: audio::sfx(Sfx::Deny); break;
        }
    }
}

// ---------------------------------------------------------------------------
// Per-frame animation
// ---------------------------------------------------------------------------
static int16_t glideQ4(int16_t v, int target) {
    int t = target << 4, d = t - v;
    if (d > -3 && d < 3) return (int16_t)t;
    return (int16_t)(v + d / 3);
}

void update(const Table &t, uint32_t frame) {
    (void)frame;
    bool flying = false;
    for (uint8_t s = 0; s < 5; s++)
        for (uint8_t i = 0; i < 7; i++) {
            CardView &v = views[s][i];
            if (!v.live) continue;
            if (v.t < 0) { v.t++; flying = true; continue; }
            int tx, ty;
            slot(t, s, i, tx, ty);
            if (v.t < FLIGHT) {
                if (v.t == 0) audio::sfx(Sfx::Deal);
                v.t++;
                int e = fx::ease(fx::OUT_CUBIC, v.t, FLIGHT);
                v.x = (int16_t)(v.sx + ((((tx << 4) - v.sx) * e) >> 8));
                v.y = (int16_t)(v.sy + ((((ty << 4) - v.sy) * e) >> 8) - ((fx::isin(v.t * 128 / FLIGHT) * 3) >> 4));
                flying = true;
            } else {
                v.x = glideQ4(v.x, tx);
                v.y = glideQ4(v.y, ty);
            }
            bool want = wantUp(t, s, i);
            if (v.t >= FLIGHT && !v.flip && v.up != want) {
                if (v.hold) v.hold--;
                else { v.flip = 1; audio::sfx(Sfx::Flip); }
            }
            if (v.flip) {
                if (++v.flip == FLIP / 2) v.up = want;
                if (v.flip > FLIP) v.flip = 0;
            }
        }
    if (!flying) dealClock = 0;
    for (auto &g : ghosts) {
        if (!g.t) continue;
        int e = fx::ease(fx::IN_OUT, g.t, 14);
        g.x = (int16_t)(g.sx + ((((MUCK_X << 4) - g.sx) * e) >> 8));
        g.y = (int16_t)(g.sy + ((((MUCK_Y << 4) - g.sy) * e) >> 8));
        if (++g.t > 14) g.t = 0;
    }
    for (auto &f : flies) {
        if (!f.T) continue;
        if (++f.t < f.T) continue;
        if (f.kind == TO_BET) dispBet[f.seat] += f.value;
        if (f.kind == TO_POT) dispPot += f.value;
        if (f.kind == TO_SEAT) { stackFlash[f.seat] = 20; audio::sfx(Sfx::Coin); }
        f.T = 0;
    }
    // Stacks roll toward the chips really there.
    for (uint8_t s = 0; s < SEATS; s++) {
        int32_t d = t.seats[s].stack - dispStack[s];
        if (d) {
            int32_t step = d / 4;
            if (!step) step = d > 0 ? 1 : -1;
            dispStack[s] += step;
            // A tick of the count, never over any other sound (the library's
            // blip would cut off a priority-0 effect or another blip).
            if (d > 0 && s == YOU && (dispStack[s] & 3) == 0 && !audio::playing())
                audio::blip((uint16_t)(3000 + ((dispStack[s] * 7) & 511)), 8);
        }
        if (stackFlash[s]) stackFlash[s]--;
        if (actT[s]) actT[s]--;
        if (joinT[s]) joinT[s]--;
    }
    if (annT < 255) annT++;
    // The dealer button glides round the table; the glove to its card.
    int x, y;
    buttonPos(t.button, x, y);
    bx16 = glideQ4(bx16, x); by16 = glideQ4(by16, y);
    if (t.phase == Phase::DrawHuman) {
        if (t.glove < 5) { slot(t, YOU, t.glove, x, y); x += 11; y -= 1; }
        else { x = 92; y = BAR_Y + 3; }
        gx16 = (int16_t)(gx16 + (((x << 4) - gx16) >> 1));
        gy16 = (int16_t)(gy16 + (((y << 4) - gy16) >> 1));
    } else {
        gx16 = (int16_t)(64 << 4); gy16 = (int16_t)(140 << 4);
    }
    if (tapT && ++tapT > 12) tapT = 0;
    if (shuffleT) shuffleT--;
    // A CPU thinking: CHChess's soft clock.
    if (t.phase == Phase::Think && acting != YOU) {
        if (++thinkT % 24 == 1) audio::sfx(thinkT & 32 ? Sfx::Tock : Sfx::Tick);
    } else thinkT = 0;
    fx::update();
}

bool busy() {
    for (uint8_t s = 0; s < 5; s++)
        for (uint8_t i = 0; i < 7; i++) {
            const CardView &v = views[s][i];
            if (v.live && (v.t < FLIGHT || v.flip)) return true;
        }
    for (auto &f : flies) if (f.T && f.kind != TO_SEAT) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static int flipWidth(const CardView &v, int full) {
    if (!v.flip) return full;
    int half = FLIP / 2;
    int w = v.flip <= half ? full * (half - v.flip + 1) / (half + 1) : full * (v.flip - half) / (half + 1);
    return w < 2 ? 2 : w;
}

static bool winning(uint8_t s, uint8_t i) {
    if (hiSeat == 0xFF) return false;
    if (s == BOARD) return (hiBoard >> i) & 1;
    return s == hiSeat && ((hiHole >> i) & 1);
}

static void drawCard(const Table &t, uint8_t s, uint8_t i, bool full) {
    const CardView &v = views[s][i];
    if (!v.live) return;
    int x = v.x >> 4, y = v.y >> 4;
    uint8_t c = s == BOARD ? t.board[i] : t.seats[s].cards[i];
    bool win = winning(s, i);
    uint8_t edge = win ? FX_A : INK;
    bool big = s == YOU || s == BOARD;
    if (big) art::card(x, y, c, v.up, flipWidth(v, CARD_W), full, edge);
    else art::mini(x, y, c, v.up, flipWidth(v, MINI_W), edge);
    // At the showdown, the cards that don't play step back.
    bool loser = hiSeat != 0xFF && !win && (s == BOARD || s == hiSeat);
    if ((loser || (s == YOU && t.seats[YOU].state == S_FOLDED)) && v.t >= FLIGHT && !v.flip)
        art::dim(x, y, big ? CARD_W + 1 : MINI_W + 1, big ? art::CARD_H + 1 : art::MINI_H);
}

static uint8_t actColour(uint8_t a) {
    switch (a) {
        case A_FOLD: case A_CHECK: return SILVER;
        case A_CALL: return CYAN;
        case A_ALLIN: return FX_A;
        case A_BET: case A_RAISE: case A_COMPLETE: return GOLD;
        default: return WHITE;
    }
}
static const char *const ACT_WORD[] = {"", "FOLD", "CHECK", "CALL", "BET", "RAISE", "ALL IN",
                                       "SMALL", "BIG", "ANTE", "BRING", "RAISE", "DRAW"};

static void actWord(char *b, uint8_t s) {
    if (lastAct[s] == A_DRAW) {
        if (drewN[s]) fmtInt(fmtStr(b, "DRAW "), drewN[s]);
        else fmtStr(b, "PAT");
    } else fmtStr(b, ACT_WORD[lastAct[s]]);
}

static void drawSeat(const Table &t, uint8_t s, uint32_t frame) {
    const Seat &p = t.seats[s];
    int bx = blockX(s);
    char b[12];
    if (p.state == S_OUT && !p.stack) {                     // busted, waiting for a new player
        roundRect(bx + 1, PLATE_Y, BLOCK_W - 2, PLATE_H, 2, FELT_DK);
        text35(bx + 8, PLATE_Y + 3, "EMPTY", FELT_DK);
        return;
    }
    int pop = joinT[s] ? (joinT[s] > 12 ? 3 : joinT[s] / 4) : 0;   // a new player's plate springs in
    uint8_t edge = s == acting ? FX_B : (p.state == S_FOLDED ? SILVER : GOLD);
    panel(bx + pop, PLATE_Y + pop, BLOCK_W - 2 * pop, PLATE_H - pop, 2, NAVY, edge);
    art::avatar(bx + 6, PLATE_Y + 4, p.colour);
    if (actT[s] && lastAct[s]) {
        actWord(b, s);
        text35(bx + 13, PLATE_Y + 3, b, actColour(lastAct[s]));
    } else {
        fmtShort(b, p.state == S_ALLIN && !dispStack[s] ? 0 : dispStack[s]);
        text35(bx + 13, PLATE_Y + 3, p.state == S_ALLIN && !dispStack[s] ? "ALL IN" : b,
               stackFlash[s] && (frame & 4) ? GOLD : WHITE);
    }
    if (s == acting && t.phase == Phase::Think)                  // thinking: dots under the stack
        for (uint32_t k = 0; k < ((frame >> 4) & 3); k++) gfx_fillRect(bx + 33 + k * 2, PLATE_Y + 6, 1, 1, FX_B);
    if (p.state == S_FOLDED) art::dim(bx, PLATE_Y, BLOCK_W, PLATE_H);
    // The cards: stud draws the down cards first, then the up cards over them.
    static const uint8_t STUD_ORDER[7] = {0, 1, 6, 2, 3, 4, 5};
    for (uint8_t k = 0; k < 7; k++) {
        uint8_t i = t.game == STUD && p.up != 0x7F ? STUD_ORDER[k] : k;
        drawCard(t, s, i, true);
    }
    if (dispBet[s] > 0) {
        art::chipStack(bx + 6, BET_Y + 3, dispBet[s], 3);
        fmtShort(b, dispBet[s]);
        text35(bx + 12, BET_Y + 1, b, WHITE);
    }
}

static void drawYou(const Table &t, uint32_t frame) {
    const Seat &p = t.seats[YOU];
    char b[12];
    uint8_t edge = acting == YOU ? FX_B : GOLD;
    panel(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 3, NAVY, edge);
    if (actT[YOU] && lastAct[YOU]) { actWord(b, YOU); text35(PANEL_X + 3, PANEL_Y + 3, b, actColour(lastAct[YOU])); }
    else text35(PANEL_X + 3, PANEL_Y + 3, "YOU", WHITE);
    fmtShort(b, dispStack[YOU]);
    text35(PANEL_X + 3, PANEL_Y + 10, b, stackFlash[YOU] && (frame & 4) ? WHITE : GOLD);
    if (dispBet[YOU] > 0) {
        art::chipStack(PANEL_X + 6, PANEL_Y + 20, dispBet[YOU], 3);
        fmtShort(b, dispBet[YOU]);
        text35(PANEL_X + 12, PANEL_Y + 18, b, WHITE);
    }
    for (uint8_t i = 0; i < 7; i++) {
        const CardView &v = views[YOU][i];
        bool full = t.game == HOLDEM || i + 1 >= p.n || !views[YOU][i + 1].live;
        if (v.live) drawCard(t, YOU, i, full || t.game == DRAW || t.game == OMAHA);
    }
}

static void drawPot(const Table &t) {
    if (dispPot <= 0) return;
    char b[16];
    if (community(t)) {
        art::chipStack(70, POT_Y + 4, dispPot, 3);
        fmtMoney(fmtStr(b, "POT "), dispPot);
        text35(77, POT_Y + 1, b, GOLD);
        return;
    }
    art::chipStack(42, 64, dispPot, 6);
    text35(52, 52, "POT", SILVER);
    fmtMoney(b, dispPot);
    text35x2(52, 59, b, GOLD);
}

static void drawHint(const Table &t) {
    if (t.opt.hints || !t.live(YOU) || t.phase == Phase::DrawHuman) return;
    // Only cards that have landed face up count, so the hint never spoils.
    uint8_t own[7], n = 0, brd[5], nb = 0;
    for (uint8_t i = 0; i < t.seats[YOU].n; i++) {
        const CardView &v = views[YOU][i];
        if (v.live && v.up && v.t >= FLIGHT) own[n++] = t.seats[YOU].cards[i];
    }
    for (uint8_t i = 0; i < t.nBoard; i++) {
        const CardView &v = views[BOARD][i];
        if (v.live && v.up && !v.flip) brd[nb++] = t.board[i];
    }
    if (n < 2) return;
    uint32_t sc;
    if (t.game == OMAHA && nb >= 3 && n == 4) sc = hand::omaha(own, brd, nb);
    else if (t.game == HOLDEM) { memcpy(own + n, brd, nb); sc = hand::eval(own, (uint8_t)(n + nb)); }
    else sc = hand::eval(own, n);
    char b[24];
    hand::describe(b, sc);
    int w = text35Width(b) + 6;
    panel(1, POT_Y - 1, w, 9, 2, NAVY, hand::cat(sc) >= hand::STRAIGHT ? FX_B : INK);
    text35(4, POT_Y + 1, b, hand::cat(sc) >= hand::TWO_PAIR ? CYAN : WHITE);
}

static void drawHud(const Table &t, uint32_t frame) {
    gfx_fillRect(0, 0, 128, 9, INK);
    gfx_hline(0, 9, 128, GOLD);
    char b[24], *p = b;
    int32_t lo = VARIANTS[t.game].limit == FIXED_LIMIT ? 2 * t.unit() : t.unit();
    p = fmtMoney(p, lo); *p++ = '/'; fmtMoney(p, 2 * lo);
    int x = 2 + text35(2, 2, b, GOLD) + 2;
    text35(x, 2, VARIANTS[t.game].hud, WHITE);
    if (acting != 0xFF && acting != YOU && (t.phase == Phase::Think || t.phase == Phase::DrawThink)) {
        p = fmtStr(b, nameOf(t, acting));
        for (uint32_t k = 0; k < ((frame >> 4) & 3); k++) *p++ = '.', *p = 0;
        text35(126 - text35Width(nameOf(t, acting)) - 8, 2, b, FX_B);
    } else {
        const char *st = streetName(t);
        text35(126 - text35Width(st), 2, st, SILVER);
    }
}

// The plate at the foot (CHChess): words in their colours on a rounded
// plate that springs open, each word dropping in.
static void drawNarration() {
    if (!annN || annT >= ANN_FRAMES) return;
    const char *w[8];
    char buf[sizeof annText];
    memcpy(buf, annText, sizeof buf);
    char *q = buf;
    for (uint8_t i = 0; i < annN; i++) {
        w[i] = q;
        while (*q && *q != '|') q++;
        if (*q) *q++ = 0;
    }
    int tw = 0;
    for (uint8_t i = 0; i < annN; i++) tw += text35Width(w[i]) + (w[i][0] == ' ' ? 1 : 0);
    int t = annT;
    int grow = t < 8 ? fx::ease(fx::OUT_BACK, t, 8) : t > ANN_FRAMES - 8 ? (ANN_FRAMES - t) * 32 : 256;
    int pw = ((tw + 10) * grow) >> 8, y = BAR_Y + 3;
    if (pw < 6) return;
    if (pw > 126) pw = 126;
    fillRound(64 - pw / 2, y, pw, 11, 2, NAVY);
    roundRect(64 - pw / 2, y, pw, 11, 2, GOLD);
    int x = 64 - tw / 2;
    for (uint8_t i = 0; i < annN; i++) {
        int d = t - 4 - 2 * i;
        if (d >= 0) text35(x, y + 3 - (d < 3 ? 3 - d : 0), w[i], annCol[i]);
        x += text35Width(w[i]) + (w[i][0] == ' ' ? 1 : 0);
    }
}

// The table's betting line: an ellipse 124 x 61 round (64, 62), as the
// half-width of each row from the middle out.
static const uint8_t RING[31] = {62, 62, 62, 62, 61, 61, 61, 60, 60, 59, 58, 58, 57, 56, 55, 54, 52, 51, 50, 48, 46, 44, 42, 40, 37, 34, 31, 27, 22, 16, 0};

static void drawFelt() {
    gfx_fillRect(0, 10, 128, TRIM_Y - 10, FELT);
    // The table's betting line, and a darker rim.
    for (int dy = 0; dy <= 30; dy++) {
        int a = RING[dy], b = dy ? RING[dy - 1] - 1 : a;      // join the steps
        if (b < a) b = a;
        for (int sy = -1; sy <= 1; sy += 2) {
            gfx_hline(64 - b, 62 + sy * dy, b - a + 1, FELT_LT);
            gfx_hline(64 + a, 62 + sy * dy, b - a + 1, FELT_LT);
        }
    }
    dither(0, 10, 128, 2, FELT_DK, 0);
    dither(0, 10, 2, TRIM_Y - 10, FELT_DK, 0);
    dither(126, 10, 2, TRIM_Y - 10, FELT_DK, 1);
}

// A still table is not redrawn: the frame is flushed again, so palette
// effects keep moving.
static uint32_t lastSig;

static bool moving() {
    int lo, hi;
    if (fx::activeRows(lo, hi)) return true;
    for (uint8_t s = 0; s < 5; s++)
        for (uint8_t i = 0; i < 7; i++) {
            const CardView &v = views[s][i];
            if (v.live && (v.t < FLIGHT || v.flip)) return true;
        }
    for (auto &g : ghosts) if (g.t) return true;
    for (auto &f : flies) if (f.T) return true;
    return annT < 40 || tapT || shuffleT;
}

static uint32_t signature(const Table &t, uint32_t frame, uint32_t ui) {
    if (moving()) return frame;
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix((uint32_t)t.phase); mix((uint32_t)t.bar); mix(t.sel); mix((uint32_t)t.raiseTo); mix(t.drawMask);
    mix(t.glove); mix(acting); mix(ui); mix((uint32_t)dispPot); mix(hiSeat);
    mix((uint32_t)(gx16 ^ (gy16 << 16))); mix((uint32_t)(bx16 ^ (by16 << 16)));
    for (uint8_t s = 0; s < SEATS; s++) {
        mix((uint32_t)dispStack[s]); mix((uint32_t)dispBet[s]); mix(actT[s] ? lastAct[s] : 0xFF);
        mix(t.seats[s].state); mix(stackFlash[s] ? 1 + ((frame >> 2) & 1) : 0); mix(joinT[s]);
    }
    for (uint8_t s = 0; s < 5; s++)
        for (uint8_t i = 0; i < 7; i++) { const CardView &v = views[s][i]; mix(v.live | (v.up << 1)); mix((uint32_t)(v.x ^ (v.y << 16))); }
    mix(annT >= ANN_FRAMES - 8 ? annT : 0);
    if (acting != 0xFF || t.phase == Phase::DrawHuman) mix(frame >> 3);       // dots, the glove's bob
    return h;
}

bool render(const Table &t, uint32_t frame, uint32_t ui) {
    uint32_t sig = signature(t, frame, ui);
    if (sig == lastSig && !forceDraw) return false;
    lastSig = sig;
    forceDraw = false;
    drawFelt();
    drawHud(t, frame);
    for (uint8_t s = 1; s < SEATS; s++) drawSeat(t, s, frame);
    for (uint8_t i = 0; i < 5; i++) drawCard(t, BOARD, i, true);
    drawPot(t);
    drawHint(t);
    drawYou(t, frame);
    // The deck: riffled at the start of a hand, then dealt from.
    bool dealing = false;
    for (uint8_t s = 0; s < 5; s++) for (uint8_t i = 0; i < 7; i++) dealing |= views[s][i].live && views[s][i].t < 0;
    if (shuffleT) {
        int o = shuffleT > 15 ? (30 - shuffleT) / 2 : shuffleT / 2;     // apart, then together
        for (int k = 0; k < 3; k++) {
            art::mini(DECK_X + 6 - o - k, DECK_Y + 6 - k, 0, false);
            art::mini(DECK_X + 6 + o - k, DECK_Y + 6 - k + ((shuffleT >> 1) & 1), 0, false);
        }
    } else if (dealing) {
        for (int k = 0; k < 3; k++) art::mini(DECK_X + 6 - k, DECK_Y + 6 - k, 0, false);
    }
    for (auto &g : ghosts) if (g.t) art::mini(g.x >> 4, g.y >> 4, 0, false);
    {   // the dealer button
        art::button(bx16 >> 4, by16 >> 4);
    }
    for (auto &f : flies) {
        if (!f.T || f.t < 0) continue;
        int e = fx::ease(fx::OUT_CUBIC, f.t, f.T);
        int x = f.x0 + (((f.x1 - f.x0) * e) >> 8);
        int y = f.y0 + (((f.y1 - f.y0) * e) >> 8) - ((fx::isin(f.t * 128 / f.T) * 8) >> 8);
        if (f.kind == TO_SEAT || f.kind == TO_POT) art::chipStack(x, y, f.value, 3);
        else art::chip(x, y, (uint8_t)art::chipDenom(f.value), true);
    }
    if (t.phase == Phase::DrawHuman) {
        int bob = (fx::isin((int)(frame >> 3) * 40) * 2) >> 8;
        if (tapT) bob = (tapT < 6 ? tapT : 12 - tapT) / 2;
        sprite4(HAND, (gx16 >> 4) - HAND_TIP, (gy16 >> 4) - HAND[1] + bob - 1);
    }
    gfx_hline(0, TRIM_Y, 128, GOLD);
    gfx_fillRect(0, BAR_Y, 128, BAR_H, NAVY);
    if (t.bar != Bar::None) bar::draw(t, frame);
    else drawNarration();
    fx::drawParticles();
    fx::drawFloats();
    fx::drawBanner();
    fx::applyShake(10, TRIM_Y);
    return true;
}

}  // namespace stage
