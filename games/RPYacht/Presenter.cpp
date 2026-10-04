#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// CHBlackjack's presenter (its Presenter.cpp) by way of CHCraps: the rolling
// purse and the band-level redraw, re-cut for a score card, a tray of dice
// and the dice cam.
#include <RPGame.h>
#include <string.h>
#include "Presenter.h"
#include "Fx.h"
#include "Yacht.h"
#include "Cam.h"
#include "Layout.h"
#include "Wall.h"
#include "Bar.h"
#include "Chips.h"
#include "Sounds.h"

namespace present {

using namespace lay;

const char *const CAT_NAME[CAT_COUNT] = {
    "ONES", "TWOS", "THREES", "FOURS", "FIVES", "SIXES",
    "3 OF A KIND", "4 OF A KIND", "FULL HOUSE", "SM STRAIGHT", "LG STRAIGHT", "YACHT", "CHANCE",
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static int32_t shown = 500, owed = 0;   // the purse on show; a bonus not yet landed
static uint8_t purseFlash = 0;

static uint8_t focus = F_NONE, sel = 0, denyT = 0, viewP = 0;
static bool rollHeld = false;

static uint8_t trayDice[5], trayWho = 0, dropT = 0xFF, dropMask = 0;
static bool trayShown = false;
static const uint8_t DROP_GAP = 3, DROP_T = 10, DROP_END = 4 * DROP_GAP + DROP_T;

enum Step : uint8_t { S_IDLE, S_SCORE, S_TURN };
static uint8_t step = S_IDLE, speed = 1, events = 0, flashCat = 0;
static uint16_t stepT = 0;

const char *seatName(const Yacht &g, uint8_t p) {
    static const char *const SEAT[4] = {"P1", "P2", "P3", "P4"};
    if (g.mode == M_SOLO) return "SCORE";
    if (g.mode == M_CPU) return p ? "DEALER" : "YOU";
    return SEAT[p & 3];
}

static void cellAt(uint8_t cat, int &x, int &y) {
    x = cat < KIND3 ? COL_X0 : COL_X1;
    y = CELL_Y + (cat < KIND3 ? cat : cat - KIND3) * CELL_PITCH;
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
void reset(const Yacht &g) {
    shown = g.purse; owed = 0; purseFlash = 0;
    step = S_IDLE; speed = 1;
    viewP = g.cur;
    trayShown = g.rolled();
    memcpy(trayDice, g.dice, sizeof trayDice);
    trayWho = g.cur;
    dropT = 0xFF;
    denyT = 0; rollHeld = false;
    fx::clear();
    invalidate();
}

bool busy() { return step != S_IDLE; }
void speedUp() { if (step != S_IDLE) speed = 3; }
void cursor(uint8_t f, uint8_t index) { focus = f; sel = index; }
void deny() { denyT = 24; audio::sfx(Sfx::Deny); }
void viewNext(const Yacht &g) { viewP = (uint8_t)((viewP + 1) % g.players); }
void setRollHeld(bool h) { rollHeld = h; }

static void party() {
    fx::fountain(fx::CONFETTI, 40, 90, 18);
    fx::fountain(fx::CONFETTI, 88, 90, 18);
    audio::led(audio::LED_TRIPLE);
}

void onRest(const Yacht &g) {
    switch (Yacht::combo(g.dice)) {
        case YACHT: fx::banner("YACHT!", fx::B_RAINBOW, 112, 90); party(); audio::sfx(Sfx::Hot); break;
        case LARGE: fx::banner("LG STRAIGHT", fx::B_GOLD, 112, 80); audio::sfx(Sfx::Win); break;
        case KIND4: fx::banner("4 OF A KIND", fx::B_GOLD, 112, 80); audio::sfx(Sfx::Win); break;
        case FULL:  fx::banner("FULL HOUSE", fx::B_CYAN, 112, 80); audio::sfx(Sfx::Point); break;
        case SMALL: fx::banner("SM STRAIGHT", fx::B_CYAN, 112, 80); audio::sfx(Sfx::Point); break;
        default: break;
    }
}

void onLanded(const Yacht &g, uint8_t thrown) {
    memcpy(trayDice, g.dice, sizeof trayDice);
    trayWho = g.cur;
    trayShown = true;
    dropT = 0; dropMask = thrown;
    viewP = g.cur;
}

void onScore(const Yacht &g, uint8_t ev) {
    step = S_SCORE; stepT = 0; speed = 1;
    events = ev; flashCat = g.lastCat;
    viewP = g.lastPlayer;
    owed = g.lastPay;
    int x, y;
    cellAt(flashCat, x, y);
    char buf[8];
    fmtInt(fmtStr(buf, "+"), g.lastPts);
    fx::floatText(buf, x + CELL_W - 12, y - 3, ev & EV_ZERO ? RED : FX_B);
    if (ev & EV_ZERO) { audio::sfx(Sfx::Lose); fx::shake(8, 2); }
    else if (ev & (EV_YACHT | EV_EXTRA)) {
        fx::banner(ev & EV_EXTRA ? "YACHT +100" : "YACHT!", fx::B_RAINBOW, 44, 80);
        party();
        audio::sfx(Sfx::BigWin);
        audio::led(audio::LED_PARTY);
    } else {
        audio::sfx(g.lastPts >= 25 ? Sfx::Win : Sfx::Point);
        fx::burst(fx::SPARK, x + CELL_W - 12, y + 4, 8, 30, GOLD);
    }
}

static void tick(const Yacht &g) {
    if (step == S_SCORE) {
        stepT++;
        bool big = events & (EV_YACHT | EV_EXTRA);
        uint16_t end = big ? 90 : 44;
        if ((events & EV_UPPER) && stepT == (big ? 80u : 30u)) {
            fx::banner("BONUS +35", fx::B_GOLD, 44, 70);
            fx::fountain(fx::CONFETTI, 64, 80, 16);
            audio::sfx(Sfx::Win);
        }
        if (events & EV_UPPER) end = (uint16_t)(end + 60);
        if (owed && stepT == 14) {                       // the bonus lands in the purse
            char buf[8];
            fmtMoney(fmtStr(buf, "+"), owed);
            fx::floatText(buf, PURSE_X + PURSE_W / 2, BAR_Y - 8, GOLD);
            fx::fountain(fx::COIN, PURSE_X + PURSE_W / 2, BAR_Y, 10);
            audio::sfx(Sfx::Coin);
            purseFlash = 24;
            owed = 0;
        }
        if (stepT >= (g.opt.speed ? end / 2 : end)) {
            owed = 0;
            if (g.over) { step = S_IDLE; return; }
            step = S_TURN; stepT = 0;
            viewP = g.cur;
            trayShown = false;
            if (g.players > 1) {
                char buf[14];
                const char *n = seatName(g, g.cur);
                if (g.mode == M_CPU) fmtStr(buf, g.cur ? "DEALER" : "YOUR TURN");
                else fmtStr(fmtStr(buf, "PLAYER "), n + 1);
                fx::banner(buf, g.cur & 1 ? fx::B_CYAN : fx::B_GOLD, 48, 44);
                audio::sfx(Sfx::Select);
            }
        }
    } else if (step == S_TURN) {
        if (g.players == 1 || ++stepT > 34) step = S_IDLE;
    }
    // The dice drop into the tray one by one.
    if (dropT < DROP_END) {
        uint8_t k = 0;
        for (uint8_t i = 0; i < 5; i++)
            if (dropMask >> i & 1) { if (dropT == k * DROP_GAP + 6) audio::sfx(Sfx::Clack); k++; }
        dropT++;
    }
    // The purse rolls toward the money that has actually landed.
    int32_t d = g.purse - owed - shown;
    if (d) {
        int32_t s = d / 5;
        if (!s) s = d > 0 ? 1 : -1;
        shown += s;
        if (d > 0 && (shown & 3) == 0) audio::blip((uint16_t)(3000 + ((shown * 7) & 511)), 8);
    }
    if (purseFlash) purseFlash--;
    if (denyT) denyT--;
    fx::update();
}

void update(const Yacht &g) {
    cam::update();
    for (uint8_t i = 0; i < speed; i++) tick(g);
    if (step == S_IDLE) speed = 1;
}

// ---------------------------------------------------------------------------
// Drawing: band-level redraw, as in CHBlackjack. The framebuffer survives
// between frames and most of the table is still most of the time, so each
// band (seats and card 0..83, tray 84..111, bar) is redrawn only when what
// it shows has changed or something moving touched it, this frame or the
// last. Every frame is still flushed, so palette animation keeps running.
// ---------------------------------------------------------------------------
struct Sig {
    uint32_t h = 2166136261u;
    void add(int32_t v) { h = (h ^ (uint32_t)v) * 16777619u; }
};

static uint32_t sigTop = 0, sigTray = 0;
static int16_t curLo = 999, curHi = -1;
static bool forceAll = true;

void invalidate() { forceAll = true; bar::invalidate(); }

static void right35(int xr, int y, const char *s, uint8_t c) { text35(xr - text35Width(s), y, s, c); }
static void centre35(int cx, int y, const char *s, uint8_t c) { text35(cx - text35Width(s) / 2, y, s, c); }

// The seats along the top: name and total, the one whose turn it is lit.
static void drawSeats(const Yacht &g) {
    wall::backdrop(HEAD_H);
    uint8_t n = g.players;
    int w = n <= 2 ? 50 : 124 / n;
    for (uint8_t p = 0; p < n; p++) {
        int x = n == 2 && p ? 76 : 2 + p * w;
        bool turn = p == g.cur && !g.over, view = p == viewP;
        char buf[16];
        fmtInt(fmtStr(fmtStr(buf, seatName(g, p)), " "), g.card[p].total());
        if (turn) fillRound(x, 1, w - 2, 9, 3, GOLD);
        else if (view) roundRect(x, 1, w - 2, 9, 3, SILVER);
        uint8_t body, shade, pip;
        art::dieColours(p, body, shade, pip);
        gfx_fillRect(x + 3, 3, 3, 5, body);
        text35(x + 8, 3, buf, turn ? INK : (view ? WHITE : SILVER));
    }
    if (n <= 2) {
        char buf[12];
        uint8_t r = (uint8_t)(g.round + 1 > Yacht::TURNS ? Yacht::TURNS : g.round + 1);
        fmtStr(fmtInt(fmtStr(buf, n == 1 ? "TURN " : ""), r), "/13");
        if (n == 1) right35(125, 3, buf, WHITE);
        else centre35(64, 3, buf, WHITE);
    }
}

static void box(int x, int y, const char *label, int lx, const char *value, uint8_t fill, uint8_t lc, uint8_t vc) {
    fillRound(x, y, CELL_W, CELL_H, 2, fill);
    text35(x + lx, y + 2, label, lc);
    if (*value) right35(x + CELL_W - 2, y + 2, value, vc);
}

static void drawCard(const Yacht &g, bool showPot) {
    const Card &c = g.card[viewP];
    gfx_fillRect(0, CARD_Y, 128, CARD_H, FELT);
    bool flashing = step == S_SCORE && stepT < 40;
    for (uint8_t cat = 0; cat < CAT_COUNT; cat++) {
        int x, y;
        cellAt(cat, x, y);
        char val[6] = "";
        bool open = c.open(cat), flash = flashing && cat == flashCat;
        uint8_t fill = open ? FELT_DK : INK, lc = open ? WHITE : SILVER, vc = WHITE;
        if (flash) { fill = (stepT >> 2) & 1 ? WHITE : GOLD; lc = vc = INK; }
        if (!open) fmtInt(val, c.score[cat] + (cat == YACHT ? c.extra * 100 : 0));
        else if (showPot && g.legal(cat)) {
            uint8_t pts = g.points(cat);
            fmtInt(val, pts);
            vc = pts ? FX_B : FELT;
        }
        box(x, y, CAT_NAME[cat], cat < KIND3 ? 10 : 3, val, fill, lc, vc);
        if (cat < KIND3) art::dieFace(x + 1, y + 1, 7, (uint8_t)(cat + 1), viewP);
        if (focus == F_CARD && sel == cat && viewP == g.cur)
            roundRect(x - 1, y - 1, CELL_W + 2, CELL_H + 2, 3, denyT ? RED : FX_A);
    }
    // The upper bonus: how far along, then the 35.
    char val[8];
    bool got = c.bonus();
    if (got) fmtStr(val, "+35");
    else fmtStr(fmtInt(val, c.upper()), "/63");
    box(COL_X0, CELL_Y + 6 * CELL_PITCH, "BONUS", 3, val, got ? INK : FELT_DK, got ? GOLD : FELT_LT, got ? GOLD : SILVER);
}

static void drawTray(const Yacht &g, uint32_t frame) {
    wall::rail(RAIL_Y);
    gfx_fillRect(0, TRAY_Y, 128, TRAY_H, FELT);
    dither(0, TRAY_Y, 128, 2, FELT_DK, 0);
    gfx_hline(0, TRIM_Y, 128, GOLD);
    if (!trayShown) {
        // The dice are in the cup.
        bool cpu = g.cpuTurn();
        centre35(64, TRAY_Y + 5, g.mode == M_CPU ? (cpu ? "DEALER TO ROLL" : "YOUR ROLL") :
                 (g.round == Yacht::TURNS - 1 ? "LAST TURN" : "NEW TURN"), GOLD);
        if (!cpu && !g.over && ((frame >> 5) & 1)) centre35(64, TRAY_Y + 14, "HOLD A TO SHAKE", WHITE);
        return;
    }
    bool mine = trayWho == g.cur && g.rolled();
    gfx_setClip(0, TRAY_Y - 1, 128, TRAY_H + 1);
    uint8_t k = 0;
    for (uint8_t i = 0; i < 5; i++) {
        bool held = mine && (g.held >> i & 1);
        int x = DIE_X0 + i * DIE_PITCH, y = held ? DIE_HELD_Y : DIE_Y;
        if (dropT < DROP_END && (dropMask >> i & 1)) {
            int t = dropT - k * DROP_GAP;
            k++;
            if (t < 0) continue;
            if (t < DROP_T) y -= (30 * (256 - fx::ease(fx::OUT_BOUNCE, t, DROP_T))) >> 8;
        }
        gfx_fillRect(x + 2, y + DIE, DIE - 2, 1, FELT_DK);     // its shadow on the baize
        art::dieFace(x, y, DIE, trayDice[i], trayWho);
        if (held) {
            roundRect(x - 1, y - 1, DIE + 2, DIE + 2, 3, GOLD);
            text35(x + 1, DIE_HELD_Y + DIE, "HOLD", GOLD);
        }
        if (focus == F_DICE && sel == i) roundRect(x - 2, y - 2, DIE + 4, DIE + 4, 4, denyT ? RED : FX_A);
    }
    gfx_resetClip();
}

void render(const Yacht &g, uint32_t frame) {
    // After the cam the whole table is redrawn.
    static bool wasCam = false;
    if (cam::render(frame)) { invalidate(); wasCam = true; return; }
    if (wasCam) invalidate();
    wasCam = false;

    bool showPot = viewP == g.cur && g.rolled() && step == S_IDLE && !g.over;
    const Card &c = g.card[viewP];
    Sig top;
    top.add(viewP); top.add(g.cur | g.round << 4 | g.over << 8 | showPot << 9);
    top.add(c.filled | c.extra << 16);
    for (uint8_t p = 0; p < g.players; p++) top.add(g.card[p].total());
    if (showPot) for (uint8_t i = 0; i < 5; i++) top.add(g.dice[i]);
    top.add(focus == F_CARD ? sel | (denyT ? 64 : 0) : -1);
    top.add(step == S_SCORE && stepT < 40 ? 1 + ((stepT >> 2) & 1) : 0);
    Sig tray;
    tray.add(trayShown | trayWho << 1 | (trayShown ? 0 : ((frame >> 5) & 1) << 4) | g.cpuTurn() << 5);
    for (uint8_t i = 0; i < 5; i++) tray.add(trayDice[i]);
    tray.add(g.held | g.rollsLeft << 8);
    tray.add(focus == F_DICE ? sel | (denyT ? 64 : 0) : -1);
    tray.add(dropT < DROP_END ? dropT : -1);

    int lo, hi;
    fx::activeRows(lo, hi);
    bool moving = hi >= lo, wasMoving = curHi >= curLo;
    auto touched = [&](int a, int b) {
        return (moving && lo <= b && hi >= a) || (wasMoving && curLo <= b && curHi >= a);
    };
    bool drawTop = forceAll || top.h != sigTop || touched(0, RAIL_Y - 1);
    bool drawLow = forceAll || tray.h != sigTray || touched(RAIL_Y, TRIM_Y);
    if (touched(BAR_Y, 127)) bar::invalidate();
    forceAll = false;
    sigTop = top.h; sigTray = tray.h;
    curLo = (int16_t)lo; curHi = (int16_t)hi;

    dbg::profStart();
    if (drawTop) { drawSeats(g); drawCard(g, showPot); }
    dbg::prof(0);
    if (drawLow) drawTray(g, frame);
    dbg::prof(1);
    bool cpu = g.cpuTurn();
    bar::View v;
    v.purse = g.staked() ? shown : -1;
    v.ante = g.over ? 0 : g.ante;
    static const char *const WHO[4] = {"PLAYER 1", "PLAYER 2", "PLAYER 3", "PLAYER 4"};
    v.who = WHO[g.cur & 3];
    v.label = rollHeld ? "SHAKE!" : (cpu ? "DEALER" : (!g.rolled() ? "ROLL" : (g.rollsLeft ? "REROLL" : "SCORE IT")));
    v.flash = purseFlash;
    v.rollsLeft = g.rollsLeft;
    v.rollOn = g.canRoll() && !cpu && step == S_IDLE;
    v.sel = focus == F_ROLL;
    v.held = rollHeld;
    bool drewBar = bar::draw(v);
    if (drawTop || drawLow || drewBar) {
        fx::drawParticles();
        fx::drawFloats();
        fx::drawBanner();
        fx::applyShake(0, TRIM_Y - 1, -1);
    }
    dbg::prof(2);
}

}  // namespace present
