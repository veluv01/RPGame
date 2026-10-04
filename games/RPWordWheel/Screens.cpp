#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// Screens after CHBlackjack's (which follow Press Play On Tape's
// GameStateTypes): title, setup, play, options, statistics and the end of
// an episode, with a pause menu over play.
#include <Arduino.h>
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Screens.h"
#include "Show.h"
#include "Bank.h"
#include "Presenter.h"
#include "Fx.h"
#include "Layout.h"
#include "Stage.h"
#include "WheelStrip.h"
#include "Sounds.h"
#include "Save.h"
#include "Shapes.h"

namespace screens {

enum class Scr : uint8_t { Title, Setup, Play, Options, Stats, End };

static Show game;
static bank::Deck deck;
static Scr cur = Scr::Title, pending = Scr::Title;
static uint8_t fadeOut = 0, fadeIn = 0;      // palette fade transitions
static uint16_t t = 0;                       // frames on this screen
static bool hasGame = false, paused = false, resumePlay = false;
static bool seeded = false;
static uint8_t menuSel = 0, pauseSel = 0, optSel = 0, setupSel = 0;
static Scr optBack = Scr::Title;
static uint8_t toastT = 0;
static const char *toastText = "";
static Phase lastPhase = Phase::Intro;
static uint32_t staticSig = 0;             // last drawn state of a still screen
static bool titleReady = false;            // the title's still parts are drawn
static int8_t needSection = -1;            // a puzzle to fetch, once the frame is out
static int16_t forcePuzzle = -1;           // debug: the next puzzle's index
static uint8_t forceSection;

static void toast(const char *s) { toastText = s; toastT = 60; }

// Options "SOUND|LEAD|ARPEGGIO|OFF" (all-zero is the default): the tune's
// lead line, its arpeggio, or no sound at all.
static void applySound(uint8_t opt) {
    audio::setMusic(opt ? audio::ARPEGGIO : audio::LEAD);
    audio::setOn(opt < 2);
}

// The play screen only redraws what changed; anything drawn over it from
// outside (pause menu, toast) has to force a full redraw.
static void redrawAll() { present::invalidate(); staticSig = 0; titleReady = false; }

static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

static void persist(bool keepGame) {
    gfx_wait();
    save::store(game, deck, keepGame);
}

// The podiums as last set up; never set up: you, Dot and Ace.
static void loadKinds() {
    static const uint8_t SEATS[3] = {P_HUMAN, P_DOT, P_ACE};
    for (uint8_t p = 0; p < 3; p++)
        game.kind[p] = game.opt.kind[p] ? (uint8_t)((game.opt.kind[p] - 1) % P_KINDS) : SEATS[p];
}

static void enter(Scr s) {
    cur = s; t = 0; fadeIn = 8;
    titleReady = false;
    fx::clear();
    pal::setCycling(true);
    switch (s) {
        case Scr::Title:
            paused = false; menuSel = 0;
            playSong(Song::Title, true);
            gfx_wait();                     // a card put in (or taken out) since last time?
            bank::begin();
            break;
        case Scr::Setup:
            setupSel = 4;
            loadKinds();
            break;
        case Scr::Play:
            audio::stopMusic();
            if (!resumePlay) present::reset(game);
            redrawAll();
            resumePlay = false;
            paused = false;
            lastPhase = game.phase;
            break;
        case Scr::End:
            if (game.human(game.winner)) { audio::sfx(Sfx::BigWin); audio::led(audio::LED_PARTY); }
            else audio::sfx(Sfx::Lose);
            break;
        default:
            break;
    }
}

void begin() {
    save::load(game, deck, hasGame);
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, game.opt.sound < 2);
    audio::setMusic(game.opt.sound ? audio::ARPEGGIO : audio::LEAD);
    enter(Scr::Title);
}

static void seedOnce() {
    if (seeded) return;
    uint32_t s = micros() * 2654435761u ^ rpgame.frameCount;
    game.seed(s);
    if (!deck.seed) deck.shuffle(s ^ 0x5bd1e995u);
    seeded = true;
}

