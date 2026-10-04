#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// Screens after CHBlackjack's (which follow Press Play On Tape's
// GameStateTypes): title, play, options, statistics, the back room, and
// the won/broke screens, with a pause menu over play.
#include <Arduino.h>
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Screens.h"
#include "Roulette.h"
#include "Nav.h"
#include "Wheel.h"
#include "WheelArt.h"
#include "Presenter.h"
#include "Fx.h"
#include "ChipArt.h"
#include "Layout.h"
#include "Table.h"
#include "Sounds.h"
#include "Save.h"
#include "src/assets/Assets.h"

namespace screens {

enum class Scr : uint8_t { Title, Play, Options, Stats, Credits, Win, Lose };

static Roulette game;
static Scr cur = Scr::Title, pending = Scr::Title;
static uint8_t fadeOut = 0, fadeIn = 0;      // palette fade transitions
static uint16_t t = 0;                       // frames on this screen
static bool hasGame = false, paused = false, resumePlay = false, demo = false;
static bool seeded = false;
static uint8_t menuSel = 0, pauseSel = 0, optSel = 0;
static Scr optBack = Scr::Title;
static uint8_t toastT = 0;
static const char *toastText = "";
static Phase lastPhase = Phase::Welcome;
static uint32_t staticSig = 0;             // last drawn state of a still screen
static bool creditsReady = false;          // the back room's static layer is drawn
static bool titleReady = false;            // the title's still parts are drawn

static void toast(const char *s) { toastText = s; toastT = 60; }

// Options "SOUND|LEAD|ARPEGGIO|OFF" (all-zero is the default) -> the sound
// engine: on or off, and the music's rendering.
static void applySound(uint8_t opt) {
    audio::setMusic(opt == 0 ? audio::LEAD : audio::ARPEGGIO);
    audio::setOn(opt < 2);
}

// The play screen only redraws what changed; anything drawn over it from
// outside (pause menu, toast) has to force a full redraw.
static void redrawAll() { present::invalidate(); staticSig = 0; creditsReady = false; titleReady = false; }

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
    creditsReady = titleReady = false;
    fx::clear();
    pal::setDesaturate(0);
    pal::setCycling(true);
    switch (s) {
        case Scr::Title:
            paused = false; menuSel = 0; demo = false;
            playSong(Song::Title, true);
            break;
        case Scr::Play:
            audio::stopMusic();
            if (!resumePlay) present::reset(game);
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
    save::load(game, hasGame);
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, false);
    applySound(game.opt.sound);
    enter(Scr::Title);
}

static void seedOnce() {
    if (seeded) return;
    game.seed(micros() * 2654435761u ^ rpgame.frameCount);
    seeded = true;
}

