#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// Screens after CHBlackjack's (its Screens.cpp): fades between them,
// the options list, statistics, the pause menu and the two endings. New
// here: the machine menu, and the scrolling paytable (drawn by Machine.cpp).
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Screens.h"
#include "Slots.h"
#include "Presenter.h"
#include "Fx.h"
#include "Layout.h"
#include "Machine.h"
#include "Sounds.h"
#include "Save.h"
#include "src/assets/Assets.h"

#if CHGAME_DEBUG && defined(CHSIM)
uint64_t sim_hostNanos();
#endif

namespace screens {

enum class Scr : uint8_t { Title, Floor, Play, Options, Stats, Win, Lose };

static Slots game;
static Scr cur = Scr::Title, pending = Scr::Title;
static uint8_t fadeOut = 0, fadeIn = 0;      // palette fade transitions
static uint16_t t = 0;                       // frames on this screen
static bool hasGame = false, paused = false, resumePlay = false;
static uint8_t menuSel = 0, pauseSel = 0, optSel = 0, floorSel = 0;
// The machines as they stand on the floor, left to right: plain, sweet, then the big one.
static const uint8_t FLOOR[M_COUNT] = {M_CLASSIC, M_SWEET, M_FORTUNE};
static Scr optBack = Scr::Title;
static uint8_t toastT = 0;
static const char *toastText = "";
static uint32_t staticSig = 0;               // last drawn state of a still screen

static void toast(const char *s) { toastText = s; toastT = 70; }

static void redrawAll() { present::invalidate(); staticSig = 0; }

static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

static void persist(bool keepGame) {
    gfx_wait();
    save::store(game, keepGame);
}

// Play-screen input state.
static uint8_t armPull = 0, autoT = 0, sinceSave = 0;
static bool armed = false, wasBusy = false, showPays = false;
// The paytable: it drifts down by itself until you take over.
static int16_t payY = 0;
static uint8_t payWait = 0;
static bool payAuto = false;

// The attract demo: the machines play themselves on a borrowed purse.
static bool demo = false;
static uint8_t demoSpins = 0, demoCount = 0;
static Slots keep;
constexpr uint16_t DEMO_AFTER = 900;         // frames of an untouched title

static void enter(Scr s) {
    cur = s; t = 0; fadeIn = 8;
    staticSig = 0;
    fx::clear();
    pal::setDesaturate(0);
    pal::setMode(pal::CASINO);
    pal::setTheme(s == Scr::Play ? game.machine : (uint8_t)mach::GREEN);    // mach::Theme follows Machine
    switch (s) {
        case Scr::Title: paused = false; playSong(Song::Title); break;
        case Scr::Floor: for (uint8_t k = 0; k < M_COUNT; k++) if (FLOOR[k] == game.machine) floorSel = k; break;
        case Scr::Play:
            if (!resumePlay) { game.fitBet(); present::reset(game); if (!demo) present::welcome(game); }
            redrawAll();
            resumePlay = false;
            paused = false;
            armed = false;
            wasBusy = false;
            showPays = false;
            break;
        case Scr::Win: playSong(Song::Title); audio::sfx(Sfx::Jackpot); audio::led(audio::LED_PARTY); break;
        case Scr::Lose: playSong(Song::None); audio::sfx(Sfx::Broke); break;
        default: break;
    }
}

static void applyOptions() {
    audio::setOn(!game.opt.sound);
    setMusicOn(!game.opt.music);
    present::setFast(game.opt.speed);
}

void begin() {
    bool ok = save::load(game, hasGame);
    (void)ok;
    if (!hasGame) game.newGame();
    game.mix(micros() * 2654435761u);
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, !game.opt.sound);
    applyOptions();
    enter(Scr::Title);
}

// ---------------------------------------------------------------------------
// Shared drawing
// ---------------------------------------------------------------------------
static void feltBackdrop() {
    gfx_clear(FELT);
    dither(0, 0, 128, 6, FELT_DK, 0);
    dither(0, 122, 128, 6, FELT_DK, 1);
    dither(0, 0, 6, 128, FELT_DK, 0);
    dither(122, 0, 6, 128, FELT_DK, 1);
    gfx_rect(2, 2, 124, 124, GOLD);
}