// ---------------------------------------------------------------------------
// Debug hooks
// ---------------------------------------------------------------------------
void debugSeed(uint32_t s) { game.seed(s); deck.shuffle(s); seeded = true; }
void debugStop(uint8_t n) { game.forceStop(n); }
void debugPuzzle(uint8_t section, uint16_t index) { forceSection = section; forcePuzzle = (int16_t)index; }
void debugCall(char letter) { if (game.phase == Phase::PickLetter && game.stepKind() == SK_ROUND) game.actCall(letter); }
void debugSolve(bool right) {
    // Only where a solve can be in progress.
    if (game.phase == Phase::Entry || game.phase == Phase::Confirm || game.phase == Phase::TurnMenu ||
        game.phase == Phase::FinalMenu)
        game.actSolve(right);
}
void debugCash(uint8_t p, int32_t cash) { if (p < 3) game.cash[p] = cash; }
void debugKinds(uint8_t k0, uint8_t k1, uint8_t k2) {
    game.kind[0] = (uint8_t)(k0 % P_KINDS); game.kind[1] = (uint8_t)(k1 % P_KINDS); game.kind[2] = (uint8_t)(k2 % P_KINDS);
    for (uint8_t p = 0; p < 3; p++) game.opt.kind[p] = (uint8_t)(game.kind[p] + 1);
}
void debugStep(uint8_t step) {
    game.stepIdx = step;
    game.resume();
    present::reset(game);
}
void debugState(char *p) {
    p = fmtStr(p, "ST phase=");
    p = fmtInt(p, (int32_t)game.phase);
    p = fmtStr(p, " step=");
    p = fmtInt(p, game.stepIdx);
    p = fmtStr(p, " cur=");
    p = fmtInt(p, game.cur);
    p = fmtStr(p, " cash=");
    for (uint8_t i = 0; i < 3; i++) { p = fmtInt(p, game.cash[i]); *p++ = i < 2 ? ',' : ' '; }
    p = fmtStr(p, "total=");
    for (uint8_t i = 0; i < 3; i++) { p = fmtInt(p, game.total[i]); *p++ = i < 2 ? ',' : ' '; }
    p = fmtStr(p, "hidden=");
    p = fmtInt(p, game.puzzle.nHidden);
    p = fmtStr(p, " busy=");
    p = fmtInt(p, present::busy(game));
    p = fmtStr(p, " card=");
    p = fmtInt(p, bank::card());
    p = fmtStr(p, " puzzles=");
    p = fmtInt(p, bank::count(0) + bank::count(1) + bank::count(2));
    fmtStr(p, "\n");
}
void debugJump(char c) {
    // Scripted tests start from a known animation clock, so a board that has
    // been running since boot renders the same frames as a fresh simulator.
    rpgame.frameCount = 0;
    pal::resetClock();
    fx::reseed();                                        // enter() clears the particles
    switch (c) {
        case 'T': enter(Scr::Title); break;
        case 'U': enter(Scr::Setup); break;
        case 'P': seedOnce(); loadKinds(); game.newEpisode(); hasGame = true; enter(Scr::Play); break;
        case 'E': enter(Scr::End); break;
        case 'O': optBack = Scr::Title; enter(Scr::Options); break;
        case 'S': enter(Scr::Stats); break;
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

// Big lettering in PPOT's font with a gradient, outline and shadow: the top
// colour for `scale` rows, mid down to row lowFrom, low below.
static void title35(const char *text, int y, uint8_t top, uint8_t mid, uint8_t low, uint8_t shadow,
                    uint8_t lowFrom = 9, uint8_t scale = 3) {
    int h = 6 * scale;
    Mask m = maskBegin(124, h);
    maskText35(m, 0, 0, text, scale);
    uint8_t ramp[26];
    for (int i = 0; i < h + 2 && i < 26; i++) ramp[i] = i < scale ? top : (i < lowFrom ? mid : low);
    maskDraw(m, 64 - text35WidthScaled(text, scale) / 2, y, mid, INK, shadow, ramp);
}

static void centred35(int y, const char *s, uint8_t c) { text35(64 - text35Width(s) / 2, y, s, c); }
static void centred57(int y, const char *s, uint8_t c) { gfx_text(64 - gfx_textWidth(s) / 2, y, s, c); }

static void bankLine(char *buf) {
    char *p = fmtStr(buf, bank::card() ? "CARD " : "BUILT-IN ");
    p = fmtInt(p, bank::count(SEC_ROUND) + bank::count(SEC_TOSS) + bank::count(SEC_BONUS));
    fmtStr(p, " PUZZLES");
}

// ---------------------------------------------------------------------------
// Title: the name over the wheel drifting past its pointer, the menu on it.
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_PLAY, I_CONTINUE, I_NEW, I_OPTIONS, I_STATS };

static uint8_t menu(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) { items[n++] = I_CONTINUE; items[n++] = I_NEW; }
    else items[n++] = I_PLAY;
#if !CHWW_LEAN
    items[n++] = I_OPTIONS;
    items[n++] = I_STATS;
#endif
    return n;
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
#if CHWW_LEAN
            case I_PLAY: case I_NEW: loadKinds(); game.newEpisode(); hasGame = true; go(Scr::Play); break;
#else
            case I_PLAY: case I_NEW: go(Scr::Setup); break;
#endif
            case I_CONTINUE: game.resume(); go(Scr::Play); break;
            case I_OPTIONS: optBack = Scr::Title; optSel = 0; go(Scr::Options); break;
            case I_STATS: go(Scr::Stats); break;
        }
    }
}

