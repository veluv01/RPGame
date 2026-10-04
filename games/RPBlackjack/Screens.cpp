#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// Screens derived from Press-Play-On-Tape/Blackjack (Apache-2.0):
// SplashScreenState, TitleScreenState, GameWinState, GameLoseState and the
// Game loop. Modified 2026 for RPGame by bateske: colour, animation, music,
// options, statistics, saving, a pause menu and an attract-mode demo.
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Screens.h"
#include "Round.h"
#include "Presenter.h"
#include "Fx.h"
#include "Bar.h"
#include "CardArt.h"
#include "Layout.h"
#include "Table.h"
#include "Sounds.h"
#include "Save.h"
#include "src/assets/Assets.h"

namespace screens {

enum class Scr : uint8_t { Splash, Title, Play, Options, Stats, Win, Lose, Credits };

static Round game;
static Scr cur = Scr::Splash, pending = Scr::Splash;
static uint8_t fadeOut = 0, fadeIn = 0;      // palette fade transitions
static uint16_t t = 0;                       // frames on this screen
static bool hasGame = false, demo = false, paused = false, resumePlay = false;
static bool seeded = false;
static uint8_t menuSel = 0, pauseSel = 0, optSel = 0;
static Scr optBack = Scr::Title;
static uint8_t toastT = 0;
static const char *toastText = "";
static Phase lastPhase = Phase::StartHand;
static uint32_t staticSig = 0;             // last drawn state of a still screen
static bool creditsReady = false;          // the credits page's static layer is drawn

static void toast(const char *s) { toastText = s; toastT = 60; }

// Options "SOUND|LEAD|ARPEGGIO|OFF" (all-zero is the default) -> audio mode
// (2 lead, 1 arpeggio, 0 off).
static uint8_t soundMode(uint8_t opt) { return opt >= 2 ? 0 : (uint8_t)(2 - opt); }

// The play screen only redraws what changed; anything drawn over it from
// outside (pause menu, toast, demo label) has to force a full redraw.
static void redrawAll() { present::invalidate(); bar::invalidate(); staticSig = 0; creditsReady = false; }

static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

static void persist(bool keepGame) {
    if (demo) return;
    gfx_wait();
    save::store(game, keepGame);
}

static void enter(Scr s) {
    cur = s; t = 0; fadeIn = 8;
    creditsReady = false;
    fx::clear();
    pal::setDesaturate(0);
    pal::setCycling(true);
    switch (s) {
        case Scr::Splash:
            audio::stopMusic();
            break;
        case Scr::Title:
            demo = false; paused = false; menuSel = 0;
            playSong(Song::Title, true);
            break;
        case Scr::Play:
            audio::stopMusic();
            if (!resumePlay) {
                present::reset(game);
                bar::reset();
            }
            redrawAll();
            resumePlay = false;
            paused = false;
            lastPhase = game.phase;
            break;
        case Scr::Win:
            playSong(Song::Victory, false);
            audio::led(audio::LED_PARTY);
            break;
        case Scr::Lose:
            playSong(Song::Broke, true);
            break;
        default:
            break;
    }
}

void begin() {
    art::fourColour = false;
    bool ok = save::load(game, hasGame);
    (void)ok;
    art::fourColour = game.opt.fourColour;
    pal::setTheme(game.opt.theme);
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, false);
    sound::setMode(soundMode(game.opt.sound));
    enter(Scr::Splash);
}

void debugSeed(uint32_t s) { game.seed(s); seeded = true; }
void debugStack(const uint8_t *c, uint8_t n) { game.stackDeck(c, n); }
void debugJump(char c) {
    // Scripted tests start from a known animation clock, so a board that has
    // been running since boot renders the same frames as a fresh simulator.
    rpgame.frameCount = 0;
    pal::resetClock();
    fx::reseed();                                        // enter() clears the particles
    switch (c) {
        case 'T': enter(Scr::Title); break;
        case 'P': game.newGame(); demo = false; enter(Scr::Play); break;
        case 'W': enter(Scr::Win); break;
        case 'L': enter(Scr::Lose); break;
        case 'O': optBack = Scr::Title; enter(Scr::Options); break;
        case 'S': enter(Scr::Stats); break;
        case 'C': enter(Scr::Credits); break;
    }
}

