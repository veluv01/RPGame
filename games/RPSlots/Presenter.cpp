#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The show (Presenter.h): a phase machine that replays a settled spin step
// by step into the View that Machine.cpp draws, and the play screen's bands.
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Presenter.h"
#include "Fx.h"
#include "Layout.h"
#include "Machine.h"
#include "Sounds.h"

namespace present {

using namespace lay;
using mach::View;

enum Phase : uint8_t {
    IDLE,
    SPIN,           // reels turning and landing
    WILD,           // dragons rising up their reels
    PAY,            // lines lit, the win counting up
    ANNOUNCE,       // a banner holds the screen (feature begins, jackpot, finale)
    WHEEL,          // LUCKY 7's bonus wheel turning
    RESPIN,         // Hold and Spin: the empty cells turn
    LAND,           // ... the new coins lock one by one
    COUNT,          // ... and at the end every coin is counted
};

static View v;
static Phase phase;
static uint16_t pt;                 // frames in this phase
static uint16_t hold;               // how long PAY / ANNOUNCE / WHEEL last
static uint8_t landAt[5], landT[5], landN[5];
static uint8_t anticMask;           // reels held back for suspense
static uint8_t rain;                // frames of coin shower left
static int32_t winTo;
static bool todoFree, todoHold, todoFinale, holdEnded, armHeld, fast;
static bool bigCount;               // a big win: the amount counts up in large figures under the banner
static uint8_t todoWheel;           // 1 = announce it, 2 = spin it
static uint8_t idleT, countAt, wheelSeg;
static uint32_t wheelFrom, wheelTurn;
static char msgBuf[20];

static bool force = true;
static uint32_t sigTop, sigMid, sigLow;
static int lastLo = 999, lastHi = -1;

constexpr int SPIN_V = 150;         // reel speed, 1/256 stops a frame (~14 px)
constexpr uint8_t LAND_N = 14;      // frames a reel takes to settle
constexpr uint8_t CREEP_N = 46;     // ... when it is the one everything hangs on
constexpr uint16_t WHEEL_N = 210;

void invalidate() { force = true; }
bool busy() { return phase != IDLE; }
bool spinning() { return phase == SPIN; }
void setFast(bool f) { fast = f; }
// The arm follows the hold; let go, it swings on down to the stop, then
// springs back up and bounces to rest.
constexpr uint8_t ARM_MAX = 64, ARM_BACK = 24;
static bool armDown;
static uint8_t armT = ARM_BACK;
void setArm(uint8_t pull, bool held) {
    if (held) v.arm = pull;
    else if (pull) armDown = true;
    else { v.arm = 0; armDown = false; armT = ARM_BACK; }       // put back at rest (pause)
    armHeld = held;
}
void setButton(bool canSpin, bool pressed) { v.canSpin = canSpin; v.pressed = pressed; }

static int cellCx(const Slots &g, uint8_t reel) { return mach::reelX(g.machine, reel) + CELL / 2; }

void reset(const Slots &g) {
    memset(&v, 0, sizeof v);
    phase = IDLE;
    v.line = mach::LINE_NONE;
    // Something pleasant in the window: the last result, or each strip's start.
    for (uint8_t i = 0; i < g.reels(); i++) {
        v.stop[i] = g.res.stop[i];
        v.pos[i] = (int32_t)v.stop[i] << 8;
        for (uint8_t row = 0; row < ROWS; row++) v.cell[i][row] = g.stripSym(i, v.stop[i] + row);
    }
    v.shownPurse = g.purse;
    v.meter[0] = g.meter(J_MAJOR); v.meter[1] = g.meter(J_GRAND);
    v.rush = g.rush;
    rain = 0;
    todoFree = todoHold = todoFinale = false;
    todoWheel = 0;
    playSong(Song::None);
    fx::clear();
    invalidate();
}

void welcome(const Slots &g) {
    static const char *const NAME[M_COUNT] = {"LUCKY 7", "DRAGON FORTUNE", "SWEET"};
    static const fx::BannerStyle STYLE[M_COUNT] = {fx::B_GOLD, fx::B_RED, fx::B_RAINBOW};
    fx::banner(NAME[g.machine], STYLE[g.machine], 56, 60);
    fx::burst(fx::STAR, 64, 56, 14, 60, FX_A);
    audio::sfx(g.machine == M_FORTUNE ? Sfx::Gong : Sfx::Win);
}

// ---------------------------------------------------------------------------
// How big was it
// ---------------------------------------------------------------------------
static void celebrate(const Slots &g, int32_t won, bool finale) {
    int32_t ratio = won / g.bet();
    bool classic = g.machine != M_FORTUNE;      // a three-reel cabinet
    hold = 30;
    if (ratio >= 10) {
        bool epic = ratio >= 50, mega = ratio >= 25;
        const char *text = epic ? (g.machine == M_CLASSIC ? "JACKPOT!" : "EPIC WIN!") : (mega ? "MEGA WIN!" : "BIG WIN!");
        hold = epic ? 170 : (mega ? 120 : 80);
        bigCount = true;
        fx::banner(text, mega ? fx::B_RAINBOW : fx::B_GOLD, 56, (uint8_t)hold);
        fx::shake(epic ? 24 : (mega ? 14 : 6), epic ? 3 : 2);
        if (mega) pal::flash(INK, 0xFFF, 3);
        rain = (uint8_t)(hold - 20);
        audio::sfx(epic ? Sfx::Jackpot : Sfx::BigWin);
        audio::led(mega ? audio::LED_PARTY : audio::LED_TRIPLE);
        fx::explode(64, 60, epic ? 40 : (mega ? 28 : 16));
        fx::fountain(fx::CONFETTI, 30, 100, 12);
        fx::fountain(fx::CONFETTI, 98, 100, 12);
    } else if (won > 0) {
        if (finale) {
            char buf[14];
            fmtStr(fmtMoney(fmtStr(buf, "WON "), won), "!");
            fx::banner(buf, fx::B_GOLD, 56, 70);
            hold = 70;
        }
        audio::sfx(Sfx::Win);
        audio::led(audio::LED_BLINK);
        // Coins spill: out of the tray under LUCKY 7's reels, or up from the bar.
        fx::fountain(fx::COIN, g.machine == M_CLASSIC ? 52 : 64, classic ? C_LOW_Y + 2 : 104, (uint8_t)(4 + ratio));
    }
}

static void announce(const char *text, fx::BannerStyle s, uint16_t frames) {
    phase = ANNOUNCE; pt = 0; hold = frames;
    fx::banner(text, s, 56, (uint8_t)frames);
}

// What comes after a step of the show.
static void next(const Slots &g) {
    v.wheelOn = v.wheelLit = false;
    if (todoWheel == 1) {
        todoWheel = 2;
        announce("BONUS WHEEL!", fx::B_RAINBOW, 70);
        audio::sfx(Sfx::Gong);
        fx::burst(fx::STAR, 52, 59, 20, 70, FX_A);
        return;
    }
    if (todoWheel == 2) {
        // Four or five turns, easing down onto the settled segment.
        todoWheel = 0;
        phase = WHEEL; pt = 0;
        wheelSeg = (uint8_t)(g.res.wheel - 1);
        wheelFrom = v.wheelAngle;
        uint16_t to = (uint16_t)(49152u - (wheelSeg * 2 + 1) * (65536u / (WHEEL_SEGS * 2)));
        wheelTurn = 4 * 65536u + (uint16_t)(to - v.wheelAngle);
        v.wheelOn = true;
        playSong(Song::Wheel);
        return;
    }
    if (todoFree) {
        todoFree = false;
        announce("FREE GAMES!", fx::B_RED, 80);
        audio::sfx(Sfx::Gong);
        fx::burst(fx::STAR, 64, 56, 20, 70, FX_A);
        return;
    }
    if (todoHold) {
        todoHold = false;
        announce("HOLD AND SPIN", fx::B_GOLD, 80);
        audio::sfx(Sfx::Gong);
        v.holdMode = true;
        v.line = mach::LINE_NONE;
        return;
    }
    if (todoFinale) {
        todoFinale = false;
        phase = ANNOUNCE; pt = 0;
        celebrate(g, g.lastWin, true);
        return;
    }
    phase = IDLE;
    idleT = 0;
}

// ---------------------------------------------------------------------------
// A spin
// ---------------------------------------------------------------------------
// With reels 0..i down, is something big one symbol away?
static bool teasing(const Slots &g, uint8_t i) {
    const Result &r = g.res;
    if (g.machine == M_CLASSIC) {
        if (i != 1) return false;
        uint8_t a = r.grid[0][1], b = r.grid[1][1];
        bool charmA = a == C_CLOVER || a == C_HORSESHOE, charmB = b == C_CLOVER || b == C_HORSESHOE;
        return (a == b && Slots::classicPay(a, a, a) >= 100) || (charmA != charmB);     // a big three, or the wheel
    }
    if (g.machine == M_SWEET) {
        // Two of the best (bear, lollipop) side by side on a row.
        if (i != 1) return false;
        for (uint8_t row = 0; row < ROWS; row++)
            if (r.grid[0][row] >= S_BEAR && r.grid[1][row] >= S_BEAR) return true;
        return false;
    }
    if (i >= 4) return false;
    uint8_t gongs = 0, coins = 0;
    for (uint8_t k = 0; k <= i; k++)
        for (uint8_t row = 0; row < ROWS; row++) {
            gongs += r.grid[k][row] == F_GONG;
            coins += r.grid[k][row] == F_COIN;
        }
    return gongs == 2 || (coins >= 4 && coins < HOLD_COINS);
}

void spin(const Slots &g) {
    phase = SPIN; pt = 0;
    bigCount = false;
    v.line = mach::LINE_NONE;
    memset(v.wildH, 0, sizeof v.wildH);
    v.antic = 0; v.holdMode = false;
    v.wheelOn = false;
    if (!g.res.wasFree) v.shownWin = 0;
    rain = 0;
    anticMask = 0;
    uint8_t t = fast ? 16 : 34, gap = fast ? 6 : 12;
    for (uint8_t i = 0; i < g.reels(); i++) {
        v.state[i] = mach::SPINNING;
        landAt[i] = t;
        landN[i] = (anticMask >> i & 1) ? CREEP_N : LAND_N;     // the reel after a tease creeps in
        t = (uint8_t)(t + gap);
        if (teasing(g, i)) { anticMask |= (uint8_t)(2u << i); t = (uint8_t)(t + (fast ? 20 : 40)); }
    }
    if (g.freeLeft || g.res.wasFree) playSong(Song::Free);
}

void hurry() {
    if (phase == SPIN) {
        uint8_t t = (uint8_t)(pt + 1);
        for (uint8_t i = 0; i < 5; i++)
            if (v.state[i] == mach::SPINNING && landAt[i] > t) { landAt[i] = t; landN[i] = LAND_N; t = (uint8_t)(t + 3); }
        anticMask = 0;
    } else if (phase == PAY) {
        v.shownWin = winTo;
    } else if (phase == WHEEL && pt + 20 < WHEEL_N) {
        pt = WHEEL_N - 20;
    }
}

static void beginPay(const Slots &g) {
    const Result &r = g.res;
    int32_t won = r.total - r.wheelPay;          // the wheel pays after it has turned
    todoFree = r.freeTrigger;
    todoHold = r.holdTrigger;
    todoWheel = r.wheel ? 1 : 0;
    todoFinale = r.wasFree && !g.freeLeft && !g.holding && g.lastWin > 0;
    // SWEET: the ladder moves now that the reels have shown why.
    if (g.machine == M_SWEET && g.rush != v.rush) {
        if (g.rush > v.rush) {
            fx::burst(fx::STAR, RUSH_X + RUSH_W / 2, C_WIN_Y + 16 + (RUSH_STEPS - 1 - g.rush) * 16, 12, 50, FX_A);
            audio::sfx(Sfx::Lock);
        }
        v.rush = g.rush;
    }
    if (!won && !todoFree && !todoHold) { next(g); return; }
    phase = PAY; pt = 0;
    v.line = mach::LINE_ALL;
    winTo = v.shownWin + won;
    celebrate(g, won, false);
    if (todoFree || todoHold) audio::sfx(Sfx::Gong);
}

static void spinUpdate(const Slots &g) {
    const Result &r = g.res;
    int32_t span = (int32_t)g.stripLen() << 8;
    bool all = true;
    for (uint8_t i = 0; i < g.reels(); i++) {
        if (v.state[i] == mach::SPINNING) {
            v.pos[i] -= SPIN_V;
            if (v.pos[i] < 0) v.pos[i] += span;
            if (pt >= landAt[i]) {
                v.state[i] = mach::LANDING;
                landT[i] = 0;
                v.stop[i] = r.stop[i];
                for (uint8_t row = 0; row < ROWS; row++) { v.cell[i][row] = r.grid[i][row]; v.coin[i][row] = r.coin[i][row]; }
            }
        }
        if (v.state[i] == mach::LANDING) {
            // Two stops out, easing in, with OUT_BACK's overshoot as the bounce.
            int32_t p = ((int32_t)v.stop[i] << 8) + (256 - fx::ease(fx::OUT_BACK, ++landT[i], landN[i])) * 2;
            v.pos[i] = (p + span) % span;
            if (landT[i] >= landN[i]) {
                v.state[i] = mach::STOPPED;
                audio::sfx(Sfx::Thunk);
                v.antic = (anticMask >> (i + 1) & 1) ? (uint8_t)(i + 2) : 0;
                if (v.antic) audio::sfx(Sfx::Antic);
                // A gong or a coin announces itself as it lands.
                for (uint8_t row = 0; row < ROWS && g.machine == M_FORTUNE; row++)
                    if (r.grid[i][row] >= F_GONG)
                        fx::burst(fx::SPARK, cellCx(g, i), F_WIN_Y + row * CELL + 12, 6, 30, FX_B);
            }
        }
        if (v.state[i] != mach::STOPPED) all = false;
    }
    if (v.antic && (pt & 3) == 0) audio::blip((uint16_t)(1500 + (pt & 63) * 30), 12);
    else if ((pt % 5) == 0) audio::blip(1300, 3);
    if (!all) return;
    v.antic = 0;
    if (r.wild) { phase = WILD; pt = 0; }
    else beginPay(g);
}

// One dragon at a time climbs its reel in a column of fire.
static void wildUpdate(const Slots &g) {
    for (uint8_t i = 0; i < 5; i++) {
        if (!(g.res.wild >> i & 1) || v.wildH[i] >= WIN_H) continue;
        if (!v.wildH[i]) { audio::sfx(Sfx::Roar); fx::shake(8, 2); }
        v.wildH[i] = (uint8_t)(v.wildH[i] + 6);
        int y = F_WIN_Y + WIN_H - v.wildH[i];
        fx::spawn(fx::SPARK, cellCx(g, i) + fx::rndRange(-10, 11), y, fx::rndRange(-12, 13), fx::rndRange(-50, -20), 18,
                  (pt & 1) ? RED : FELT_LT);
        if (v.wildH[i] >= WIN_H) fx::burst(fx::SPARK, cellCx(g, i), F_WIN_Y + 8, 12, 60, GOLD);
        return;
    }
    beginPay(g);
}

static void wheelUpdate(const Slots &g) {
    uint16_t before = v.wheelAngle;
    v.wheelAngle = (uint16_t)(wheelFrom + (uint32_t)(((uint64_t)wheelTurn * (uint32_t)fx::ease(fx::OUT_CUBIC, pt, WHEEL_N)) >> 8));
    // A click for every wedge that passes the pointer.
    if ((uint32_t)before * WHEEL_SEGS >> 16 != (uint32_t)v.wheelAngle * WHEEL_SEGS >> 16) audio::blip(2600, 6);
    if (pt < WHEEL_N) return;
    playSong(Song::None);
    v.wheelLit = true;
    phase = PAY; pt = 0;
    winTo = v.shownWin + g.res.wheelPay;
    celebrate(g, g.res.wheelPay, true);
    if (hold < 90) hold = 90;
}

// ---------------------------------------------------------------------------
// Hold and Spin
// ---------------------------------------------------------------------------
void respin(const Slots &g) {
    phase = RESPIN; pt = 0;
    v.cellHide = g.heldNew;
    v.cellSpin = 0;
    for (uint8_t c = 0; c < CELLS; c++)
        if (!g.held[c] || (g.heldNew >> c & 1)) v.cellSpin |= (uint16_t)(1u << c);
    holdEnded = !g.holding;
}

static void holdUpdate(const Slots &g) {
    if (phase == RESPIN) {
        if ((pt % 5) == 0) audio::blip(1300, 3);
        if (pt < (fast ? 20 : 40)) return;
        phase = LAND; pt = 0;
        v.cellSpin = v.cellHide;            // the blanks stop; the coins are still coming
        return;
    }
    if (phase == LAND) {
        if (pt % 9 != 1) return;
        for (uint8_t c = 0; c < CELLS; c++) {
            uint16_t bit = (uint16_t)(1u << c);
            if (!(v.cellHide & bit)) continue;
            v.cellHide &= (uint16_t)~bit;
            v.cellSpin &= (uint16_t)~bit;
            audio::sfx(Sfx::Lock);
            fx::burst(fx::SPARK, cellCx(g, (uint8_t)(c / 3)), F_WIN_Y + (c % 3) * CELL + 12, 10, 50, FX_B);
            fx::shake(4, 1);
            return;
        }
        if (!holdEnded) { phase = IDLE; idleT = 0; return; }
        phase = COUNT; pt = 0; countAt = 0;
        return;
    }
    // COUNT: every coin lights and pays in turn.
    if (pt % 8 != 1) return;
    while (countAt < CELLS && !g.held[countAt]) countAt++;
    if (countAt < CELLS) {
        v.flashCell = (uint8_t)(countAt + 1);
        v.shownWin += g.coinValue(g.held[countAt]);
        audio::sfx(Sfx::Coin);
        fx::fountain(fx::COIN, cellCx(g, (uint8_t)(countAt / 3)), F_WIN_Y + (countAt % 3) * CELL + 12, 2);
        countAt++;
        return;
    }
    v.flashCell = 0;
    v.shownWin = g.lastWin;
    if (g.holdJackpot) {
        static const char *const NAME[J_COUNT] = {"MINI", "MINOR", "MAJOR", "GRAND"};
        char buf[14];
        fmtStr(fmtStr(buf, NAME[g.holdJackpot - 1]), " JACKPOT");
        announce(buf, fx::B_RAINBOW, 170);
        rain = 150;
        fx::shake(24, 3);
        pal::flash(INK, 0xFFF, 3);
        fx::explode(64, 60, 48);
        audio::sfx(Sfx::Jackpot);
        audio::led(audio::LED_PARTY);
    } else {
        phase = ANNOUNCE; pt = 0;
        celebrate(g, g.holdPay, true);
    }
}

// ---------------------------------------------------------------------------
// Every tick
// ---------------------------------------------------------------------------
static void message(const Slots &g) {
    char *p = msgBuf;
    v.msg = msgBuf;
    if (v.holdMode && g.holding) fmtInt(fmtStr(p, "RESPINS "), g.respins);
    else if (g.freeLeft) fmtInt(fmtStr(p, "FREE GAMES "), g.freeLeft);
    else if (g.res.wasFree && busy()) fmtStr(p, "LAST FREE GAME");
    else if (v.wheelOn) fmtStr(p, "BONUS WHEEL");
    else if (g.machine == M_SWEET && busy() && g.res.mult > 1 && g.res.total)
        fmtStr(fmtInt(fmtStr(p, "SUGAR RUSH X"), g.res.mult), "!");
    else if (g.machine != M_CLASSIC && v.line >= 0 && v.line < LINES && g.res.lineCount[v.line])
        fmtMoney(fmtStr(fmtInt(fmtStr(p, "LINE "), v.line + 1), " "), g.linePay((uint8_t)v.line));
    else if (!v.canSpin && !busy()) fmtStr(p, "NOT ENOUGH");
    else v.msg = g.machine == M_CLASSIC ? "HOLD A: PULL  B: PAYS" : "B: PAYS";
}

void update(const Slots &g) {
    pt++;
    if (!armHeld) {
        if (armDown) {
            v.arm = (uint8_t)(v.arm + 12 > ARM_MAX ? ARM_MAX : v.arm + 12);
            if (v.arm == ARM_MAX) { armDown = false; armT = 0; audio::sfx(Sfx::Lever); }
        } else if (armT < ARM_BACK) {
            int up = (fx::ease(fx::OUT_BOUNCE, ++armT, ARM_BACK) * ARM_MAX) >> 8;
            v.arm = (uint8_t)(up >= ARM_MAX ? 0 : ARM_MAX - up);
        }
    }
    switch (phase) {
        case SPIN: spinUpdate(g); break;
        case WILD: wildUpdate(g); break;
        case WHEEL: wheelUpdate(g); break;
        case PAY:
            if (v.shownWin < winTo) {
                v.shownWin += (winTo - v.shownWin) / 10 + 1;
                if (v.shownWin > winTo) v.shownWin = winTo;
                if ((pt & 1) == 0) audio::blip((uint16_t)(3000 + (pt & 7) * 150), 8);
            } else if (pt >= hold) next(g);
            break;
        case ANNOUNCE:
            if (pt >= hold) {
                if (!g.holding && v.holdMode && !todoHold) v.holdMode = false;     // the feature is over
                next(g);
            }
            break;
        case RESPIN: case LAND: case COUNT: holdUpdate(g); break;
        default:
            // Idle: walk through the lines that paid.
            if (g.machine != M_CLASSIC && g.res.nLines > 1 && v.line != mach::LINE_NONE && ++idleT >= 50) {
                idleT = 0;
                int8_t l = v.line;
                do { l = (int8_t)(l >= LINES ? 0 : l + 1); } while (l < LINES && !g.res.lineCount[l]);
                v.line = l;
            }
            if (!g.freeLeft && !g.holding) playSong(Song::None);
            break;
    }
    if (rain) {
        rain--;
        if ((rain % 3) == 0) fx::fountain(fx::COIN, fx::rndRange(8, 120), 126, 2);
        if ((rain % 24) == 0) fx::fountain(fx::CONFETTI, fx::rndRange(20, 108), 100, 10);
        if ((rain % 32) == 16) fx::explode(fx::rndRange(24, 104), fx::rndRange(30, 80), 12);
    }
    // The purse follows the win as it counts up.
    v.shownPurse = g.purse - (g.lastWin - v.shownWin);
    if (phase == IDLE) { v.shownWin = g.lastWin; v.shownPurse = g.purse; }
    // The progressive meters tick up to where they really are.
    for (uint8_t j = 0; j < 2; j++) {
        int32_t to = g.meter((uint8_t)(J_MAJOR + j));
        if (v.meter[j] < to && to - v.meter[j] < 400) v.meter[j] += (pt & 3) ? 0 : 1;
        else v.meter[j] = to;
    }
    pal::setMode(g.machine == M_FORTUNE && (g.freeLeft || (g.res.wasFree && busy())) ? pal::FIRE : pal::CASINO);
    message(g);
    fx::update();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static uint32_t mixSig(uint32_t h, uint32_t x) { return (h ^ x) * 16777619u; }

void render(const Slots &g, uint32_t frame) {
#ifdef CHSIM_FORCE_FULL
    force = true;                                           // the redraw check's reference build (rpgame redraw)
#endif
    bool classic = g.machine == M_CLASSIC, three = g.machine != M_FORTUNE;
    int midY = three ? C_TOP_H : F_TOP_H, lowY = three ? C_LOW_Y : F_LOW_Y;

    const uint32_t BASIS = 2166136261u;
    uint32_t st = classic ? (frame >> 3 & 3) : g.machine == M_SWEET ? (frame >> 4 & 1) : mixSig(mixSig(mixSig(BASIS, (uint32_t)v.meter[0]), (uint32_t)v.meter[1]), g.bet());
    uint32_t sm = BASIS;
    bool moving = phase == WILD || phase == WHEEL || v.wheelLit;
    for (uint8_t i = 0; i < 5; i++) {
        if (v.state[i] != mach::STOPPED) moving = true;
        sm = mixSig(sm, v.stop[i] | v.wildH[i] << 8);
        if (v.state[i] == mach::STOPPED) sm = mixSig(sm, (uint32_t)v.pos[i]);
        for (uint8_t row = 0; row < ROWS; row++) sm = mixSig(sm, v.cell[i][row] | v.coin[i][row] << 4 | g.held[i * 3 + row] << 8);
    }
    sm = mixSig(sm, (uint32_t)(v.line + 2) | v.antic << 16 | v.arm << 20 | (uint32_t)v.wheelOn << 28);
    sm = mixSig(sm, v.cellHide | (uint32_t)v.flashCell << 16 | (uint32_t)v.holdMode << 24);
    sm = mixSig(sm, (uint32_t)(v.cellSpin != 0) | (uint32_t)v.wheelLit << 1);
    sm = mixSig(sm, g.bet() | (uint32_t)(g.freeLeft != 0) << 16 | (uint32_t)g.res.wasFree << 17 | (uint32_t)v.rush << 18);
    if (classic && v.line != mach::LINE_NONE) sm = mixSig(sm, frame >> 3 & 1);
    if (v.cellSpin) moving = true;
    uint32_t sl = mixSig(mixSig(mixSig(BASIS, (uint32_t)v.shownWin), (uint32_t)v.shownPurse), (uint32_t)g.purse);
    sl = mixSig(sl, g.bet() | (uint32_t)v.canSpin << 16 | (uint32_t)v.pressed << 17 | (uint32_t)(g.freeLeft != 0) << 18 |
                    (v.canSpin ? (frame >> 4 & 1) << 19 : 0));
    for (const char *s = v.msg; s && *s; s++) sl = mixSig(sl, (uint8_t)*s);

    // Anything transient (particles, banner, shake) dirties the bands it
    // crosses, this frame and the one after (to wipe it).
    int lo, hi;
    bool act = fx::activeRows(lo, hi);
    int dlo = lo < lastLo ? lo : lastLo, dhi = hi > lastHi ? hi : lastHi;
    lastLo = lo; lastHi = hi;
    bool touched = dhi >= dlo;

    bool dTop = force || st != sigTop || (touched && dlo < midY);
    bool fullMid = force || sm != sigMid || (touched && dhi >= midY && dlo < lowY);
    bool dMid = fullMid || moving;
    bool dLow = force || sl != sigLow || (touched && dhi >= lowY);
    force = false;
    sigTop = st; sigMid = sm; sigLow = sl;
    if (dTop) mach::top(g, v, frame);
    if (dMid) mach::window(g, v, frame, fullMid);
    if (dLow) mach::low(g, v, frame);
    if (act) {
        fx::drawParticles();
        fx::drawFloats();
        fx::drawBanner();
        if (bigCount && fx::bannerActive() && v.shownWin) {
            // The win so far, in figures you can read across the room.
            char buf[12];
            fmtMoney(buf, v.shownWin);
            int w = text35WidthScaled(buf, 3);
            fillRound(64 - w / 2 - 5, 71, w + 10, 21, 4, INK);
            roundRect(64 - w / 2 - 5, 71, w + 10, 21, 4, FX_B);
            Mask m = maskBegin(w + 1, 18);
            maskText35(m, 0, 0, buf, 3);
            uint8_t ramp[18];
            for (uint8_t i = 0; i < 18; i++) ramp[i] = i < 6 ? WHITE : (i < 11 ? FX_B : GOLD);       // (wood would vanish on the plate)
            maskDraw(m, 64 - w / 2, 73, GOLD, -1, -1, ramp);
        }
        fx::applyShake(0, 127, INK);
    }
}

}  // namespace present