static void titleRender(uint32_t frame) {
    uint8_t items[5], n = menu(items);
    if (!titleReady) {
        titleReady = true;
        gfx_fillRect(0, 0, 128, lay::RIM_Y, FELT_DK);
        title35("WORD", 1, FX_B, GOLD, WOOD, WINE, 13, 4);
        title35("WHEEL", 21, FX_B, GOLD, WOOD, WINE, 13, 4);
        char buf[28];
        bankLine(buf);
        gfx_fillRect(0, lay::PROMPT_Y, 128, lay::PROMPT_H, INK);
        gfx_hline(0, lay::PROMPT_Y, 128, GOLD);
        centred35(lay::PROMPT_Y + 2, buf, FELT_LT);
    }
    wheelstrip::draw(game, (int32_t)((frame * 96u) % (uint32_t)(72 * 14 * 256)), 0, frame);
    static const char *const LABEL[5] = {"PLAY", "CONTINUE", "NEW GAME", "OPTIONS", "STATS"};
    int y0 = 116 - n * 10;
    edgedRound(26, y0 - 4, 76, n * 10 + 5, 4, INK, GOLD);
    for (uint8_t i = 0; i < n; i++) {
        int y = y0 + i * 10;
        bool sel = i == menuSel;
        int w = gfx_textWidth(LABEL[items[i]]);
        if (sel) fillRound(64 - w / 2 - 5, y - 2, w + 10, 11, 3, NAVY);
        centred57(y, LABEL[items[i]], sel ? FX_B : WHITE);
    }
}

#if !CHWW_LEAN
// ---------------------------------------------------------------------------
// Setup: who stands at each podium, and the length of the game.
// ---------------------------------------------------------------------------
static const char *const KIND[P_KINDS] = {"HUMAN", "ACE", "DOT", "BUZZ"};
static const char *const KIND_SAYS[P_KINDS] = {"PASS THE HANDHELD ROUND", "ACE: SHARP, SOLVES EARLY",
                                               "DOT: STEADY, LOVES VOWELS", "BUZZ: A ROOKIE WHO GAMBLES"};

static void setupUpdate() {
    if (rpgame.repeat(UP_BUTTON)) { setupSel = (uint8_t)((setupSel + 4) % 5); audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON)) { setupSel = (uint8_t)((setupSel + 1) % 5); audio::sfx(Sfx::Cursor); }
    int d = rpgame.justPressed(RIGHT_BUTTON) ? 1 : (rpgame.justPressed(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.justPressed(A_BUTTON) && setupSel < 4) d = 1;
    if (d && setupSel < 3) {
        game.kind[setupSel] = (uint8_t)((game.kind[setupSel] + P_KINDS + d) % P_KINDS);
        audio::sfx(Sfx::Coin);
    } else if (d && setupSel == 3) {
        game.opt.game ^= 1;
        audio::sfx(Sfx::Coin);
    } else if (setupSel == 4 && rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
        for (uint8_t p = 0; p < 3; p++) game.opt.kind[p] = (uint8_t)(game.kind[p] + 1);
        game.newEpisode();
        hasGame = true;
        go(Scr::Play);
    }
    if (rpgame.justPressed(B_BUTTON)) {
        // Back to the title: the saved episode, if any, keeps its own podiums.
        audio::sfx(Sfx::Select);
        save::load(game, deck, hasGame);
        go(Scr::Title);
    }
}