// ---------------------------------------------------------------------------
// Debug hooks
// ---------------------------------------------------------------------------
void debugSeed(uint32_t s) { game.seed(s); seeded = true; }
void debugForce(uint8_t n) { game.force(n); }
void debugGlove(uint8_t spot) {
    nav::Glove g = nav::at(spot, game.us);
    game.cursor = g.spot; game.gx = g.x; game.gy = g.y;
}
bool debugPlace(uint8_t spot, uint8_t amount) {
    if (game.phase != Phase::Betting) return false;
    game.place(spot, amount);
    return true;
}
void debugPurse(int32_t p) { game.purse = p; }
void debugJump(char c) {
    // Scripted tests start from a known animation clock, so a board that has
    // been running since boot renders the same frames as a fresh simulator.
    rpgame.frameCount = 0;
    pal::resetClock();
    fx::reseed();                                        // enter() clears the particles
    switch (c) {
        case 'T': enter(Scr::Title); break;
        case 'P': game.newGame(); enter(Scr::Play); break;
        case 'W': enter(Scr::Win); break;
        case 'L': enter(Scr::Lose); break;
        case 'O': optBack = Scr::Title; enter(Scr::Options); break;
        case 'S': enter(Scr::Stats); break;
        case 'C': enter(Scr::Credits); break;
    }
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

static void lettering(const uint8_t *bits, int w, int h, int x, int y, const uint8_t *ramp, int outline, int shadow) {
    Mask m = maskBegin(w, h);
    maskBlit1(m, bits, (uint8_t)w, (uint8_t)h);
    maskDraw(m, x, y, GOLD, outline, shadow, ramp);
}

// "Roulette" in the title colours: the top rows in FX_B, so the palette
// makes it shimmer with no redraw.
static void logo(int y, bool printed = false) {
    Mask m = maskBegin(LOGO_W, LOGO_H);
    maskBlit1(m, LOGO, LOGO_W, LOGO_H);
    if (printed) { maskDraw(m, 64 - LOGO_W / 2, y, FELT_LT); return; }
    uint8_t ramp[LOGO_H + 2];
    for (int i = 0; i < LOGO_H + 2; i++) ramp[i] = i < 3 ? FX_B : (i < 12 ? GOLD : WOOD);
    maskDraw(m, 64 - LOGO_W / 2, y, GOLD, INK, WINE, ramp);
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

#if CHRL_DEMO
// Attract mode: after ten idle seconds on the title, once its tune has
// played to its end, the croupier plays a few spins on his own.
static uint8_t demoSpins;

static void startDemo() {
    demo = true;
    demoSpins = 0;
    game.seed(fx::rnd());
    game.newGame();
    enter(Scr::Play);
    demo = true;
}
#endif

static void titleUpdate() {
#if CHRL_DEMO
    static uint16_t idle = 0;
    idle = rpgame.buttons() ? 0 : (uint16_t)(idle + 1);
    audio::loopMusic(idle <= 600);
    if (idle > 600 && !audio::musicPlaying()) { idle = 0; startDemo(); return; }
#endif
    uint8_t items[5], n = menu(items);
    if (menuSel >= n) menuSel = 0;
    if (rpgame.repeat(UP_BUTTON) && menuSel > 0) { menuSel--; titleReady = false; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && menuSel + 1 < n) { menuSel++; titleReady = false; audio::sfx(Sfx::Cursor); }
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
}

// The felt, the logo, the menu and the chips are drawn once; each frame
// redraws only the band the wheel turns in.
static const int TITLE_WHEEL_Y0 = 24, TITLE_WHEEL_CY = 58;

static void titleWheel(uint32_t frame, int y1) {
    // The band's background: the felt, its dark edges and gold line, the spotlight.
    int h = y1 - TITLE_WHEEL_Y0;
    gfx_fillRect(0, TITLE_WHEEL_Y0, 128, h, FELT);
    dither(0, TITLE_WHEEL_Y0, 6, h, FELT_DK, 0);
    dither(122, TITLE_WHEEL_Y0, 6, h, FELT_DK, 1);
    gfx_vline(2, TITLE_WHEEL_Y0, h, GOLD);
    gfx_vline(125, TITLE_WHEEL_Y0, h, GOLD);
    dither(24, TITLE_WHEEL_Y0, 80, h, FELT_LT, 0);
    // The wheel turning slowly under the light, the ball lapping it.
    const uint32_t T = 37u << 16;
    uint32_t rho = (frame * 164u * 37u) % T;                     // 0.15 rev/s
    wheelart::BallView b = {(uint32_t)(T - (frame * 1100u * 37u) % T), 52 << 8, 0, 0, false};   // 1 rev/s
    wheelart::draw(64, TITLE_WHEEL_CY, rho, 37, 0xFF, FX_A, 0, 128, TITLE_WHEEL_Y0, y1, TITLE_WHEEL_Y0, y1, false, &b, 0);
}

static void titleRender(uint32_t frame) {
    uint8_t items[5], n = menu(items);
    int y0 = 128 - n * 9 - 4;                                    // the menu, pitch 9
    if (!titleReady) {
        titleReady = true;
        feltBackdrop();
        logo(6);
        art::chipStack(15, 116, 25 * 3 + 10 * 2, 6);
        art::chipStack(113, 116, 100 + 25 * 2 + 5, 6);
        static const char *const LABEL[5] = {"PLAY", "CONTINUE", "NEW GAME", "OPTIONS", "STATS"};
        dither(6, y0 - 3, 116, 125 - y0, INK, 1);
        for (uint8_t i = 0; i < n; i++) {
            char buf[20];
            if (items[i] == I_CONTINUE)
            *fmtMoney(fmtStr(buf, "CONTINUE "), game.purse + (game.phase == Phase::Quit ? 0 : game.onTable())) = 0;
            else strcpy(buf, LABEL[items[i]]);
            int y = y0 + i * 9;
            bool sel = i == menuSel;
            int w = gfx_textWidth(buf);
            if (sel) panel(64 - w / 2 - 6, y - 2, w + 12, 11, 3, NAVY, FX_B);   // FX_B pulses on its own
            centred57(y, buf, sel ? GOLD : WHITE);
        }
    }
    titleWheel(frame, y0 - 3);
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
#if CHRL_DEMO
// The demo's player: a few showy bets, walked to with D-pad taps one at a
// time - at a pace a watcher can follow - then SPIN.
static uint8_t demoInput() {
    static uint8_t cool = 0, want[5], nWant = 0, presses = 0, chips = 0;
    static Phase seen = Phase::Quit;
    if (game.phase != seen) {
        seen = game.phase;
        if (seen == Phase::Betting) {
            if (game.purse < 100) game.purse = 500;      // the demo never goes broke
            nWant = 0;
            if (!game.onTable()) {
                want[nWant++] = spots::straightId((uint8_t)(fx::rnd() % 37));
                want[nWant++] = (uint8_t)(spots::BET_RED + (fx::rnd() & 1));
                want[nWant++] = (uint8_t)(spots::DOZEN + fx::rnd() % 3);
                want[nWant++] = (uint8_t)(3 * 24 + 2 + 2 * (fx::rnd() % 11));   // a split in the middle row
            }
            want[nWant++] = spots::SPIN;
            presses = chips = 0;
        }
        if (seen == Phase::Result && ++demoSpins >= 3) { memset(game.bet, 0, sizeof game.bet); save::load(game, hasGame); go(Scr::Title); }
    }
    if (cool) { cool--; return 0; }
    cool = 12;
    if (game.phase != Phase::Betting || !nWant) return 0;
    uint8_t target = want[0];
    nav::Glove g = nav::at(target, game.us);
    if (game.cursor == target || ++presses > 40) {
        if (game.cursor == target && (target == spots::SPIN || ++chips < 2)) return A_BUTTON;
        for (uint8_t i = 1; i < nWant; i++) want[i - 1] = want[i];
        nWant--; presses = chips = 0;
        return 0;
    }
    int dx = g.x - game.gx, dy = g.y - game.gy;
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)) return dx > 0 ? RIGHT_BUTTON : LEFT_BUTTON;
    return dy > 0 ? DOWN_BUTTON : UP_BUTTON;
}
#endif

static void playUpdate() {
    uint8_t pressed = rpgame.justPressedMask();
    uint8_t rep = 0;
    static const uint8_t RB[6] = {LEFT_BUTTON, RIGHT_BUTTON, UP_BUTTON, DOWN_BUTTON, A_BUTTON, B_BUTTON};
    for (uint8_t i = 0; i < 6; i++) if (rpgame.repeat(RB[i])) rep |= RB[i];

#if CHRL_DEMO
    if (demo) {
        if (pressed) { memset(game.bet, 0, sizeof game.bet); save::load(game, hasGame); go(Scr::Title); return; }
        pressed = rep = demoInput();
    }
#endif
    // Fast-forward: A held during the spin - pressed again after SPIN.
    static bool ffArmed = false;
    if (game.phase != Phase::Spin) ffArmed = false;
    else if (pressed & A_BUTTON) ffArmed = true;
    present::fastForward(ffArmed && rpgame.pressed(A_BUTTON));

    if (paused) {
        if (pressed & (UP_BUTTON | DOWN_BUTTON)) { pauseSel = (uint8_t)((pauseSel + ((pressed & UP_BUTTON) ? 2 : 1)) % 3); audio::sfx(Sfx::Cursor); }
        if (pressed & (START_BUTTON | B_BUTTON)) { paused = false; audio::sfx(Sfx::Select); }
        if (pressed & A_BUTTON) {
            audio::sfx(Sfx::Select);
            if (pauseSel == 0) paused = false;
            else if (pauseSel == 1) { optBack = Scr::Play; optSel = 0; resumePlay = true; go(Scr::Options); }
            else {
                bool spun = game.phase >= Phase::NoMoreBets && game.phase <= Phase::EndOfSpin;
                game.quitNow();
                int32_t bank = game.purse;           // quitNow(): everything the player has
                if (spun && (bank >= game.goal() || bank < 1)) {   // the spin ended the game
                    if (bank > 0) game.stats.gamesWon++; else game.stats.gamesBroke++;
                    memset(game.bet, 0, sizeof game.bet);          // the Win screen adds onTable()
                    hasGame = false;
                    persist(false);
                    go(bank > 0 ? Scr::Win : Scr::Lose);
                } else {
                    hasGame = bank > 0;
                    persist(hasGame);
                    go(Scr::Title);
                }
            }
        }
        return;
    }
    if (pressed & START_BUTTON) { paused = true; pauseSel = 0; audio::sfx(Sfx::Select); return; }

    game.update(pressed & ~START_BUTTON, rep, present::busy());
    present::onEvents(game);
    present::update(game);

    if (game.phase != lastPhase) {
        if (game.phase == Phase::Betting && lastPhase == Phase::EndOfSpin && game.stats.spins % 5 == 0) persist(true);
        if (game.phase == Phase::GameWon) { hasGame = false; persist(false); go(Scr::Win); }
        if (game.phase == Phase::GameLost) { hasGame = false; persist(false); go(Scr::Lose); }
        if (game.phase == Phase::Quit) { hasGame = true; persist(true); go(Scr::Title); }
        lastPhase = game.phase;
    }
}

static void playRender(uint32_t frame) {
    if (paused || toastT) redrawAll();
#if CHRL_DEMO
    static bool blinkWas = false;                          // the DEMO label: a full redraw as it blinks
    bool blink = demo && (frame & 32);
    if (blink != blinkWas) { blinkWas = blink; present::invalidate(); }
#endif
    bool drew = present::render(game, frame);
    if (drew) present::overlay(game, frame);
#if CHRL_DEMO
    if (demo && (frame & 32)) {
        fillRound(44, 2, 40, 11, 3, INK);
        centred57(4, "DEMO", FX_A);
    }
#endif
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
enum Opt : uint8_t { O_WHEEL, O_GOAL, O_PACE, O_SOUND, O_DEALER, O_BACK, OPT_COUNT };

// Options' first five bytes, in this order; each entry is "LABEL|value|value...".
static const char *const OPT_TEXT[OPT_COUNT] = {
    "WHEEL|EUROPEAN|AMERICAN", "GOAL|$1000|$5000|ENDLESS", "PACE|FUN|QUICK", "SOUND|LEAD|ARPEGGIO|OFF",
    "DEALER|CLASSIC|NIGHT", "BACK",
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
        if (optSel == O_SOUND) applySound(*f);
        audio::sfx(Sfx::Chip);
    }
    if ((rpgame.justPressed(A_BUTTON) && optSel == O_BACK) || rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        // Not mid-spin: a save there could be power-cycled to undo the spin.
        // The options go with the next save.
        if (optBack != Scr::Play || game.phase == Phase::Betting || game.phase == Phase::Welcome) persist(hasGame);
        go(optBack);
    }
}

static void optionsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("OPTIONS", 6, FX_B, GOLD, WOOD, WINE, 13);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 30 + i * 12;
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
    const char *help = optSel == O_WHEEL ? (game.opt.wheel ? "DOUBLE ZERO: 38 POCKETS" : "SINGLE ZERO: 37 POCKETS")
                     : optSel == O_PACE ? (game.opt.pace ? "SHORTER SPINS, NO DRAMA" : "THE FULL SHOW")
                     : "B: BACK";
    centred35(103, help, GOLD);
    centred35(111, "CROUPIER ART: VAMPIRICS", SILVER);    // Press Play On Tape's (see NOTICE)
    centred35(117, "FONT: PRESS PLAY ON TAPE", SILVER);
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
            audio::sfx(Sfx::Lose);
        }
        return;
    }
    statHold = 0;
