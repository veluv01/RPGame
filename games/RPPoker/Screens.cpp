// The screens (Screens.h) and the flow between them: title (with the attract
// demo), lobby, the table and its pause menu, options, statistics, the
// endings; saving; and the debug protocol's hooks.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library)
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "config.h"
#include "Screens.h"
#include "Fx.h"
#include "Sounds.h"
#include "Table.h"
#include "Hand.h"
#include "CardArt.h"
#include "src/assets/Assets.h"
#include "Bar.h"
#include "Stage.h"
#include "Save.h"
#ifdef CHSIM
#include <sim.h>
#endif

namespace screens {

enum class Scr : uint8_t { Title, Lobby, Play, Options, Stats, Won, Broke };
static Scr cur = Scr::Title, pending = Scr::Title, optBack = Scr::Title;
static uint16_t t;                   // frames on this screen
static uint16_t idle;                // frames since a button on the title (the demo starts at 900)
static uint8_t fadeOut, fadeIn;
static uint8_t sel;                  // menu cursor
static Table table;
static int32_t buyIn;                // chosen in the lobby
static bool seeded;

enum Overlay : uint8_t { NONE, PAUSE, RANKS };
static Overlay overlay;
#if CHGAME_DEBUG
static uint32_t thinkAt, thinkLast, thinkMax;       // a CPU's think, wall time (debug W)
#endif

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
    overlay = NONE;
    fx::clear();
    pal::setMode(pal::CASINO);
    pal::setDesaturate(0);
    stage::invalidate();
    if (s == Scr::Title) { audio::sfx(Sfx::Title); idle = 0; }
    if (s == Scr::Won) { audio::sfx(Sfx::BigWin); audio::led(audio::LED_PARTY); }
    if (s == Scr::Broke) audio::sfx(Sfx::Bust);
}

static void persist() {
    gfx_wait();                      // save builds its page in the chunk scratch
    save::store(table.opt, table.stats, table.wealth());
    table.wantSave = false;
}

static void applyOptions() {
    audio::setOn(table.opt.sound == 0);
    pal::setTheme(table.opt.felt);
    art::fourColour = table.opt.deck != 0;
}

static uint32_t seedNow() { return micros() * 2654435761u ^ rpgame.frameCount; }

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
static void centred2(int y, const char *s, uint8_t c) { text35x2(64 - text35x2Width(s) / 2, y, s, c); }

static void menuItem(int y, const char *s, bool on, uint32_t frame) {
    int w = text35x2Width(s);
    if (on) {
        fillRound(64 - w / 2 - 6, y - 3, w + 12, 15, 3, NAVY);
        roundRect(64 - w / 2 - 6, y - 3, w + 12, 15, 3, (frame & 16) ? FX_B : GOLD);
    }
    centred2(y, s, on ? GOLD : WHITE);
}

static bool menuNav(uint8_t n) {
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel + 1 < n) { sel++; audio::sfx(Sfx::Cursor); }
    return rpgame.justPressed(A_BUTTON);
}

static void arrows(int y, int w, bool on, uint32_t frame) {
    if (!on) return;
    int bob = (frame >> 3) & 1;
    text35(64 - w / 2 - 9 - bob, y + 1, "<", GOLD);
    text35(64 + w / 2 + 6 + bob, y + 1, ">", GOLD);
}

// A lobby choice: boxed while chosen, with arrows to change it.
static void choice(int y, const char *s, bool on, uint32_t frame) {
    int w = text35x2Width(s);
    if (on) fillRound(64 - w / 2 - 5, y - 3, w + 10, 15, 3, NAVY);
    centred2(y, s, on ? GOLD : WHITE);
    arrows(y + 2, w, on, frame);
}

static void panel(int y, int h) {
    fillRound(14, y, 100, h, 3, NAVY);
    roundRect(14, y, 100, h, 3, GOLD);
}

static void stakes(char *p, uint8_t game, uint8_t level) {
    int32_t lo = VARIANTS[game].limit == FIXED_LIMIT ? 2 * UNIT[level] : UNIT[level];
    p = fmtMoney(p, lo);
    *p++ = '/';
    fmtMoney(p, 2 * lo);
}

// ---------------------------------------------------------------------------
// Title: a royal flush fans out under the lettering.
// ---------------------------------------------------------------------------
static const char *const TITLE_ITEM[3] = {"PLAY", "OPTIONS", "STATS"};