static void setupRender(uint32_t frame) {
    (void)frame;
    static const uint8_t POD[3] = {RED, GOLD, BLUE};
    feltBackdrop();
    title35("CONTESTANTS", 6, FX_B, GOLD, WOOD, WINE, 9, 2);
    char buf[4];
    for (uint8_t i = 0; i < 5; i++) {
        int y = 26 + i * 15 + (i == 4 ? 4 : 0);
        bool sel = i == setupSel;
        if (sel) fillRound(8, y - 3, 112, 13, 3, NAVY);
        const char *value = "";
        if (i < 3) {
            fillRound(12, y - 1, 9, 9, 2, POD[i]);
            buf[0] = (char)('1' + i); buf[1] = 0;
            text35(15, y + 1, buf, i == 1 ? INK : WHITE);
            text35(26, y + 1, "PODIUM", sel ? WHITE : FELT_LT);
            value = KIND[game.kind[i] % P_KINDS];
        } else if (i == 3) {
            text35(12, y + 1, "GAME", sel ? WHITE : FELT_LT);
            value = game.opt.game ? "QUICK PLAY" : "FULL EPISODE";
        } else {
            centred57(y, "START THE SHOW", sel ? FX_B : WHITE);
            continue;
        }
        int w = gfx_textWidth(value);
        gfx_text(108 - w, y, value, sel ? FX_B : GOLD);
        if (sel) { text35(100 - w, y + 1, "<", SILVER); text35(112, y + 1, ">", SILVER); }
    }
    centred35(107, setupSel < 3 ? KIND_SAYS[game.kind[setupSel] % P_KINDS]
                   : setupSel == 3 ? (game.opt.game ? "TOSS-UP, A ROUND, THE BONUS" : "2 TOSS-UPS, 3 ROUNDS, BONUS")
                   : "B: BACK", GOLD);
}

#endif  // !CHWW_LEAN

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
static void playUpdate() {
    uint8_t pressed = rpgame.justPressedMask();
    uint8_t rep = 0;
    static const uint8_t RB[6] = {LEFT_BUTTON, RIGHT_BUTTON, UP_BUTTON, DOWN_BUTTON, A_BUTTON, B_BUTTON};
    for (uint8_t i = 0; i < 6; i++) if (rpgame.repeat(RB[i])) rep |= RB[i];

    if (paused) {
        if (pressed & (UP_BUTTON | DOWN_BUTTON)) { pauseSel = (uint8_t)((pauseSel + ((pressed & UP_BUTTON) ? 2 : 1)) % 3); audio::sfx(Sfx::Cursor); }
        if (pressed & (START_BUTTON | B_BUTTON)) { paused = false; audio::sfx(Sfx::Select); }
        if (pressed & A_BUTTON) {
            audio::sfx(Sfx::Select);
            if (pauseSel == 0) paused = false;
#if !CHWW_LEAN
            else if (pauseSel == 1) { optBack = Scr::Play; optSel = 0; resumePlay = true; go(Scr::Options); }
#else
            else if (pauseSel == 1) paused = false;
#endif
            else {
                // The step in play starts over, with a new puzzle, on CONTINUE.
                hasGame = game.phase != Phase::EpisodeEnd;
                persist(hasGame);
                go(Scr::Title);
            }
        }
        return;
    }
    if (pressed & START_BUTTON) { paused = true; pauseSel = 0; audio::sfx(Sfx::Select); return; }

    // A held hurries the host along and a CPU's spin.
    present::hurry(rpgame.pressed(A_BUTTON) && !(game.human(game.cur) && game.phase == Phase::Charge));

    game.update(pressed & ~START_BUTTON, rep, rpgame.buttons(), present::busy(game));
    present::onEvents(game);
    present::update(game);
    int8_t need = present::takeNeed();
    if (need >= 0) needSection = need;

    if (game.phase != lastPhase) {
        lastPhase = game.phase;
        if (game.phase == Phase::EpisodeEnd) hasGame = false;
    }
    if (game.phase == Phase::EpisodeEnd && !present::busy(game) && !fadeOut) {
        persist(false);
        go(Scr::End);
    }
}

// The rules asked for a puzzle: fetch it now that the frame has gone out
// (the card shares the LCD's bus), and save where the episode stands.
static void fetchPuzzle() {
    if (needSection < 0) return;
    uint8_t sec = (uint8_t)needSection;
    needSection = -1;
    char text[bank::TEXT_MAX], cat[bank::CAT_MAX];
    uint16_t index = deck.draw(sec);
    if (forcePuzzle >= 0 && forceSection == sec) { index = (uint16_t)forcePuzzle; forcePuzzle = -1; }
    if (!bank::fetch(sec, index, text, cat)) {
        bank::flashFetch(sec, (uint16_t)(index % bank::flashCount(sec)), text, cat);
        toast("CARD LOST");
    }
    game.supply(text, cat);
    if (game.humans()) save::store(game, deck, true);
}