// Big lettering in PPOT's font, scale 3, with a gradient, outline and shadow:
// top colour for 3 rows, mid down to row lowFrom, low below.
static void title35(const char *text, int y, uint8_t top, uint8_t mid, uint8_t low, uint8_t shadow,
                    uint8_t lowFrom = 9) {
    Mask m = maskBegin(124, 18);
    maskText35(m, 0, 0, text, 3);
    uint8_t ramp[18];
    for (int i = 0; i < 18; i++) ramp[i] = i < 3 ? top : (i < lowFrom ? mid : low);
    maskDraw(m, 64 - text35WidthScaled(text, 3) / 2, y, mid, INK, shadow, ramp);
}

static void centred35(int y, const char *s, uint8_t c) { text35(64 - text35Width(s) / 2, y, s, c); }
static void centred57(int y, const char *s, uint8_t c) { gfx_text(64 - gfx_textWidth(s) / 2, y, s, c); }

static void sym(uint8_t index, int x, int y, const uint8_t *remap = nullptr) {
    sprite4(SYMBOLS + SYMBOL_AT[index], x, y, remap);
}

// ---------------------------------------------------------------------------
// Title: the logo, three reels that keep coming up sevens, the menu.
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_PLAY, I_CONTINUE, I_NEW, I_OPTIONS, I_STATS };

static uint8_t menu(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) { items[n++] = I_CONTINUE; items[n++] = I_NEW; }
    else items[n++] = I_PLAY;
    items[n++] = I_OPTIONS;
    items[n++] = I_STATS;
    return n;
}

static void titleUpdate() {
    uint8_t items[5], n = menu(items);
    if (menuSel >= n) menuSel = 0;
    if (rpgame.repeat(UP_BUTTON) && menuSel > 0) { menuSel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && menuSel + 1 < n) { menuSel++; audio::sfx(Sfx::Cursor); }
    if (rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
        game.mix(micros() ^ rpgame.frameCount);
        switch (items[menuSel]) {
            case I_PLAY: case I_NEW: game.newGame(); hasGame = true; go(Scr::Floor); break;
            case I_CONTINUE: go(Scr::Floor); break;
            case I_OPTIONS: optBack = Scr::Title; optSel = 0; go(Scr::Options); break;
            case I_STATS: go(Scr::Stats); break;
        }
    }
    uint16_t k = t % 300;
    if (k == 186) fx::burst(fx::STAR, 64, 51, 16, 70, FX_A);
#if CHSL_DEMO
    if (rpgame.anyPressed(0xFF)) t = t % 300;          // any touch puts the demo off
    else if (t >= DEMO_AFTER && !fadeOut) {
        keep = game;
        game.newGame();
        game.machine = (uint8_t)(demoCount++ % M_COUNT);
        game.betIdx[game.machine] = 2;
        demo = true; demoSpins = 0;
        go(Scr::Play);
    }
#endif
}

static void titleStatic(uint32_t frame) {
    feltBackdrop();
    uint8_t ramp[LOGO_H];
    for (int i = 0; i < LOGO_H; i++) ramp[i] = i < 3 ? FX_B : (i < 11 ? GOLD : WOOD);
    Mask m = maskBegin(LOGO_W, LOGO_H);
    maskBlit1(m, LOGO, (uint8_t)LOGO_W, (uint8_t)LOGO_H);
    maskDraw(m, 64 - LOGO_W / 2, 8, GOLD, INK, WINE, ramp);
    centred35(27, "~THE~ONE~ARMED~BANDIT~", CYAN);
    uint8_t items[5], n = menu(items);
    if (n == 3) {
        text35(7, 116, "3 MACHINES", FELT_LT);
        text35(121 - text35Width("CHGAME 2026"), 116, "CHGAME 2026", FELT_LT);
    }
    static const char *const LABEL[5] = {"PLAY", "CONTINUE", "NEW GAME", "OPTIONS", "STATS"};
    int y0 = n == 3 ? 80 : 76;
    for (uint8_t i = 0; i < n; i++) {
        char buf[20];
        if (items[i] == I_CONTINUE) fmtMoney(fmtStr(buf, "CONTINUE "), game.purse);
        else strcpy(buf, LABEL[items[i]]);
        int y = y0 + i * 11;
        bool sel = i == menuSel;
        int w = gfx_textWidth(buf);
        if (sel) panel(64 - w / 2 - 6, y - 2, w + 12, 11, 3, NAVY, (frame & 16) ? FX_B : GOLD);
        centred57(y, buf, sel ? GOLD : WHITE);
    }
}