#if CHRL_CREDITS && !CHRL_LEAN
    if (rpgame.justPressed(A_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Credits); return; }
#endif
    if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

// The hottest and coldest numbers so far (the first of any tie).
static void hotCold(uint8_t &hot, uint8_t &cold) {
    hot = cold = 0;
    uint8_t end = (game.opt.wheel || game.stats.hits[37]) ? 38 : 37;   // 00 only once it's been on a wheel
    for (uint8_t n = 1; n < end; n++) {
        if (game.stats.hits[n] > game.stats.hits[hot]) hot = n;
        if (game.stats.hits[n] < game.stats.hits[cold]) cold = n;
    }
}

static void statsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("STATISTICS", 6, WHITE, CYAN, BLUE, NAVY);
    const Stats &s = game.stats;
    static const char *const NAME[8] = {"SPINS", "SPINS WON", "WAGERED", "BIGGEST WIN", "BEST PURSE",
                                        "STRAIGHT UPS HIT", "BANKS BROKEN", "TIMES BROKE"};
    const int32_t val[8] = {(int32_t)s.spins, (int32_t)s.spinsWon, (int32_t)s.wagered, s.biggestWin,
                            s.bestPurse, s.straightHits, s.gamesWon, s.gamesBroke};
    for (int i = 0; i < 8; i++) {
        char buf[16];
        int y = 25 + i * 8;
        text35(10, y, NAME[i], FELT_LT);
        if (i >= 2 && i <= 4) *fmtMoney(buf, val[i]) = 0; else *fmtInt(buf, val[i]) = 0;
        text35(118 - text35Width(buf), y, buf, i >= 2 && i <= 4 ? GOLD : WHITE);
    }
    if (s.spins) {
        uint8_t hot, cold;
        hotCold(hot, cold);
        char buf[24], *p = fmtStr(buf, "HOT ");
        p = wheel::name(p, hot);
        p = fmtStr(p, "   COLD ");
        p = wheel::name(p, cold);
        *p = 0;
        centred35(91, buf, SILVER);
    }