static void seedOnce() {
    if (seeded) return;
    game.seed(micros() * 2654435761u ^ rpgame.frameCount);
    seeded = true;
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

static void lettering(const uint8_t *bits, int w, int x, int y, const uint8_t *ramp, int outline, int shadow) {
    Mask m = maskBegin(w, 16);
    maskBlit1(m, bits, (uint8_t)w, 16);
    maskDraw(m, x, y, GOLD, outline, shadow, ramp);
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

// ---------------------------------------------------------------------------
// Splash: PPOT's logo, now with a little colour
// ---------------------------------------------------------------------------
// Frames at which each item appears (one a second), then a three-second hold.
// The still-screen check in render() redraws only when one of these passes.
enum : uint16_t { SPLASH_PRESENTS = 60, SPLASH_LOGO = 120, SPLASH_TAG = 180, SPLASH_END = 360 };

static void splashUpdate() {
    if (t == 1) audio::blip(2600, 60);
    if (rpgame.justPressedMask() || t > SPLASH_END) { audio::blip(1800, 40); go(Scr::Title); }
}

// Press Play On Tape's logo, 65x32: in its blue gradient (splash), or
// printed flat on the felt like the table's own lettering (credits).
static void ppotLogo(int x, int y, bool printed = false) {
    Mask m = maskBegin(PPOT_LOGO_W, PPOT_LOGO_H);
    maskBlit1(m, PPOT_LOGO, PPOT_LOGO_W, PPOT_LOGO_H);
    if (printed) { maskDraw(m, x, y, FELT_LT); return; }
    uint8_t ramp[PPOT_LOGO_H];
    for (int i = 0; i < PPOT_LOGO_H; i++) ramp[i] = i < 11 ? WHITE : (i < 22 ? CYAN : BLUE);
    maskDraw(m, x, y, WHITE, NAVY, -1, ramp);
}

static void splashRender(uint32_t frame) {
    gfx_clear(INK);
    ppotLogo(64 - PPOT_LOGO_W / 2, 26);
    if (t > SPLASH_PRESENTS) centred35(68, "PRESENTS", SILVER);
    if (t > SPLASH_LOGO) {
        Mask l = maskBegin(104, 14);
        maskBlit1(l, LOGO, 104, 14);
        uint8_t r2[16];
        for (int i = 0; i < 16; i++) r2[i] = i < 3 ? FX_B : (i < 13 ? GOLD : WOOD);
        maskDraw(l, 12, 82, GOLD, INK, WINE, r2);
    }
    if (t > SPLASH_TAG) centred35(104, "NOW IN COLOUR", FX_A);
}

// ---------------------------------------------------------------------------
// Title
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

static void startDemo() {
    demo = true;
    game.seed(fx::rnd());
    game.newGame();
    enter(Scr::Play);
    demo = true;
}

static void titleUpdate() {
    uint8_t items[5], n = menu(items);
    if (menuSel >= n) menuSel = 0;
    if (rpgame.repeat(UP_BUTTON) && menuSel > 0) { menuSel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && menuSel + 1 < n) { menuSel++; audio::sfx(Sfx::Cursor); }
    if (rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
        seedOnce();
        switch (items[menuSel]) {
            case I_PLAY: case I_NEW: game.newGame(); hasGame = true; go(Scr::Play); break;
            case I_CONTINUE: game.resume(); go(Scr::Play); break;
            case I_OPTIONS: optBack = Scr::Title; optSel = 0; go(Scr::Options); break;
            case I_STATS: go(Scr::Stats); break;
        }
    }
    // Ten idle seconds, then attract mode - but the tune plays out to its last
    // bar first (it stops at its end instead of looping), so the demo never
    // cuts it off mid-phrase. A button press puts it back on repeat.
    static uint16_t idle = 0;
    idle = rpgame.buttons() ? 0 : (uint16_t)(idle + 1);
    audio::loopMusic(idle <= 600);
    if (idle > 600 && !audio::musicPlaying()) { idle = 0; startDemo(); }
}

static void titleRender(uint32_t frame) {
    feltBackdrop();
    // Spotlight.
    dither(24, 30, 80, 44, FELT_LT, 0);
    // Logo: the top rows use FX_B, so the palette makes it shimmer with no redraw.
    Mask m = maskBegin(104, 14);
    maskBlit1(m, LOGO, 104, 14);
    uint8_t ramp[16];
    for (int i = 0; i < 16; i++) ramp[i] = i < 3 ? FX_B : (i < 12 ? GOLD : WOOD);
    maskDraw(m, 12, 8, GOLD, INK, WINE, ramp);
    centred35(25, "~COLOUR~EDITION~", CYAN);

    // A fanned blackjack hand dealt in from above, gently bobbing.
    static const uint8_t HAND[5] = {0 + 26, 12, 11 + 13, 10 + 39, 9 + 26};   // A-spade K-heart Q-diamond J-club 10-spade
    for (int i = 0; i < 5; i++) {
        int start = i * 10;
        int k = (int)t - start;
        if (k < 0) continue;
        int e = fx::ease(fx::OUT_BACK, k, 18);
        int tx = 18 + i * 16, ty = 38 + (i == 2 ? 0 : (i == 1 || i == 3 ? 2 : 6));
        int y = -30 + ((ty + 30) * e >> 8);
        art::card(tx, y, HAND[i], k > 12, 22, true);
        if (k == 12) audio::sfx(Sfx::Flip);
    }
    if (t == 60) fx::burst(fx::STAR, 64, 52, 14, 60, FX_A);
    // Chip stacks either side.
    art::chipStack(14, 110, 25 * 3 + 10 * 2, 6);
    art::chipStack(114, 110, 100 + 25 * 2 + 5, 6);

    // Credits in the bottom corners of the flat felt, 1 px in from the
    // dithered border (feltBackdrop: 6 px, so the flat area is 6..121). Drawn
    // before the menu: with four items, the last one's highlight overlaps them.
    text35(7, 116, "PPOT 2018", FELT_LT);
    text35(121 - text35Width("CHGAME 2026"), 116, "CHGAME 2026", FELT_LT);

    // Menu.
    uint8_t items[5], n = menu(items);
    static const char *const LABEL[5] = {"PLAY", "CONTINUE", "NEW GAME", "OPTIONS", "STATS"};
    int y0 = 76;
    for (uint8_t i = 0; i < n; i++) {
        char buf[20];
        if (items[i] == I_CONTINUE) fmtMoney(fmtStr(buf, "CONTINUE "), game.purse);
        else strcpy(buf, LABEL[items[i]]);
        int y = y0 + i * 11;
        bool sel = i == menuSel;
        int w = gfx_textWidth(buf);
        if (sel) {
            panel(64 - w / 2 - 6, y - 2, w + 12, 11, 3, NAVY, (frame & 16) ? FX_B : GOLD);
        }
        centred57(y, buf, sel ? GOLD : WHITE);
    }
    fx::update();
    fx::drawParticles();
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
static uint8_t demoInput() {
    // One press every half second - half a quick player's pace - so a watcher
    // can read the hand and follow each decision. Animations run at full speed.
    static uint8_t cool = 0;
    static uint16_t want = 0;                        // this hand's bet, chosen as betting opens
    static Phase seen = Phase::Quit;
    if (game.phase != seen) {
        seen = game.phase;
        if (seen == Phase::InitBet) want = (uint16_t)(5 + (fx::rnd() % 4) * 5);
    }
    if (cool) { cool--; return 0; }
    cool = 29;
    auto toward = [](uint8_t target) -> uint8_t {
        if (game.sel < target) return RIGHT_BUTTON;
        if (game.sel > target) return LEFT_BUTTON;
        return A_BUTTON;
    };
    switch (game.phase) {
        case Phase::InitBet:
            if (game.initBet < want && game.purse >= 5) return toward(B_5);
            return toward(B_DEAL);
        case Phase::OfferInsurance: return toward(I_NO);
        case Phase::PlayHand: return toward(game.hands[game.active].best() < 17 ? P_HIT : P_STAND);
        case Phase::EndOfGame:
            if (game.purse < 30) game.purse = 500;        // the demo never goes broke
            return toward(E_CONTINUE);
        default: return 0;
    }
}

static void playUpdate() {
    uint8_t pressed = rpgame.justPressedMask();
    uint8_t rep = 0;
    static const uint8_t RB[6] = {LEFT_BUTTON, RIGHT_BUTTON, UP_BUTTON, DOWN_BUTTON, A_BUTTON, B_BUTTON};
    for (uint8_t i = 0; i < 6; i++) if (rpgame.repeat(RB[i])) rep |= RB[i];

    if (demo) {
        if (pressed) { save::load(game, hasGame); go(Scr::Title); return; }
        pressed = rep = demoInput();
    }

    if (paused) {
        if (pressed & (UP_BUTTON | DOWN_BUTTON)) { pauseSel = (uint8_t)((pauseSel + ((pressed & UP_BUTTON) ? 2 : 1)) % 3); audio::sfx(Sfx::Cursor); }
        if (pressed & (START_BUTTON | B_BUTTON)) { paused = false; audio::sfx(Sfx::Select); }
        if (pressed & A_BUTTON) {
            audio::sfx(Sfx::Select);
            if (pauseSel == 0) paused = false;
            else if (pauseSel == 1) { optBack = Scr::Play; optSel = 0; resumePlay = true; go(Scr::Options); }
            else {
                bool mid = game.phase != Phase::EndOfGame && game.phase != Phase::InitBet;
                (void)mid;
                persist(game.purse > 0);
                hasGame = game.purse > 0;
                go(Scr::Title);
            }
        }
        return;
    }
    if (!demo && (pressed & START_BUTTON)) { paused = true; pauseSel = 0; audio::sfx(Sfx::Select); return; }
    if (pressed & SELECT_BUTTON) { sound::mute(!sound::muted()); toast(sound::muted() ? "SOUND OFF" : "SOUND ON"); }
    if ((pressed & B_BUTTON) && game.phase != Phase::InitBet) present::dismissBubble();

    game.update(pressed & ~START_BUTTON, rep, present::busy());
    present::onEvents(game);
    present::update(game);

    if (game.phase != lastPhase) {
        if (game.phase == Phase::EndOfGame && game.stats.hands % 5 == 0) persist(true);
        if (game.phase == Phase::GameWon) { hasGame = false; persist(false); go(Scr::Win); }
        if (game.phase == Phase::GameLost) { hasGame = false; persist(false); go(Scr::Lose); }
        if (game.phase == Phase::Quit) { hasGame = true; persist(true); go(Scr::Title); }
        lastPhase = game.phase;
    }
}

static void playRender(uint32_t frame) {
    if (paused || demo || toastT) redrawAll();
    bool drew = present::render(game, frame);
    dbg::prof(9);
    if (present::rowsMoving(lay::BAR_Y, 127)) bar::invalidate();     // a chip left the bar
    drew |= bar::draw(game, frame);
    if (drew) present::overlay();
    dbg::prof(10);
    if (demo && (frame & 32)) {
        fillRound(44, 50, 40, 11, 3, INK);
        centred57(52, "DEMO", FX_A);
    }
    if (paused) {
        dither(0, 0, 128, 128, INK, 0);
        panel(24, 34, 80, 56, 4, NAVY, GOLD);
        centred57(39, "PAUSED", GOLD);
        static const char *const P[3] = {"RESUME", "OPTIONS", "SAVE & QUIT"};
        for (int i = 0; i < 3; i++) {
            int y = 53 + i * 11;
            if (i == pauseSel) fillRound(30, y - 2, 68, 11, 3, INK);
            centred57(y, P[i], i == pauseSel ? FX_B : WHITE);
        }
    }
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
enum Opt : uint8_t { O_RULES, O_GOAL, O_SPEED, O_SOUND, O_FELT, O_DECK, O_TOTALS, O_DEALER, O_BACK, OPT_COUNT };

// Options is eight bytes in this order; each entry is "LABEL|value|value...".
static const char *const OPT_TEXT[OPT_COUNT] = {
    "RULES|CASINO|CLASSIC", "GOAL|$1000|$5000|ENDLESS", "SPEED|NORMAL|FAST", "SOUND|LEAD|ARPEGGIO|OFF",
    "FELT|GREEN|BLUE|RED|PURPLE", "CARDS|2 COLOUR|4 COLOUR", "TOTALS|SHOW|HIDE", "DEALER|CLASSIC|NIGHT", "BACK",
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
        uint8_t before = *f;
        *f = (uint8_t)((*f + n + d) % n);
        switch (optSel) {
            case O_RULES: if (*f != before) game.resetShoe(); break;
            case O_SOUND: sound::setMode(soundMode(*f)); break;
            case O_FELT: pal::setTheme(*f); break;
            case O_DECK: art::fourColour = *f; break;
        }
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
        int y = 26 + i * 10;
        bool sel = i == optSel;
        char buf[12];
        if (sel) fillRound(6, y - 2, 116, 10, 3, NAVY);
        optField(OPT_TEXT[i], 0, buf);
        text35(10, y, buf, sel ? WHITE : FELT_LT);
        if (i == O_BACK) continue;
        optField(OPT_TEXT[i], (uint8_t)(1 + ((uint8_t *)&game.opt)[i]), buf);
        int w = text35Width(buf);
        text35(118 - w, y, buf, sel ? FX_B : GOLD);
        if (sel) { text35(112 - w, y, "<", SILVER); text35(120, y, ">", SILVER); }
    }
    const char *help = optSel == O_RULES
        ? (game.opt.rules ? "PPOT: DEALER HITS SOFT 17S" : "STANDS ON 17, 6 DECKS")
        : "B: BACK";
    centred35(118, help, SILVER);
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
// Holding SELECT on the stats page for 1.5 s wipes the lifetime stats.
static const uint8_t STAT_RESET_FRAMES = 90;
static uint8_t statHold = 0;                     // frames SELECT has been held here

static void statsUpdate() {
    if (rpgame.pressed(SELECT_BUTTON)) {
        if (statHold < 255) statHold++;
        if (statHold == STAT_RESET_FRAMES) {
            memset(&game.stats, 0, sizeof game.stats);
            persist(hasGame);
            audio::sfx(Sfx::Bust);
        }
        return;
    }
    statHold = 0;
    if (rpgame.justPressed(A_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Credits); }
    else if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void statsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("STATISTICS", 6, WHITE, CYAN, BLUE, NAVY);
    const Stats &s = game.stats;
    static const char *const NAME[9] = {"HANDS PLAYED", "HANDS WON", "HANDS LOST", "PUSHES", "BLACKJACKS",
                                        "BEST PURSE", "BIGGEST WIN", "BANKS BROKEN", "TIMES BROKE"};
    const int32_t val[9] = {(int32_t)s.hands, (int32_t)s.won, (int32_t)s.lost, (int32_t)s.pushed,
                            (int32_t)s.blackjacks, s.bestPurse, s.biggestWin, s.gamesWon, s.gamesBroke};
    for (int i = 0; i < 9; i++) {
        char buf[16];
        int y = 26 + i * 9;
        text35(10, y, NAME[i], FELT_LT);
        if (i == 5 || i == 6) fmtMoney(buf, val[i]); else fmtInt(buf, val[i]);
        text35(118 - text35Width(buf), y, buf, i >= 5 ? GOLD : WHITE);
    }
    centred35(106, "PRESS A FOR CREDITS", SILVER);
    if (statHold >= STAT_RESET_FRAMES) centred35(112, "STATS RESET", GOLD);
    else if (statHold) {                          // the reset gesture filling up
        gfx_rect(24, 112, 80, 5, SILVER);
        gfx_fillRect(25, 113, 78 * statHold / STAT_RESET_FRAMES, 3, RED);
    } else centred35(112, "HOLD SELECT TO RESET", FELT_LT);
    centred35(119, save::available() ? "SAVED IN FLASH" : "SAVING UNAVAILABLE", SILVER);
}

// ---------------------------------------------------------------------------
// Credits: the back room. The dealer, under his lamp, tells you who made the
// game while Press Play On Tape's name is printed on the felt the way a
// casino prints its own. A neon sign flickers; a cigarette smoulders.
//
// The felt is drawn once; each frame redraws only the wall band (dealer,
// bubble, neon, smoke) and the rail.
// ---------------------------------------------------------------------------
static void creditsUpdate() {
    if (rpgame.justPressed(A_BUTTON | B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Stats); }
}

#if !CHBJ_LEAN
static const char *const CREDIT_LINES[] = {
    "Thanks for\nplaying!", "Blackjack by\nPress Play\nOn Tape", "Code by\nfilmote", "Art by\nvampirics",
    "In colour\non RPGame!", "Another\nhand?",
};
static const uint16_t CREDIT_FRAMES = 180;       // per line: typed at a letter every 2 frames, then held

#endif

static void creditsRender(uint32_t frame) {
#if CHBJ_LEAN
    (void)frame;                                         // device debug builds: no credits page
#else
    if (!creditsReady) {                                  // the felt, once
        creditsReady = true;
        const int top = lay::RAIL_Y + lay::RAIL_H;
        gfx_fillRect(0, top, 128, 128 - top, FELT);
        dither(0, top, 6, 128, FELT_DK, 0);           // the lamplight falls off
        dither(122, top, 6, 128, FELT_DK, 1);
        dither(0, 122, 128, 6, FELT_DK, 1);
        ppotLogo(64 - PPOT_LOGO_W / 2, 51, true);         // printed on the felt
        art::chipStack(16, 80, 25 * 3 + 10 * 2, 6);
        art::chipStack(112, 80, 100 + 25 * 2 + 5, 6);
        text35(24, 93, "CODE\nART", FELT_LT);
        text35(68, 93, "FILMOTE\nVAMPIRICS", GOLD);
        centred35(115, "ORIGINAL FOR ARDUBOY 2018", FELT_LT);
    }

    // The wall band: the dealer telling the credits one line at a time.
    table::wall(frame);
    const int n = (int)(sizeof CREDIT_LINES / sizeof CREDIT_LINES[0]);
    int line = (t / CREDIT_FRAMES) % n, typed = (t % CREDIT_FRAMES) / 2;
    const char *text = CREDIT_LINES[line];
    uint8_t expr = typed < (int)strlen(text) ? (((t >> 2) & 1) ? table::E_TALK : table::E_NORMAL)
                 : (t % 150) < 6              ? table::E_BLINK
                 : (line == 0 || line == n - 1) ? table::E_SMILE : table::E_NORMAL;
    table::dealer(expr, 1, game.opt.dealer != 0);
    present::speechBubble(text, typed);

    // A neon sign that doesn't quite work.
    bool lit = (frame % 97) >= 3 && (frame % 211) >= 2;
    roundRect(103, 5, 23, 21, 3, lit ? RED : WINE);
    text35(107, 9, "OPEN", lit ? SKIN : WINE);
    text35(109, 17, "24H", lit ? SKIN : WINE);

    // An ashtray on the rail, and a curl of smoke drifting up past the sign.
    gfx_fillEllipse(111, 40, 6, 2, SILVER);
    gfx_hline(108, 39, 7, INK);
    gfx_hline(113, 38, 5, WHITE);
    gfx_pixel(118, 38, (frame & 8) ? RED : GOLD);
    for (int i = 2; i < 30; i++) {
        if (((uint32_t)i - (frame >> 2)) % 5u == 0) continue;          // gaps rising with the smoke
        if (i > 18 && (i & 1)) continue;                              // thinning out
        gfx_pixel(118 + ((fx::isin(i * 10 - (int)frame * 2) * (i / 5 + 1)) >> 8), 38 - i, SILVER);
    }
    table::rail(frame);
#endif
}

// ---------------------------------------------------------------------------
// Game won / lost (PPOT's GameWinState / GameLoseState, in colour)
// ---------------------------------------------------------------------------
static void endUpdate() {
    if (t > 60 && rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
        audio::stopMusic();
        go(Scr::Title);
    }
}

// gfx_line's Bresenham from the sunburst's centre, stopped where it leaves
// the screen: a ray only moves outward, so nothing after that is visible
// (gfx_line walks all 120 px and clips each pixel).
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
    // Rotating sunburst - PPOT's radiating stars, grown up.
    for (int i = 0; i < 16; i++) {
        int a = i * 16 + (int)(frame & 255);
        int x1 = 64 + ((fx::isin(a + 64) * 120) >> 8), y1 = 60 + ((fx::isin(a) * 120) >> 8);
        ray(x1, y1, (i & 1) ? FX_A : WINE);
    }
    uint8_t ramp[16];
    static const uint8_t R[5] = {RED, GOLD, FELT_LT, CYAN, BLUE};
    for (int i = 0; i < 16; i++) ramp[i] = R[((i / 3) + frame / 4) % 5];
    lettering(YOUWON1, YOUWON1_W, 64 - YOUWON1_W / 2, 12, ramp, INK, WINE);
    lettering(YOUWON2, YOUWON2_W, 64 - YOUWON2_W / 2, 36, ramp, INK, WINE);
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
    // Colour drains out of the world...
    pal::setDesaturate((uint8_t)(t / 12 > 12 ? 12 : t / 12));
    for (int x = 3; x < 128; x += 8) gfx_vline(x, 0, 128, NAVY);
    uint8_t ramp[16];
    for (int i = 0; i < 16; i++) ramp[i] = i < 4 ? WHITE : (i < 10 ? RED : WINE);
    int drop = t < 40 ? (40 - t) : 0;
    lettering(BROKE1, BROKE1_W, 64 - BROKE1_W / 2, 14 - drop, ramp, INK, NAVY);
    if (t > 20) lettering(BROKE2, BROKE2_W, 64 - BROKE2_W / 2, 40 - (t < 60 ? 60 - t : 0), ramp, INK, NAVY);
    // ...and it rains. (PPOT's falling pixels.)
    for (int i = 0; i < 2; i++) fx::spawn(fx::RAIN, fx::rndRange(0, 128), -4, -6, 64, 40, CYAN);
    fx::update();
    fx::drawParticles();
    // The dealer, delighted.
    table::dealer(table::E_SMILE, 1, game.opt.dealer != 0, 40, 70);
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
        case Scr::Splash: splashUpdate(); break;
        case Scr::Title: titleUpdate(); break;
        case Scr::Play: playUpdate(); break;
        case Scr::Options: optionsUpdate(); break;
        case Scr::Stats: statsUpdate(); break;
        case Scr::Credits: creditsUpdate(); break;
        case Scr::Win: case Scr::Lose: endUpdate(); break;
    }
}

// Screens other than play are drawn only when something on them changed;
// palette animation (FX_A/FX_B pulses, fades) runs regardless, because every
// frame is still flushed.

static bool unchanged(uint32_t sig) {
    if (sig == staticSig && !fx::particlesAlive() && !toastT) return true;
    staticSig = sig;
    return false;
}

void render(uint32_t frame) {
    uint32_t step = cur == Scr::Splash ? (t > SPLASH_PRESENTS) + (t > SPLASH_LOGO) + (t > SPLASH_TAG)
                                       : (t < 90 ? t : 90);
    uint32_t sig = (uint32_t)cur * 2654435761u ^ step ^ ((uint32_t)menuSel << 8) ^
                   ((uint32_t)optSel << 12) ^ ((uint32_t)hasGame << 16) ^ ((uint32_t)game.purse << 17) ^
                   ((uint32_t)statHold << 24);
    for (uint8_t i = 0; i < 8; i++) sig = sig * 31u + ((uint8_t *)&game.opt)[i];
    sig |= 1;    // never 0: staticSig == 0 means "not drawn yet" (the splash sat black until its first reveal)
    bool still = cur == Scr::Title || cur == Scr::Options || cur == Scr::Stats || cur == Scr::Splash;
    if (still && unchanged(sig)) return;
    if (!still) staticSig = 0;
    switch (cur) {
        case Scr::Splash: splashRender(frame); break;
        case Scr::Title: titleRender(frame); break;
        case Scr::Play: playRender(frame); break;
        case Scr::Options: optionsRender(frame); break;
        case Scr::Stats: statsRender(frame); break;
        case Scr::Credits: creditsRender(frame); break;
        case Scr::Win: winRender(frame); break;
        case Scr::Lose: loseRender(frame); break;
    }
    if (toastT) {
        int w = gfx_textWidth(toastText) + 8;
        panel(64 - w / 2, 2, w, 11, 3, INK, GOLD);
        centred57(4, toastText, WHITE);
    }
}

}  // namespace screens
