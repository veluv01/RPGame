// The screens (Screens.h), after CHBlackjack's (which follow Press Play On
// Tape's GameStateTypes): title, the tables room, play, options, statistics
// and the won/broke screens, with a pause menu and the rules card over play.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
#include <Arduino.h>
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Screens.h"
#include "Match.h"
#include "Text.h"
#include "Fx.h"
#include "ChipArt.h"
#include "Stage.h"
#include "Table.h"
#include "Sounds.h"
#include "Save.h"
#include "src/assets/Assets.h"

namespace screens {

enum class Scr : uint8_t { Title, Tables, Play, Options, Stats, Win, Lose };
enum : uint8_t { OV_NONE, OV_PAUSE, OV_RULES };

static Casino casino;
static Match match;
static Scr cur = Scr::Title, pending = Scr::Title;
static uint8_t fadeOut = 0, fadeIn = 0;      // palette fade transitions
static uint16_t t = 0;                       // frames on this screen
static bool hasGame = false, resumePlay = false, seeded = false, settled = false;
static bool two = false;                     // two players: no money, a score
static uint8_t overlay = OV_NONE;
static uint8_t menuSel = 0, pauseSel = 0, optSel = 0, unsaved = 0;
static Scr optBack = Scr::Title;
static uint32_t staticSig = 0;             // last drawn state of a still screen
static uint16_t typedAt = 0;               // tables room: t when the rules began to type out

static void redrawAll() { stage::invalidate(); staticSig = 0; }

static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

static void persist(bool keepGame) {
    gfx_wait();
    save::store(casino, keepGame);
    unsaved = 0;
}

// Two players can't share a table that keeps secrets or a clock.
static bool offered(uint8_t mode) { return !two || !(MODES[mode].flags & (F_BLITZ | F_DARK | F_AUCTION)); }

static void enter(Scr s) {
    cur = s; t = 0; fadeIn = 8;
    staticSig = 0;
    pal::setDesaturate(0);
    pal::setCycling(true);
    if (s != Scr::Play || !resumePlay) fx::clear();
    switch (s) {
        case Scr::Title:
            menuSel = 0;
            audio::sfx(Sfx::Title);
            break;
        case Scr::Tables:
            typedAt = 0;
            while (casino.ante && ANTES[casino.ante] > casino.purse) casino.ante--;
            while (!offered(casino.mode)) casino.mode = (uint8_t)((casino.mode + 1) % MODE_COUNT);
            break;
        case Scr::Play:
            redrawAll();
            resumePlay = false;
            overlay = OV_NONE;
            break;
        case Scr::Win:
            audio::sfx(Sfx::BigWin);
            audio::led(audio::LED_PARTY);
            break;
        case Scr::Lose:
            audio::sfx(Sfx::Broke);
            break;
        default:
            break;
    }
}

void begin() {
    casino.purse = START_PURSE;
    casino.ante = 1;
    save::load(casino, hasGame);
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, !casino.opt.sound);
    enter(Scr::Title);
}

static void seedOnce() {
    if (seeded) return;
    match.seed(micros() * 2654435761u ^ rpgame.frameCount);
    seeded = true;
}

static int32_t goal() { return GOALS[casino.opt.goal > 2 ? 2 : casino.opt.goal]; }

// The run is over when the purse can't cover the smallest stake, or reaches
// the goal: on to the last screen. False: carry on.
static bool runEnded() {
    bool won = goal() && casino.purse >= goal();
    if (!won && casino.purse >= ANTES[0]) return false;
    if (won) casino.stats.gamesWon++; else casino.stats.gamesBroke++;
    hasGame = false;
    persist(false);
    go(won ? Scr::Win : Scr::Lose);
    return true;
}

static void startGame() {
    seedOnce();
    if (!two) casino.purse -= ANTES[casino.ante];
    match.start((Mode)casino.mode, casino.opt.dealer, two);
    stage::reset(match);
    settled = false;
    hasGame = true;
}