#if CHRL_CREDITS && !CHRL_LEAN
    centred35(100, "PRESS A FOR CREDITS", SILVER);
#endif
    if (statHold >= STAT_RESET_FRAMES) centred35(108, "STATS RESET", GOLD);
    else if (statHold) {                          // the reset gesture filling up
        gfx_rect(24, 108, 80, 5, SILVER);
        gfx_fillRect(25, 109, 78 * statHold / STAT_RESET_FRAMES, 3, RED);
    } else centred35(108, "HOLD SELECT TO RESET", FELT_LT);
    centred35(116, save::available() ? "SAVED IN FLASH" : "SAVING UNAVAILABLE", SILVER);
}

// ---------------------------------------------------------------------------
// Credits: CHBlackjack's back room. The croupier, under his lamp, tells you
// who made the game. A neon sign flickers; a cigarette smoulders. The felt is
// drawn once; each frame redraws only the wall band and the rail.
// ---------------------------------------------------------------------------
static void creditsUpdate() {
    if (rpgame.justPressed(A_BUTTON | B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Stats); }
}

#if CHRL_CREDITS && !CHRL_LEAN
static const char *const CREDIT_LINES[] = {
    "Thanks for\nplaying!", "I'm from\nBlackjack's\ntables", "Art by\nvampirics", "Font by\nPress Play\nOn Tape",
    "In colour\non RPGame!", "Another\nspin?",
};
static const uint16_t CREDIT_FRAMES = 180;       // per line: typed at a letter every 2 frames, then held
#endif

