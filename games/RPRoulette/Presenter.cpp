#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The presenter (Presenter.h): the rules' events in; flying chips, the
// croupier, the camera whip, the ball and the payout out; and the play screen
// drawn in parts that repaint only when something in them changed.
#include <RPGame.h>
#include <string.h>
#include "Presenter.h"
#include "Fx.h"
#include "Roulette.h"
#include "Spots.h"
#include "Wheel.h"
#include "Ball.h"
#include "WheelArt.h"
#include "Layout.h"
#include "Table.h"
#include "ChipArt.h"
#include "Felt.h"
#include "Bar.h"
#include "Remap.h"
#include "Sounds.h"
#include "src/assets/Assets.h"

namespace present {

using namespace lay;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum FlyKind : uint8_t {
    DROP,          // glove -> spot (one chip)
    LIFT,          // spot -> glove
    HOME,          // a stack to the player (CLR, the winnings)
    REBET_IN,      // the player's chips back onto a spot
    SWEEP,         // a losing stack to the croupier's rack
    WIN_IN,        // winnings from the rack, beside a winning stack
};
struct Fly { int16_t x0, y0, x1, y1; int16_t t; uint8_t T, spot, kind, arc; int32_t value; };
static Fly flies[16];

static uint8_t disp[spots::NBET];       // the stakes the felt shows (chips land before they count)
static int32_t shown, pending;          // the plaque's purse, and money still on its way home
static uint8_t purseFlash;

// Winnings stacked beside their stakes during the payout.
struct Win { uint8_t spot; int32_t shown, total; };
static Win wins[20];
static uint8_t nWins;

// The player's glove: Q4 position gliding to the rules' glove, a tap dip,
// a red flash.
static int16_t gx16, gy16;
static uint8_t tapT, denyT, denyWhy;
static int32_t armedAmt;                // CLR armed: what a second press clears

// The croupier.
static char bubText[40];
static uint8_t bubChars, bubLen, bubHold, face, faceT, blinkT = 90, blinking, look = 1;
static bool bubOn;

// The camera: 0 = the layout, 128 = the wheel; it whips between them.
static int16_t cam;                     // even
static int8_t whipDir;
static uint8_t whipT;

// The spin.
static ball::Plan plan;
static ball::Ball live;
static bool solving, ballOn, spinning;
static uint8_t num;                     // this spin's number
static uint8_t landT, hiPocket = 0xFF, deflLit, deflT;

// The payout: the croupier's glove, the dolly, the stages.
enum Stage : uint8_t { ST_NONE, ST_WHIP_OUT, ST_DOLLY, ST_SWEEP, ST_PAY, ST_COLLECT };
static Stage stage;
static uint8_t stT;
static int16_t cx16, cy16, ctx, cty;    // the croupier's glove (Q4) and its target
static bool crOn, dollyOn;
static uint8_t crTap;
static int32_t winTotal, lostTotal, stakeTotal;
static bool bigWin;
static uint8_t veilT;                   // the big win's dark veil under its banner

static bool quick, ff;

void fastForward(bool on) { ff = on; }

static wheelart::BallView ballView(bool trail) {
    wheelart::BallView v = {(uint32_t)live.a, (int16_t)live.r, (int16_t)live.z, trail ? live.w : 0,
                            live.ph >= ball::DONE};
    return v;
}

static Fly *fly(int x0, int y0, int x1, int y1, uint8_t spot, int32_t value, uint8_t kind,
                uint8_t T, uint8_t arc, int delay = 0) {
    for (auto &f : flies) {
        if (f.T) continue;
        f.x0 = (int16_t)x0; f.y0 = (int16_t)y0; f.x1 = (int16_t)x1; f.y1 = (int16_t)y1;
        f.t = (int16_t)-delay; f.T = T; f.spot = spot; f.kind = kind; f.arc = arc; f.value = value;
        return &f;
    }
    // Pool full: the effect happens at once.
    if (kind == DROP || kind == REBET_IN) disp[spot] = (uint8_t)(disp[spot] + value);
    if (kind == WIN_IN) for (uint8_t i = 0; i < nWins; i++) if (wins[i].spot == spot) wins[i].shown += value;
    if (kind == HOME) pending -= value;
    return nullptr;
}

static int tipX() { return gx16 >> 4; }
static int tipY() { return (gy16 >> 4) - 4; }
static uint8_t pockets(const Roulette &r) { return r.us ? 38 : 37; }

static uint8_t exprFor(uint8_t f) {
    static const uint8_t E[5] = {table::E_NORMAL, table::E_ANGRY, table::E_RAISED, table::E_SMILE, table::E_SURPRISED};
    return f < 5 ? E[f] : table::E_NORMAL;
}

void reset(const Roulette &r) {
    memset(flies, 0, sizeof flies);
    memcpy(disp, r.bet, sizeof disp);
    shown = r.purse; pending = 0; purseFlash = 0;
    gx16 = (int16_t)(r.gx << 4); gy16 = (int16_t)(r.gy << 4);
    tapT = denyT = 0; armedAmt = 0;
    bubOn = false; face = F_NORMAL;
    cam = 0; whipDir = 0;
    solving = ballOn = spinning = false;
    landT = 0; hiPocket = 0xFF; deflLit = deflT = 0;
    stage = ST_NONE; crOn = dollyOn = false; nWins = 0; veilT = 0;
    invalidate();
}

void dismissBubble() { if (bubOn) { bubOn = false; face = F_NORMAL; } }

static void whip(int8_t dir) {
    whipDir = dir; whipT = 0;
    audio::sfx(Sfx::Whoosh);
    invalidate();
}

static const uint8_t RACK_X = TRAY_X + TRAY_W / 2, RACK_Y = RAIL_Y + 2;

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
void onEvents(Roulette &r) {
    Event e;
    while (r.popEvent(e)) {
        spots::Geo g = spots::geo(e.a, r.us);
        switch (e.type) {
            case Ev::BetAdd:
                dismissBubble();
                fly(tipX(), tipY(), g.ax, g.ay, e.a, e.amount, DROP, 6, 0);
                tapT = 1;
                break;
            case Ev::BetRemove:
                dismissBubble();
                for (auto &f : flies)                // this spot's chips still in the air land first
                    if (f.T && f.kind == DROP && f.spot == e.a) { disp[e.a] = (uint8_t)(disp[e.a] + f.value); f.T = 0; }
                disp[e.a] = (uint8_t)(disp[e.a] > e.amount ? disp[e.a] - e.amount : 0);
                fly(g.ax, g.ay, tipX(), tipY(), e.a, e.amount, LIFT, 6, 0);
                audio::sfx(Sfx::Chip);
                break;
            case Ev::Clear: {
                for (auto &f : flies)                // chips still in the air go home with the rest
                    if (f.T && (f.kind == DROP || f.kind == REBET_IN)) { disp[f.spot] = (uint8_t)(disp[f.spot] + f.value); f.T = 0; }
                pending += e.amount;
                int k = 0;
                for (uint8_t s = 0; s < spots::NBET; s++) {
                    if (!disp[s]) continue;
                    spots::Geo h = spots::geo(s, r.us);
                    fly(h.ax, h.ay, 8, 140, s, disp[s], HOME, 18, 10, (k++ & 7) * 2);
                    disp[s] = 0;
                }
                armedAmt = 0;
                audio::sfx(Sfx::Whoosh);
                break;
            }
            case Ev::ClearArmed: armedAmt = e.amount; audio::sfx(Sfx::Select); break;
            case Ev::Rebet: {
                dollyOn = false;
                nWins = 0;
                int k = 0;
                for (uint8_t s = 0; s < spots::NBET; s++) {
                    if (r.bet[s] <= disp[s]) { disp[s] = r.bet[s]; continue; }
                    spots::Geo h = spots::geo(s, r.us);
                    fly(-8, 132, h.ax, h.ay, s, r.bet[s] - disp[s], REBET_IN, 14, 8, (k++ & 7) * 2);
                }
                invalidate();
                break;
            }
            case Ev::ChipSel: dismissBubble(); audio::sfx(Sfx::Chip); break;
            case Ev::Cursor: dismissBubble(); armedAmt = 0; audio::sfx(Sfx::Cursor); break;
            case Ev::Deny: denyT = 24; denyWhy = e.a; armedAmt = 0; audio::sfx(Sfx::Deny); break;
            case Ev::Say: {
                if (stage != ST_NONE) { face = e.b; faceT = 150; break; }   // the payout: the plaque stays up
                const char *t = r.lineText(e.a, e.c, bubText);
                if (t != bubText) { strncpy(bubText, t, sizeof bubText - 1); bubText[sizeof bubText - 1] = 0; }
                bubLen = (uint8_t)strlen(bubText);
                bubChars = 0; bubHold = e.a == L_PLACE ? 40 : 100; bubOn = true; face = e.b;
                break;
            }
            case Ev::NoMoreBets:
                // Solve the ball for the number now, a slice a tick, while the
                // croupier calls it.
                armedAmt = 0;
                quick = r.opt.pace != 0;
                ball::begin(plan, wheel::indexOf(e.a, r.us), fx::rnd(), pockets(r), quick);
                solving = true;
                break;
            case Ev::Spin:
                num = e.a;
                if (solving) { ball::solve(plan, 0xFFFF); solving = false; }
                ball::launch(plan, live);
                ballOn = spinning = true;
                landT = 0; hiPocket = 0xFF; deflLit = 0;
                whip(1);
                break;
            case Ev::Settle: {
                // The payout show: back to the layout, the dolly, the sweeps,
                // the winnings.
                dismissBubble();                   // the plaque: the purse is about to roll
                stakeTotal = lostTotal = winTotal = 0;
                nWins = 0;
                for (uint8_t s = 0; s < spots::NBET; s++) {
                    if (!r.bet[s]) continue;
                    stakeTotal += r.bet[s];
                    if (!spots::covers(s, e.a, r.us)) { lostTotal += r.bet[s]; continue; }
                    int32_t w = (int32_t)r.bet[s] * spots::payout(s, r.us);
                    winTotal += w;
                    if (nWins < 20) wins[nWins++] = {s, 0, w};
                    else pending += 0;
                }
                bool straight = r.bet[spots::straightId(e.a)] != 0;
                bigWin = winTotal > 0 && (straight || winTotal - lostTotal >= 17 * stakeTotal);
                pending += winTotal;               // the rules paid it; it shows as it arrives
                stage = ST_WHIP_OUT; stT = 0;
                whip(-1);
                break;
            }
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Per tick
// ---------------------------------------------------------------------------
static void ballEvents(uint8_t ev) {
    if (ev & ball::EV_FLICK) audio::sfx(Sfx::Flick);
    if (ev & ball::EV_ROLL) {
        int32_t w = live.w < 0 ? -live.w : live.w;
        int32_t hz = 650 + (w / live.n) / 5;
        audio::blip((uint16_t)(hz > 1500 ? 1500 : hz), 2, true);
    }
    if (ev & ball::EV_LEAVE) audio::blip(1400, 4, true);
    if (ev & ball::EV_FRET) audio::blip((uint16_t)(2300 + (fx::rnd() & 511)), 4);
    if (ev & ball::EV_BOUNCE) audio::blip(1500, 3, true);
    wheelart::BallView bv = ballView(false);
    int bx, by;
    wheelart::ballXY(WHEEL_CX, WHEEL_CY, bv, live.n, bx, by);
    if (ev & ball::EV_DEFLECT) {
        audio::sfx(Sfx::Clack);
        deflLit = (uint8_t)(1 << (live.defl & 7)); deflT = 6;
        fx::burst(fx::SPARK, bx, by, 4, 24, WHITE);
    }
    if (ev & ball::EV_LAND) {
        audio::sfx(Sfx::Thunk);
        audio::led(audio::LED_BLINK);
        hiPocket = live.pocket; landT = 1;
        uint8_t c = wheel::colour(num);
        fx::burst(fx::SPARK, bx, by, 10, 36, GOLD);
        fx::burst(fx::DUST, bx, by + 1, 8, 20, c == wheel::GREEN ? FELT_LT : c == wheel::RED_NUM ? RED : INK);
    }
}

static void stagePay(const Roulette &r) {
    // Winnings fly out of the rack beside each winning stack, a little apart.
    for (uint8_t i = 0; i < nWins; i++) {
        spots::Geo g = spots::geo(wins[i].spot, r.us);
        fly(RACK_X, RACK_Y, g.ax + 7, g.ay, wins[i].spot, wins[i].total, WIN_IN, 16, 10, i * 4);
    }
    char buf[12];
    if (winTotal > 0) {
        *fmtMoney(fmtStr(buf, "+"), winTotal) = 0;
        spots::Geo g = spots::geo(wins[0].spot, r.us);
        fx::floatText(buf, g.ax + 4, g.ay - 10, GOLD);
        if (bigWin) {
            *fmtStr(fmtInt(buf, spots::payout(spots::straightId(num), r.us)), " TO 1!") = 0;
            fx::banner(r.bet[spots::straightId(num)] ? buf : "BIG WIN!", fx::B_RAINBOW, 70, 110);
            fx::fountain(fx::CONFETTI, 40, 100, 20);
            fx::fountain(fx::CONFETTI, 88, 100, 20);
            fx::fountain(fx::COIN, 64, 100, 8);
            audio::sfx(Sfx::BigWin);
            audio::led(audio::LED_PARTY);
            veilT = 110;
        } else {
            fx::banner("WIN!", fx::B_GOLD, 70, 60);
            fx::fountain(fx::CONFETTI, 64, 100, 14);
            audio::sfx(Sfx::Win);
            audio::led(audio::LED_BLINK);
        }
    }
}

static void stageCollect(const Roulette &r) {
    // The winnings slide home to the player; the purse rolls up as they land.
    for (uint8_t i = 0; i < nWins; i++) {
        spots::Geo g = spots::geo(wins[i].spot, r.us);
        fly(g.ax + 7, g.ay, 8, 140, wins[i].spot, wins[i].total, HOME, 18, 10, i * 3);
    }
    nWins = 0;
}

static bool flying(uint8_t kind) {
    for (auto &f : flies) if (f.T && f.kind == kind) return true;
    return false;
}

static void payout(const Roulette &r) {
    stT++;
    uint8_t slow = quick ? 1 : 2;
    switch (stage) {
        case ST_WHIP_OUT:
            if (whipDir) break;
            // The croupier's red glove comes from the rack to the number.
            crOn = true;
            cx16 = (int16_t)(RACK_X << 4); cy16 = (int16_t)((RACK_Y + 4) << 4);
            {
                spots::Geo g = spots::geo(spots::straightId(num), r.us);
                ctx = g.ax; cty = g.ay;
            }
            crTap = 0;
            stage = ST_DOLLY; stT = 0;
            break;
        case ST_DOLLY:
            if (stT == 12 * slow) { crTap = 1; audio::sfx(Sfx::Select); }
            if (stT == 12 * slow + 4) {
                dollyOn = true;
                fx::burst(fx::DUST, ctx, cty + 2, 10, 20, FELT_LT);
            }
            if (stT >= 18 * slow + 6) {
                ctx = RACK_X; cty = RACK_Y + 4;            // off back to the rack
                // Every losing stack goes to the rack, one after another.
                int k = 0;
                for (uint8_t s = 0; s < spots::NBET; s++) {
                    if (!disp[s] || spots::covers(s, num, r.us)) continue;
                    spots::Geo g = spots::geo(s, r.us);
                    fly(g.ax, g.ay, RACK_X, RACK_Y, s, disp[s], SWEEP, 16, 10, (k++) * 3);
                    disp[s] = 0;
                }
                if (k) {
                    audio::sfx(Sfx::Rake);
                    char buf[12];
                    *fmtMoney(fmtStr(buf, "-"), lostTotal) = 0;
                    if (!winTotal) { fx::floatText(buf, 64, 70, RED); audio::sfx(Sfx::Lose); }
                }
                stage = ST_SWEEP; stT = 0;
            }
            break;
        case ST_SWEEP:
            if (flying(SWEEP) || stT < 10) break;
            crOn = false;
            stagePay(r);
            stage = ST_PAY; stT = 0;
            break;
        case ST_PAY:
            if (flying(WIN_IN) || stT < (bigWin ? 70 : 24) * slow / 2) break;
            stageCollect(r);
            stage = ST_COLLECT; stT = 0;
            break;
        case ST_COLLECT:
            if (flying(HOME)) break;
            stage = ST_NONE;
            break;
        default: break;
    }
}

void update(const Roulette &r) {
    quick = r.opt.pace != 0;
    // The glove halves its distance to the rules' glove every tick.
    gx16 = (int16_t)(gx16 + (((r.gx << 4) - gx16 + 1) >> 1));
    gy16 = (int16_t)(gy16 + (((r.gy << 4) - gy16 + 1) >> 1));
    if (tapT && ++tapT > 8) tapT = 0;
    if (denyT) denyT--;
    if (!r.clrArm) armedAmt = 0;
    if (crOn) {
        cx16 = (int16_t)(cx16 + (((ctx << 4) - cx16) >> 2));
        cy16 = (int16_t)(cy16 + (((cty << 4) - cy16) >> 2));
    }
    if (crTap && ++crTap > 12) crTap = 0;

    for (auto &f : flies) {
        if (!f.T) continue;
        if (++f.t < f.T) continue;
        switch (f.kind) {
            case DROP: case REBET_IN:
                disp[f.spot] = (uint8_t)(disp[f.spot] + f.value);
                audio::sfx(Sfx::Chip);
                break;
            case WIN_IN:
                for (uint8_t i = 0; i < nWins; i++) if (wins[i].spot == f.spot) wins[i].shown += f.value;
                audio::sfx(Sfx::Coin);
                break;
            case HOME: pending -= f.value; purseFlash = 24; audio::sfx(Sfx::Coin); break;
            default: break;
        }
        f.T = 0;
    }

    // The camera whip: one step a tick, eased.
    if (whipDir) {
        uint8_t W = quick ? 8 : 12;
        whipT++;
        int e = fx::ease(fx::IN_OUT, whipT, W);
        int p = (128 * e) >> 8;
        cam = (int16_t)((whipDir > 0 ? p : 128 - p) & ~1);
        if (whipT >= W) {
            cam = whipDir > 0 ? 128 : 0;
            if (whipDir < 0) ballOn = false;
            whipDir = 0;
            invalidate();
        }
    }

    // The ball: solved a slice at a time, then run.
    if (solving && ball::solve(plan, 25)) solving = false;
    if (ballOn) {
        ballEvents(ball::step(live));
        if (ff && live.ph < ball::DONE) ballEvents(ball::step(live));
    }
    if (landT && landT < 255) {
        landT++;
        if (landT == 8) {
            char s[14], *p = wheel::name(s, num);
            uint8_t c = wheel::colour(num);
            *fmtStr(p, c == wheel::GREEN ? " GREEN" : c == wheel::RED_NUM ? " RED" : " BLACK") = 0;
            fx::banner(s, c == wheel::GREEN ? fx::B_GREEN : c == wheel::RED_NUM ? fx::B_RED : fx::B_BLACK, 58,
                       quick ? 50 : 80);
            fx::shake(6, 2);
        }
        if (landT >= (quick ? 36 : 70)) spinning = false;
    }
    if (deflT && !--deflT) deflLit = 0;
    if (stage) payout(r);
    if (veilT) veilT--;

    // The purse rolls toward the money that has actually come home.
    int32_t d = (r.purse - pending) - shown;
    if (d) {
        int32_t step = d / 5;
        if (!step) step = d > 0 ? 1 : -1;
        shown += step;
        if (d > 0 && (shown & 3) == 0) audio::blip((uint16_t)(3000 + ((shown * 7) & 511)), 8);
    }
    if (purseFlash) purseFlash--;

    // Speech bubble typewriter, the croupier's face.
    if (bubOn) {
        if (bubChars < bubLen) {
            bubChars++;
            if (bubChars & 1) audio::blip((uint16_t)(1900 + (bubChars * 97) % 700), 12);
        } else if (bubHold) bubHold--;
        else { bubOn = false; face = F_NORMAL; }
    } else if (faceT && !--faceT) face = F_NORMAL;
    if (blinking) blinking--;
    else if (--blinkT == 0) { blinking = 6; blinkT = (uint8_t)fx::rndRange(90, 220); }
    int lx = 64;
    if (r.phase == Phase::Betting) lx = tipX();
    else if (ballOn && cam) {
        wheelart::BallView bv = ballView(false);
        int by;
        wheelart::ballXY(WHEEL_CX + 128 - cam, WHEEL_CY, bv, live.n, lx, by);
    } else if (crOn) lx = cx16 >> 4;
    look = lx < 40 ? 0 : (lx > 88 ? 2 : 1);

    fx::update();
}

bool busy() {
    for (auto &f : flies) if (f.T) return true;
    return whipDir || spinning || solving || stage != ST_NONE || veilT;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
// The croupier's glove flicking the ball in: its fingertip, and whether it shows.
static bool flickGlove(int &x, int &y) {
    if (!ballOn || cam != 128 || live.ph > ball::TRACK || live.phaseT >= 16) return false;
    wheelart::BallView bv = ballView(false);
    wheelart::ballXY(WHEEL_CX, WHEEL_CY, bv, live.n, x, y);
    y -= 4 + (live.ph == ball::TRACK ? live.phaseT : 0);
    return true;
}

struct Sig {
    uint32_t h = 2166136261u;
    void add(int32_t v) { h = (h ^ (uint32_t)v) * 16777619u; }
};

static uint32_t sigWall = 0, sigTable = 0;
static bool forceAll = true;
static int16_t wheelLo = TABLE_Y, wheelHi = GFX_H;   // the wheel's moving rows last frame
static bool wheelFull = true;

void invalidate() { forceAll = true; wheelFull = true; }

static void flyPos(const Fly &f, int &x, int &y) {
    int e = fx::ease(f.kind == DROP ? fx::OUT_BOUNCE : fx::OUT_CUBIC, f.t, f.T);
    x = f.x0 + (((f.x1 - f.x0) * e) >> 8);
    y = f.y0 + (((f.y1 - f.y0) * e) >> 8) - ((fx::isin(f.t * 128 / f.T) * f.arc) >> 8);
}

static bool betting(const Roulette &r) { return r.phase == Phase::Betting && !cam && !whipDir; }

// The plate at the foot of the layout: words in their colours, centred
// (CHChess's plate).
static void plate(const char *const *w, const uint8_t *c, uint8_t n, int ox) {
    int tw = 0;
    for (uint8_t i = 0; i < n; i++) if (w[i][0]) tw += text35Width(w[i]) + 5;   // a space between words
    if (tw > 0) tw -= 5;
    int pw = tw + 8;
    fillRound(64 - pw / 2 + ox, PLATE_Y, pw, 11, 2, NAVY);
    roundRect(64 - pw / 2 + ox, PLATE_Y, pw, 11, 2, GOLD);
    int x = 64 - tw / 2 + ox;
    for (uint8_t i = 0; i < n; i++) {
        if (!w[i][0]) continue;
        text35(x, PLATE_Y + 3, w[i], c[i]);
        x += text35Width(w[i]) + 5;
    }
}

static void bettingPlate(const Roulette &r) {
    char nums[24], amt[12], pay[16];
    const char *w[4];
    uint8_t c[4] = {WHITE, WHITE, GOLD, SILVER};
    uint8_t s = r.cursor;
    if (denyT) {
        static const char *const WHY[5] = {"SPOT MAX", "TABLE MAX", "NO CASH", "NO BET", "NOTHING THERE"};
        w[0] = WHY[denyWhy < 5 ? denyWhy : 4];
        c[0] = (denyT & 4) ? RED : SILVER;
        plate(w, c, 1, 0);
        return;
    }
    if (s == spots::CLR) {
        w[0] = armedAmt ? "A AGAIN TO CLEAR" : "CLEAR THE TABLE";
        *fmtMoney(amt, armedAmt ? armedAmt : r.onTable()) = 0;
        w[1] = amt; c[1] = GOLD;
        plate(w, c, r.onTable() ? 2 : 1, 0);
        return;
    }
    if (s >= spots::CHIP0 && s < spots::SPIN) return;     // the chip buttons speak for themselves
    if (s == spots::SPIN) {
        if (!r.onTable()) { w[0] = "PLACE A BET FIRST"; plate(w, c, 1, 0); return; }
        uint8_t nb = 0;
        for (uint8_t i = 0; i < spots::NBET; i++) if (r.bet[i]) nb++;
        *fmtMoney(amt, r.onTable()) = 0;
        *fmtStr(fmtInt(fmtStr(pay, "ON "), nb), nb == 1 ? " BET" : " BETS") = 0;
        w[0] = "SPIN"; c[0] = GOLD; w[1] = amt; c[1] = GOLD; w[2] = pay; c[2] = WHITE;
        plate(w, c, 3, 0);
        return;
    }
    *spots::numbers(nums, s, r.us) = 0;
    amt[0] = 0;
    if (r.bet[s]) *fmtMoney(amt, r.bet[s]) = 0;
    *fmtStr(fmtInt(pay, spots::payout(s, r.us)), " TO 1") = 0;
    w[0] = spots::kindName(s, r.us); w[1] = nums; w[2] = amt; w[3] = pay;
    int tw = text35Width(w[0]) + text35Width(nums) + text35Width(amt) + text35Width(pay) + 5 * (amt[0] ? 3 : 2);
    if (tw > 120) nums[0] = 0;
    plate(w, c, 4, 0);
}

// The number called out word by word, then what it paid.
static void resultPlate(const Roulette &r, int ox) {
    char n[4], amt[16];
    *wheel::name(n, num) = 0;
    const char *w[4];
    uint8_t c[4] = {WHITE, SILVER, SILVER, SILVER};
    uint8_t col = wheel::colour(num);
    w[0] = n;
    w[1] = col == wheel::GREEN ? "GREEN" : col == wheel::RED_NUM ? "RED" : "BLACK";
    c[1] = col == wheel::GREEN ? FELT_LT : col == wheel::RED_NUM ? RED : SILVER;
    if (stage >= ST_PAY || stage == ST_NONE) {
        if (winTotal > 0) { *fmtMoney(fmtStr(amt, "YOU WIN "), winTotal) = 0; w[2] = amt; c[2] = GOLD; }
        else w[2] = "THE HOUSE WINS";
        plate(w, c, 3, ox);
        return;
    }
    if (col == wheel::GREEN) { plate(w, c, 2, ox); return; }
    w[2] = (num & 1) ? "ODD" : "EVEN";
    static const char *const DOZ[3] = {"1st 12", "2nd 12", "3rd 12"};
    w[3] = DOZ[(num - 1) / 12];
    plate(w, c, 4, ox);
}

// Rings on every number the spot under the glove covers, and on its own
// cell (outside bets); a ghost chip where a chip would land on a line.
static void hover(const Roulette &r) {
    uint8_t s = r.cursor;
    if (!spots::isBet(s)) return;
    for (uint8_t n = 0; n < pockets(r); n++) {
        if (!spots::covers(s, n, r.us)) continue;
        spots::Geo g = spots::geo(spots::straightId(n), r.us);
        felt::ring(g.x0, g.y0, g.x1, g.y1, FX_B, 0);
    }
    spots::Geo g = spots::geo(s, r.us);
    if (g.x1 > g.x0) felt::ring(g.x0, g.y0, g.x1, g.y1, FX_B, 0);
    else if (!disp[s]) art::ghost(g.ax, g.ay, FX_B);
}

// Stacks back to front: rows from the top, then left to right.
static void stacks(const Roulette &r, uint32_t frame, int ox) {
    uint8_t order[spots::NBET], n = 0;
    uint16_t key[spots::NBET];
    for (uint8_t s = 0; s < spots::NBET; s++) {
        if (!disp[s]) continue;
        spots::Geo g = spots::geo(s, r.us);
        order[n] = s; key[n++] = (uint16_t)(g.ay << 8 | g.ax);
    }
    for (uint8_t i = 1; i < n; i++)                      // insertion sort: a few dozen at most
        for (uint8_t j = i; j > 0 && key[j - 1] > key[j]; j--) {
            uint16_t k = key[j]; key[j] = key[j - 1]; key[j - 1] = k;
            uint8_t o = order[j]; order[j] = order[j - 1]; order[j - 1] = o;
        }
    for (uint8_t i = 0; i < n; i++) {
        spots::Geo g = spots::geo(order[i], r.us);
        bool hot = betting(r) && order[i] == r.cursor;
        art::miniStack(g.ax + ox, g.ay, disp[order[i]], hot ? fx::RAIN[(frame >> 3) % 5] : -1);
    }
    for (uint8_t i = 0; i < nWins; i++) {
        if (!wins[i].shown) continue;
        spots::Geo g = spots::geo(wins[i].spot, r.us);
        art::miniStack(g.ax + 7 + ox, g.ay, wins[i].shown);
    }
}

static void layoutScene(const Roulette &r, uint32_t frame, int ox) {
    felt::draw(r.us, ox);
    if (betting(r)) hover(r);
    if (dollyOn) {
        spots::Geo g = spots::geo(spots::straightId(num), r.us);
        felt::ring(g.x0, g.y0, g.x1, g.y1, FX_A, ox);
    }
    stacks(r, frame, ox);
    if (dollyOn) {
        spots::Geo g = spots::geo(spots::straightId(num), r.us);
        sprite4(DOLLY, g.ax - 2 + ox, g.ay + 2 - 8, RM_ID);
    }
    if (veilT) dither(0, TABLE_Y + 1, 128, BAR_Y - TABLE_Y - 2, INK, 0);   // the big win owns the moment
    if (betting(r)) bettingPlate(r);
    else if (stage >= ST_DOLLY || (dollyOn && r.phase != Phase::Betting)) resultPlate(r, ox);
}

static void barBand(const Roulette &r, int ox) {
    uint8_t h = r.cursor >= spots::CLR ? (uint8_t)(r.cursor - spots::CLR) : 0xFF;
    bar::draw(r.chip, betting(r) ? h : 0xFF, armedAmt != 0, r.onTable() > 0, ox, true);
}

// The wheel's still parts (felt, bowl) are repainted only in the rows
// something moving touched, this frame or the last: the ball and its trail,
// the croupier's glove, particles, a banner.
static void wheelScene(const Roulette &r, int x0) {
    (void)r;
    wheelart::BallView bv = ballView(true);
    int lo = 999, hi = -1, a, b;
    if (ballOn) { wheelart::ballRows(WHEEL_CX + 128 - cam, WHEEL_CY, bv, live.n, a, b); lo = a; hi = b; }
    if (flickGlove(a, b)) { if (b - 16 < lo) lo = b - 16; if (b + 1 > hi) hi = b + 1; }
    if (fx::activeRows(a, b)) { if (a < lo) lo = a; if (b > hi) hi = b; }
    int by0 = lo < wheelLo ? lo : wheelLo, by1 = (hi > wheelHi ? hi : wheelHi) + 1;
    if (wheelFull || whipDir || cam != 128) { by0 = TABLE_Y; by1 = GFX_H; }
    wheelFull = false;
    wheelLo = (int16_t)lo; wheelHi = (int16_t)hi;
    uint8_t hc = landT && landT < 5 ? WHITE : FX_A;
    wheelart::draw(WHEEL_CX + 128 - cam, WHEEL_CY, live.rho, live.n, hiPocket, hc, x0, 128, TABLE_Y, GFX_H,
                   by0, by1, true, ballOn ? &bv : nullptr, deflLit);
}

static int32_t onFelt() {
    int32_t t = 0;
    for (uint8_t s = 0; s < spots::NBET; s++) t += disp[s];
    return t;
}

static void wallBand(const Roulette &r, uint8_t expr) {
    table::wall();
    table::dealer(expr, look, r.opt.dealer != 0);
    if (bubOn) table::speechBubble(bubText, bubChars);
    else table::plaque(shown, onFelt(), purseFlash);
    table::tote(r.history, r.nHist);
    table::rail();
}

bool render(const Roulette &r, uint32_t frame) {
#ifdef CHSIM_FORCE_FULL
    forceAll = true; wheelFull = true;                      // the redraw check's reference build (rpgame redraw)
#endif
    uint8_t expr = exprFor(face);
    if (bubOn && bubChars < bubLen && ((frame >> 2) & 1)) expr = table::E_TALK;
    if (blinking) expr = table::E_BLINK;

    int lo, hi;
    bool moving = fx::activeRows(lo, hi);
    bool anyFly = false, high = false;
    bool low = moving && hi >= BAR_Y - 2;               // something over the action bar
    for (auto &f : flies) if (f.T) {
        anyFly = true;
        if (f.t >= 0) {
            int x, y; flyPos(f, x, y);
            if (y - 5 < TABLE_Y) high = true;
            if (y > BAR_Y - 4) low = true;
        }
    }
    bool glove = betting(r);
    bool gliding = glove && ((gx16 >> 4) != r.gx || (gy16 >> 4) != r.gy);
    if (glove && tipY() - 16 < TABLE_Y) high = true;    // the palm reaches over the rail
    if (glove && tipY() + 2 >= BAR_Y) low = true;
    if (crOn && (cy16 >> 4) - 19 < TABLE_Y) high = true;
    { int gx, gy; if (flickGlove(gx, gy) && gy - 16 < TABLE_Y) high = true; }   // the croupier's flick
    bool show = whipDir || cam || stage || crOn || crTap || veilT;   // the wheel and the payout move every tick

    // Four parts, each redrawn only when what it shows changed or something
    // moving touched it this frame or the last: the wall, the plaque in it
    // (the purse rolls on its own), the table, the action bar.
    Sig w;
    w.add(expr); w.add(look); w.add(bubOn); w.add(bubChars);
    w.add(r.nHist); w.add(r.nHist ? r.history[0] : 0); w.add(r.opt.dealer);
    Sig q;
    q.add(shown); q.add(purseFlash ? 1 + ((purseFlash >> 2) & 1) : 0); q.add(onFelt());
    Sig t;
    t.add((int32_t)r.phase); t.add(r.cursor); t.add(r.chip); t.add(armedAmt); t.add(r.us);
    t.add(denyT ? 1 + ((denyT >> 2) & 1) : 0); t.add(dollyOn);
    for (uint8_t s = 0; s < spots::NBET; s++) t.add(disp[s]);
    if (glove) { t.add((int32_t)(frame >> 3)); t.add(gx16 >> 4); t.add(gy16 >> 4); }   // the bob, the outline, the glove
    t.add(veilT != 0);
    Sig b;
    b.add(r.chip); b.add(betting(r) ? r.cursor : 0xFF); b.add(armedAmt != 0); b.add(r.onTable() > 0);

    static bool wasMoving = false, wasWallFx = false;
    bool drawTable = forceAll || t.h != sigTable || moving || wasMoving || anyFly || gliding || tapT || denyT || show;
    wasMoving = moving;
    bool wallFx = moving && lo < TABLE_Y;
    // Anything reaching over the rail (the glove's palm, a chip going to the
    // rack) needs the wall redrawn while it is there and once after.
    static bool wasHigh = false, wasLow = false;
    static uint32_t sigPlaque = 0, sigBar = 0;
    bool drawWall = forceAll || w.h != sigWall || (drawTable && (high || wasHigh)) || wallFx || wasWallFx;
    wasWallFx = wallFx;
    bool drawPlaque = !drawWall && !bubOn && q.h != sigPlaque;
    bool drawBar = cam < 128 && (forceAll || b.h != sigBar || cam || whipDir || low || wasLow || veilT);
    if (drawTable) wasHigh = high;
    wasLow = low;
    forceAll = false;
    sigWall = w.h; sigTable = t.h; sigPlaque = q.h; sigBar = b.h;

    dbg::profStart();
    if (drawWall) wallBand(r, expr);
    else if (drawPlaque) table::plaque(shown, onFelt(), purseFlash);
    dbg::prof(0);
    if (drawBar) {
        gfx_setClip(0, BAR_Y, 128 - cam, GFX_H - BAR_Y);
        barBand(r, -cam);
        gfx_resetClip();
    }
    if (drawTable) {
        if (cam < 128) {
            gfx_setClip(0, TABLE_Y, 128 - cam, BAR_Y - TABLE_Y);
            layoutScene(r, frame, -cam);
            gfx_resetClip();
        }
        dbg::prof(1);
        if (cam > 0) wheelScene(r, 128 - cam);
        if (whipDir) {                                   // speed streaks across the move
            for (int k = 0; k < 6; k++) {
                int len = 16 + fx::rndRange(0, 24), y = fx::rndRange(TABLE_Y + 2, 126);
                static const uint8_t SC[3] = {WHITE, SILVER, FELT_LT};
                gfx_hline(fx::rndRange(0, 128 - len), y, len, SC[k % 3]);
            }
        }
        dbg::prof(2);
    }
    return drawWall || drawPlaque || drawTable || drawBar;
}

void overlay(const Roulette &r, uint32_t frame) {
    int ox = -cam;
    for (auto &f : flies) {
        if (!f.T || f.t < 0) continue;
        int x, y;
        flyPos(f, x, y);
        art::miniStack(x + ox, y, f.value);
    }
    if (betting(r)) {
        int bob = (fx::isin((int)((frame >> 3) * 40)) + 128) >> 8;
        int dip = tapT ? (tapT < 4 ? tapT : 8 - tapT) >> 1 : 0;
        sprite4(HAND, tipX() - HAND_TIP, tipY() - 15 + bob + dip, (denyT & 4) ? RM_ALERT : RM_ID);
    }
    if (crOn && cam == 0) {
        int dip = crTap ? (crTap < 6 ? crTap : 12 - crTap) >> 1 : 0;
        sprite4(HAND, (cx16 >> 4) - HAND_TIP, (cy16 >> 4) - 4 - 15 + dip, RM_CPU);
    }
    int fgx, fgy;
    if (flickGlove(fgx, fgy)) sprite4(HAND, fgx - HAND_TIP, fgy - 15, RM_CPU);   // the croupier flicks the ball in
    dbg::prof(3);
    fx::drawParticles(2);
    fx::drawFloats();
    fx::drawBanner();
    fx::applyShake(TABLE_Y, 127);
    dbg::prof(4);
}

}  // namespace present
