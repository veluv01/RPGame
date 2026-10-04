#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// Screens after CHBlackjack's (which follow Press Play On Tape's
// GameStateTypes): title, play, options, statistics and the broke screen,
// with a pause menu over play.
#include <Arduino.h>
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Screens.h"
#include "Bingo.h"
#include "Presenter.h"
#include "Fx.h"
#include "Cards.h"
#include "Layout.h"
#include "Table.h"
#include "Sounds.h"
#include "Save.h"
#include "src/assets/Assets.h"

namespace screens {

enum class Scr : uint8_t { Title, Play, Options, Stats, Lose };

static Bingo game;
static Scr cur = Scr::Title, pending = Scr::Title;
static uint8_t fadeOut = 0, fadeIn = 0;      // palette fade transitions
static uint16_t t = 0;                       // frames on this screen
static bool hasGame = false, paused = false, board = false, resumePlay = false;
static bool seeded = false;
static uint8_t menuSel = 0, pauseSel = 0, optSel = 0;
static Scr optBack = Scr::Title;
static Phase lastPhase = Phase::Welcome;
static uint32_t staticSig = 0;             // last drawn state of a still screen
static bool titleReady = false;            // the title's still parts are drawn
static int16_t mgx16, mgy16;               // the menu glove (Q4); mgy16 0 = it appears in place

// Options "SOUND|ON|OFF" (all-zero is the default): sound is on unless OFF.
static bool soundOn(uint8_t opt) { return opt == 0; }

// The play screen only redraws what changed; anything drawn over it from
// outside (the pause menu) has to force a full redraw.
static void redrawAll() { present::invalidate(); staticSig = 0; titleReady = false; }

static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

static void persist(bool keepGame) {
    gfx_wait();
    save::store(game, keepGame);
}

static void enter(Scr s) {
    cur = s; t = 0; fadeIn = 8;
    mgy16 = 0;
    titleReady = false;
    fx::clear();
    pal::setDesaturate(0);
    pal::setCycling(true);
    switch (s) {
        case Scr::Title:
            paused = false; menuSel = 0;
            break;
        case Scr::Play:
            if (!resumePlay) present::reset(game);
            redrawAll();
            resumePlay = false;
            paused = board = false;
            lastPhase = game.phase;
            break;
        case Scr::Lose:
            audio::sfx(Sfx::Broke);
            break;
        default:
            break;
    }
}

void begin() {
    save::load(game, hasGame);
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, soundOn(game.opt.sound));
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
#if CHGAME_DEBUG
void debugSeed(uint32_t s) { game.seed(s); seeded = true; }
void debugJump(char c) {
    // Scripted tests start from a known animation clock, so a board that has
    // been running since boot renders the same frames as a fresh simulator.
    rpgame.frameCount = 0;
    pal::resetClock();
    fx::reseed();                                        // enter() clears the particles
    switch (c) {
        case 'T': enter(Scr::Title); break;
        case 'P': game.newGame(); hasGame = true; enter(Scr::Play); break;
        case 'L': enter(Scr::Lose); break;
        case 'O': optBack = Scr::Title; enter(Scr::Options); break;
        case 'S': enter(Scr::Stats); break;
    }
}
bool debugGame(char cmd, uint32_t v) {
    bool round = game.phase == Phase::Calling;
    switch (cmd) {
        case 'C': if (game.phase != Phase::Buy) return false; game.start((uint8_t)v); return true;
        case 'F': if (!round) return false; game.force((uint8_t)v); return true;
        case 'W': if (!round) return false; game.forceWin(); return true;
        case 'D': if (!round) return false; game.daubAll(); return true;
        case 'H': if (!round) return false; game.hallBall = (uint8_t)v; return true;
        case 'G': if (!round || v >= game.nCards) return false; game.focus = (uint8_t)v; present::invalidate(); return true;
        case 'X': game.grant((uint8_t)v); return true;
        case 'I': game.forceRare((uint8_t)v); return true;
        case 'M': game.purse = (int32_t)v; return true;
        case 'A': game.jackpot = (int32_t)v; return true;
        case 'V':                                        // power off and on: only the flash survives
            memset((void *)&game, 0, sizeof game);
            seeded = false;
            begin();
            return true;
    }
    return false;
}
void debugState(char *buf) {
    uint32_t h = 2166136261u;
    for (uint8_t k = 0; k < game.nCards; k++) h = (h ^ game.daub[k] ^ (game.pend[k] << 1)) * 16777619u;
    int32_t waiting = 0;                                 // the cards with a called number on them
    for (uint8_t k = 0; k < game.nCards; k++) if (game.pend[k]) waiting |= 1 << k;
    const int32_t v[14] = {game.purse, game.jackpot, (int32_t)game.phase, game.nCards, game.nCalled, game.focus,
                           game.hallBall, (int32_t)(h & 0x7FFFFFFF), hasGame, (int32_t)game.stats.rounds,
                           game.opt.speed, (int32_t)cur, waiting, game.power};
    char *p = fmtStr(buf, "STATE");
    for (int i = 0; i < 14; i++) { *p++ = ' '; p = fmtInt(p, v[i]); }
    *fmtStr(p, "\n") = 0;
}
#endif

// ---------------------------------------------------------------------------
// Shared drawing
// ---------------------------------------------------------------------------
// The felt with its darkened edges and gold line, rows y0..y1-1 of it (the
// dither is aligned to the screen, so bands drawn apart meet seamlessly).
static void feltBand(int y0, int y1) {
    int h = y1 - y0;
    gfx_fillRect(0, y0, 128, h, FELT);
    dither(0, y0, 6, h, FELT_DK, 0);
    dither(122, y0, 6, h, FELT_DK, 1);
    if (y0 < 6) dither(0, 0, 128, 6, FELT_DK, 0);
    if (y1 > 122) dither(0, 122, 128, 6, FELT_DK, 1);
    gfx_vline(2, y0 < 2 ? 2 : y0, (y1 > 126 ? 126 : y1) - (y0 < 2 ? 2 : y0), GOLD);
    gfx_vline(125, y0 < 2 ? 2 : y0, (y1 > 126 ? 126 : y1) - (y0 < 2 ? 2 : y0), GOLD);
    if (y0 <= 2) gfx_hline(2, 2, 124, GOLD);
    if (y1 >= 126) gfx_hline(2, 125, 124, GOLD);
}

static void feltBackdrop() { feltBand(0, 128); }

// CHChess's glove turned to point right, at the menu item chosen: it glides
// from item to item and nods toward it.

static void menuGlove(int tx, int ty, uint32_t frame) {
    if (!mgy16) { mgx16 = (int16_t)(tx << 4); mgy16 = (int16_t)(ty << 4); }
    int dx = (tx << 4) - mgx16, dy = (ty << 4) - mgy16;
    mgx16 = (int16_t)(mgx16 + (dx / 3 ? dx / 3 : dx));
    mgy16 = (int16_t)(mgy16 + (dy / 3 ? dy / 3 : dy));
    int nod = (fx::isin((int)(frame * 8)) + 256) >> 8;   // 0..2 px toward the item
    sprite4(HAND_R, (mgx16 >> 4) - 15 + nod, (mgy16 >> 4) - HAND_R_TIP, cards::cuff(cards::daub(game)));
}

static void lettering(const uint8_t *bits, int w, int h, int x, int y, const uint8_t *ramp, int outline, int shadow) {
    Mask m = maskBegin(w, h);
    maskBlit1(m, bits, (uint8_t)w, (uint8_t)h);
    maskDraw(m, x, y, GOLD, outline, shadow, ramp);
}

// "Bingo" in CHBlackjack's title lettering and colours: the top rows in
// FX_B, so the palette makes it shimmer with no redraw.
static void logo(int y) {
    uint8_t ramp[LOGO_H + 2];
    for (int i = 0; i < LOGO_H + 2; i++) ramp[i] = i < 3 ? FX_B : (i < 12 ? GOLD : WOOD);
    lettering(LOGO, LOGO_W, LOGO_H, 64 - LOGO_W / 2, y, ramp, INK, WINE);
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

// One of B I N G O at double size.
static void bigLetter(int x, int y, int i, uint8_t c) {
    char s[2] = {"BINGO"[i], 0};
    text35x2(x, y, s, c);
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
}

// The felt and the logo are drawn once; each frame redraws the band the
// balls bounce through and the menu, where the glove moves.
static const int TITLE_BAND_Y = 35;

// One bingo ball, lit from the top left, its letter on a white spot.
static void ball(int cx, int cy, int letter) {
    static const uint8_t BODY[5] = {BLUE, RED, SILVER, FELT_LT, GOLD};
    uint8_t c = BODY[letter];
    gfx_fillCircle(cx, cy, 10, c);
    gfx_fillCircle(cx + 1, cy + 1, 9, DARKER[c]);            // shaded on the side away from the light
    gfx_fillCircle(cx - 1, cy - 1, 8, c);
    gfx_fillCircle(cx, cy, 6, WHITE);
    gfx_hline(cx - 6, cy - 7, 3, LIGHTER[c]);                // the glint
    gfx_pixel(cx - 7, cy - 6, LIGHTER[c]);
    bigLetter(cx - 3, cy - 5, letter, INK);
}

// Balls go bouncing by, right to left, a word at a time: B I N G O, a gap,
// B I N G O... with B in front, so the word reads as it passes. They all
// bounce at one rate, to one height, each a beat behind the ball ahead.
// Each runs a little ahead of its place or a little behind, on its own slow
// swing, so they don't travel in lockstep; the swing (3 px either way) is
// less than the room between them, so none ever passes another.
static void titleBalls(uint32_t frame, int y1) {
    static const int PITCH = 27, WORD = 5 * PITCH + 30, PACE = 3;   // PACE/5 px a frame; balls are 21 px
    static const int BOUNCE = 48, BEAT = 10;               // ticks a bounce; the lag from ball to ball
    feltBand(TITLE_BAND_Y, y1);
    gfx_setClip(3, TITLE_BAND_Y, 122, y1 - TITLE_BAND_Y);   // they roll in from behind the frame
    int floor = y1 - 4, height = (y1 - TITLE_BAND_Y - 26) * 7 / 8;
    int32_t gone = (int32_t)(frame * PACE / 5);            // how far the stream has travelled
    // Ball l of word w sits at w * WORD + l * PITCH - gone; the first words
    // start on screen.
    for (int32_t w = (gone - 12 - 4 * PITCH) / WORD; w <= (gone + 140) / WORD + 1; w++) {
        if (w < 0) continue;
        for (int l = 0; l < 5; l++) {
            uint32_t h = (uint32_t)(w * 5 + l) * 2654435761u;
            int swing = 180 + (int)((h >> 4) % 5) * 30;           // ticks for one swing ahead and back
            int cx = (int)(16 + w * WORD + l * PITCH - gone) +
                     ((fx::isin((int)((frame + (h >> 12)) % (uint32_t)swing * 256 / swing)) * 3) >> 8);
            if (cx < -12 || cx > 140) continue;
            uint32_t beat = frame + (uint32_t)(BOUNCE * 64) - (uint32_t)((w * 5 + l) % 64) * BEAT;
            int up = fx::isin((int)((beat % BOUNCE) * 128 / BOUNCE));
            int cy = floor - 10 - ((up * height) >> 8);
            int r = 3 + ((256 - up) >> 5);                   // its shadow: wide on the felt, thin in the air
            gfx_hline(cx - r, floor, 2 * r + 1, FELT_DK);
            ball(cx, cy, l);
        }
    }
    gfx_resetClip();
}

static void titleRender(uint32_t frame) {
    uint8_t items[5], n = menu(items);
    const int PITCH = 12;
    int top = 128 - n * PITCH - 10;                              // the menu band's top edge
    if (!titleReady) {
        titleReady = true;
        feltBackdrop();
        logo(7);
        centred35(28, "~75~BALL~CLASSIC~", CYAN);
    }
    titleBalls(frame, top);
    // The menu on its darker band, and the glove pointing at the choice.
    feltBand(top, 128);
    dither(6, top, 116, 122 - top, FELT_DK, 1);
    static const char *const LABEL[5] = {"PLAY", "CONTINUE", "NEW GAME", "OPTIONS", "STATS"};
    int selX = 0, y0 = top + 6;
    for (uint8_t i = 0; i < n; i++) {
        char buf[20];
        if (items[i] == I_CONTINUE) *fmtMoney(fmtStr(buf, "CONTINUE "), game.purse) = 0;
        else strcpy(buf, LABEL[items[i]]);
        int y = y0 + i * PITCH;
        bool sel = i == menuSel;
        int w = gfx_textWidth(buf);
        if (sel) { panelLit(64 - w / 2 - 6, y - 2, w + 12, 11, 3, NAVY, FX_B); selX = 64 - w / 2 - 7; }
        centred57(y, buf, sel ? GOLD : WHITE);
    }
    menuGlove(selX - 1, y0 + menuSel * PITCH + 3, frame);
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
static const uint8_t PAUSE_ITEMS = 4;

static void playUpdate() {
    uint8_t pressed = rpgame.justPressedMask();
    uint8_t rep = 0;
    static const uint8_t RB[4] = {LEFT_BUTTON, RIGHT_BUTTON, UP_BUTTON, DOWN_BUTTON};
    for (uint8_t i = 0; i < 4; i++) if (rpgame.repeat(RB[i])) rep |= RB[i];

    if (paused) {
        if (board) {                                             // the called board: any button closes it
            if (pressed) { board = false; audio::sfx(Sfx::Select); }
            return;
        }
        if (pressed & UP_BUTTON) { pauseSel = (uint8_t)((pauseSel + PAUSE_ITEMS - 1) % PAUSE_ITEMS); audio::sfx(Sfx::Cursor); }
        if (pressed & DOWN_BUTTON) { pauseSel = (uint8_t)((pauseSel + 1) % PAUSE_ITEMS); audio::sfx(Sfx::Cursor); }
        if (pressed & (START_BUTTON | B_BUTTON)) { paused = false; audio::sfx(Sfx::Select); }
        if (pressed & A_BUTTON) {
            audio::sfx(Sfx::Select);
            if (pauseSel == 0) paused = false;
            else if (pauseSel == 1) board = true;
            else if (pauseSel == 2) { optBack = Scr::Play; optSel = 0; resumePlay = true; go(Scr::Options); }
            else {
                game.quitNow();
                hasGame = game.purse > 0 || game.hasRound();
                persist(hasGame);
                go(Scr::Title);
            }
        }
        return;
    }
    if (pressed & START_BUTTON) { paused = true; board = false; pauseSel = 0; mgy16 = 0; audio::sfx(Sfx::Select); return; }

    game.update(pressed & ~START_BUTTON, rep, present::busy());
    present::onEvents(game);
    present::update(game);

    if (game.phase != lastPhase) {
        // A round is over: its result is saved before the next buy-in.
        if (game.phase == Phase::Buy && (lastPhase == Phase::Won || lastPhase == Phase::Lost)) persist(true);
        if (game.phase == Phase::GameLost) { hasGame = false; persist(false); go(Scr::Lose); }
        lastPhase = game.phase;
    }
}

// Every number, in its letter's column: lit once it has been called.
static void calledBoard() {
    panelLit(12, 6, 104, 116, 4, INK, GOLD);
    bool live = game.inPlay();
    for (int c = 0; c < 5; c++) {
        int x = 22 + c * 19;
        bigLetter(x + 1, 10, c, cards::LETTER_COL[c]);
        for (int r = 0; r < 15; r++) {
            uint8_t n = (uint8_t)(c * 15 + r + 1);
            char s[4];
            *fmtInt(s, n) = 0;
            text35(x + (n < 10 ? 2 : 0), 24 + r * 6, s, live && game.called(n) ? cards::LETTER_COL[c] : NAVY);
        }
    }
    char buf[20];
    *fmtStr(fmtInt(buf, live ? game.nCalled : 0), " CALLED") = 0;
    centred35(115, buf, SILVER);
}

static void playRender(uint32_t frame) {
    static bool wasPaused = false;                         // the frame after the menu closes repaints too
    if (paused || wasPaused) redrawAll();
    wasPaused = paused;
    bool drew = present::render(game, frame);
    if (drew) present::overlay(game, frame);
    if (!paused) return;
    dither(0, 0, 128, 128, INK, 0);
    if (board) { calledBoard(); return; }
    panelLit(24, 28, 80, 67, 4, NAVY, GOLD);
    centred57(33, "PAUSED", GOLD);
    static const char *const P[PAUSE_ITEMS] = {"RESUME", "CALLED", "OPTIONS", "SAVE & QUIT"};
    for (int i = 0; i < PAUSE_ITEMS; i++) {
        int y = 47 + i * 11;
        if (i == pauseSel) fillRound(30, y - 2, 68, 11, 3, INK);
        centred57(y, P[i], i == pauseSel ? FX_B : WHITE);
    }
    menuGlove(29, 47 + pauseSel * 11 + 3, frame);
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
enum Opt : uint8_t { O_STAKES, O_HALL, O_SPEED, O_SOUND, O_DEALER, O_DAUBER, O_BACK, OPT_COUNT };

// Options' first six bytes, in this order; each entry is "LABEL|value|value...".
static const char *const OPT_TEXT[OPT_COUNT] = {
    "STAKES|$5 A CARD|$10 A CARD|$25 A CARD", "HALL|BUSY|PACKED|QUIET", "SPEED|NORMAL|FAST|SLOW", "SOUND|ON|OFF",
    "DEALER|CLASSIC|NIGHT", "DAUBER|RED|BLUE|GREEN|CYAN|PEACH", "BACK",
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
        char tmp[16];
        uint8_t n = (uint8_t)(optField(OPT_TEXT[optSel], 0, tmp) - 1);
        uint8_t *f = (uint8_t *)&game.opt + optSel;
        *f = (uint8_t)((*f + n + d) % n);
        if (optSel == O_SOUND) audio::setOn(soundOn(*f));
        audio::sfx(Sfx::Chip);
    }
    if ((rpgame.justPressed(A_BUTTON) && optSel == O_BACK) || rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        // Not mid-round: a save there could be power-cycled back to. The
        // options go with the next save.
        if (optBack != Scr::Play || game.phase != Phase::Calling) persist(hasGame);
        go(optBack);
    }
}

// Redrawn every frame: the glove glides between the rows.
static void optionsRender(uint32_t frame) {
    feltBackdrop();
    title35("OPTIONS", 6, FX_B, GOLD, WOOD, WINE, 13);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 27 + i * 10;
        bool sel = i == optSel;
        char buf[16];
        if (sel) fillRound(18, y - 2, 104, 9, 3, NAVY);
        optField(OPT_TEXT[i], 0, buf);
        text35(22, y, buf, sel ? WHITE : FELT_LT);
        if (i == O_BACK) continue;
        optField(OPT_TEXT[i], (uint8_t)(1 + ((uint8_t *)&game.opt)[i]), buf);
        int w = text35Width(buf);
        uint8_t c = sel ? FX_B : GOLD;
        if (i == O_DAUBER) {                                     // the colour itself, and a blot of it
            c = cards::daub(game);
            fillRound(104 - w, y - 1, 7, 7, 2, DARKER[c]);
            fillRound(104 - w, y - 1, 7, 6, 2, c);
            gfx_pixel(106 - w, y, LIGHTER[c]);
        }
        text35(118 - w, y, buf, c);
        if (sel) { text35(112 - w - (i == O_DAUBER ? 10 : 0), y, "<", SILVER); text35(120, y, ">", SILVER); }
    }
    menuGlove(17, 27 + optSel * 10 + 2, frame);
    static const char *const HALL[3] = {"20 RIVAL CARDS", "40 RIVAL CARDS: A BIG POT", "8 RIVAL CARDS: A SMALL POT"};
    const char *help = optSel == O_STAKES ? "FROM THE NEXT ROUND"
                     : optSel == O_HALL ? HALL[game.opt.hall < 3 ? game.opt.hall : 0]
                     : optSel == O_SPEED ? "HOW FAST THE BALLS COME"
                     : optSel == O_DAUBER ? "YOUR DAUBS, YOUR GLOVE"
                     : "B: BACK";
    centred35(100, help, GOLD);
    centred35(111, "CALLER ART: VAMPIRICS", SILVER);      // Press Play On Tape's (see NOTICE)
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
    static const char *const NAME[9] = {"ROUNDS", "BINGOS", "CARDS BOUGHT", "BIGGEST WIN", "BEST PURSE",
                                        "BEST STREAK", "FASTEST BINGO", "JACKPOTS", "TIMES BROKE"};
    const int32_t val[9] = {(int32_t)s.rounds, (int32_t)s.wins, (int32_t)s.cards, s.biggestWin, s.bestPurse,
                            s.bestStreak, s.fastest, s.jackpots, s.gamesBroke};
    for (int i = 0; i < 9; i++) {
        char buf[16];
        int y = 25 + i * 8;
        text35(10, y, NAME[i], FELT_LT);
        if (i == 3 || i == 4) *fmtMoney(buf, val[i]) = 0;
        else if (i == 6 && val[i]) *fmtStr(fmtInt(buf, val[i]), " CALLS") = 0;
        else *fmtInt(buf, val[i]) = 0;
        text35(118 - text35Width(buf), y, buf, i == 3 || i == 4 ? GOLD : WHITE);
    }
    if (statHold >= STAT_RESET_FRAMES) centred35(104, "STATS RESET", GOLD);
    else if (statHold) {                          // the reset gesture filling up
        gfx_rect(24, 104, 80, 5, SILVER);
        gfx_fillRect(25, 105, 78 * statHold / STAT_RESET_FRAMES, 3, RED);
    } else centred35(104, "HOLD SELECT TO RESET", FELT_LT);
    centred35(114, save::available() ? "SAVED IN FLASH" : "SAVING UNAVAILABLE", SILVER);
}

// ---------------------------------------------------------------------------
// Broke (PPOT's GameLoseState, in colour)
// ---------------------------------------------------------------------------
static void endUpdate() {
    if (t > 60 && rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
        go(Scr::Title);
    }
}

static void loseRender(uint32_t frame) {
    gfx_clear(INK);
    pal::setDesaturate((uint8_t)(t / 12 > 12 ? 12 : t / 12));   // colour drains out of the world...
    for (int x = 3; x < 128; x += 8) gfx_vline(x, 0, 128, NAVY);
    uint8_t ramp[16];
    for (int i = 0; i < 16; i++) ramp[i] = i < 4 ? WHITE : (i < 10 ? RED : WINE);
    int drop = t < 40 ? (40 - t) : 0;
#if CHBN_LEAN
    title35("BROKE", 20 - drop, WHITE, RED, WINE, NAVY);
#else
    lettering(BROKE1, BROKE1_W, 16, 64 - BROKE1_W / 2, 14 - drop, ramp, INK, NAVY);
    if (t > 20) lettering(BROKE2, BROKE2_W, 16, 64 - BROKE2_W / 2, 40 - (t < 60 ? 60 - t : 0), ramp, INK, NAVY);
#endif
    for (int i = 0; i < 2; i++) fx::spawn(fx::RAIN_DROP, fx::rndRange(0, 128), -4, -6, 64, 40, CYAN);   // ...and it rains
    fx::update();
    fx::drawParticles(1);
    table::dealer(table::E_SMILE, 1, game.opt.dealer != 0, 40, 70);   // the caller, delighted
    gfx_fillRect(0, 112, 128, 16, INK);
    if (t > 60 && (frame & 16)) centred35(116, "PRESS A", WHITE);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------
void update() {
    t++;
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
        case Scr::Lose: endUpdate(); break;
    }
}

// Screens other than play are drawn only when something on them changed;
// palette animation (FX_A/FX_B pulses, fades) runs regardless, because every
// frame is still flushed.
static bool unchanged(uint32_t sig) {
    if (sig == staticSig && !fx::particles()) return true;
    staticSig = sig;
    return false;
}

void render(uint32_t frame) {
    uint32_t sig = (uint32_t)cur * 2654435761u ^ ((uint32_t)menuSel << 8) ^ ((uint32_t)optSel << 12) ^
                   ((uint32_t)hasGame << 16) ^ ((uint32_t)game.purse << 17) ^ ((uint32_t)statHold << 24);
    for (uint8_t i = 0; i < 8; i++) sig = sig * 31u + ((uint8_t *)&game.opt)[i];
    sig |= 1;    // never 0: staticSig == 0 means "not drawn yet"
    bool still = cur == Scr::Stats;                              // the title's balls, the options' glove move
    if (still && unchanged(sig)) return;
    if (!still) staticSig = 0;
    switch (cur) {
        case Scr::Title: titleRender(frame); break;
        case Scr::Play: playRender(frame); break;
        case Scr::Options: optionsRender(frame); break;
        case Scr::Stats: statsRender(frame); break;
        case Scr::Lose: loseRender(frame); break;
    }
}

}  // namespace screens
