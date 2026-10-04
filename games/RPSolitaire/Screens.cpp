// The screens and the moves between them: title, the table (with its pause
// menu and the win), the deck of backs, options, statistics; and the debug
// protocol's commands. The table itself is Stage.cpp.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library)
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "config.h"
#include "Screens.h"
#include "Fx.h"
#include "Sounds.h"
#include "Klondike.h"
#include "CardArt.h"
#include "src/assets/Assets.h"
#include "Stage.h"
#include "Save.h"
#ifdef CHSIM
#include <sim.h>
#endif

namespace screens {

enum class Scr : uint8_t { Title, Play, Deck, Options, Stats };
static Scr cur = Scr::Title, pending = Scr::Title, back = Scr::Title;   // back: where Deck and Options return to
static uint16_t t;                   // frames on this screen
static uint8_t fadeOut, fadeIn;
static uint8_t sel;                  // menu cursor
static bool paused;
static void titleCards();
static uint8_t doneT;                // frames since the cascade ended
static int32_t bonus;                // the time bonus of the game just won

static Klondike game;
static Options opt;
static Stats stats;

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------
static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

static void enter(Scr s) {
    cur = s;
    t = 0;
    sel = 0;
    fadeIn = 8;
    paused = false;
    fx::clear();
    stage::invalidate();
    if (s == Scr::Title) {
        audio::sfx(Sfx::Title);
        titleCards();
    }
}

static void persist() {
    gfx_wait();                      // save builds its page in the chunk scratch
    save::store(opt, stats, game);
}

static void applyOptions() {
    audio::setOn(opt.sound == 0);
    art::fourColour = opt.deck != 0;
    art::backStyle = opt.back;
}

static void newDeal() {
    if (game.live) {
        // Walking away from a game ends the streak and, in Vegas, costs
        // what it stands at. A deal nobody touched never happened.
        if (!game.started) stats.played--;
        else {
            stats.streak = 0;
            if (game.scoring == VEGAS) stats.bank += game.score;
        }
    }
    game.deal(micros() * 2654435761u ^ rpgame.frameCount, opt);
    stats.played++;
    stage::bank = stats.bank;
    stage::deal(game);
    doneT = 0;
    persist();
}

static void won() {
    stats.won++;
    if (++stats.streak > stats.bestStreak) stats.bestStreak = stats.streak;
    bonus = game.bonus();
    game.score += bonus;
    if (game.scoring == STANDARD && game.score > stats.bestScore) stats.bestScore = game.score;
    if (game.secs && (!stats.bestTime || game.secs < stats.bestTime)) stats.bestTime = game.secs;
    if (game.scoring == VEGAS) stats.bank += game.score;
    audio::sfx(Sfx::BigWin);
    audio::led(audio::LED_PARTY);
    persist();
}

// ---------------------------------------------------------------------------
// Shared drawing (CHBlackjack's and CHChess's look)
// ---------------------------------------------------------------------------
static void feltBackdrop() {
    gfx_clear(FELT);
    dither(0, 0, 128, 6, FELT_DK, 0);
    dither(0, 122, 128, 6, FELT_DK, 1);
    dither(0, 0, 6, 128, FELT_DK, 0);
    dither(122, 0, 6, 128, FELT_DK, 1);
    gfx_rect(2, 2, 124, 124, GOLD);
}

// Big lettering in PPOT's font with a gradient, outline and shadow.
static void title35(const char *text, int y, uint8_t scale, uint8_t top, uint8_t mid, uint8_t low,
                    uint8_t shadow, uint8_t lowFrom) {
    int h = 6 * scale, w = text35WidthScaled(text, scale);
    Mask m = maskBegin(w, h);
    maskText35(m, 0, 0, text, scale);
    uint8_t ramp[32];
    for (int i = 0; i < h + 2 && i < 32; i++) ramp[i] = i < scale ? top : (i < lowFrom ? mid : low);
    maskDraw(m, 64 - w / 2, y, 0, INK, shadow, ramp);
}

static void centred35(int y, const char *s, uint8_t c) { text35(64 - text35Width(s) / 2, y, s, c); }

static bool menuNav(uint8_t n) {
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel + 1 < n) { sel++; audio::sfx(Sfx::Cursor); }
    return rpgame.justPressed(A_BUTTON);
}