static void creditsRender(uint32_t frame) {
#if !CHRL_CREDITS || CHRL_LEAN
    (void)frame;                                         // device debug builds: no credits page
#else
    if (!creditsReady) {                                  // the felt, once
        creditsReady = true;
        const int top = lay::RAIL_Y + lay::RAIL_H;
        gfx_fillRect(0, top, 128, 128 - top, FELT);
        dither(0, top, 6, 128, FELT_DK, 0);              // the lamplight falls off
        dither(122, top, 6, 128, FELT_DK, 1);
        dither(0, 122, 128, 6, FELT_DK, 1);
        logo(54, true);                                  // printed on the felt
        art::chipStack(16, 84, 25 * 3 + 10 * 2, 6);
        art::chipStack(112, 84, 100 + 25 * 2 + 5, 6);
        text35(24, 92, "ART\nFONT", FELT_LT);
        text35(56, 92, "VAMPIRICS\nPPOT", GOLD);
        centred35(110, "CHGAME CASINO 2026", FELT_LT);
    }
    table::wall();
    const int n = (int)(sizeof CREDIT_LINES / sizeof CREDIT_LINES[0]);
    int line = (t / CREDIT_FRAMES) % n, typed = (t % CREDIT_FRAMES) / 2;
    const char *text = CREDIT_LINES[line];
    uint8_t expr = typed < (int)strlen(text) ? (((t >> 2) & 1) ? table::E_TALK : table::E_NORMAL)
                 : (t % 150) < 6              ? table::E_BLINK
                 : (line == 0 || line == n - 1) ? table::E_SMILE : table::E_NORMAL;
    table::dealer(expr, 1, game.opt.dealer != 0);
    table::speechBubble(text, typed);
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
    table::rail();
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

// Bresenham from the sunburst's centre, stopped where it leaves the screen.
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
    for (int i = 0; i < 16; i++) {                       // the rotating sunburst
        int a = i * 16 + (int)(frame & 255);
        int x1 = 64 + ((fx::isin(a + 64) * 120) >> 8), y1 = 60 + ((fx::isin(a) * 120) >> 8);
        ray(x1, y1, (i & 1) ? FX_A : WINE);
    }
    uint8_t ramp[16];
    for (int i = 0; i < 16; i++) ramp[i] = fx::RAIN[((i / 3) + frame / 4) % 5];
#if CHRL_LEAN
    title35("YOU WIN", 18, ramp[0], ramp[5], ramp[10], WINE);   // device debug builds: no PPOT lettering
#else
    lettering(YOUWON1, YOUWON1_W, 16, 64 - YOUWON1_W / 2, 12, ramp, INK, WINE);
    lettering(YOUWON2, YOUWON2_W, 16, 64 - YOUWON2_W / 2, 36, ramp, INK, WINE);
#endif
    char buf[16];
    *fmtMoney(buf, game.purse + game.onTable()) = 0;
    title35(buf, 64, FX_B, GOLD, WOOD, WINE, 13);
    if ((frame % 6) == 0) fx::fountain(fx::COIN, fx::rndRange(20, 108), 120, 2);
    if ((frame % 24) == 0) fx::burst(fx::STAR, fx::rndRange(16, 112), fx::rndRange(10, 50), 16, 60, FX_A);
    if ((frame % 30) == 0) fx::fountain(fx::CONFETTI, fx::rndRange(20, 108), 100, 10);
    fx::update();
    fx::drawParticles(1);
    if (t > 60 && (frame & 16)) centred35(108, "PRESS A", WHITE);
}