static void startDemo() {
    // Attract mode: the CPU plays your seat at a random game.
    uint8_t g = (uint8_t)(fx::rnd() % GAMES);
    Options keep = table.opt;
    Stats keepS = table.stats;
    int32_t purse = table.purse;
    table.demo = true;
    table.purse = 1000;
    table.sitDown(g, ROOKIE, 200, fx::rnd());
    table.opt = keep;
    table.stats = keepS;
    table.purse = purse;                   // the demo never touches your money
    stage::reset();
    bar::reset();
    go(Scr::Play);
}

static void titleUpdate() {
    if (menuNav(3)) {
        audio::sfx(Sfx::Select);
        if (!seeded) { table.seed(seedNow()); seeded = true; }
        if (sel == 0) go(Scr::Lobby);
        else if (sel == 1) { optBack = Scr::Title; go(Scr::Options); }
        else go(Scr::Stats);
    }
    idle = rpgame.anyPressed(0xFF) ? 0 : (uint16_t)(idle + 1);
    // The royal flush: each card kicks up a puff of felt as it lands, and
    // the ace, turning up last, completes it with a burst of stars.
    for (int i = 0; i < 5; i++) {
        int d = (int)t - 10 - i * 6;
        if (d == 16) fx::burst(fx::DUST, 24 + i * 20, 80, 8, 22, FELT_LT);
        if (d == 20 && i == 4) {
            fx::burst(fx::STAR, 104, 64, 14, 48, FX_A);
            fx::burst(fx::SPARK, 64, 64, 16, 56, GOLD);
        }
    }
    if (idle > 900 && !fadeOut) startDemo();
}

static void titleRender(uint32_t frame) {
    feltBackdrop();
    // "Poker" in the BlackJack logo's lettering, at twice its size; the top
    // rows are FX_B, so the palette makes it shimmer.
    {
        Mask m = maskBegin(LOGO_W * 2, LOGO_H * 2);
        maskBlit1(m, LOGO, LOGO_W, LOGO_H, 2);
        uint8_t ramp[LOGO_H * 2];
        for (int i = 0; i < LOGO_H * 2; i++) ramp[i] = i < 4 ? FX_B : (i < 23 ? GOLD : WOOD);
        maskDraw(m, 64 - LOGO_W, 8, 0, INK, WINE, ramp);
    }
    centred35(41, "HOLD'EM - DRAW - OMAHA - STUD", CYAN);
    // A royal flush in spades, dropped in one card at a time, each turning
    // over as it lands. They overlap by 2 px: the court portraits leave
    // their last column empty, so every face shows whole.
    static const uint8_t ROYAL[5] = {makeCard(RT, SPADES), makeCard(RJ, SPADES), makeCard(RQ, SPADES),
                                     makeCard(RK, SPADES), makeCard(RA, SPADES)};
    for (int i = 0; i < 5; i++) {
        int d = (int)t - 10 - i * 6;
        if (d < 0) continue;
        int e = fx::ease(fx::OUT_BACK, d, 16);
        int y = 50 - 40 + ((40 * e) >> 8) + (i == 2 ? -2 : (i == 1 || i == 3) ? 0 : 2);
        int f = d - 16, w = art::CARD_W;                 // the flip: squash, then open face up
        if (f >= 0 && f < 8) w = f < 4 ? art::CARD_W * (4 - f) / 5 : art::CARD_W * (f - 3) / 5;
        if (w < 2) w = 2;
        art::card(13 + i * 20, y, ROYAL[i], f >= 4, w, true);
    }
    fx::drawParticles();
    for (uint8_t i = 0; i < 3; i++) menuItem(86 + i * 13, TITLE_ITEM[i], i == sel, frame);
}

// ---------------------------------------------------------------------------
// Lobby: the game, the table (stakes = the CPUs' skill) and the buy-in.
// ---------------------------------------------------------------------------
static bool canSit(uint8_t level) { return table.purse >= minBuyIn(level); }

static void clampBuyIn() {
    int32_t lo = minBuyIn(table.opt.level), hi = maxBuyIn(table.opt.level);
    if (hi > table.purse) hi = table.purse;
    if (buyIn > hi) buyIn = hi;
    if (buyIn < lo) buyIn = lo;
}