static void panel(int y, int h) {
    fillRound(12, y, 104, h, 3, NAVY);
    roundRect(12, y, 104, h, 3, GOLD);
}

// ---------------------------------------------------------------------------
// Title: cards drifting down the felt behind the name (CHMahjong's tiles).
// ---------------------------------------------------------------------------
static const char *const TITLE_ITEM[4] = {"PLAY", "DECK", "OPTIONS", "STATS"};
enum { RAIL_TOP = 30, RAIL_BOT = 110 };         // the felt shows between them

// The menu: CHBlackjack's button bar. The button under the cursor grows and
// takes its colour (PLAY light green, the others gold); the rest shrink
// back, PLAY to dark green, the others to grey.
static int16_t buttonQ4[4];          // widths, Q4, easing to their targets

static void titleButtons() {
    static const uint8_t BASE[4] = {21, 21, 31, 25};
    int16_t w[4];
    int total = 0;
    for (uint8_t i = 0; i < 4; i++) {
        int16_t want = (int16_t)((BASE[i] + (i == sel ? 22 : 0)) << 4);
        buttonQ4[i] = (int16_t)(buttonQ4[i] + (want - buttonQ4[i]) / 3);
        if (buttonQ4[i] - want < 8 && want - buttonQ4[i] < 8) buttonQ4[i] = want;
        w[i] = (int16_t)((buttonQ4[i] + 8) >> 4);
        total += w[i];
    }
    int x = (128 - total - 6) / 2;
    for (uint8_t i = 0; i < 4; i++) {
        bool on = i == sel;
        int grown = w[i] - BASE[i] > 11;                            // more than half way out: taller too
        int y = RAIL_BOT + 4 - grown, h = 11 + 2 * grown;
        uint8_t face = i == 0 ? (on ? FELT_LT : FELT_DK) : (on ? GOLD : SILVER);
        fillRound(x, y, w[i], h, 3, face);
        gfx_hline(x + 2, y + h - 2, w[i] - 4, i == 0 ? (on ? FELT : INK) : (on ? WOOD : NAVY));
        roundRect(x, y, w[i], h, 3, on ? FX_B : INK);
        int tw = text35Width(TITLE_ITEM[i]);
        text35(x + w[i] / 2 - tw / 2, y + h / 2 - 3, TITLE_ITEM[i], i == 0 && !on ? FELT_LT : (i && !on ? WHITE : INK));
        x += w[i] + 2;
    }
}

// Calm, like koi: each sinks slowly, swaying from side to side, and turns
// over now and then to show the back of the deck you picked. Now and then
// a meteor: one card streaks across with a rainbow trail.
struct Faller {
    int16_t x16, y16;                // Q4
    int8_t vx, vy, spin;             // Q4 a frame; spin in Q4 angle units (256 a turn)
    uint8_t swaySpd, amp, card;
    uint16_t ang, sway;              // the turn and the sway's phase, Q4
};
static Faller fallers[10], meteor;
static bool meteorOn;
static uint8_t rail[(RAIL_TOP + 1) * GFX_FB_STRIDE];   // the top rail's pixels
static bool railKept;
static uint16_t meteorIn;            // frames until the next

static void dropFaller(Faller &f, int y) {
    f.x16 = (int16_t)(fx::rndRange(2, 110) << 4);
    f.y16 = (int16_t)(y << 4);
    f.vx = (int8_t)fx::rndRange(-1, 2);
    f.vy = (int8_t)fx::rndRange(4, 10);
    f.card = (uint8_t)(fx::rnd() % 52);
    f.ang = (uint16_t)fx::rnd();
    f.sway = (uint16_t)fx::rnd();
    int spin = fx::rndRange(6, 15);                                // a turn in 5-10 s
    f.spin = (int8_t)(fx::rnd() & 1 ? spin : -spin);
    f.swaySpd = (uint8_t)fx::rndRange(10, 18);                     // a sway in 4-7 s
    f.amp = (uint8_t)fx::rndRange(3, 9);
}