static void loseRender(uint32_t frame) {
    gfx_clear(INK);
    pal::setDesaturate((uint8_t)(t / 12 > 12 ? 12 : t / 12));   // colour drains out of the world...
    for (int x = 3; x < 128; x += 8) gfx_vline(x, 0, 128, NAVY);
    uint8_t ramp[16];
    for (int i = 0; i < 16; i++) ramp[i] = i < 4 ? WHITE : (i < 10 ? RED : WINE);
    int drop = t < 40 ? (40 - t) : 0;
#if CHRL_LEAN
    title35("BROKE", 20 - drop, WHITE, RED, WINE, NAVY);
#else
    lettering(BROKE1, BROKE1_W, 16, 64 - BROKE1_W / 2, 14 - drop, ramp, INK, NAVY);
    if (t > 20) lettering(BROKE2, BROKE2_W, 16, 64 - BROKE2_W / 2, 40 - (t < 60 ? 60 - t : 0), ramp, INK, NAVY);
#endif
    for (int i = 0; i < 2; i++) fx::spawn(fx::RAIN_DROP, fx::rndRange(0, 128), -4, -6, 64, 40, CYAN);   // ...and it rains
    fx::update();
    fx::drawParticles(1);
    table::dealer(table::E_SMILE, 1, game.opt.dealer != 0, 40, 70);   // the croupier, delighted
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
        pal::setFade((uint8_t)((fadeOut - 1) * 2));
        if (--fadeOut == 0) enter(pending);
        return;
    }
    if (fadeIn) { fadeIn--; pal::setFade((uint8_t)(16 - fadeIn * 2)); }
    switch (cur) {
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
    if (sig == staticSig && !fx::particles() && !toastT) return true;
    staticSig = sig;
    return false;
}

void render(uint32_t frame) {
    uint32_t sig = (uint32_t)cur * 2654435761u ^ ((uint32_t)menuSel << 8) ^ ((uint32_t)optSel << 12) ^
                   ((uint32_t)hasGame << 16) ^ ((uint32_t)game.purse << 17) ^ ((uint32_t)statHold << 24) ^
                   ((frame >> 4) & 1);
    for (uint8_t i = 0; i < 8; i++) sig = sig * 31u + ((uint8_t *)&game.opt)[i];
    sig |= 1;    // never 0: staticSig == 0 means "not drawn yet"
    bool still = cur == Scr::Options || cur == Scr::Stats;      // the title's wheel turns
    if (still && unchanged(sig)) return;
    if (!still) staticSig = 0;
    switch (cur) {
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