static void titleRender(uint32_t frame) {
    uint32_t sig = (uint32_t)menuSel << 1 | (uint32_t)hasGame << 4 | ((frame >> 4) & 1) << 5 | 1;
    if (sig != staticSig) { staticSig = sig; titleStatic(frame); }
    // The little machine: it spins, stops left to right, and it is always sevens.
    const int X = 22, Y = 36, W = 84, H = 32;
    panel(X, Y, W, H, 3, INK, GOLD);
    uint16_t k = t % 300;
    for (uint8_t i = 0; i < 3; i++) {
        int x = X + 4 + i * 26;
        gfx_fillRect(x, Y + 3, 24, 26, WHITE);
        gfx_setClip(x, Y + 3, 24, 26);
        if (k < 150 + i * 15) {
            int step = (int)(t * 5 + i * 37);
            for (int j = 0; j < 2; j++)
                sym((uint8_t)((step / 26 + j + i) % C_COUNT), x + 1, Y + 5 + (step % 26) - j * 26);
            dither(x, Y + 3, 24, 26, WHITE, (uint8_t)(frame & 1));
        } else {
            int drop = (int)k - (150 + i * 15);
            sym(C_SEVEN, x + 1, Y + 5 + (drop < 6 ? 6 - drop : 0) - (drop >= 6 && drop < 9 ? 1 : 0));
        }
        gfx_resetClip();
    }
    fx::update();
    fx::drawParticles();
}

// ---------------------------------------------------------------------------
// The machines: a menu, one row each.
// ---------------------------------------------------------------------------
static void floorUpdate() {
    int d = rpgame.repeat(DOWN_BUTTON) ? 1 : (rpgame.repeat(UP_BUTTON) ? M_COUNT - 1 : 0);
    if (d) { floorSel = (uint8_t)((floorSel + d) % M_COUNT); audio::sfx(Sfx::Cursor); }
    if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
    if (rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        if (game.purse < Slots::betOf(FLOOR[floorSel], 0)) { audio::sfx(Sfx::Deny); toast("NOT ENOUGH TO PLAY IT"); return; }
        audio::sfx(Sfx::Select);
        game.machine = FLOOR[floorSel];
        go(Scr::Play);
    }
}