static void titleCards() {
    for (auto &f : fallers) dropFaller(f, fx::rndRange(8, RAIL_BOT));
    meteorOn = false;
    meteorIn = 120;
    memset(buttonQ4, 0, sizeof buttonQ4);
    railKept = false;
}

static void moveFaller(Faller &f) {
    f.x16 = (int16_t)(f.x16 + f.vx);
    f.y16 = (int16_t)(f.y16 + f.vy);
    f.ang = (uint16_t)(f.ang + f.spin);
    f.sway = (uint16_t)(f.sway + f.swaySpd);
}

static int fallerX(const Faller &f) { return (f.x16 >> 4) + ((fx::isin(f.sway >> 4) * f.amp) >> 8); }

static void drawFaller(const Faller &f) {
    // Flat for most of the turn (a card edge-on shows nothing), quick through the flip.
    int xs = fx::isin((f.ang >> 4) + 64) * 3;
    bool up = xs >= 0;
    if (xs < 0) xs = -xs;
    int w = xs >= 256 ? art::SW : (art::SW * xs) >> 8;
    art::small(fallerX(f), f.y16 >> 4, f.card, up, w < 2 ? 2 : w);
}

static void titleUpdate() {
    for (auto &f : fallers) {
        moveFaller(f);
        if (f.y16 > (RAIL_BOT + 2) << 4) dropFaller(f, fx::rndRange(0, 6));      // behind the top rail
    }
    if (meteorOn) {
        moveFaller(meteor);
        int x = fallerX(meteor) + 8, y = (meteor.y16 >> 4) + 11;
        for (int k = 0; k < 2; k++)
            fx::spawn(k ? fx::STAR : fx::SPARK, x + fx::rndRange(-5, 6), y + fx::rndRange(-6, 7),
                      fx::rndRange(-8, 9) - meteor.vx / 4, fx::rndRange(-12, 3), (uint8_t)fx::rndRange(18, 34),
                      fx::HUES[(t / 2 + k) % 5]);
        if (y > 140 || x < -30 || x > 158) { meteorOn = false; meteorIn = (uint16_t)fx::rndRange(150, 400); }
    } else if (!--meteorIn) {
        bool left = fx::rnd() & 1;
        dropFaller(meteor, -24);
        meteor.x16 = (int16_t)((left ? fx::rndRange(-10, 40) : fx::rndRange(70, 120)) << 4);
        meteor.vx = (int8_t)(left ? fx::rndRange(8, 17) : -fx::rndRange(8, 17));
        meteor.vy = (int8_t)fx::rndRange(30, 42);
        meteor.spin = (int8_t)fx::rndRange(40, 70);
        meteorOn = true;
    }
    fx::update();

    uint8_t p = rpgame.justPressedMask();
    if (rpgame.repeat(LEFT_BUTTON) || rpgame.repeat(UP_BUTTON)) { sel = (uint8_t)((sel + 3) & 3); audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(RIGHT_BUTTON) || rpgame.repeat(DOWN_BUTTON)) { sel = (uint8_t)((sel + 1) & 3); audio::sfx(Sfx::Cursor); }
    if (!(p & A_BUTTON)) return;
    audio::sfx(Sfx::Select);
    back = Scr::Title;
    switch (sel) {
        case 0:
            if (game.live) { stage::bank = stats.bank; stage::resume(); }
            else newDeal();
            go(Scr::Play);
            break;
        case 1: go(Scr::Deck); break;
        case 2: go(Scr::Options); break;
        default: go(Scr::Stats); break;
    }
}