// ---------------------------------------------------------------------------
// Debug hooks
// ---------------------------------------------------------------------------
void debugSeed(uint32_t s) { match.seed(s); seeded = true; }
void debugPurse(int32_t p) { casino.purse = p; }
void debugLevel(uint8_t l) { casino.opt.dealer = l > 2 ? 2 : l; }
void debugClock(uint16_t ticks) { match.clock = ticks; }
void debugDealer(uint8_t cell) { match.forced = cell; }
bool debugCell(uint8_t cell, uint8_t arg) {
    if (cur != Scr::Play || match.phase != Phase::Human || !rules::legal(match.b, cell, arg)) return false;
    match.cur = cell;
    match.size = arg;
    match.place(cell, arg);
    stage::onEvents(match);
    match.nEv = 0;
    return true;
}
void debugJump(char c, uint8_t mode) {
    // Scripted tests start from a known animation clock, so a board that has
    // been running since boot renders the same frames as a fresh simulator.
    rpgame.frameCount = 0;
    pal::resetClock();
    fx::reseed();                                        // enter() clears the particles
    switch (c) {
        case 'T': enter(Scr::Title); break;
        case 'G': two = false; casino.mode = mode < MODE_COUNT ? mode : 0; enter(Scr::Tables); break;
        case 'P': two = false; casino.mode = mode < MODE_COUNT ? mode : 0; startGame(); enter(Scr::Play); break;
        case '2': two = true; casino.mode = mode < MODE_COUNT ? mode : 0; startGame(); enter(Scr::Play); break;
        case 'W': enter(Scr::Win); break;
        case 'L': enter(Scr::Lose); break;
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

static void lettering(const uint8_t *bits, int w, int h, int x, int y, const uint8_t *ramp, int outline, int shadow) {
    Mask m = maskBegin(w, h);
    maskBlit1(m, bits, (uint8_t)w, (uint8_t)h);
    maskDraw(m, x, y, GOLD, outline, shadow, ramp);
}

// Big lettering in PPOT's font, scale 3, with a gradient, outline and shadow:
// top colour for 3 rows, mid down to row lowFrom, low below.
static void title35(const char *text, int y, uint8_t top, uint8_t mid, uint8_t low, uint8_t shadow,
                    uint8_t lowFrom = 9) {
    uint8_t scale = text35WidthScaled(text, 3) > 100 ? 2 : 3;
    Mask m = maskBegin(124, 18);
    maskText35(m, 0, 0, text, scale);
    uint8_t ramp[18];
    for (int i = 0; i < 18; i++) ramp[i] = i < scale ? top : (i < lowFrom * scale / 3 ? mid : low);
    maskDraw(m, 64 - text35WidthScaled(text, scale) / 2, y + (3 - scale) * 3, mid, INK, shadow, ramp);
}

static void centred35(int y, const char *s, uint8_t c) { text35(64 - text35Width(s) / 2, y, s, c); }
static void centred57(int y, const char *s, uint8_t c) { gfx_text(64 - gfx_textWidth(s) / 2, y, s, c); }

// "PAYS 2:1" for a table, at the dealer now sitting.
static char *odds(char *p, uint8_t mode) {
    if (two) return fmtStr(p, "FOR GLORY");
    if (MODES[mode].flags & F_BLITZ) return fmtStr(p, "PAYS BY THE BOARD");
    p = fmtInt(fmtStr(p, "PAYS "), MODES[mode].payNum * (casino.opt.dealer + 1));
    *p++ = ':';
    return fmtInt(p, MODES[mode].payDen);
}

// ---------------------------------------------------------------------------
// Title
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_PLAY, I_CONTINUE, I_NEW, I_TWO, I_OPTIONS, I_STATS };

static uint8_t menu(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) { items[n++] = I_CONTINUE; items[n++] = I_NEW; }
    else items[n++] = I_PLAY;
    items[n++] = I_TWO;
    items[n++] = I_OPTIONS;
    items[n++] = I_STATS;
    return n;
}

static void titleUpdate() {
    uint8_t items[6], n = menu(items);
    if (menuSel >= n) menuSel = 0;
    if (rpgame.repeat(UP_BUTTON) && menuSel > 0) { menuSel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && menuSel + 1 < n) { menuSel++; audio::sfx(Sfx::Cursor); }
    if (rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
        seedOnce();
        two = items[menuSel] == I_TWO;
        switch (items[menuSel]) {
            case I_PLAY: case I_NEW:
                casino.purse = START_PURSE; casino.streak = 0;
                hasGame = true;
                go(Scr::Tables);
                break;
            case I_TWO: match.score[0] = match.score[1] = 0; go(Scr::Tables); break;
            case I_CONTINUE: go(Scr::Tables); break;
            case I_OPTIONS: optBack = Scr::Title; optSel = 0; go(Scr::Options); break;
            case I_STATS: go(Scr::Stats); break;
        }
    }
}