static void lobbyUpdate() {
    int d = rpgame.repeat(RIGHT_BUTTON) ? 1 : (rpgame.repeat(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel < 3) { sel++; audio::sfx(Sfx::Cursor); }
    Options &o = table.opt;
    if (d && sel == 0) {
        o.game = (uint8_t)((o.game + GAMES + d) % GAMES);
        audio::sfx(Sfx::Coin);
    }
    if (d && sel == 1) {
        o.level = (uint8_t)((o.level + LEVELS + d) % LEVELS);
        buyIn = maxBuyIn(o.level) / 2;
        audio::sfx(Sfx::Coin);
    }
    if (d && sel == 2) {
        buyIn += d * 10 * UNIT[o.level];
        audio::sfx(Sfx::Chip);
    }
    clampBuyIn();
    if (rpgame.justPressed(A_BUTTON)) {
        if (sel < 3) { sel++; audio::sfx(Sfx::Cursor); }
        else if (!canSit(o.level)) audio::sfx(Sfx::Deny);
        else {
            audio::sfx(Sfx::Select);
            table.demo = false;
            table.sitDown(o.game, o.level, buyIn, seedNow() ^ table.purse);
            stage::reset();
            bar::reset();
            persist();
            go(Scr::Play);
        }
    }
    if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void lobbyRender(uint32_t frame) {
    feltBackdrop();
    const Options &o = table.opt;
    char buf[32], *p;
    fmtMoney(fmtStr(buf, "PURSE "), table.purse);
    centred35(7, buf, GOLD);
    choice(18, VARIANTS[o.game].name, sel == 0, frame);
    centred35(33, VARIANTS[o.game].line, FELT_LT);
    choice(45, LEVEL_NAME[o.level], sel == 1, frame);
    for (int i = 0; i <= o.level; i++) text35(64 - o.level * 4 + i * 8 - 1, 59, "*", FX_B);
    p = buf;
    stakes(p, o.game, o.level);
    p = fmtStr(p + strlen(p), " - ");
    if (canSit(o.level)) fmtStr(p, LEVEL_LINE[o.level]);
    else fmtMoney(fmtStr(p, "NEED "), minBuyIn(o.level));
    centred35(66, buf, canSit(o.level) ? FELT_LT : RED);
    fmtMoney(fmtStr(buf, "BUY IN "), canSit(o.level) ? buyIn : minBuyIn(o.level));
    choice(78, buf, sel == 2, frame);
    p = fmtMoney(fmtStr(buf, "MIN "), minBuyIn(o.level));
    fmtMoney(fmtStr(p, "  MAX "), maxBuyIn(o.level));
    centred35(93, buf, FELT_LT);
    menuItem(107, "SIT DOWN", sel == 3, frame);
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------
enum PauseItem : uint8_t { P_RESUME, P_OPTIONS, P_RANKS, P_LEAVE, PAUSE_N };
static const char *const PAUSE_ITEM[PAUSE_N] = {"RESUME", "OPTIONS", "HAND RANKS", "LEAVE TABLE"};

static void playUpdate(bool firstTick) {
    if (table.demo) {
        if (rpgame.justPressedMask()) {
            table.demo = false;
            table.seats[YOU].stack = 0;             // the demo's chips were never yours
            table.phase = Phase::Idle;
            go(Scr::Title);
        }
    } else if (overlay == PAUSE) {
        bool start = rpgame.justPressed(START_BUTTON);
        if (menuNav(PAUSE_N) || start) {
            audio::sfx(Sfx::Select);
            overlay = NONE;
            if (!start) {
                if (sel == P_OPTIONS) { optBack = Scr::Play; go(Scr::Options); }
                if (sel == P_RANKS) overlay = RANKS;
                if (sel == P_LEAVE) table.leave();
            }
        } else if (rpgame.justPressed(B_BUTTON)) overlay = NONE;
        stage::update(table, rpgame.frameCount);
        return;                                  // the table waits while paused
    } else if (overlay == RANKS) {
        if (rpgame.justPressedMask()) { overlay = NONE; audio::sfx(Sfx::Select); }
        stage::update(table, rpgame.frameCount);
        return;
    } else if (rpgame.justPressed(START_BUTTON)) {
        overlay = PAUSE; sel = 0; audio::sfx(Sfx::Select);
    }
    uint8_t pressed = overlay ? 0 : rpgame.justPressedMask(), rep = 0;
    static const uint8_t DIRS = UP_BUTTON | DOWN_BUTTON | LEFT_BUTTON | RIGHT_BUTTON;
    for (uint8_t b = 1; b; b <<= 1) if ((DIRS & b) && rpgame.repeat(b)) rep |= b;
    if (overlay) rep = 0;
#if CHGAME_DEBUG
    bool wasThinking = table.phase == Phase::Think;
#endif
    table.update(pressed, rep, stage::busy(), firstTick);
#if CHGAME_DEBUG
    bool thinking = table.phase == Phase::Think;
    if (thinking && !wasThinking) thinkAt = millis();
    if (!thinking && wasThinking) {
        thinkLast = millis() - thinkAt;
        if (thinkLast > thinkMax) thinkMax = thinkLast;
    }
#endif
    stage::onEvents(table);
    stage::update(table, rpgame.frameCount);
    if (table.wantSave && !table.demo) persist();
    switch (table.phase) {
        case Phase::Leave: go(Scr::Lobby); break;
        case Phase::Won:   go(Scr::Won); break;
        case Phase::Broke: go(Scr::Broke); break;
        default: break;
    }
}

static void ranksRender() {
    fillRound(4, 14, 120, 98, 3, NAVY);
    roundRect(4, 14, 120, 98, 3, GOLD);
    centred35(18, "HAND RANKS", GOLD);
    static const char *const R[10] = {"ROYAL FLUSH", "STRAIGHT FLUSH", "FOUR OF A KIND", "FULL HOUSE", "FLUSH",
                                      "STRAIGHT", "THREE OF A KIND", "TWO PAIR", "PAIR", "HIGH CARD"};
    static const char *const EX[10] = {"AKQJT SUITED", "98765 SUITED", "7777", "QQQ 55", "5 SUITED",
                                       "65432", "888", "KK 33", "JJ", "A HIGH"};
    for (int i = 0; i < 10; i++) {
        text35(8, 27 + i * 8, R[i], i < 2 ? FX_B : WHITE);
        text35(121 - text35Width(EX[i]), 27 + i * 8, EX[i], SILVER);
    }
}

static void playRender(uint32_t frame) {
    uint32_t ui = overlay | (sel << 4);
    if (overlay) ui ^= (frame >> 4) << 8;
    if (!stage::render(table, frame, ui)) return;
    if (table.demo && (frame & 32)) centred35(52, "DEMO", FX_A);
    if (overlay == PAUSE) {
        dither(0, 10, 128, 101, INK, 0);
        panel(28, 66);
        for (uint8_t i = 0; i < PAUSE_N; i++) {
            int y = 34 + i * 15;
            if (i == sel) fillRound(18, y - 3, 92, 15, 3, INK);
            centred2(y, PAUSE_ITEM[i], i == sel ? FX_B : WHITE);
        }
    } else if (overlay == RANKS) {
        ranksRender();
    }
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
enum Opt : uint8_t { O_SOUND, O_FELT, O_DECK, O_PACE, O_GOAL, O_HINTS, O_BACK, OPT_COUNT };
static const char *const OPT_TEXT[OPT_COUNT] = {
    "SOUND|ON|OFF", "FELT|GREEN|BLUE|RED|PURPLE", "DECK|2 COLOUR|4 COLOUR", "PACE|FUN|QUICK",
    "GOAL|$10000|$50000|ENDLESS", "HINTS|ON|OFF", "BACK",
};
static uint8_t &optByte(uint8_t i) { return ((uint8_t *)&table.opt)[i]; }

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
    if ((rpgame.justPressed(A_BUTTON) && sel == O_BACK) || rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        if (optBack != Scr::Play) persist();
        Scr back = optBack;
        go(back);
        if (back == Scr::Play) stage::invalidate();
    }
}

static void optionsRender(uint32_t frame) {
    feltBackdrop();
    title35("OPTIONS", 7, 3, FX_B, GOLD, WOOD, WINE, 13);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 29 + i * 12;
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
    centred35(116, "ART: PRESS PLAY ON TAPE", SILVER);
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
static uint8_t resetHold;

static void statsUpdate() {
    resetHold = rpgame.pressed(SELECT_BUTTON) ? (uint8_t)(resetHold < 255 ? resetHold + 1 : 255) : 0;
    if (resetHold == 90) {
        memset(&table.stats, 0, sizeof table.stats);
        persist();
        audio::sfx(Sfx::Bust);
    }
    if (rpgame.justPressed(A_BUTTON) || rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void statsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("STATS", 6, 3, WHITE, CYAN, BLUE, NAVY, 13);
    static const char *const COL[GAMES] = {"HOLD", "DRAW", "OMAHA", "STUD"};
    static const char *const ROW[5] = {"HANDS", "WON", "NET", "BIG POT", "BEST"};
    static const char *const BEST[hand::CATS + 1] = {"-", "HIGH", "PAIR", "2PAIR", "TRIPS", "STRT", "FLUSH",
                                                     "FULL", "QUADS", "SFLUSH"};
    char b[16];
    for (uint8_t g = 0; g < GAMES; g++) text35(52 + g * 21 - text35Width(COL[g]) / 2, 28, COL[g], GOLD);
    for (uint8_t r = 0; r < 5; r++) {
        int y = 37 + r * 9;
        text35(8, y, ROW[r], WHITE);
        for (uint8_t g = 0; g < GAMES; g++) {
            const GameStats &s = table.stats.g[g];
            switch (r) {
                case 0: fmtInt(b, (int32_t)s.hands); break;
                case 1: fmtInt(b, (int32_t)s.won); break;
                case 2: fmtShort(b, s.net); break;
                case 3: fmtShort(b, s.biggest); break;
                default: fmtStr(b, BEST[s.best < hand::CATS + 1 ? s.best : 0]); break;
            }
            text35(52 + g * 21 - text35Width(b) / 2, y, b, r == 2 ? (s.net < 0 ? RED : FELT_LT) : SILVER);
        }
    }
    char *p = fmtMoney(fmtStr(b, "PURSE "), table.purse);
    (void)p;
    centred35(85, b, GOLD);
    p = fmtInt(fmtStr(b, "BANKS BROKEN "), table.stats.banks);
    fmtInt(fmtStr(p, "  BROKE "), table.stats.broke);
    centred35(93, b, WHITE);
    fmtMoney(fmtStr(b, "BEST PURSE "), table.stats.bestPurse);
    centred35(101, b, SILVER);
    centred35(110, resetHold ? "KEEP HOLDING TO RESET" : "HOLD SELECT TO RESET", SILVER);
    centred35(118, (!CHPK_LEAN && save::available()) ? "SAVED IN FLASH" : "SAVING UNAVAILABLE", FELT_LT);
}

// ---------------------------------------------------------------------------
// The endings
// ---------------------------------------------------------------------------
static void endUpdate() {
    if (cur == Scr::Won && (t % 40) == 1) {
        fx::fountain(fx::COIN, 24 + (int)(fx::rnd() % 80), 120, 8);
        fx::fountain(fx::CONFETTI, 64, 120, 10);
    }
    if (cur == Scr::Broke) {
        pal::setDesaturate((uint8_t)(t < 96 ? t / 8 : 12));
        if (t & 1) fx::spawn(fx::RAIN, (int)(fx::rnd() % 128), 0, 0, 40, 60, SILVER);
    }
    if (t > 60 && rpgame.justPressed(A_BUTTON)) {
        audio::sfx(Sfx::Select);
        table.seats[YOU].stack = 0;                 // the table is over
        table.phase = Phase::Idle;
        table.newPurse();
        persist();
        go(Scr::Title);
    }
}

static void endRender(uint32_t frame) {
    char buf[24];
    if (cur == Scr::Won) {
        gfx_clear(NAVY);
        // Rings of light spreading out behind the lettering.
        for (int k = 0; k < 6; k++) {
            int r = (int)((frame / 2 + k * 12) % 72);
            roundRect(64 - r, 58 - r, 2 * r + 1, 2 * r + 1, 4, k & 1 ? BLUE : WINE);
        }
        title35("YOU BROKE", 10, 3, FX_B, GOLD, WOOD, WINE, 13);
        title35("THE BANK", 34, 3, FX_B, GOLD, WOOD, WINE, 13);
        fmtMoney(buf, table.wealth());
        title35(buf, 64, 3, WHITE, CYAN, BLUE, INK, 13);
    } else {
        gfx_clear(INK);
        dither(0, 70, 128, 58, NAVY, 0);
        title35("YOU ARE", 18, 3, WHITE, SILVER, BLUE, NAVY, 13);
        title35("BROKE", 42, 4, WHITE, SILVER, BLUE, NAVY, 17);
    }
    fx::drawParticles();
    if (t > 60 && (frame & 32)) centred35(110, "PRESS A: A NEW $500 PURSE", WHITE);
}

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   G <game> <level> <buyin> <seed>    sit down at a table
//   D <c1,c2,...>                      stack the next cards dealt (0..51 = rank*4+suit)
//   $ <amount>                         set the purse
//   J <T|L|O|S|W|B>                    jump to a screen
//   H                                  the table: phase, your turn, stacks
static bool debugHook(char cmd, const char *args) {
    switch (cmd) {
        case 'G': {
            uint8_t g = (uint8_t)dbg::parseNum(args, 10), lv = (uint8_t)dbg::parseNum(args, 10);
            int32_t in = (int32_t)dbg::parseNum(args, 10);
            uint32_t seed = dbg::parseNum(args, 10);
            if (g >= GAMES || lv >= LEVELS) return false;
            if (table.purse < in) table.purse = in;
            table.demo = false;
            table.opt.game = g; table.opt.level = lv;
            table.sitDown(g, lv, in, seed);
            stage::reset();
            bar::reset();
            enter(Scr::Play);
            return true;
        }
        case 'D': {
            uint8_t cards[24], n = 0;
            const char *p = args;
            while (*p && n < 24) cards[n++] = (uint8_t)dbg::parseNum(p, 10);
            table.stackDeck(cards, n);
            return true;
        }
        case '$': table.purse = (int32_t)dbg::parseNum(args, 10); return true;
        case 'W': {
            char b[48];
            char *p = fmtInt(fmtStr(b, "THINK last="), (int32_t)thinkLast);
            p = fmtInt(fmtStr(p, " max="), (int32_t)thinkMax);
            fmtStr(p, "\n");
            dbg::print(b);
            thinkMax = 0;
            return true;
        }
        case 'J': {
            static const char K[] = "TLOSWB";
            const char *q = strchr(K, args[0]);
            if (!q) return false;
            static const Scr S[] = {Scr::Title, Scr::Lobby, Scr::Options, Scr::Stats, Scr::Won, Scr::Broke};
            enter(S[q - K]);
            return true;
        }
#ifdef CHSIM
        case 'Z': begin(); return true;          // "power cycle": reload the save, back to the title
#endif
        case 'H': {
            char b[64], *p = fmtStr(b, "TABLE ");
            p = fmtInt(p, (int32_t)table.phase);
            p = fmtStr(p, table.phase == Phase::Human || table.phase == Phase::DrawHuman ? " 1" : " 0");
            for (uint8_t s = 0; s < SEATS; s++) { *p++ = ' '; p = fmtInt(p, table.seats[s].stack); }
            p = fmtInt(fmtStr(p, " button="), table.button);
            fmtStr(p, "\n");
            dbg::print(b);
            return true;
        }
    }
    return false;
}
#endif

// ---------------------------------------------------------------------------
void begin() {
    table.newPurse();
    save::load(table.opt, table.stats, table.purse);
    if (table.opt.game >= GAMES) table.opt.game = HOLDEM;
    if (table.opt.level >= LEVELS) table.opt.level = 0;
    if (table.purse < minBuyIn(ROOKIE)) table.newPurse();
    buyIn = maxBuyIn(table.opt.level) / 2;
    clampBuyIn();
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, true);
    applyOptions();
#if CHGAME_DEBUG
    dbg::hook = debugHook;
#endif
    enter(Scr::Title);
}

void update(bool firstTick) {
    t++;
    if (fadeOut) {
        pal::setFade((uint8_t)((fadeOut - 1) * 2));
        if (--fadeOut == 0) {
            if (pending == Scr::Lobby) clampBuyIn();
            enter(pending);
        }
        fx::update();
        return;
    }
    if (fadeIn) { fadeIn--; pal::setFade((uint8_t)(16 - fadeIn * 2)); }
    switch (cur) {
        case Scr::Title:   titleUpdate(); break;
        case Scr::Lobby:   lobbyUpdate(); break;
        case Scr::Play:    playUpdate(firstTick); return;         // the stage runs the effects
        case Scr::Options: optionsUpdate(); break;
        case Scr::Stats:   statsUpdate(); break;
        case Scr::Won: case Scr::Broke: endUpdate(); break;
    }
    fx::update();
}

void render(uint32_t frame) {
    switch (cur) {
        case Scr::Title:   titleRender(frame); break;
        case Scr::Lobby:   lobbyRender(frame); break;
        case Scr::Play:    playRender(frame); break;
        case Scr::Options: optionsRender(frame); break;
        case Scr::Stats:   statsRender(frame); break;
        case Scr::Won: case Scr::Broke: endRender(frame); break;
    }
}

}  // namespace screens