static void titleRender(uint32_t frame) {
    // The felt and the cards on it; the rails go over them.
    gfx_fillRect(0, RAIL_TOP, 128, RAIL_BOT - RAIL_TOP, FELT);
    art::clock = (uint8_t)(frame >> 4);
    for (auto &f : fallers) drawFaller(f);
    if (meteorOn) drawFaller(meteor);
    fx::drawParticles();
    // The outlined lettering is the costliest thing in the game (~5 ms), so
    // the top rail is drawn once and kept: the cards pass under a copy.
    if (railKept) {
        memcpy(gfx_fb, rail, sizeof rail);
    } else {
        gfx_fillRect(0, 0, 128, RAIL_TOP, INK);
        gfx_hline(0, RAIL_TOP, 128, GOLD);
        // The top rows are FX_B, so the palette makes it shimmer.
        Mask m = maskBegin(LOGO_W, LOGO_H);
        maskBlit1(m, LOGO, LOGO_W, LOGO_H, 1);
        uint8_t ramp[LOGO_H];
        for (int i = 0; i < LOGO_H; i++) ramp[i] = i < 5 ? FX_B : (i < 14 ? GOLD : WOOD);
        maskDraw(m, 64 - LOGO_W / 2, 5, 0, INK, WINE, ramp);
        memcpy(rail, gfx_fb, sizeof rail);
        railKept = true;
    }
    gfx_fillRect(0, RAIL_BOT, 128, 128 - RAIL_BOT, NAVY);
    gfx_hline(0, RAIL_BOT, 128, GOLD);
    titleButtons();
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------
enum PauseItem : uint8_t { P_RESUME, P_DEAL, P_DECK, P_OPTIONS, P_QUIT, PAUSE_N };
static const char *const PAUSE_ITEM[PAUSE_N] = {"RESUME", "NEW DEAL", "DECK", "OPTIONS", "QUIT"};

static void playUpdate() {
    uint8_t pressed = rpgame.justPressedMask(), rep = 0;
    for (uint8_t b = UP_BUTTON; b <= RIGHT_BUTTON; b <<= 1) if (rpgame.repeat(b)) rep |= b;
    if (paused) {
        bool start = pressed & START_BUTTON;
        if (menuNav(PAUSE_N) || start) {
            audio::sfx(Sfx::Select);
            paused = false;
            if (!start) {
                back = Scr::Play;
                if (sel == P_DEAL) newDeal();
                if (sel == P_DECK) go(Scr::Deck);
                if (sel == P_OPTIONS) go(Scr::Options);
                if (sel == P_QUIT) go(Scr::Title);
            }
        } else if (pressed & B_BUTTON) paused = false;
        pressed = rep = 0;
    } else if ((pressed & START_BUTTON) && stage::state() == stage::PLAY) {
        paused = true;
        sel = 0;
        audio::sfx(Sfx::Select);
        persist();                               // pausing is what saves the game
        pressed = rep = 0;
    }
    bool live = game.live;
    stage::update(game, pressed, rep, paused || fadeOut);
    if (live && !game.live) won();
    if (stage::state() == stage::DONE) {
        if (doneT < 255) doneT++;
        if (doneT > 20 && (pressed & A_BUTTON)) { audio::sfx(Sfx::Select); newDeal(); }
        if (doneT > 20 && (pressed & B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
    }
}

static void playRender(uint32_t frame) {
    stage::render(game, frame);
    char b[32], *p;
    if (paused) {
        dither(0, 0, 128, 120, INK, 0);
        panel(24, 72);
        for (uint8_t i = 0; i < PAUSE_N; i++) {
            int y = 29 + i * 13;
            if (i == sel) fillRound(16, y - 2, 96, 13, 3, INK);
            int w = text35x2Width(PAUSE_ITEM[i]);
            text35x2(64 - w / 2, y, PAUSE_ITEM[i], i == sel ? FX_B : WHITE);
        }
    } else if (stage::state() == stage::DONE && doneT > 8) {
        panel(32, 60);
        title35("YOU WIN!", 37, 3, FX_B, GOLD, WOOD, WINE, 13);
        if (game.scoring == VEGAS) fmtMoney(fmtStr(b, "BANK "), stats.bank);
        else if (game.scoring == STANDARD) fmtInt(fmtStr(b, "SCORE "), game.score);
        else fmtInt(fmtStr(b, "MOVES "), game.moves);
        centred35(60, b, WHITE);
        p = fmtTime(fmtStr(b, "TIME "), game.secs);
        if (bonus) fmtInt(fmtStr(p, "   BONUS "), bonus);
        centred35(68, b, CYAN);
        centred35(81, "A: DEAL AGAIN   B: MENU", (frame & 32) ? GOLD : SILVER);
    }
}

// ---------------------------------------------------------------------------
// The deck: pick a card back, as in the Windows game's dialog.
// ---------------------------------------------------------------------------
static void leave() {
    audio::sfx(Sfx::Select);
    applyOptions();
    if (back != Scr::Play) persist();
    go(back);
}

static void deckUpdate() {
    uint8_t b = opt.back;
    if (rpgame.repeat(LEFT_BUTTON)) b = (uint8_t)((b & 12) | ((b + 3) & 3));
    if (rpgame.repeat(RIGHT_BUTTON)) b = (uint8_t)((b & 12) | ((b + 1) & 3));
    if (rpgame.repeat(UP_BUTTON)) b = (uint8_t)((b + art::BACKS - 4) % art::BACKS);
    if (rpgame.repeat(DOWN_BUTTON)) b = (uint8_t)((b + 4) % art::BACKS);
    if (b != opt.back) { opt.back = b; audio::sfx(Sfx::Flip); }
    if (rpgame.justPressed(A_BUTTON) || rpgame.justPressed(B_BUTTON)) leave();
}

static void deckRender(uint32_t frame) {
    feltBackdrop();
    title35("DECK", 6, 3, FX_B, GOLD, WOOD, WINE, 13);
    art::clock = (uint8_t)(frame >> 4);
    for (uint8_t i = 0; i < art::BACKS; i++) {
        int x = 16 + (i & 3) * 26, y = 28 + (i >> 2) * 27;
        if (i == opt.back) roundRect(x - 2, y - 2, art::SW + 4, art::SH + 4, 3, (frame & 16) ? FX_B : GOLD);
        art::back(x, y, art::SW, i);
    }
    const char *n = art::BACK_NAME[opt.back];
    text35x2(64 - text35x2Width(n) / 2, 109, n, GOLD);
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
enum Opt : uint8_t { O_DRAW, O_SCORING, O_TIMED, O_SOUND, O_SUITS, O_BACK, OPT_COUNT };
static const char *const OPT_TEXT[OPT_COUNT] = {
    "DRAW|THREE|ONE", "SCORING|STANDARD|VEGAS|NONE", "TIMED GAME|ON|OFF", "SOUND|ON|OFF",
    "SUITS|2 COLOUR|4 COLOUR", "BACK",
};
static uint8_t &optByte(uint8_t i) { return ((uint8_t *)&opt)[i]; }

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
    if (rpgame.repeat(UP_BUTTON)) { sel = (uint8_t)((sel + OPT_COUNT - 1) % OPT_COUNT); audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON)) { sel = (uint8_t)((sel + 1) % OPT_COUNT); audio::sfx(Sfx::Cursor); }
    int d = rpgame.justPressed(RIGHT_BUTTON) ? 1 : (rpgame.justPressed(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.justPressed(A_BUTTON) && sel != O_BACK) d = 1;
    if (d && sel != O_BACK) {
        char tmp[16];
        uint8_t n = (uint8_t)(optField(OPT_TEXT[sel], 0, tmp) - 1);
        uint8_t &f = optByte(sel);
        f = (uint8_t)((f + n + d) % n);
        applyOptions();
        audio::sfx(Sfx::Coin);
    }
    if ((rpgame.justPressed(A_BUTTON) && sel == O_BACK) || rpgame.justPressed(B_BUTTON)) leave();
}

static void optionsRender(uint32_t frame) {
    feltBackdrop();
    title35("OPTIONS", 7, 3, FX_B, GOLD, WOOD, WINE, 13);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 31 + i * 12;
        char label[16], value[16];
        optField(OPT_TEXT[i], 0, label);
        if (i == sel) {
            fillRound(8, y - 2, 112, 12, 3, NAVY);
            roundRect(8, y - 2, 112, 12, 3, (frame & 16) ? FX_B : GOLD);
        }
        if (i == O_BACK) { centred35(y + 2, label, i == sel ? GOLD : WHITE); continue; }
        text35(14, y + 2, label, i == sel ? GOLD : WHITE);
        optField(OPT_TEXT[i], (uint8_t)(optByte(i) + 1), value);
        text35(114 - text35Width(value), y + 2, value, i == sel ? WHITE : FELT_LT);
    }
    centred35(107, "THE RULES CHANGE WITH", SILVER);
    centred35(114, "THE NEXT DEAL", SILVER);
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
static uint8_t resetHold;

static void statsUpdate() {
    resetHold = rpgame.pressed(SELECT_BUTTON) ? (uint8_t)(resetHold < 255 ? resetHold + 1 : 255) : 0;
    if (resetHold == 90) {
        memset(&stats, 0, sizeof stats);
        persist();
        audio::sfx(Sfx::Bust);
    }
    if (rpgame.justPressed(A_BUTTON) || rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void statsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("STATS", 6, 3, WHITE, CYAN, BLUE, NAVY, 13);
    static const char *const ROW[7] = {"GAMES PLAYED", "GAMES WON", "STREAK", "BEST STREAK", "BEST SCORE",
                                       "BEST TIME", "VEGAS BANK"};
    char b[16];
    for (uint8_t r = 0; r < 7; r++) {
        int y = 29 + r * 10;
        char *p = b;
        switch (r) {
            case 0: fmtInt(b, (int32_t)stats.played); break;
            case 1:
                p = fmtInt(b, (int32_t)stats.won);
                if (stats.played) fmtStr(fmtInt(fmtStr(p, " ("), (int32_t)(stats.won * 100 / stats.played)), "%)");
                break;
            case 2: fmtInt(b, stats.streak); break;
            case 3: fmtInt(b, stats.bestStreak); break;
            case 4: fmtInt(b, stats.bestScore); break;
            case 5: if (stats.bestTime) fmtTime(b, stats.bestTime); else fmtStr(b, "-"); break;
            default: fmtMoney(b, stats.bank); break;
        }
        text35(12, y, ROW[r], WHITE);
        text35(116 - text35Width(b), y, b, r == 6 ? (stats.bank < 0 ? SKIN : GOLD) : CYAN);
    }
    centred35(104, resetHold ? "KEEP HOLDING TO RESET" : "HOLD SELECT TO RESET", SILVER);
    centred35(113, (!CHSO_LEAN && save::available()) ? "SAVED IN FLASH" : "SAVING UNAVAILABLE", FELT_LT);
}

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   G <seed>           deal that game (with the options as they are)
//   W <cards>          all but that many cards already on the foundations
//   O <i> <value>      set option byte i
//   C <pile> <cards>   put the glove on a pile (0 stock, 1 waste, 2-5 foundations, 6-12 columns)
//   $ <amount>         set the Vegas bank
//   J <T|P|D|O|S>      jump to a screen
//   H                  the table in numbers
//   X, Z, Q            simulator only: the tallest column, a power cycle, timing calibration
static bool debugHook(char cmd, const char *args) {
    switch (cmd) {
        case 'G':
            game.live = 0;
            game.deal(dbg::parseNum(args, 10), opt);
            stats.played++;
            stage::bank = stats.bank;
            stage::deal(game);
            doneT = 0;
            enter(Scr::Play);
            return true;
        case 'W':
            game.nearWin((uint8_t)dbg::parseNum(args, 10));
            stage::resume();
            return true;
        case 'O': {
            uint8_t i = (uint8_t)dbg::parseNum(args, 10);
            if (i >= sizeof opt) return false;
            optByte(i) = (uint8_t)dbg::parseNum(args, 10);
            applyOptions();
            return true;
        }
        case 'C': {
            uint8_t pile = (uint8_t)dbg::parseNum(args, 10);
            stage::point(pile, (uint8_t)dbg::parseNum(args, 10));
            return true;
        }
        case '$': stats.bank = (int32_t)dbg::parseNum(args, 10); return true;
        case 'J': {
            static const char K[] = "TPDOS";
            const char *q = strchr(K, args[0]);
            if (!q) return false;
            enter((Scr)(q - K));
            return true;
        }
        case 'H': {
            char b[160], *p = fmtStr(b, "TABLE state=");
            p = fmtInt(p, stage::state());
            p = fmtInt(fmtStr(p, " live="), game.live);
            p = fmtInt(fmtStr(p, " score="), game.score);
            p = fmtInt(fmtStr(p, " moves="), game.moves);
            p = fmtInt(fmtStr(p, " stock="), game.nStock);
            p = fmtInt(fmtStr(p, " waste="), game.nWaste);
            p = fmtInt(fmtStr(p, " up="), game.found[0] + game.found[1] + game.found[2] + game.found[3]);
            p = fmtInt(fmtStr(p, " won="), (int32_t)stats.won);
            p = fmtInt(fmtStr(p, " cur="), stage::cursor());
            p = fmtInt(fmtStr(p, " held="), stage::holding());
            p = fmtInt(fmtStr(p, " secs="), game.secs);
            p = fmtStr(p, " cols=");
            for (uint8_t i = 0; i < 7; i++) { if (i) *p++ = '/'; p = fmtInt(p, game.nTab[i]); }
            fmtStr(p, "\n");
            dbg::print(b);
            return true;
        }
#ifdef CHSIM
        case 'X':                                // the tallest column there can be: six down, king to ace up
            game.nearWin(52);
            memmove(game.tab[0] + 6, game.tab[0], 13);
            game.nTab[0] = 19;
            game.down[0] = 6;
            stage::resume();
            return true;
        case 'Z': begin(); return true;          // "power cycle": reload the save, back to the title
#endif
    }
    return false;
}
#endif

// ---------------------------------------------------------------------------
void begin() {
    memset(&opt, 0, sizeof opt);
    memset(&stats, 0, sizeof stats);
    game.live = 0;
    save::load(opt, stats, game);
    if (opt.back >= art::BACKS) opt.back = 0;
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, false);   // on once applyOptions() reads the option
    applyOptions();
#if CHGAME_DEBUG
    dbg::hook = debugHook;
#endif
    enter(Scr::Title);
}

void update() {
    t++;
    if (fadeOut) {
        pal::setFade((uint8_t)((fadeOut - 1) * 2));
        if (--fadeOut == 0) enter(pending);
        return;
    }
    if (fadeIn) { fadeIn--; pal::setFade((uint8_t)(16 - fadeIn * 2)); }
    switch (cur) {
        case Scr::Title:   titleUpdate(); break;
        case Scr::Play:    playUpdate(); break;
        case Scr::Deck:    deckUpdate(); break;
        case Scr::Options: optionsUpdate(); break;
        case Scr::Stats:   statsUpdate(); break;
    }
}

void render(uint32_t frame) {
    switch (cur) {
        case Scr::Title:   titleRender(frame); break;
        case Scr::Play:    playRender(frame); break;
        case Scr::Deck:    deckRender(frame); break;
        case Scr::Options: optionsRender(frame); break;
        case Scr::Stats:   statsRender(frame); break;
    }
}

}  // namespace screens