// The title's little board plays the same game over and over: X takes the
// left column, and the line is struck through.
static const uint8_t DEMO[7] = {4, 1, 0, 8, 6, 2, 3};

static uint8_t titleStep = 0xFF;            // the little board's move, as last drawn

// The little board alone: the lettering above it is the costly part of the
// title, and never changes.
static void titleBoard(uint32_t frame, int y0) {
    uint8_t step = titleStep = (uint8_t)((frame / 20) % 11);
    const int cw = 11, bx = 64 - 16, by = 42 + (y0 - 79) / 2;
    gfx_fillRect(bx - 1, by - 2, 3 * cw + 1, 3 * cw + 3, FELT);
    for (int i = 1; i < 3; i++) {
        gfx_vline(bx + i * cw - 1, by, 3 * cw - 1, GOLD);
        gfx_hline(bx, by + i * cw - 1, 3 * cw - 1, GOLD);
    }
    for (uint8_t i = 0; i < 7 && i < step; i++)
        stage::mark(bx + DEMO[i] % 3 * cw + 5, by + DEMO[i] / 3 * cw + 5, 3, (uint8_t)(1 + (i & 1)));
    if (step > 7) gfx_fillRect(bx + 4, by - 2, 3, 3 * cw + 3, FX_A);
}

static void titleRender(uint32_t frame) {
    uint8_t items[6], n = menu(items);
    int y0 = 128 - n * 9 - 4;                                    // the menu, pitch 9
    feltBackdrop();
    uint8_t ramp[ROYALE_H + 2];
    for (int i = 0; i < LOGO_H + 2; i++) ramp[i] = i < 3 ? FX_B : (i < 9 ? GOLD : WOOD);
    lettering(LOGO, LOGO_W, LOGO_H, 64 - LOGO_W / 2, 6, ramp, INK, WINE);
    for (int i = 0; i < ROYALE_H + 2; i++) ramp[i] = i < 6 ? WHITE : (i < 14 ? RED : WINE);
    lettering(ROYALE, ROYALE_W, ROYALE_H, 64 - ROYALE_W / 2 + 2, 17, ramp, INK, INK);

    const int by = 42 + (y0 - 79) / 2;
    titleBoard(frame, y0);

    static const char *const LABEL[6] = {"PLAY", "CONTINUE", "NEW GAME", "2 PLAYERS", "OPTIONS", "STATS"};
    dither(6, y0 - 3, 116, 125 - y0, INK, 1);
    for (uint8_t i = 0; i < n; i++) {
        char buf[20];
        if (items[i] == I_CONTINUE) *fmtMoney(fmtStr(buf, "CONTINUE "), casino.purse) = 0;
        else strcpy(buf, LABEL[items[i]]);
        int y = y0 + i * 9;
        bool sel = i == menuSel;
        int w = gfx_textWidth(buf);
        if (sel) panel(64 - w / 2 - 6, y - 2, w + 12, 11, 3, NAVY, FX_B);   // FX_B pulses on its own
        centred57(y, buf, sel ? GOLD : WHITE);
    }
}

// ---------------------------------------------------------------------------
// The tables room: the dealer explains each table; pick one and a stake.
// ---------------------------------------------------------------------------

static void tablesUpdate() {
    uint8_t was = casino.mode;
    int8_t d = rpgame.repeat(LEFT_BUTTON) ? MODE_COUNT - 1 : (rpgame.repeat(RIGHT_BUTTON) ? 1 : 0);
    if (d) do casino.mode = (uint8_t)((casino.mode + d) % MODE_COUNT); while (!offered(casino.mode));
    if (casino.mode != was) { typedAt = t; audio::sfx(Sfx::Cursor); }
    uint8_t a = casino.ante;
    if (!two && rpgame.repeat(UP_BUTTON) && a + 1 < ANTE_COUNT && ANTES[a + 1] <= casino.purse) casino.ante++;
    if (!two && rpgame.repeat(DOWN_BUTTON) && a > 0) casino.ante--;
    if (casino.ante != a) audio::sfx(Sfx::Chip);
    if (rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
        startGame();
        go(Scr::Play);
    }
    if (rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        if (!two) persist(true);
        go(Scr::Title);
    }
}

