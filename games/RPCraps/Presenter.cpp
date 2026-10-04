#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// CHBlackjack's presenter (its Presenter.cpp): chip flights, the rolling
// purse, the talking dealer and the band-level redraw, re-cut for the craps
// layout and the dice cam.
#include <RPGame.h>
#include <string.h>
#include "Presenter.h"
#include "Fx.h"
#include "Craps.h"
#include "Cam.h"
#include "Layout.h"
#include "Zones.h"
#include "Felt.h"
#include "Wall.h"
#include "Bar.h"
#include "Chips.h"
#include "Sounds.h"

namespace present {

using namespace lay;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum FlyKind : uint8_t { F_IN, F_OUT, F_TRAY, F_PAY, F_HOME, F_MOVE };
struct Fly { int16_t x0, y0, x1, y1; int16_t t; uint8_t T, bet, kind; int32_t value; };
static Fly flies[16];

static int32_t disp[BET_COUNT];        // chips shown on each spot
static int32_t paid[BET_COUNT];        // winnings set down beside a spot, not yet collected
static int32_t shown = 500, pending = 0;
static uint8_t purseFlash = 0;

static uint8_t zoneSel = 0xFF, denom = 1, holdW = 0, tapT = 0, denyT = 0;
static bool rollHeld = false;

static char bubText[40];
static uint8_t bubChars = 0, bubLen = 0, bubHold = 0, face = wall::E_NORMAL;
static uint8_t blinkT = 90, blinking = 0;
static int8_t waggle = 0;

static uint8_t shownPoint = 0, puckFrom = 0, puckT = 0xFF;    // the puck on the felt and its slide
static const uint8_t PUCK_T = 18;


enum Step : uint8_t { S_IDLE, S_CAM, S_CALL, S_LOSE, S_PAY, S_HOME, S_MOVE };
static uint8_t step = S_IDLE, walk = 0, speed = 1;
static uint16_t stepT = 0;
static char callText[40];
static uint8_t callFace = wall::E_NORMAL;
static uint8_t boardLag = 0;            // newest rolls kept off the board until called
static bool bigWin = false;

static int32_t money(const Res &r) {
    if (r.kind == R_WIN) return r.win;
    if (r.kind == R_WIN_HOME) return r.win + r.stake;
    if (r.kind == R_RETURN) return r.stake;
    return 0;
}

// ---------------------------------------------------------------------------
// Flights
// ---------------------------------------------------------------------------
static const int HOME_X = 6, HOME_Y = 136;            // the player's own rack, off the bottom left

static bool spot(const Craps &g, uint8_t b, int &x, int &y) { return zones::anchor(g.opt.table, b, x, y); }

static bool fly(int x0, int y0, int x1, int y1, uint8_t bet, uint8_t kind, int32_t value, int delay, uint8_t T) {
    for (auto &f : flies) {
        if (f.T) continue;
        f.x0 = (int16_t)x0; f.y0 = (int16_t)y0; f.x1 = (int16_t)x1; f.y1 = (int16_t)y1;
        f.t = (int16_t)-delay; f.T = T; f.bet = bet; f.kind = kind; f.value = value;
        return true;
    }
    return false;
}

static void land(Fly &f) {
    switch (f.kind) {
        case F_IN: case F_MOVE: disp[f.bet] += f.value; break;
        case F_PAY: paid[f.bet] += f.value; audio::sfx(Sfx::Chip); break;
        case F_HOME:
            pending -= f.value;
            purseFlash = 24;
            audio::sfx(Sfx::Coin);
            break;
        default: break;
    }
}

static int32_t inbound(uint8_t b) {
    int32_t v = 0;
    for (auto &f : flies) if (f.T && f.bet == b && (f.kind == F_IN || f.kind == F_MOVE)) v += f.value;
    return v;
}

// Outside a roll the chips follow the bets: new money flies down onto the
// spot, money taken back flies to the rack.
static void reconcile(const Craps &g) {
    for (uint8_t b = 0; b < BET_COUNT; b++) {
        int32_t have = disp[b] + inbound(b), want = g.bet[b];
        if (want == have) continue;
        int x, y;
        if (!spot(g, b, x, y)) { disp[b] = want; continue; }
        if (want > have) {
            if (!fly(x, y - 11, x, y, b, F_IN, want - have, 0, 7)) disp[b] = want;
        } else {
            int32_t d = have - want;
            disp[b] -= d;
            if (disp[b] < 0) disp[b] = 0;                     // still in the air: it lands short
            const Zone &z = zones::at(g.opt.table, (uint8_t)(zones::count(g.opt.table) - 5 + denom));
            fly(x, y, z.ax, z.ay - 4, b, F_OUT, d, 0, 12);
        }
    }
}

// ---------------------------------------------------------------------------
// The calls
// ---------------------------------------------------------------------------
static const char *const WORD[13] = {"", "", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT",
                                     "NINE", "TEN", "ELEVEN", "TWELVE"};

static void makeCall(const Craps &g) {
    uint8_t s = g.sum(), k = g.histKind[0];
    bool hard = g.d1 == g.d2;
    char *p = callText;
    callFace = wall::E_NORMAL;
    if (k == H_SEVEN_OUT) { fmtStr(p, "SEVEN OUT!\nLINE AWAY"); callFace = wall::E_RAISED; }
    else if (k == H_WINNER && g.prevPoint) { fmtStr(fmtStr(fmtStr(p, "WINNER\n"), WORD[s]), "!\nPAY THE LINE"); callFace = wall::E_SMILE; }
    else if (k == H_WINNER) { fmtStr(p, s == 7 ? "SEVEN!\nFRONT LINE\nWINNER" : "YO-LEVEN!\nFRONT LINE\nWINNER"); callFace = wall::E_SMILE; }
    else if (k == H_CRAPS) fmtStr(p, s == 2 ? "ACES!\nCRAPS" : (s == 3 ? "ACE-DEUCE!\nCRAPS" : "BOXCARS!\nCRAPS"));
    else if (k == H_POINT) { fmtStr(fmtStr(fmtStr(p, "THE POINT\nIS "), WORD[s]), "!\nMARK IT"); }
    else if (s == 2) fmtStr(p, "ACES!\nSNAKE EYES");
    else if (s == 3) fmtStr(p, "ACE-DEUCE");
    else if (s == 11) fmtStr(p, "YO-LEVEN!");
    else if (s == 12) fmtStr(p, "BOXCARS!\nMIDNIGHT");
    else if (s == 5) fmtStr(p, "FIVE, NO\nFIELD FIVE");
    else if (s == 9) fmtStr(p, "NINE,\nCENTER FIELD");
    else if (hard) { fmtStr(fmtStr(p, "HARD "), WORD[s]); fmtStr(p + strlen(p), "!"); callFace = wall::E_SURPRISED; }
    else fmtStr(fmtStr(p, "EASY "), WORD[s]);
    if (g.rollWon >= 100 || (g.rollWon >= 25 && g.rollWon >= 4 * (g.rollLost + 1))) callFace = wall::E_SURPRISED;
}

// The dice have stopped in the cam: the big word.
static void restBanner(const Craps &g) {
    uint8_t s = g.sum(), k = g.histKind[0];
    char b[14];
    fx::BannerStyle st = fx::B_WHITE;
    if (k == H_SEVEN_OUT) { fmtStr(b, "SEVEN OUT"); st = fx::B_RED; audio::sfx(Sfx::SevenOut); }
    else if (k == H_WINNER && g.prevPoint && g.handPoints >= 2) { fmtStr(b, "HOT HAND!"); st = fx::B_RAINBOW; audio::sfx(Sfx::Hot); }
    else if (k == H_WINNER) { fmtStr(b, s == 11 ? "YO!" : "WINNER!"); st = fx::B_RAINBOW; audio::sfx(Sfx::Win); }
    else if (k == H_CRAPS) { fmtStr(b, "CRAPS!"); st = fx::B_RED; audio::sfx(Sfx::Craps); }
    else if (k == H_POINT) { char *q = fmtStr(b, "POINT "); fmtInt(q, s); st = fx::B_GOLD; audio::sfx(Sfx::Point); }
    else if (g.d1 == g.d2 && s >= 4 && s <= 10) { char *q = fmtStr(b, "HARD "); fmtInt(q, s); st = fx::B_GOLD; }
    else if (s == 11) { fmtStr(b, "YO!"); st = fx::B_CYAN; }
    else { fmtStr(b, WORD[s]); st = fx::B_CYAN; }
    if (g.rollWon && k != H_SEVEN_OUT && k != H_WINNER) audio::sfx(Sfx::Win);
    fx::banner(b, st, 30, 90);
    if (st == fx::B_RAINBOW) { fx::fountain(fx::CONFETTI, 40, 90, 18); fx::fountain(fx::CONFETTI, 88, 90, 18); audio::led(audio::LED_TRIPLE); }
    if (k == H_SEVEN_OUT) fx::shake(14, 3);
    if (bigWin) { fx::fountain(fx::COIN, 64, 100, 16); audio::sfx(Sfx::BigWin); audio::led(audio::LED_PARTY); }
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
void reset(const Craps &g) {
    memset(flies, 0, sizeof flies);
    memset(paid, 0, sizeof paid);
    for (uint8_t b = 0; b < BET_COUNT; b++) disp[b] = g.bet[b];
    shown = g.purse; pending = 0; purseFlash = 0;
    step = S_IDLE; speed = 1; boardLag = 0;
    shownPoint = g.point; puckT = 0xFF;
    bubLen = bubChars = 0; bubHold = 0; face = wall::E_NORMAL;
    fx::clear();
    invalidate();
}

bool busy() { return step != S_IDLE; }
void speedUp() { if (step >= S_CALL) speed = 3; }
int32_t shownPurse() { return shown; }

void say(const char *text, uint8_t f, uint8_t hold) {
    strncpy(bubText, text, sizeof bubText - 1);
    bubText[sizeof bubText - 1] = 0;
    bubChars = 0; bubLen = (uint8_t)strlen(bubText); bubHold = hold; face = f;
}
void dismissBubble() { bubLen = 0; bubHold = 0; face = wall::E_NORMAL; }

void cursor(uint8_t z) { zoneSel = z; }
void tap() { tapT = 10; }
void setDenom(uint8_t d) { denom = d; }
void setRollHeld(bool h) { rollHeld = h; }
void setHoldBar(uint8_t w) { holdW = w; }

void denied(const Craps &g, uint8_t reason) {
    static const char *const WHY[DENY_COUNT] = {
        "", "NOT ON THIS\nTABLE", "LINE BETS GO\nDOWN ON THE\nCOME-OUT", "COME OPENS\nAFTER THE\nPOINT",
        "ODDS WAIT\nFOR A POINT", "BET THE\nLINE FIRST", "TABLE MAX\nIS $500", "THAT'S FULL\nODDS",
        "OUT OF\nCHIPS!", "THAT ONE\nSTAYS UP!", "", "BET THE\nLINE FIRST,\nSHOOTER",
    };
    (void)g;
    denyT = 24;
    audio::sfx(Sfx::Deny);
    if (reason < DENY_COUNT && *WHY[reason]) say(WHY[reason], wall::E_RAISED, 70);
}

void onThrow(const Craps &g) {
    step = S_CAM; stepT = 0; walk = 0; speed = 1;
    pending = 0;
    for (uint8_t b = 0; b < BET_COUNT; b++) pending += money(g.res[b]);
    bigWin = g.rollWon >= 200 || (g.rollWon >= 50 && g.rollWon >= 10 * (g.rollLost + 1));
    makeCall(g);
    boardLag = 1;
    dismissBubble();
}

static void nextStep() { step++; stepT = 0; walk = 0; }

// One spot of this step's kind each few ticks; true once all are on their way.
static bool walkSpots(const Craps &g, bool (*want)(const Res &), void (*send)(const Craps &, uint8_t)) {
    if (stepT % 4 != 1) return walk >= BET_COUNT;
    while (walk < BET_COUNT && !want(g.res[walk])) walk++;
    if (walk < BET_COUNT) { send(g, walk); walk++; }
    while (walk < BET_COUNT && !want(g.res[walk])) walk++;
    return walk >= BET_COUNT;
}

static bool isLose(const Res &r) { return r.kind == R_LOSE; }
static bool isWin(const Res &r) { return r.kind == R_WIN || r.kind == R_WIN_HOME; }
static bool isHome(const Res &r) { return r.kind == R_WIN || r.kind == R_WIN_HOME || r.kind == R_RETURN; }
static bool isMove(const Res &r) { return r.kind == R_MOVE; }

static void sendLose(const Craps &g, uint8_t b) {
    int x, y;
    if (!spot(g, b, x, y) || disp[b] <= 0) { disp[b] = 0; return; }
    fly(x, y, TRAY_CX, TRAY_CY + 6, b, F_TRAY, disp[b], 0, 16);
    disp[b] = 0;
    audio::sfx(Sfx::Sweep);
}

static void sendPay(const Craps &g, uint8_t b) {
    int x, y;
    const Res &r = g.res[b];
    if (!spot(g, b, x, y)) { paid[b] += r.win; return; }
    fly(TRAY_CX, TRAY_CY + 4, x + 6, y, b, F_PAY, r.win, 0, 14);
    char buf[10];
    fmtMoney(fmtStr(buf, "+"), r.win);
    fx::floatText(buf, x, y - 10, GOLD);
    fx::burst(fx::SPARK, x + 6, y, 6, 30, GOLD);
}

static void sendHome(const Craps &g, uint8_t b) {
    int x, y;
    const Res &r = g.res[b];
    int32_t v = paid[b];
    paid[b] = 0;
    if (r.kind != R_WIN) { v += disp[b]; disp[b] = 0; }
    if (!spot(g, b, x, y) || !fly(x + 3, y, HOME_X, HOME_Y, b, F_HOME, v, 0, 18)) pending -= v;
}

static void sendMove(const Craps &g, uint8_t b) {
    int x0, y0, x1, y1;
    const Res &r = g.res[b];
    if (!spot(g, b, x0, y0) || !spot(g, r.to, x1, y1) ||
        !fly(x0, y0, x1, y1, r.to, F_MOVE, disp[b], 0, 16)) disp[r.to] += disp[b];
    disp[b] = 0;
    audio::sfx(Sfx::Chip);
}

static bool flying() { for (auto &f : flies) if (f.T) return true; return false; }

static void showTick(const Craps &g) {
    stepT++;
    switch (step) {
        case S_CAM: {
            uint16_t r = cam::restT();
            if (r == 1) restBanner(g);
            if (r == 1) cam::result(g.d1, g.d2);
            uint16_t hold = g.opt.speed ? 50 : 90;
            if (r > hold) cam::leave();
            if (cam::phase() == cam::RESULT && g.histKind[0] == H_SEVEN_OUT)
                pal::setDesaturate((uint8_t)(r / 4 > 10 ? 10 : r / 4));
            if (!cam::active()) {
                pal::setDesaturate(0);
                nextStep();
                say(callText, callFace, 110);
                boardLag = 0;
                if (g.point != shownPoint) { puckFrom = shownPoint; puckT = 0; }
            }
            break;
        }
        case S_CALL:
            if (stepT > (g.opt.speed ? 14u : 30u)) nextStep();
            break;
        case S_LOSE:
            if (walkSpots(g, isLose, sendLose) && !flying()) nextStep();
            break;
        case S_PAY:
            if (walkSpots(g, isWin, sendPay) && !flying() && stepT > 20) nextStep();
            break;
        case S_HOME:
            if (walkSpots(g, isHome, sendHome) && !flying()) nextStep();
            break;
        case S_MOVE:
            if (walkSpots(g, isMove, sendMove) && !flying()) {
                step = S_IDLE;
                pending = 0;
                memset(paid, 0, sizeof paid);
            }
            break;
    }
}

static void tick(const Craps &g) {
    cam::update();
    if (step != S_IDLE) showTick(g);
    else if (!cam::active()) reconcile(g);
    for (auto &f : flies) {
        if (!f.T) continue;
        if (++f.t >= f.T) { land(f); f.T = 0; }
    }
    // The purse rolls toward the money that has actually come home.
    int32_t target = g.purse - pending, d = target - shown;
    if (d) {
        int32_t s = d / 5;
        if (!s) s = d > 0 ? 1 : -1;
        shown += s;
        if (d > 0 && (shown & 3) == 0) audio::blip((uint16_t)(3000 + ((shown * 7) & 511)), 8);
    }
    if (purseFlash) purseFlash--;
    if (puckT < PUCK_T) {
        if (++puckT == PUCK_T) {
            shownPoint = g.point;
            puckT = 0xFF;
            if (g.point) audio::sfx(Sfx::Chip);
        }
    }
    if (tapT) tapT--;
    if (denyT) denyT--;
    fx::update();
}

void update(const Craps &g) {
    for (uint8_t i = 0; i < speed; i++) tick(g);
    if (step == S_IDLE) speed = 1;
    // The stickman talks (one letter a frame), blinks, waggles his stick.
    if (bubLen) {
        if (bubChars < bubLen) {
            bubChars++;
            if (bubChars & 1) audio::blip((uint16_t)(1900 + (bubChars * 97) % 700), 12);
        } else if (bubHold) bubHold--;
        else dismissBubble();
    }
    waggle = (int8_t)(bubLen && bubChars < bubLen ? ((bubChars >> 1) & 1) : 0);
    if (blinking) blinking--;
    else if (--blinkT == 0) { blinking = 6; blinkT = (uint8_t)fx::rndRange(90, 220); }
}

// ---------------------------------------------------------------------------
// Drawing: band-level redraw, as in CHBlackjack. The framebuffer survives
// between frames and most of the table is still most of the time, so each
// band (wall 0..45, felt 46..111) is redrawn only when what it shows has
// changed or something moving touched it, this frame or the last. Every
// frame is still flushed, so palette animation keeps running.
// ---------------------------------------------------------------------------
struct Sig {
    uint32_t h = 2166136261u;
    void add(int32_t v) { h = (h ^ (uint32_t)v) * 16777619u; }
};

static uint32_t sigWall = 0, sigFelt = 0;
static int16_t curLo = 999, curHi = -1;
static bool forceAll = true;

void invalidate() { forceAll = true; bar::invalidate(); }

static void flyPos(const Fly &f, int &x, int &y) {
    int e = fx::ease(fx::OUT_CUBIC, f.t, f.T);
    x = f.x0 + (((f.x1 - f.x0) * e) >> 8);
    y = f.y0 + (((f.y1 - f.y0) * e) >> 8);
    if (f.kind != F_IN) y -= (fx::isin(f.t * 128 / f.T) * 10) >> 8;     // an arc
    if (f.kind == F_TRAY || f.kind == F_PAY) { if (y < RAIL_Y + RAIL_H + 2) y = RAIL_Y + RAIL_H + 2; }
}

// Where the cursor's chip hovers (and bobs, and dips when a chip goes down).
static bool cursorChip(const Craps &g, uint32_t frame, int &x, int &y) {
    if (zoneSel == 0xFF || step != S_IDLE || cam::active()) return false;
    const Zone &z = zones::at(g.opt.table, zoneSel);
    if (z.bet >= Z_CHIP0) return false;
    int bob = (fx::isin((int)(frame >> 3) * 40) * 2) >> 8;
    int dip = tapT ? (tapT > 5 ? 10 - tapT : tapT) : 0;
    x = z.ax + (denyT ? ((denyT >> 1) & 1 ? 2 : -2) : 0);
    int32_t on = disp[z.bet];                            // hover just above what's there
    uint8_t chips = 0;
    for (int8_t d = art::DENOMS - 1; d >= 0 && chips < 3; d--)
        while (on >= art::CHIP_VALUE[d] && chips < 3) { on -= art::CHIP_VALUE[d]; chips++; }
    y = z.ay - 6 - 2 * chips + bob + dip;
    return true;
}

static void movingRows(int &lo, int &hi) {
    fx::activeRows(lo, hi);
    auto span = [&](int a, int b) { if (a < lo) lo = a; if (b > hi) hi = b; };
    for (auto &f : flies) {
        if (!f.T || f.t < 0) continue;
        int x, y; flyPos(f, x, y);
        span(y - 8, y + 6);
    }
    if (puckT < PUCK_T) span(FELT_Y - 8, TRIM_Y);       // the badge reaches up over the rail
}

// OFF, the puck lies in the Come (or, on the Beginner table, the field);
// ON, it sits on the point's box like a notification badge on an app icon:
// pinned to the top right corner, over the rail, the box's number clear.
static void puckPos(const Craps &g, uint8_t n, int &x, int &y) {
    if (!n) {
        if (g.opt.table == TABLE_BEGINNER) { x = 120; y = 79; }
        else { x = 9; y = 72; }
        return;
    }
    const Zone &z = zones::at(g.opt.table, zones::find(g.opt.table, (uint8_t)(PLACE4 + box(n))));
    x = z.x + z.w - (n == 10 ? 0 : 2);                   // (10's number is wider)
    y = z.y - 2;
    if (x > 122) { x = 122; y -= 2; }                    // the last box: kept on screen, clear of its 10
}

static void drawPuck(const Craps &g) {
    int x, y;
    if (puckT < PUCK_T) {
        int x0, y0, x1, y1;
        puckPos(g, puckFrom, x0, y0);
        puckPos(g, g.point, x1, y1);
        int e = fx::ease(fx::OUT_BACK, puckT, PUCK_T);
        x = x0 + (((x1 - x0) * e) >> 8);
        y = y0 + (((y1 - y0) * e) >> 8) - ((fx::isin(puckT * 128 / PUCK_T) * 8) >> 8);
        bool on = puckT * 2 < PUCK_T ? puckFrom != 0 : g.point != 0;
        art::puck(x, y, on, on ? (puckT * 2 < PUCK_T ? puckFrom : g.point) : 0);
        return;
    }
    puckPos(g, shownPoint, x, y);
    art::puck(x, y, shownPoint != 0, shownPoint);
}

static void spotInfo(const Craps &g, uint8_t z, char *name, char *pays, bool &off) {
    *name = *pays = 0;
    off = false;
    if (z == 0xFF) return;
    uint8_t b = zones::at(g.opt.table, z).bet;
    if (b >= Z_CHIP0) {
        if (b == Z_ROLL) { fmtStr(name, "ROLL"); fmtStr(pays, g.canRoll() == D_OK ? "HOLD A" : "BET LINE"); }
        else { fmtMoney(fmtStr(name, "CHIP "), art::CHIP_VALUE[b - Z_CHIP0]); fmtStr(pays, "A PICKS"); }
        return;
    }
    static const char *const NAME[] = {"PASS LINE", "DONT PASS", "ODDS", "LAY", "COME", "FIELD"};
    static const char *const RATIO[] = {"9:5", "7:5", "7:6", "7:6", "7:5", "9:5"};
    static const char *const TRUE_ODDS[] = {"2:1", "3:2", "6:5", "6:5", "3:2", "2:1"};
    static const char *const LAY_ODDS[] = {"1:2", "2:3", "5:6", "5:6", "2:3", "1:2"};
    if (b <= FIELD) {
        fmtStr(name, NAME[b]);
        int8_t i = box(g.point);
        if (b == PASS_ODDS) fmtStr(pays, i >= 0 ? TRUE_ODDS[i] : "--");
        else if (b == DONT_ODDS) fmtStr(pays, i >= 0 ? LAY_ODDS[i] : "--");
        else if (b == COME && !g.point) fmtStr(pays, "--");
        else fmtStr(pays, b == FIELD ? "1:1+" : "1:1");
        return;
    }
    if (b <= PLACE10) { fmtInt(fmtStr(name, "PLACE "), BOX_NUM[b - PLACE4]); fmtStr(pays, RATIO[b - PLACE4]); }
    else if (b <= HARD10) { fmtInt(fmtStr(name, "HARD "), 4 + 2 * (b - HARD4)); fmtStr(pays, b == HARD4 || b == HARD10 ? "7:1" : "9:1"); }
    else if (b == ANY7) { fmtStr(name, "ANY 7"); fmtStr(pays, "4:1"); }
    else if (b == ANYCRAPS) { fmtStr(name, "ANY CRAPS"); fmtStr(pays, "7:1"); }
    else if (b == YO) { fmtStr(name, "YO 11"); fmtStr(pays, "15:1"); }
    else if (b >= CODDS4) { fmtInt(fmtStr(name, "ODDS ON "), BOX_NUM[b - CODDS4]); fmtStr(pays, TRUE_ODDS[b - CODDS4]); }
    off = disp[b] > 0 && !g.working(b);
}

static uint8_t lookAt(const Craps &g) {
    if (zoneSel == 0xFF) return 1;
    int x = zones::at(g.opt.table, zoneSel).ax;
    return x < 40 ? 0 : (x > 90 ? 2 : 1);
}

void render(const Craps &g, uint32_t frame) {
    // After the cam the whole table is redrawn.
    static bool wasCam = false;
    if (cam::render(frame)) { invalidate(); wasCam = true; return; }
    if (wasCam) invalidate();
    wasCam = false;

    uint8_t expr = face;
    if (bubLen && bubChars < bubLen && ((frame >> 2) & 1)) expr = wall::E_TALK;
    if (blinking) expr = wall::E_BLINK;
    uint8_t look = lookAt(g);
    char name[16], pays[12];
    bool off;
    spotInfo(g, zoneSel, name, pays, off);
    uint8_t bz = zoneSel;
    int32_t onSpot = 0;
    if (bz != 0xFF && zones::at(g.opt.table, bz).bet < Z_CHIP0) onSpot = disp[zones::at(g.opt.table, bz).bet];

    Sig w;
    w.add(expr); w.add(look); w.add(bubLen); w.add(bubChars); w.add(shown); w.add(waggle);
    w.add(purseFlash ? 1 + ((purseFlash >> 2) & 1) : 0); w.add(bz); w.add(onSpot); w.add(off);
    w.add(boardLag); w.add(g.hist[0] | g.hist[1] << 8 | g.hist[2] << 16 | g.hist[3] << 24);
    Sig f;
    f.add(g.opt.table); f.add(bz); f.add(shownPoint); f.add(g.point); f.add(puckT); f.add(holdW);
    for (uint8_t b = 0; b < BET_COUNT; b++) { f.add(disp[b]); f.add(paid[b]); }
    int cx = 0, cy = 0;
    bool hasCursor = cursorChip(g, frame, cx, cy);
    f.add(hasCursor ? cx | cy << 8 : -1);

    int lo, hi;
    movingRows(lo, hi);
    bool moving = hi >= lo, wasMoving = curHi >= curLo;
    auto touched = [&](int a, int b) {
        return (moving && lo <= b && hi >= a) || (wasMoving && curLo <= b && curHi >= a);
    };
    bool drawWall = forceAll || w.h != sigWall || touched(0, RAIL_Y + RAIL_H - 1);
    bool drawFelt = forceAll || f.h != sigFelt || touched(FELT_Y, TRIM_Y);
    bool barMoving = touched(BAR_Y, 127);
    forceAll = false;
    sigWall = w.h; sigFelt = f.h;
    curLo = (int16_t)lo; curHi = (int16_t)hi;

    dbg::profStart();
    if (drawWall) {
        wall::backdrop();
        wall::dealer(expr, look);
        wall::stick(waggle);
        if (bubLen) {
            // The speech bubble: CHBlackjack's, typed a letter a frame.
            int x = BUBBLE_X, y = BUBBLE_Y, bw = BUBBLE_W, bh = BUBBLE_H;
            panel(x, y, bw, bh, 4, WHITE, INK);
            for (int i = 0; i < 5; i++) {
                gfx_hline(x - 5 + i, y + 22 + i, 6 - i, WHITE);
                gfx_pixel(x - 6 + i, y + 22 + i, INK);
            }
            gfx_vline(x, y + 21, 4, WHITE);
            int lines = 1;
            for (const char *p = bubText; *p; p++) if (*p == '\n') lines++;
            int ty = y + bh / 2 - (lines * 7) / 2 + 1, typed = bubChars;
            for (const char *p = bubText; *p;) {
                const char *e = strchr(p, '\n');
                int len = e ? (int)(e - p) : (int)strlen(p);
                char line[20];
                int n = len < 19 ? len : 19;
                memcpy(line, p, n);
                line[n] = 0;
                int lx = x + bw / 2 - text35Width(line) / 2;
                int show = typed < n ? (typed > 0 ? typed : 0) : n;
                line[show] = 0;
                text35(lx, ty, line, INK);
                typed -= len + 1;
                ty += 7;
                if (!e) break;
                p = e + 1;
            }
        } else {
            wall::plaque(shown, purseFlash, name, pays, onSpot, off);
        }
        wall::board(g, boardLag);                            // the roll in the air isn't on it yet
        wall::rail();
    }
    dbg::prof(0);
    if (drawFelt) {
        felt::draw(g);
        if (bz != 0xFF) felt::hover(g.opt.table, bz, denyT ? RED : FX_A);
        for (uint8_t b = 0; b < BET_COUNT; b++) {
            if (disp[b] <= 0 && paid[b] <= 0) continue;
            int x, y;
            if (!spot(g, b, x, y)) continue;
            if (disp[b] > 0) {
                art::stackSmall(x, y, disp[b], 3);
                if (!g.working(b) && step == S_IDLE) dither(x - 5, y - 4, 11, 8, INK, 0);   // OFF: dimmed
            }
            if (paid[b] > 0) art::stackSmall(x + 6, y, paid[b], 3);
        }
        if (holdW) gfx_hline(0, TRIM_Y, holdW, CYAN);
    }
    dbg::prof(1);
    if (barMoving) bar::invalidate();
    int8_t cursorBar = -1;
    if (bz != 0xFF && zones::inBar(g.opt.table, bz)) cursorBar = (int8_t)(zones::at(g.opt.table, bz).bet - Z_CHIP0);
    bool drewBar = bar::draw(denom, cursorBar, g.canRoll() == D_OK && step == S_IDLE, rollHeld, frame);
    if (drawWall || drawFelt || drewBar) {
        drawPuck(g);                                     // over both bands: it straddles the rail
        for (auto &fl : flies) {
            if (!fl.T || fl.t < 0) continue;
            int x, y; flyPos(fl, x, y);
            if (fl.kind == F_IN) art::chipSmall(x, y, (uint8_t)art::chipDenom(fl.value), true);
            else art::stack(x, y, fl.value, 4);
        }
        if (hasCursor) {
            const Zone &z = zones::at(g.opt.table, bz);
            gfx_fillEllipse(z.ax, z.ay + 1, 4, 1, FELT_DK);
            art::chipSmall(cx, cy, denom, true);
        }
        fx::drawParticles();
        fx::drawFloats();
        fx::drawBanner();
        // Every shake (a die off the wall, a seven out) starts and ends while
        // the dice cam is up, so this never moves anything here. INK, as in
        // the cam: one fill keeps one shake path (gfx_scroll) in the build.
        fx::applyShake(0, TRIM_Y - 1, INK);
    }
    dbg::prof(2);
}

}  // namespace present