static void floorRender(uint32_t frame) {
    static const char *const NAME[M_COUNT] = {"LUCKY 7", "DRAGON FORTUNE", "SWEET"};
    static const char *const WHAT[M_COUNT] = {"1 LINE, BONUS WHEEL", "25 LINES, JACKPOTS", "5 LINES, SUGAR RUSH"};
    // Each machine's mascot. They are drawn for their own palettes: here the
    // felt colours they use are swapped for ones this screen has.
    static const uint8_t ICON[M_COUNT] = {C_SEVEN, SYM_FORTUNE + F_DRAGON, SYM_SWEET + S_BEAR};
    uint8_t remap[16];
    for (uint8_t i = 0; i < 16; i++) remap[i] = i;
    remap[FELT_LT] = GOLD;          // the dragon's orange whiskers
    remap[FELT_DK] = SKIN;          // the bear's pink belly
    feltBackdrop();
    title35("MACHINES", 5, FX_B, GOLD, WOOD, WINE, 13);
    char buf[20];
    for (uint8_t k = 0; k < M_COUNT; k++) {
        uint8_t m = FLOOR[k];
        int y = 27 + k * 28;
        bool sel = k == floorSel, can = game.purse >= Slots::betOf(m, 0);
        if (sel) panel(5, y, 118, 26, 4, NAVY, (frame & 16) ? FX_B : GOLD);
        else fillRound(5, y, 118, 26, 4, FELT_DK);
        panel(8, y + 1, 24, 24, 3, WHITE, sel ? GOLD : SILVER);
        sym(ICON[m], 9, y + 2, remap);
        gfx_text(36, y + 4, NAME[m], sel ? GOLD : WHITE);
        text35(36, y + 16, WHAT[m], sel ? CYAN : FELT_LT);
        if (!can) { fmtMoney(fmtStr(buf, "NEEDS "), Slots::betOf(m, 0)); text35(119 - text35Width(buf), y + 16, buf, RED); }
    }
    fmtMoney(fmtStr(buf, "PURSE "), game.purse);
    text35(8, 116, buf, GOLD);
    text35(120 - text35Width("A: PLAY"), 116, "A: PLAY", FELT_LT);
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
static void doSpin() {
    game.mix(micros() ^ (rpgame.frameCount << 16));
    if (!game.freeLeft) { sinceSave++; demoSpins++; }
    game.spin();
    present::spin(game);
}

// A game (a paid spin and all it set off) has been shown in full.
static void afterGame() {
    if (game.reachedGoal()) {
        game.stats.banksBroken++;
        hasGame = false;
        persist(false);
        go(Scr::Win);
    } else if (game.broke()) {
        game.stats.timesBroke++;
        hasGame = false;
        persist(false);
        go(Scr::Lose);
    } else {
        game.fitBet();
        if (sinceSave >= 10) { sinceSave = 0; persist(true); }
    }
}

static void idleInput(uint8_t pressed) {
    if (pressed & (UP_BUTTON | RIGHT_BUTTON | DOWN_BUTTON | LEFT_BUTTON | SELECT_BUTTON)) {
        bool ok;
        if (pressed & SELECT_BUTTON) {                  // SELECT walks round the bets
            ok = game.changeBet(1);
            if (!ok) { game.betIdx[game.machine] = 0; ok = true; }
        } else ok = game.changeBet((pressed & (UP_BUTTON | RIGHT_BUTTON)) ? 1 : -1);
        audio::sfx(ok ? Sfx::Chip : Sfx::Deny);
    }
    if (pressed & B_BUTTON) {                           // B: the paytable
        showPays = true; payY = 0; payAuto = true; payWait = 70;
        audio::sfx(Sfx::Select);
        return;
    }
    bool can = game.canSpin();
    if (pressed & A_BUTTON) {
        if (!can) { audio::sfx(Sfx::Deny); toast("NOT ENOUGH FOR THIS BET"); }
        else if (game.machine == M_CLASSIC) { armed = true; armPull = 0; }
        else doSpin();
    }
    if (armed) {
        if (rpgame.pressed(A_BUTTON)) {
            if (armPull < 64) armPull = (uint8_t)(armPull + 4);
            present::setArm(armPull, true);
        } else {
            // Let go: the arm swings on down whatever the pull, and springs back.
            armed = false;
            present::setArm(armPull, false);
            doSpin();
        }
    }
}

static void playUpdate() {
    uint8_t pressed = rpgame.justPressedMask();
    if (paused) {
        if (pressed & (UP_BUTTON | DOWN_BUTTON)) { pauseSel = (uint8_t)((pauseSel + ((pressed & UP_BUTTON) ? 3 : 1)) % 4); audio::sfx(Sfx::Cursor); }
        if (pressed & (START_BUTTON | B_BUTTON)) { paused = false; audio::sfx(Sfx::Select); }
        if (pressed & A_BUTTON) {
            audio::sfx(Sfx::Select);
            switch (pauseSel) {
                case 0: paused = false; break;
                case 1: go(Scr::Floor); break;
                case 2: optBack = Scr::Play; optSel = 0; resumePlay = true; go(Scr::Options); break;
                default: hasGame = true; persist(true); go(Scr::Title); break;
            }
        }
        return;
    }
    if (showPays) {
        // It drifts down by itself; the D-pad takes over, A and B skip a page
        // (and leave past either end), START closes it.
        int top = mach::payScrollMax(game), was = payY;
        if (rpgame.pressed(UP_BUTTON | DOWN_BUTTON)) { payAuto = false; payY = (int16_t)(payY + (rpgame.pressed(DOWN_BUTTON) ? 2 : -2)); }
        if (pressed & A_BUTTON) { payAuto = false; if (payY >= top) showPays = false; payY = (int16_t)(payY + mach::PAY_PAGE); }
        if (pressed & B_BUTTON) { payAuto = false; if (payY <= 0) showPays = false; payY = (int16_t)(payY - mach::PAY_PAGE); }
        if (pressed & START_BUTTON) showPays = false;
        if (payAuto) {
            if (payWait) { if (!--payWait && payY >= top) { payY = 0; payWait = 70; } }
            else if (t & 1) { if (++payY >= top) payWait = 120; }
        }
        if (payY > top) payY = (int16_t)top;
        if (payY < 0) payY = 0;
        if ((pressed & (A_BUTTON | B_BUTTON)) && payY != was) audio::sfx(Sfx::Cursor);
        if (!showPays) audio::sfx(Sfx::Select);
        return;
    }
    bool busy = present::busy();
    if (demo) {
        // Any button hands the machine back; otherwise it plays six games.
        bool idle = !busy && !game.inFeature();
        if (pressed || (idle && demoSpins >= 6 && autoT > 60)) {
            if (!fadeOut) { game = keep; demo = false; go(Scr::Title); }
            return;
        }
        if (idle && !game.canSpin()) game.purse = START_PURSE;
        if (busy) autoT = 0;
        else if (game.holding) { if (++autoT >= 16) { autoT = 0; game.respin(); present::respin(game); } }
        else if (++autoT >= 45 && demoSpins < 6) { autoT = 0; doSpin(); }
        present::setButton(true, false);
        present::update(game);
        return;
    }
    if (busy) {
        autoT = 0;
        if (pressed & (A_BUTTON | B_BUTTON)) present::hurry();
    } else if (game.holding) {
        if (++autoT >= 16) { autoT = 0; game.respin(); present::respin(game); }
    } else if (game.freeLeft) {
        if (++autoT >= 30) { autoT = 0; doSpin(); }
    } else if (pressed & START_BUTTON) {
        paused = true; pauseSel = 0; armed = false;
        present::setArm(0, false);
        audio::sfx(Sfx::Select);
        return;
    } else idleInput(pressed);

    present::setButton(game.canSpin() && !present::busy(), rpgame.pressed(A_BUTTON) && !armed);
    present::update(game);
    busy = present::busy();
    if (wasBusy && !busy && !game.inFeature()) afterGame();
    wasBusy = busy;
}

static void playRender(uint32_t frame) {
    if (paused || toastT || showPays || demo) redrawAll();
    if (showPays) { mach::paytable(game, payY); return; }
    present::render(game, frame);
    if (demo && (frame & 32)) {
        panel(46, 58, 36, 11, 3, INK, GOLD);
        centred57(60, "DEMO", WHITE);
    }
    if (paused) {
        dither(0, 0, 128, 128, INK, 0);
        panel(20, 30, 88, 62, 4, NAVY, GOLD);
        centred57(35, "PAUSED", GOLD);
        static const char *const P[4] = {"RESUME", "MACHINES", "OPTIONS", "SAVE & QUIT"};
        for (int i = 0; i < 4; i++) {
            int y = 48 + i * 11;
            if (i == pauseSel) fillRound(26, y - 2, 76, 11, 3, INK);
            centred57(y, P[i], i == pauseSel ? FX_B : WHITE);
        }
    }
}

#if !CHSL_LEAN
// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
enum Opt : uint8_t { O_GOAL, O_SPEED, O_SOUND, O_MUSIC, O_BACK, OPT_COUNT };

// Options is eight bytes in this order; each entry is "LABEL|value|value...".
static const char *const OPT_TEXT[OPT_COUNT] = {
    "GOAL|$1000|$5000|ENDLESS", "REELS|NORMAL|FAST", "SOUND|ON|OFF", "MUSIC|ON|OFF", "BACK",
};
static_assert(sizeof(Options) == 8, "options menu indexes Options as bytes");

// Copy field k of an "a|b|c" string into buf; returns the number of fields.
static uint8_t optField(const char *s, uint8_t k, char *buf) {
    uint8_t n = 0;
    for (;;) {
        const char *e = s;
        while (*e && *e != '|') e++;
        if (n == k) { uint8_t len = (uint8_t)(e - s); memcpy(buf, s, len); buf[len] = 0; }
        n++;
        if (!*e) return n;
        s = e + 1;
    }
}

static void optionsUpdate() {
    if (rpgame.repeat(UP_BUTTON)) { optSel = (uint8_t)((optSel + OPT_COUNT - 1) % OPT_COUNT); audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON)) { optSel = (uint8_t)((optSel + 1) % OPT_COUNT); audio::sfx(Sfx::Cursor); }
    int d = rpgame.justPressed(RIGHT_BUTTON) ? 1 : (rpgame.justPressed(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.justPressed(A_BUTTON) && optSel != O_BACK) d = 1;
    if (d && optSel != O_BACK) {
        char tmp[12];
        uint8_t n = (uint8_t)(optField(OPT_TEXT[optSel], 0, tmp) - 1);
        uint8_t *f = (uint8_t *)&game.opt + optSel;
        *f = (uint8_t)((*f + n + d) % n);
        applyOptions();
        audio::sfx(Sfx::Chip);
    }
    if ((rpgame.justPressed(A_BUTTON) && optSel == O_BACK) || rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        persist(hasGame);
        go(optBack);
    }
}

static void optionsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("OPTIONS", 6, FX_B, GOLD, WOOD, WINE, 13);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 34 + i * 13;
        bool sel = i == optSel;
        char buf[12];
        if (sel) fillRound(6, y - 3, 116, 11, 3, NAVY);
        optField(OPT_TEXT[i], 0, buf);
        if (i == O_BACK) { centred35(y, buf, sel ? WHITE : FELT_LT); continue; }
        text35(10, y, buf, sel ? WHITE : FELT_LT);
        optField(OPT_TEXT[i], (uint8_t)(1 + ((uint8_t *)&game.opt)[i]), buf);
        int w = text35Width(buf);
        text35(118 - w, y, buf, sel ? FX_B : GOLD);
        if (sel) { text35(112 - w, y, "<", SILVER); text35(120, y, ">", SILVER); }
    }
    centred35(100, optSel == O_GOAL ? "REACH IT TO BREAK THE BANK" : "B: BACK", SILVER);
    centred35(112, "FONT BY", FELT_LT);
    centred35(119, "PRESS PLAY ON TAPE", FELT_LT);
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
static const uint8_t STAT_RESET_FRAMES = 90;
static uint8_t statHold = 0;