static void tablesRender(uint32_t frame) {
    const char *rulesText = MODE_RULES[casino.mode];
    int typed = (t - typedAt) * 2 / 3, len = (int)strlen(rulesText);
    table::wall();
    table::dealer(typed < len ? (((t >> 2) & 1) ? table::E_TALK : table::E_NORMAL)
                              : ((t % 150) < 6 ? table::E_BLINK : table::E_SMILE),
                  1, casino.opt.look != 0);
    table::bubble(rulesText, typed);
    table::rail();
    gfx_fillRect(0, 46, 128, 82, FELT);
    dither(0, 46, 6, 82, FELT_DK, 0);
    dither(122, 46, 6, 82, FELT_DK, 1);
    dither(0, 122, 128, 6, FELT_DK, 1);

    title35(MODE_NAME[casino.mode], 51, FX_B, GOLD, WOOD, WINE, 13);
    bool blink = (frame & 16) != 0;
    text35x2(4, 54, "<", blink ? GOLD : WOOD);
    text35x2(118, 54, ">", blink ? GOLD : WOOD);
    char buf[28], *p = fmtInt(fmtStr(buf, "TABLE "), casino.mode + 1);
    p = fmtStr(p, "/16   ");
    *odds(p, casino.mode) = 0;
    centred35(73, buf, WHITE);

    if (two) {                                           // no money down: the score so far
        p = fmtInt(fmtStr(buf, "P1  "), match.score[0]);
        *fmtStr(fmtInt(fmtStr(p, " - "), match.score[1]), "  P2") = 0;
        panel(20, 84, 88, 22, 3, INK, GOLD);
        centred57(91, buf, WHITE);
        centred35(115, "A: PLAY    B: LEAVE", SILVER);
        return;
    }
    panel(8, 82, 54, 28, 3, INK, GOLD);
    text35(12, 85, "STAKE", FELT_LT);
    *fmtMoney(buf, ANTES[casino.ante]) = 0;
    gfx_text(12, 94, buf, GOLD);
    gfx_text(13, 94, buf, GOLD);
    panel(66, 82, 54, 28, 3, INK, GOLD);
    text35(70, 85, "PURSE", FELT_LT);
    *fmtMoney(buf, casino.purse) = 0;
    gfx_text(70, 94, buf, WHITE);
    if (casino.streak) {
        p = fmtInt(fmtStr(buf, "STREAK "), casino.streak);
        *p = 0;
        text35(70, 103, buf, FX_A);
    }
    centred35(115, "A: SIT DOWN    B: LEAVE", SILVER);
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
static void settle() {
    settled = true;
    if (two) return;
    Stats &s = casino.stats;
    int32_t ante = ANTES[casino.ante], back = match.payout(ante, casino.streak), net = back - ante;
    casino.purse += back;
    s.played[casino.mode]++;
    if (match.result == R_P0) {
        s.won[casino.mode]++;
        if (++casino.streak > s.bestStreak) s.bestStreak = casino.streak;
    } else if (match.result == R_DRAW) s.cats++;
    else casino.streak = 0;
    if (net > s.biggestWin) s.biggestWin = net;
    if (casino.purse > s.bestPurse) s.bestPurse = casino.purse;
    stage::paid(net);
    if (match.result == R_P0 && casino.streak > 1) {
        char buf[12];
        *fmtInt(fmtStr(buf, "STREAK "), casino.streak) = 0;
        fx::floatText(buf, 64, 34, FX_A);
    }
    if (++unsaved >= 5) persist(true);                   // every few games: flash wears
}

static void playUpdate() {
    uint8_t pressed = rpgame.justPressedMask();
    uint8_t rep = 0;
    static const uint8_t RB[4] = {LEFT_BUTTON, RIGHT_BUTTON, UP_BUTTON, DOWN_BUTTON};
    for (uint8_t i = 0; i < 4; i++) if (rpgame.repeat(RB[i])) rep |= RB[i];

    if (overlay == OV_RULES) {
        if (pressed) { overlay = OV_NONE; redrawAll(); audio::sfx(Sfx::Select); }
        return;
    }
    if (overlay == OV_PAUSE) {
        if (pressed & (UP_BUTTON | DOWN_BUTTON)) { pauseSel = (uint8_t)((pauseSel + ((pressed & UP_BUTTON) ? 3 : 1)) % 4); audio::sfx(Sfx::Cursor); }
        if (pressed & (START_BUTTON | B_BUTTON)) { overlay = OV_NONE; redrawAll(); audio::sfx(Sfx::Select); }
        if (pressed & A_BUTTON) {
            audio::sfx(Sfx::Select);
            overlay = OV_NONE;
            redrawAll();
            if (pauseSel == 1) overlay = OV_RULES;
            else if (pauseSel == 2) { optBack = Scr::Play; optSel = 0; resumePlay = true; go(Scr::Options); }
            else if (pauseSel == 3) {                    // walk away: the stake stays
                if (two) { go(Scr::Tables); return; }
                if (!settled) { casino.stats.played[casino.mode]++; casino.streak = 0; }
                if (!runEnded()) { persist(true); go(Scr::Tables); }
            }
        }
        return;
    }
    if (pressed & START_BUTTON) { overlay = OV_PAUSE; pauseSel = 0; audio::sfx(Sfx::Select); return; }
    if (pressed & SELECT_BUTTON) {                       // the iso table <-> the map; else the rules
        if (stage::canIso(match.b)) { stage::toggleView(); audio::sfx(Sfx::Whoosh); }
        else { overlay = OV_RULES; audio::sfx(Sfx::Select); }
        return;
    }

    match.iso = stage::isoOn(match.b);
    match.update(pressed, rep, stage::busy());
    stage::onEvents(match);
    stage::update(match);

    if (match.phase != Phase::Over) return;
    if (!settled) settle();
    if (stage::busy() || !(pressed & (A_BUTTON | B_BUTTON))) return;
    audio::sfx(Sfx::Select);
    if (!two && runEnded()) return;
    if ((pressed & A_BUTTON) && (two || casino.purse >= ANTES[casino.ante])) { startGame(); redrawAll(); }
    else go(Scr::Tables);
}

// Text of several lines, each centred.
static void centredLines(const char *s, int y, uint8_t c) {
    char line[20];
    for (;;) {
        uint8_t n = 0;
        while (*s && *s != '\n' && n < 19) line[n++] = *s++;
        line[n] = 0;
        centred35(y, line, c);
        y += 8;
        if (!*s++) return;
    }
}

static void playRender(uint32_t frame) {
    if (overlay) stage::invalidate();
    if (!stage::render(match, casino, frame)) return;
    if (!overlay) return;
    dither(0, 0, 128, 128, INK, 0);
    if (overlay == OV_PAUSE) {
        panel(24, 28, 80, 68, 4, NAVY, GOLD);
        centred57(33, "PAUSED", GOLD);
        static const char *const P[4] = {"RESUME", "HOW TO PLAY", "OPTIONS", "WALK AWAY"};
        for (int i = 0; i < 4; i++) {
            int y = 47 + i * 11;
            if (i == pauseSel) fillRound(28, y - 2, 72, 11, 3, INK);
            centred57(y, P[i], i == pauseSel ? FX_B : WHITE);
        }
    } else {
        panel(14, 20, 100, 84, 4, NAVY, GOLD);
        centred57(26, MODE_NAME[casino.mode], GOLD);
        centredLines(MODE_RULES[casino.mode], 40, WHITE);
        char buf[20];
        *odds(buf, casino.mode) = 0;
        centred35(85, buf, FX_B);
        centred35(94, two ? "TAKE TURNS WITH THE PAD" : "A DRAW IS A PUSH", SILVER);
    }
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
enum Opt : uint8_t { O_DEALER, O_GOAL, O_SOUND, O_LOOK, O_BACK, OPT_COUNT };

// Options' first four bytes, in this order; each entry is "LABEL|value|value...".
static const char *const OPT_TEXT[OPT_COUNT] = {
    "DEALER|TIPSY|SHARP|SHARK", "GOAL|$1000|$5000|ENDLESS", "SOUND|ON|OFF", "CROUPIER|CLASSIC|NIGHT", "BACK",
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
        uint8_t *f = (uint8_t *)&casino.opt + optSel;
        *f = (uint8_t)((*f + n + d) % n);
        if (optSel == O_SOUND) audio::setOn(!*f);
        audio::sfx(Sfx::Chip);
    }
    if ((rpgame.justPressed(A_BUTTON) && optSel == O_BACK) || rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        if (optBack != Scr::Play) persist(hasGame);      // mid-game: they go with the next save
        go(optBack);
    }
}

static void optionsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("OPTIONS", 6, FX_B, GOLD, WOOD, WINE, 13);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 32 + i * 12;
        bool sel = i == optSel;
        char buf[12];
        if (sel) fillRound(6, y - 2, 116, 10, 3, NAVY);
        optField(OPT_TEXT[i], 0, buf);
        text35(10, y, buf, sel ? WHITE : FELT_LT);
        if (i == O_BACK) continue;
        optField(OPT_TEXT[i], (uint8_t)(1 + ((uint8_t *)&casino.opt)[i]), buf);
        int w = text35Width(buf);
        text35(118 - w, y, buf, sel ? FX_B : GOLD);
        if (sel) { text35(112 - w, y, "<", SILVER); text35(120, y, ">", SILVER); }
    }
    static const char *const PAYS[3] = {"HE SLIPS UP. PAYS 1X", "A FAIR GAME. PAYS DOUBLE", "NO MERCY. PAYS TRIPLE"};
    const char *help = optSel == O_DEALER ? PAYS[casino.opt.dealer]
                     : optSel == O_GOAL ? "CASH OUT THERE AND WIN" : "B: BACK";
    centred35(98, help, GOLD);
    centred35(109, "CROUPIER ART: VAMPIRICS", SILVER);    // Press Play On Tape's (see NOTICE)
    centred35(116, "FONT: PRESS PLAY ON TAPE", SILVER);
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
            memset(&casino.stats, 0, sizeof casino.stats);
            persist(hasGame);
            audio::sfx(Sfx::Lose);
        }
        return;
    }
    statHold = 0;
    if (rpgame.justPressed(A_BUTTON | B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void statsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("STATISTICS", 5, WHITE, CYAN, BLUE, NAVY);
    const Stats &s = casino.stats;
    char buf[16];
    for (int i = 0; i < MODE_COUNT; i++) {              // won/played at every table
        int x = i < 9 ? 8 : 67, y = 23 + (i % 9) * 7;
        text35(x, y, MODE_NAME[i], FELT_LT);
        char *p = fmtInt(buf, s.won[i]);
        *p++ = '/';
        *fmtInt(p, s.played[i]) = 0;
        text35(x + 54 - text35Width(buf), y, buf, WHITE);
    }
    gfx_hline(8, 86, 112, FELT_DK);
    static const char *const NAME[4] = {"BEST PURSE", "BIGGEST WIN", "BEST STREAK", "CAT'S GAMES"};
    const int32_t val[4] = {s.bestPurse, s.biggestWin, s.bestStreak, s.cats};
    for (int i = 0; i < 4; i++) {
        int y = 88 + i * 7;
        text35(10, y, NAME[i], FELT_LT);
        if (i < 2) *fmtMoney(buf, val[i]) = 0; else *fmtInt(buf, val[i]) = 0;
        text35(118 - text35Width(buf), y, buf, i < 2 ? GOLD : WHITE);
    }
    if (statHold >= STAT_RESET_FRAMES) centred35(117, "STATS RESET", GOLD);
    else if (statHold) {                          // the reset gesture filling up
        gfx_rect(24, 117, 80, 5, SILVER);
        gfx_fillRect(25, 118, 78 * statHold / STAT_RESET_FRAMES, 3, RED);
    } else centred35(117, (!CHTT_LEAN && save::available()) ? "HOLD SELECT TO RESET" : "SAVING UNAVAILABLE", SILVER);
}

// ---------------------------------------------------------------------------
// Game won / lost (PPOT's GameWinState / GameLoseState, in colour)
// ---------------------------------------------------------------------------
static void endUpdate() {
    if (t > 60 && rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
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
#if CHTT_LEAN
    title35("YOU WIN", 18, ramp[0], ramp[5], ramp[10], WINE);   // device debug builds: no PPOT lettering
#else
    lettering(YOUWON1, YOUWON1_W, 16, 64 - YOUWON1_W / 2, 12, ramp, INK, WINE);
    lettering(YOUWON2, YOUWON2_W, 16, 64 - YOUWON2_W / 2, 36, ramp, INK, WINE);
#endif
    char buf[16];
    *fmtMoney(buf, casino.purse) = 0;
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
#if CHTT_LEAN
    title35("BROKE", 20 - drop, WHITE, RED, WINE, NAVY);
#else
    lettering(BROKE1, BROKE1_W, 16, 64 - BROKE1_W / 2, 14 - drop, ramp, INK, NAVY);
    if (t > 20) lettering(BROKE2, BROKE2_W, 16, 64 - BROKE2_W / 2, 40 - (t < 60 ? 60 - t : 0), ramp, INK, NAVY);
#endif
    for (int i = 0; i < 2; i++) fx::spawn(fx::RAIN_DROP, fx::rndRange(0, 128), -4, -6, 64, 40, CYAN);   // ...and it rains
    fx::update();
    fx::drawParticles(1);
    table::dealer(table::E_SMILE, 1, casino.opt.look != 0, 40, 70);   // the croupier, delighted
    gfx_fillRect(0, 112, 128, 16, INK);
    centred35(116, t > 60 && (frame & 16) ? "PRESS A" : "AT TIC TAC TOE", WHITE);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------
void update() {
    t++;
    audio::update();
    if (fadeOut) {
        pal::setFade((uint8_t)((fadeOut - 1) * 2));
        if (--fadeOut == 0) enter(pending);
        return;
    }
    if (fadeIn) { fadeIn--; pal::setFade((uint8_t)(16 - fadeIn * 2)); }
    switch (cur) {
        case Scr::Title: titleUpdate(); break;
        case Scr::Tables: tablesUpdate(); break;
        case Scr::Play: playUpdate(); break;
        case Scr::Options: optionsUpdate(); break;
        case Scr::Stats: statsUpdate(); break;
        case Scr::Win: case Scr::Lose: endUpdate(); break;
    }
}

void render(uint32_t frame) {
    // Still screens are drawn only when something on them changed; palette
    // animation (FX_A/FX_B pulses, fades) runs regardless, because every
    // frame is still flushed.
    if (cur == Scr::Title || cur == Scr::Options || cur == Scr::Stats || cur == Scr::Tables) {
        uint32_t sig = (uint32_t)cur * 2654435761u ^ ((uint32_t)menuSel << 8) ^ ((uint32_t)optSel << 12) ^
                       ((uint32_t)hasGame << 16) ^ ((uint32_t)statHold << 24);
        if (cur == Scr::Tables) {
            uint32_t typed = (uint32_t)(t - typedAt) * 2 / 3;
            sig ^= (casino.mode << 4) ^ (casino.ante << 20) ^ ((frame >> 4) & 1) ^
                   ((typed < 90 ? (typed << 1 | ((t >> 2) & 1)) : (t % 150 < 6)) << 8);
        }
        for (uint8_t i = 0; i < 8; i++) sig = sig * 31u + ((uint8_t *)&casino.opt)[i];
        sig |= 1;    // never 0: staticSig == 0 means "not drawn yet"
        if (sig == staticSig) {
            if (cur == Scr::Title && titleStep != (frame / 20) % 11) {
                uint8_t items[6];
                titleBoard(frame, 128 - menu(items) * 9 - 4);
            }
            return;
        }
        staticSig = sig;
    }
    switch (cur) {
        case Scr::Title: titleRender(frame); break;
        case Scr::Tables: tablesRender(frame); break;
        case Scr::Play: playRender(frame); break;
        case Scr::Options: optionsRender(frame); break;
        case Scr::Stats: statsRender(frame); break;
        case Scr::Win: winRender(frame); break;
        case Scr::Lose: loseRender(frame); break;
    }
}

}  // namespace screens