static void playRender(uint32_t frame) {
    fetchPuzzle();
    if (paused || toastT) redrawAll();
    bool drew = present::render(game, frame);
    if (drew) present::overlay(game, frame);
    if (paused) {
        dither(0, 0, 128, 128, INK, 0);
        edgedRound(24, 34, 80, 56, 4, NAVY, GOLD);
        centred57(39, "PAUSED", GOLD);
        static const char *const P[3] = {"RESUME", "OPTIONS", "SAVE & QUIT"};
        for (int i = 0; i < 3; i++) {
            int y = 53 + i * 11;
            if (i == pauseSel) fillRound(30, y - 2, 68, 11, 3, INK);
            centred57(y, P[i], i == pauseSel ? FX_B : WHITE);
        }
    }
}

#if !CHWW_LEAN
// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
enum Opt : uint8_t { O_GAME, O_PACE, O_CLOCK, O_SOUND, O_HOST, O_BACK, OPT_COUNT };

// Options' first five bytes, in this order; each entry is "LABEL|value|value...".
static const char *const OPT_TEXT[OPT_COUNT] = {
    "GAME|FULL EPISODE|QUICK PLAY", "PACE|FUN|QUICK", "CLOCK|TV|RELAXED", "SOUND|LEAD|ARPEGGIO|OFF",
    "HOST|CLASSIC|NIGHT", "BACK",
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
    // The game's length belongs to the episode: not changed while one is on.
    if (d && optSel == O_GAME && (optBack == Scr::Play || hasGame)) { audio::sfx(Sfx::Deny); d = 0; }
    if (d && optSel != O_BACK) {
        char tmp[16];
        uint8_t n = (uint8_t)(optField(OPT_TEXT[optSel], 0, tmp) - 1);
        uint8_t *f = (uint8_t *)&game.opt + optSel;
        *f = (uint8_t)((*f + n + d) % n);
        if (optSel == O_SOUND) applySound(*f);
        audio::sfx(Sfx::Coin);
    }
    if ((rpgame.justPressed(A_BUTTON) && optSel == O_BACK) || rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        if (optBack != Scr::Play) persist(hasGame);      // mid-episode they go with the next save
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
        char buf[16];
        if (sel) fillRound(6, y - 2, 116, 10, 3, NAVY);
        optField(OPT_TEXT[i], 0, buf);
        text35(10, y, buf, sel ? WHITE : FELT_LT);
        if (i == O_BACK) continue;
        optField(OPT_TEXT[i], (uint8_t)(1 + ((uint8_t *)&game.opt)[i]), buf);
        int w = text35Width(buf);
        text35(118 - w, y, buf, sel ? FX_B : GOLD);
        if (sel) { text35(112 - w, y, "<", SILVER); text35(120, y, ">", SILVER); }
    }
    const char *help = optSel == O_GAME ? (optBack == Scr::Play || hasGame ? "SET BEFORE THE SHOW STARTS" : "QUICK: ONE ROUND + BONUS")
                     : optSel == O_PACE ? (game.opt.pace ? "SHORTER SPINS, LESS CHAT" : "THE FULL SHOW")
                     : optSel == O_CLOCK ? (game.opt.clock ? "TWICE THE TIME TO SOLVE" : "10 SECONDS IN THE BONUS")
                     : "B: BACK";
    centred35(103, help, GOLD);
    centred35(111, "HOST ART: VAMPIRICS", SILVER);        // Press Play On Tape's (see NOTICE)
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
    if (rpgame.justPressed(B_BUTTON | A_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void statsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("STATISTICS", 6, WHITE, CYAN, BLUE, NAVY);
    const Stats &s = game.stats;
    static const char *const NAME[9] = {"CAREER WINNINGS", "BEST EPISODE", "BEST ROUND", "EPISODES", "EPISODES WON",
                                        "BONUS ROUNDS WON", "PUZZLES SOLVED", "TOSS-UPS WON", "BANKRUPTS"};
    const uint32_t val[9] = {s.career, s.bestEpisode, s.bestRound, s.episodes, s.wins, s.bonusWins, s.solved,
                             s.tossups, s.bankrupts};
    for (int i = 0; i < 9; i++) {
        char buf[16];
        int y = 25 + i * 8;
        text35(10, y, NAME[i], FELT_LT);
        if (i < 3) fmtCash(buf, (int32_t)val[i]); else fmtInt(buf, (int32_t)val[i]);
        text35(118 - text35Width(buf), y, buf, i < 3 ? GOLD : WHITE);
    }
    char buf[28];
    bankLine(buf);
    centred35(99, buf, SILVER);
    if (statHold >= STAT_RESET_FRAMES) centred35(108, "STATS RESET", GOLD);
    else if (statHold) {                          // the reset gesture filling up
        gfx_rect(24, 108, 80, 5, SILVER);
        gfx_fillRect(25, 109, 78 * statHold / STAT_RESET_FRAMES, 3, RED);
    } else centred35(108, "HOLD SELECT TO RESET", FELT_LT);
    centred35(116, save::available() ? "SAVED IN FLASH" : "SAVING UNAVAILABLE", SILVER);
}

#endif  // !CHWW_LEAN

// ---------------------------------------------------------------------------
// The end of an episode: the champion in a rain of coins and confetti, or
// the host's consolation.
// ---------------------------------------------------------------------------
static void endUpdate() {
    if (t > 60 && rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
        audio::stopMusic();
        go(Scr::Title);
    }
}

static void endRender(uint32_t frame) {
    char buf[24], nm[5];
    uint8_t w = game.winner < 3 ? game.winner : 0;
    bool won = game.human(w);
    if (won) {
        gfx_clear(NAVY);
        uint8_t c0 = fx::RAIN[(frame / 4) % 5], c1 = fx::RAIN[(frame / 4 + 2) % 5];
        title35(game.humans() > 1 ? game.name(w, nm) : "YOU ARE", 10, c0, c1, c1, WINE);
        title35(game.humans() > 1 ? "WINS!" : "CHAMPION", 32, c0, c1, c1, WINE);
        if ((frame % 6) == 0) fx::fountain(fx::COIN, fx::rndRange(20, 108), 120, 2);
        if ((frame % 30) == 0) fx::fountain(fx::CONFETTI, fx::rndRange(20, 108), 100, 10);
    } else {
        gfx_clear(INK);
        for (int x = 3; x < 128; x += 8) gfx_vline(x, 0, 128, NAVY);
        *fmtStr(fmtStr(buf, game.name(w, nm)), " WINS") = 0;
        title35(buf, 12, WHITE, RED, WINE, NAVY);
        stage::dealer(stage::E_SMILE, 1, game.opt.host != 0, 40, 74);
        gfx_fillRect(0, 116, 128, 12, INK);
    }
    fmtCash(buf, game.total[w]);
    title35(buf, won ? 60 : 36, FX_B, GOLD, WOOD, WINE, 13);
    if (!won && game.humans()) {
        // The best human's take-home.
        int32_t best = 0;
        for (uint8_t p = 0; p < 3; p++) if (game.human(p) && game.total[p] > best) best = game.total[p];
        *fmtCash(fmtStr(buf, "YOU TAKE HOME "), best) = 0;
        centred35(62, buf, WHITE);
    }
    fx::update();
    fx::drawParticles();
    if (t > 60 && (frame & 16)) centred35(won ? 108 : 119, "PRESS A", WHITE);
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
        case Scr::End: endUpdate(); break;
#if !CHWW_LEAN
        case Scr::Setup: setupUpdate(); break;
        case Scr::Options: optionsUpdate(); break;
        case Scr::Stats: statsUpdate(); break;
#else
        default: go(Scr::Title); break;
#endif
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
                   ((uint32_t)hasGame << 16) ^ ((uint32_t)setupSel << 17);
#if !CHWW_LEAN
    sig ^= (uint32_t)statHold << 24;
#endif
    for (uint8_t i = 0; i < 8; i++) sig = sig * 31u + ((uint8_t *)&game.opt)[i];
    for (uint8_t i = 0; i < 3; i++) sig = sig * 31u + game.kind[i];
    sig |= 1;    // never 0: staticSig == 0 means "not drawn yet"
    bool still = cur == Scr::Options || cur == Scr::Stats || cur == Scr::Setup;
    if (still && unchanged(sig)) return;
    if (!still) staticSig = 0;
    switch (cur) {
        case Scr::Title: titleRender(frame); break;
        case Scr::Play: playRender(frame); break;
        case Scr::End: endRender(frame); break;
#if !CHWW_LEAN
        case Scr::Setup: setupRender(frame); break;
        case Scr::Options: optionsRender(frame); break;
        case Scr::Stats: statsRender(frame); break;
#else
        default: break;
#endif
    }
    if (toastT) {
        int w = gfx_textWidth(toastText) + 8;
        edgedRound(64 - w / 2, 2, w, 11, 3, INK, GOLD);
        centred57(4, toastText, WHITE);
    }
}

}  // namespace screens