static void statsUpdate() {
    if (rpgame.pressed(SELECT_BUTTON)) {
        if (statHold < 255) statHold++;
        if (statHold == STAT_RESET_FRAMES) {
            memset(&game.stats, 0, sizeof game.stats);
            persist(hasGame);
            audio::sfx(Sfx::Deny);
        }
        return;
    }
    statHold = 0;
    if (rpgame.justPressed(A_BUTTON | B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void statsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("STATS", 6, WHITE, CYAN, BLUE, NAVY);
    const Stats &s = game.stats;
    static const char *const NAME[8] = {"SPINS", "FREE GAMES", "HOLD AND SPINS", "JACKPOTS",
                                        "BEST PURSE", "BIGGEST WIN", "BANKS BROKEN", "TIMES BROKE"};
    const int32_t val[8] = {(int32_t)s.spins, s.freeGames, s.holdSpins, s.jackpots,
                            s.bestPurse, s.biggestWin, s.banksBroken, s.timesBroke};
    for (int i = 0; i < 8; i++) {
        char buf[16];
        int y = 28 + i * 10;
        text35(10, y, NAME[i], FELT_LT);
        if (i == 4 || i == 5) fmtMoney(buf, val[i]); else fmtInt(buf, val[i]);
        text35(118 - text35Width(buf), y, buf, (i == 4 || i == 5) ? GOLD : WHITE);
    }
    if (statHold >= STAT_RESET_FRAMES) centred35(110, "STATS RESET", GOLD);
    else if (statHold) {
        gfx_rect(24, 110, 80, 5, SILVER);
        gfx_fillRect(25, 111, 78 * statHold / STAT_RESET_FRAMES, 3, RED);
    } else centred35(110, "HOLD SELECT TO RESET", FELT_LT);
    centred35(118, save::available() ? "SAVED IN FLASH" : "SAVING UNAVAILABLE", SILVER);
}

#else
// Device debug builds leave the options and stats pages out to fit the
// serial protocol (CHSL_LEAN, config.h): the tests drive the game directly.
static void optionsUpdate() { go(Scr::Title); }
static void optionsRender(uint32_t) { gfx_clear(FELT); }
static void statsUpdate() { go(Scr::Title); }
static void statsRender(uint32_t) { gfx_clear(FELT); }
static uint8_t statHold = 0;
#endif

// ---------------------------------------------------------------------------
// Game won / lost (CHBlackjack's, after PPOT's GameWinState / GameLoseState)
// ---------------------------------------------------------------------------
static void endUpdate() {
    if (t > 60 && rpgame.justPressed(A_BUTTON | START_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void ray(int x1, int y1, uint8_t c) {
    int x = 64, y = 60;
    int dx = x1 > x ? x1 - x : x - x1, dy = y1 > y ? y1 - y : y - y1;
    int sx = x < x1 ? 1 : -1, sy = y < y1 ? 1 : -1, err = dx - dy;
    while ((unsigned)x < GFX_W && (unsigned)y < GFX_H) {
        gfx_pixel(x, y, c);
        if (x == x1 && y == y1) break;
        int e2 = err << 1;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
    }
}

static void winRender(uint32_t frame) {
    gfx_clear(NAVY);
    for (int i = 0; i < 16; i++) {
        int a = i * 16 + (int)(frame & 255);
        int x1 = 64 + ((fx::isin(a + 64) * 120) >> 8), y1 = 60 + ((fx::isin(a) * 120) >> 8);
        ray(x1, y1, (i & 1) ? FX_A : WINE);
    }
    static const uint8_t R[5] = {RED, GOLD, FELT_LT, CYAN, BLUE};
    uint8_t c = R[(frame / 4) % 5], d = R[(frame / 4 + 2) % 5];
    title35("YOU BROKE", 12, WHITE, c, d, WINE, 10);
    title35("THE BANK!", 36, WHITE, d, c, WINE, 10);
    char buf[16];
    fmtMoney(buf, game.purse);
    title35(buf, 64, FX_B, GOLD, WOOD, WINE, 13);
    if ((frame % 6) == 0) fx::fountain(fx::COIN, fx::rndRange(20, 108), 120, 2);
    if ((frame % 24) == 0) fx::burst(fx::STAR, fx::rndRange(16, 112), fx::rndRange(10, 50), 16, 60, FX_A);
    if ((frame % 30) == 0) fx::fountain(fx::CONFETTI, fx::rndRange(20, 108), 100, 10);
    fx::update();
    fx::drawParticles();
    if (t > 60 && (frame & 16)) centred35(108, "PRESS A", WHITE);
}

static void loseRender(uint32_t frame) {
    gfx_clear(INK);
    pal::setDesaturate((uint8_t)(t / 12 > 12 ? 12 : t / 12));
    for (int x = 3; x < 128; x += 8) gfx_vline(x, 0, 128, NAVY);
    int drop = t < 40 ? (40 - t) : 0;
    title35("YOU ARE", 14 - drop, WHITE, RED, WINE, NAVY, 10);
    if (t > 20) title35("BROKE", 40 - (t < 60 ? 60 - t : 0), WHITE, RED, WINE, NAVY, 10);
    for (int i = 0; i < 2; i++) fx::spawn(fx::RAIN, fx::rndRange(0, 128), -4, -6, 64, 40, CYAN);
    fx::update();
    fx::drawParticles();
    // So close: seven, seven, lemon.
    static const uint8_t MISS[3] = {C_SEVEN, C_SEVEN, C_LEMON};
    for (int i = 0; i < 3; i++) {
        panel(24 + i * 28, 72, 26, 26, 2, WHITE, SILVER);
        sym(MISS[i], 26 + i * 28, 74);
    }
    gfx_fillRect(0, 112, 128, 16, INK);
    if (t > 60 && (frame & 16)) centred35(116, "PRESS A", WHITE);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------
void update() {
    t++;
    if (toastT && !--toastT) redrawAll();
    static bool wasPaused = false;
    if (paused != wasPaused) { wasPaused = paused; redrawAll(); }
    audio::update();
    if (fadeOut) {
        pal::setFade((uint8_t)(fadeOut * 2));
        if (--fadeOut == 0) enter(pending);
        return;
    }
    if (fadeIn) { fadeIn--; pal::setFade((uint8_t)(16 - fadeIn * 2)); }
    switch (cur) {
        case Scr::Title: titleUpdate(); break;
        case Scr::Floor: floorUpdate(); break;
        case Scr::Play: playUpdate(); break;
        case Scr::Options: optionsUpdate(); break;
        case Scr::Stats: statsUpdate(); break;
        case Scr::Win: case Scr::Lose: endUpdate(); break;
    }
}

static bool unchanged(uint32_t sig) {
    if (sig == staticSig && !fx::particlesAlive() && !toastT) return true;
    staticSig = sig;
    return false;
}

void render(uint32_t frame) {
    if (cur == Scr::Options || cur == Scr::Stats || cur == Scr::Floor) {
        uint32_t sig = (uint32_t)cur * 2654435761u ^ ((uint32_t)optSel << 12) ^ ((uint32_t)statHold << 20) ^
                       ((uint32_t)floorSel << 28) ^ (uint32_t)game.purse;
        for (uint8_t i = 0; i < 8; i++) sig = sig * 31u + ((uint8_t *)&game.opt)[i];
        if (cur == Scr::Floor) sig ^= (frame >> 4 & 1) << 8;
        if (unchanged(sig | 1)) return;
    }
    switch (cur) {
        case Scr::Title: titleRender(frame); break;
        case Scr::Floor: floorRender(frame); break;
        case Scr::Play: playRender(frame); break;
        case Scr::Options: optionsRender(frame); break;
        case Scr::Stats: statsRender(frame); break;
        case Scr::Win: winRender(frame); break;
        case Scr::Lose: loseRender(frame); break;
    }
    if (toastT) {
        int w = gfx_textWidth(toastText) + 8;
        panel(64 - w / 2, 2, w, 11, 3, INK, GOLD);
        centred57(4, toastText, WHITE);
    }
}

// ---------------------------------------------------------------------------
// Debug protocol (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   R <seed>             the reels from a fixed seed (timing no longer mixed in)
//   F <s0> .. <s4>       force the next spin's stops
//   G <n>                force a feature on the next Fortune spin: 1 free games,
//                        2 hold and spin, 3 a dragon, 4 five cats; 5 LUCKY 7's bonus wheel
//   J <T|M|C|F|Y|O|S|W|L>  jump: title, machines, play LUCKY 7, DRAGON FORTUNE or SWEET,
//                        options, stats, win, lose
//   M <purse>            set the purse
//   E <index>            set the bet (0..3)
//   V                    reboot the game (reload from the save, back to the title)
//   H                    state: STATE purse machine bet lastWin free holding busy
//   Q                    (simulator) host-time calibration for perf estimates
bool debugCommand(char cmd, const char *args) {
    char buf[120], *p;
    switch (cmd) {
        case 'R': game.seed(dbg::parseNum(args, 10)); return true;
        case 'F': {
            uint8_t stops[5];
            for (int i = 0; i < 5; i++) stops[i] = (uint8_t)(dbg::parseNum(args, 10) % game.stripLen());
            game.force(stops);
            return true;
        }
        case 'G': game.forceFeature((uint8_t)dbg::parseNum(args, 10)); return true;
        case 'J': {
            rpgame.frameCount = 0;
            pal::resetClock();
            fx::reseed();
            fadeOut = fadeIn = 0;
            pal::setFade(16);
            switch (args[0]) {
                case 'T': enter(Scr::Title); break;
                case 'M': enter(Scr::Floor); break;
                case 'C': case 'F': case 'Y':
                    game.newGame(); hasGame = true; resumePlay = false;
                    game.machine = args[0] == 'F' ? M_FORTUNE : (args[0] == 'Y' ? M_SWEET : M_CLASSIC);
                    enter(Scr::Play); break;
                case 'O': optBack = Scr::Title; enter(Scr::Options); break;
                case 'S': enter(Scr::Stats); break;
                case 'W': enter(Scr::Win); break;
                case 'L': enter(Scr::Lose); break;
                default: return false;
            }
            return true;
        }
        case 'M': game.purse = (int32_t)dbg::parseNum(args, 10); present::reset(game); return true;
        case 'E': game.betIdx[game.machine] = (uint8_t)(dbg::parseNum(args, 10) & 3); return true;
        case 'V': memset(&game, 0, sizeof game); begin(); return true;     // a reboot: everything from flash
        case 'H': {
            p = fmtStr(buf, "STATE ");
            p = fmtInt(p, game.purse); *p++ = ' ';
            p = fmtInt(p, game.machine); *p++ = ' ';
            p = fmtInt(p, game.bet()); *p++ = ' ';
            p = fmtInt(p, game.lastWin); *p++ = ' ';
            p = fmtInt(p, game.freeLeft); *p++ = ' ';
            p = fmtInt(p, game.holding); *p++ = ' ';
            p = fmtInt(p, present::busy());
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
    }
    return false;
}
#else
bool debugCommand(char, const char *) { return false; }
#endif

}  // namespace screens
