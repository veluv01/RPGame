#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and Tile.cpp)
// The screens (Screens.h): title, setup, play with its pause, stuck and
// result panels, options; and the debug protocol's game commands.
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "config.h"
#include "Screens.h"
#include "Tile.h"
#include "Fx.h"
#include "Sounds.h"
#include "MahjongBoard.h"
#include "src/game/Layouts.h"
#include "Nav.h"
#include "Stage.h"
#include "Save.h"
#include "src/assets/Assets.h"
#ifdef CHSIM
#include <sim.h>
#endif

namespace screens {

enum class Scr : uint8_t { Title, Setup, Play, Options };
static Scr cur = Scr::Title, pending = Scr::Title;
static uint16_t t;                   // frames on this screen
static uint8_t fadeOut, fadeIn;
static uint8_t sel;                  // menu cursor
static Scr optBack = Scr::Title;

static Options opt;
static Stats stats;
static bool hasGame;                 // a saved game is waiting

// Play-screen overlays.
enum Overlay : uint8_t { NONE, PAUSE, STUCK, RESULT };
static Overlay overlay;
static bool newBest;                 // the table just cleared beat this layout's record

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------
static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

static void titleTiles();

static void enter(Scr s) {
    cur = s;
    stage::invalidate();
    t = 0;
    sel = 0;
    fadeIn = 8;
    if (s != Scr::Play) fx::clear();
    if (s == Scr::Title) {
        titleTiles();
        audio::sfx(Sfx::Title);
    }
    if (s == Scr::Setup) {
        board::load(opt.layout);     // for the picture of it
        sel = 1;
    }
}

static void persist(bool withGame) {
    gfx_wait();                      // save builds its page in the chunk scratch
    board::mark = stage::cursor();
    save::store(opt, stats, withGame);
    hasGame = withGame;
}

static void applyOptions() {
    audio::setOn(opt.sound != 0);
    pal::setTheme(opt.felt);
    stage::setFaces(opt.faces != 0);
    stage::setQuick(opt.speed != 0);
}

// ---------------------------------------------------------------------------
// Shared drawing (CHBlackjack's look)
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
    Mask m = maskBegin(w, h);                    // just the lettering's bytes
    maskText35(m, 0, 0, text, scale);
    uint8_t ramp[32];
    for (int i = 0; i < h + 2 && i < 32; i++) ramp[i] = i < scale ? top : (i < lowFrom ? mid : low);
    maskDraw(m, 64 - w / 2, y, 0, INK, shadow, ramp);
}

// Embossed: the text over its own shade.
static void centred35(int y, const char *s, uint8_t c) { text35s(64 - text35Width(s) / 2, y, s, c); }
static void centred2(int y, const char *s, uint8_t c) { text35x2s(64 - text35x2Width(s) / 2, y, s, c); }

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

static uint32_t seedNow() { return micros() * 2654435761u ^ rpgame.frameCount; }

// "4:32", at most "99:59"
static char *fmtClock(char *p, uint16_t s) { return fmtTime(p, s > 5999 ? 5999 : s); }

static void newDeal() {
    stage::deal(opt.layout, seedNow());
    overlay = NONE;
}

// ---------------------------------------------------------------------------
// Title: tiles tumbling down the felt behind the name.
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_PLAY, I_CONTINUE, I_OPTIONS };
static const char *const ITEM[3] = {"PLAY", "CONTINUE", "OPTIONS"};

// Tiles drift down behind the title, flipping over like coins (face, edge,
// back, edge); now and then one tumbles instead, turning in the plane. Now
// and then a meteor: one tile streaks across at speed, spinning hard, with
// a rainbow trail, over everything.
enum Spin : uint8_t { TUMBLE, FLIP };
// Calm, like koi: each sinks slowly, swaying from side to side, and turns
// over a few times a minute.
struct Faller {
    int16_t x16, y16;                // Q4
    int8_t vx, vy;                   // Q4 a frame
    uint16_t ang, sway;              // the turn and the sway's phase, Q4 (4096 a cycle)
    int8_t spin;                     // Q4 angle units (256 a turn) a frame
    uint8_t swaySpd, amp;            // Q4 phase a frame; px each way
    uint8_t face, mode;
};
static Faller fallers[18], meteor;
static bool meteorOn;
static uint16_t meteorIn;            // frames until the next

static int8_t randSpin(int lo, int hi) {
    int v = fx::rndRange(lo, hi + 1);
    return (int8_t)(fx::rnd() & 1 ? v : -v);
}

static void dropFaller(Faller &f, int y) {
    f.x16 = (int16_t)(fx::rndRange(4, 124) << 4);
    f.y16 = (int16_t)(y << 4);
    f.vx = (int8_t)fx::rndRange(-1, 2);
    f.vy = (int8_t)fx::rndRange(4, 10);
    f.face = (uint8_t)(fx::rnd() % board::FACES);
    f.ang = (uint16_t)fx::rnd();
    f.sway = (uint16_t)fx::rnd();
    f.mode = (uint8_t)(fx::rnd() % 12 ? FLIP : TUMBLE);           // a tumbler is rare
    f.spin = randSpin(6, 14);                                      // a turn in 5-10 s
    f.swaySpd = (uint8_t)fx::rndRange(10, 18);                     // a sway in 4-7 s
    f.amp = (uint8_t)fx::rndRange(3, 9);
}

static void launchMeteor() {
    Faller &m = meteor;
    bool left = fx::rnd() & 1;
    m.x16 = (int16_t)((left ? fx::rndRange(-10, 50) : fx::rndRange(78, 138)) << 4);
    m.y16 = (int16_t)(-20 << 4);
    m.vx = (int8_t)(left ? fx::rndRange(8, 17) : -fx::rndRange(8, 17));
    m.vy = (int8_t)fx::rndRange(32, 45);
    m.face = (uint8_t)(fx::rnd() % board::FACES);
    m.ang = (uint16_t)fx::rnd();
    m.sway = (uint16_t)fx::rnd();
    m.mode = (uint8_t)(fx::rnd() % 12 ? FLIP : TUMBLE);
    m.spin = randSpin(28, 48);                                     // a turn in 1.5-2.5 s
    m.swaySpd = (uint8_t)fx::rndRange(24, 34);
    m.amp = (uint8_t)fx::rndRange(6, 11);
    meteorOn = true;
}

static uint8_t titleFull;

static void titleTiles() {
    // Only the felt between the rails shows (rows ~35-100): they start just
    // above it, and go round again once below it.
    for (auto &f : fallers) dropFaller(f, fx::rndRange(22, 100));
    meteorOn = false;
    meteorIn = 50;
    titleFull = 2;
}

static void moveFaller(Faller &f) {
    f.x16 = (int16_t)(f.x16 + f.vx);
    f.y16 = (int16_t)(f.y16 + f.vy);
    f.ang = (uint16_t)(f.ang + f.spin);
    f.sway = (uint16_t)(f.sway + f.swaySpd);
}

// Where it is now, with its sway.
static int fallerX(const Faller &f) { return (f.x16 >> 4) + ((fx::isin(f.sway >> 4) * f.amp) >> 8); }

static void titleFallers(uint32_t frame) {
    for (auto &f : fallers) {
        moveFaller(f);
        if (f.y16 > 104 << 4 || f.x16 < -12 << 4 || f.x16 > 140 << 4) dropFaller(f, fx::rndRange(20, 28));
    }
    if (meteorOn) {
        moveFaller(meteor);
        int x = fallerX(meteor), y = meteor.y16 >> 4;
        // The trail: sparks in the casino rainbow, left behind as it flies.
        for (int k = 0; k < 3; k++)
            fx::spawn(k == 2 ? fx::STAR : fx::SPARK, x + fx::rndRange(-4, 5), y + fx::rndRange(-4, 5),
                      fx::rndRange(-8, 9) - meteor.vx / 4, fx::rndRange(-12, 3), (uint8_t)fx::rndRange(18, 34),
                      fx::RAIN[(frame / 2 + k) % 5]);
        if (y > 150 || x < -30 || x > 158) { meteorOn = false; meteorIn = (uint16_t)fx::rndRange(90, 260); }
    } else if (!--meteorIn) launchMeteor();
}

static tile::Face titleFace(uint8_t f) {
#if !CHMJ_LEAN
    if (opt.faces) return tile::Face{TILE_CELL_EASY[f], TILE_INK_EASY[f], nullptr, 0};
#endif
    return tile::Face{TILE_CELL_CLASSIC[f], TILE_INK_CLASSIC[f], TILE_CELL_BIG[f], TILE_INK_BIG[f]};
}

static void drawFaller(const Faller &f, bool big) {
    static const tile::Style FRONT = {WHITE, SKIN, WOOD, SKIN, WOOD};
    static const tile::Style BACK = {FELT_LT, FELT_LT, WOOD, SKIN, WOOD};
    int cs, sn, xs = 256;
    tile::Face face = titleFace(f.face);
    const tile::Style *st = &FRONT;
    if (f.mode == TUMBLE) {
        uint8_t a = (uint8_t)(f.ang >> 4);
        cs = fx::isin(a + 64);
        sn = fx::isin(a);
    } else {
        // A flip: upright, squeezed by the turn; past edge-on, its back.
        cs = 256; sn = 0;
        xs = fx::isin((f.ang >> 4) + 64);
        if (xs < 0) { xs = -xs; face = titleFace(TILE_BACK); st = &BACK; }
    }
    tile::drawSpun(face, *st, fallerX(f), f.y16 >> 4, cs, sn, xs, big && face.big);
}

static uint8_t titleItems(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) items[n++] = I_CONTINUE;
    items[n++] = I_PLAY;
    items[n++] = I_OPTIONS;
    return n;
}

static void titleUpdate() {
    uint8_t items[3], n = titleItems(items);
    if (sel >= n) sel = 0;
    if (menuNav(n)) {
        audio::sfx(Sfx::Select);
        switch (items[sel]) {
            case I_PLAY: go(Scr::Setup); break;
            case I_CONTINUE:
                if (save::loadGame()) { opt.layout = board::layout; stage::resume(); overlay = NONE; go(Scr::Play); }
                else { hasGame = false; audio::sfx(Sfx::Deny); }
                break;
            case I_OPTIONS: optBack = Scr::Title; go(Scr::Options); break;
        }
    }
    titleFallers(t);
}

// The rails (the logo's and the menu's) are drawn again only when the
// meteor or its sparks are over them, or were last frame (to wipe them), or
// the menu changes: the framebuffer keeps them. Outlined lettering is the
// costliest thing on screen.
static void titleRender(uint32_t frame) {
    static bool topWas, botWas;
    static uint32_t menuWas;
    uint8_t items[3], n = titleItems(items);
    int y0 = 128 - n * 14 - 1, top = 35, bot = y0 - 6;   // the felt between the rails: [top, bot)
    int lo, hi;
    bool parts = fx::activeRows(lo, hi);
    int my = meteor.y16 >> 4;
    bool full = titleFull != 0;
    if (titleFull) titleFull--;
    bool topNow = full || (parts && lo < top) || (meteorOn && my - 20 < top);
    bool botNow = full || (parts && hi >= bot) || (meteorOn && my + 20 >= bot);
    uint32_t menu = (uint32_t)n << 8 | sel << 4 | ((frame >> 4) & 1);
    bool drawTop = topNow || topWas, drawBot = botNow || botWas || menu != menuWas;
    topWas = topNow;
    botWas = botNow;
    menuWas = menu;

    // The felt and the tiles falling on it, every frame.
    gfx_fillRect(0, top, 128, bot - top, FELT);
    tile::setClip(top, bot);
    for (auto &f : fallers) drawFaller(f, false);
    tile::setClip(0, GFX_H);
    if (drawTop) {
        // The logo on a rail of its own, as the menu: the tiles fall between.
        gfx_fillRect(0, 0, 128, 34, INK);
        gfx_hline(0, 34, 128, GOLD);
        // The logo, in CHBlackjack's lettering and colours: the top rows are
        // FX_B, so the palette makes it shimmer with no redraw.
        Mask m = maskBegin(LOGO_W, LOGO_H);
        maskBlit1(m, LOGO, LOGO_W, LOGO_H);
        uint8_t ramp[LOGO_H];
        for (int i = 0; i < LOGO_H; i++) ramp[i] = i < 3 ? FX_B : (i < 12 ? GOLD : WOOD);
        maskDraw(m, 64 - LOGO_W / 2, 4, 0, INK, WINE, ramp);
        centred35(26, "~SOLITAIRE~", CYAN);
    }
    if (drawBot) {
        // The menu on a rail of its own.
        gfx_fillRect(0, bot, 128, 128 - bot, INK);
        gfx_hline(0, bot, 128, GOLD);
        for (uint8_t i = 0; i < n; i++) menuItem(y0 + i * 14, ITEM[items[i]], i == sel, frame);
    }
    // The meteor and its trail, over everything.
    fx::drawParticles();
    if (meteorOn) drawFaller(meteor, true);
}

// ---------------------------------------------------------------------------
// Setup: the layout.
// ---------------------------------------------------------------------------
static void setupUpdate() {
    int d = rpgame.repeat(RIGHT_BUTTON) ? 1 : (rpgame.repeat(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel < 1) { sel++; audio::sfx(Sfx::Cursor); }
    if (d) {
        // Left and right change the layout wherever the cursor is.
        opt.layout = (uint8_t)((opt.layout + board::LAYOUTS + d) % board::LAYOUTS);
        board::load(opt.layout);
        audio::sfx(Sfx::Coin);
    }
    // Hold SELECT to wipe your record on this layout.
    static uint8_t hold;
    hold = rpgame.pressed(SELECT_BUTTON) ? (uint8_t)(hold + 1) : 0;
    if (hold == 90) {
        stats.bestChips[opt.layout] = stats.bestSecs[opt.layout] = stats.cleared[opt.layout] = 0;
        persist(hasGame);
        audio::sfx(Sfx::Undo);
    }
    if (rpgame.justPressed(A_BUTTON)) {
        if (sel < 1) { sel++; audio::sfx(Sfx::Cursor); }
        else { audio::sfx(Sfx::Select); persist(hasGame); newDeal(); go(Scr::Play); }
    }
    if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void arrows(int y, int w, bool on, uint32_t frame) {
    int bob = on ? (frame >> 3) & 1 : 0;
    text35s(64 - w / 2 - 9 - bob, y + 1, "<", GOLD);
    text35s(64 + w / 2 + 6 + bob, y + 1, ">", GOLD);
}

// A setup choice: boxed while chosen, with arrows to change it.
static void choice(int y, const char *s, bool on, uint32_t frame) {
    int w = text35x2Width(s);
    if (on) fillRound(64 - w / 2 - 5, y - 3, w + 10, 15, 3, NAVY);
    centred2(y, s, on ? GOLD : WHITE);
    arrows(y + 2, w, on, frame);
}

static void setupRender(uint32_t frame) {
    feltBackdrop();
    title35("LAYOUT", 9, 3, FX_B, GOLD, WOOD, WINE, 13);
    choice(34, board::LAYOUT_NAME[opt.layout], sel == 0, frame);
    // The pile from above, brighter the higher it goes.
    static const uint8_t RAMP[5] = {WOOD, SKIN, GOLD, WHITE, FX_B};
    gfx_fillRect(31, 48, 66, 38, FELT_DK);
    for (uint8_t i = 0; i < board::count; i++) {
        const board::Pos &p = board::pos[i];
        gfx_fillRect(34 + p.x2 * 2 - p.z, 51 + p.y2 * 2 - p.z, 3, 3, RAMP[p.z]);
    }
    char buf[28], *p;
    uint8_t l = opt.layout;
    if (stats.cleared[l]) {
        p = fmtStr(buf, "BEST ");
        p = fmtMoney(p, stats.bestChips[l]);
        p = fmtStr(p, "  ");
        fmtClock(p, stats.bestSecs[l]);
        centred35(90, buf, GOLD);
    } else centred35(90, "NOT CLEARED YET", SILVER);
    menuItem(104, "BEGIN", sel == 1, frame);
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
// B: a tap puts the tile down (or undoes the last pair) when it is let go;
// held, it switches the view - the close-up, or with VIEW set to CLOSE the
// whole table - until it is let go, and the D-pad and A still play.
static const uint8_t HOLD_B = 10;
static uint8_t bHeld;
static bool bUsed, bArmed;           // bArmed: pressed in play (not to close a menu)

static void viewInput() {
    if (rpgame.justPressed(B_BUTTON)) bUsed = false;
    if (rpgame.pressed(B_BUTTON)) {
        if (bHeld < 255) bHeld++;
        if (bHeld > HOLD_B || rpgame.anyPressed(A_BUTTON | UP_BUTTON | DOWN_BUTTON | LEFT_BUTTON | RIGHT_BUTTON))
            bUsed = true;
    } else bHeld = 0;
    bool other = bHeld && bUsed && overlay == NONE;
    stage::setZoom((opt.view != 0) != other);
}

static void playInput() {
    if (rpgame.repeat(UP_BUTTON)) stage::hop(0, -1);
    else if (rpgame.repeat(DOWN_BUTTON)) stage::hop(0, 1);
    else if (rpgame.repeat(LEFT_BUTTON)) stage::hop(-1, 0);
    else if (rpgame.repeat(RIGHT_BUTTON)) stage::hop(1, 0);
    if (rpgame.justPressed(A_BUTTON)) stage::press();
    if (rpgame.justPressed(B_BUTTON)) bArmed = true;
    if (rpgame.justReleased(B_BUTTON)) {
        if (bArmed && !bUsed) stage::back();
        bArmed = false;
    }
    if (rpgame.justPressed(SELECT_BUTTON)) stage::hint();
}

enum Action : uint8_t { A_RESUME, A_SHUFFLE, A_UNDO, A_DEAL, A_QUIT };
static const char *const ACTION[5] = {"RESUME", "SHUFFLE", "UNDO", "NEW DEAL", "SAVE + QUIT"};
static const uint8_t PAUSE_ITEMS[4] = {A_RESUME, A_SHUFFLE, A_DEAL, A_QUIT};
static const uint8_t STUCK_ITEMS[4] = {A_SHUFFLE, A_UNDO, A_DEAL, A_QUIT};

static bool canDo(uint8_t a) {
    if (a == A_SHUFFLE) return board::shufflesLeft() && board::left >= 2;
    if (a == A_UNDO) return board::canUndo();
    return true;
}

static void doAction(uint8_t a) {
    switch (a) {
        case A_SHUFFLE: stage::shuffle(); break;
        case A_UNDO: stage::undo(); break;
        case A_DEAL: newDeal(); break;
        case A_QUIT:
            persist(!board::cleared() && !board::dealing());
            go(Scr::Title);
            break;
    }
}

static void menuInput(const uint8_t *items) {
    bool start = overlay == PAUSE && rpgame.justPressed(START_BUTTON);
    if (menuNav(4) || start) {
        uint8_t a = start ? (uint8_t)A_RESUME : items[sel];
        if (!canDo(a)) { audio::sfx(Sfx::Deny); return; }
        overlay = NONE;
        audio::sfx(Sfx::Select);
        doAction(a);
        return;
    }
    if (overlay == PAUSE && rpgame.justPressed(B_BUTTON)) overlay = NONE;
}

static void countResult() {
    uint8_t l = board::layout;
    uint16_t chips = board::chips > 0xFFFF ? 0xFFFF : (uint16_t)board::chips, secs = board::secs();
    newBest = chips > stats.bestChips[l];
    if (newBest) stats.bestChips[l] = chips;
    if (!stats.cleared[l] || secs < stats.bestSecs[l]) stats.bestSecs[l] = secs;
    if (stats.cleared[l] < 0xFFFF) stats.cleared[l]++;
    persist(false);
}

static void playUpdate() {
    switch (overlay) {
        case PAUSE: menuInput(PAUSE_ITEMS); break;
        case STUCK: menuInput(STUCK_ITEMS); break;
        case RESULT:
            if (rpgame.justPressed(A_BUTTON)) { audio::sfx(Sfx::Select); newDeal(); }
            if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
            break;
        default:
            if (rpgame.justPressed(START_BUTTON)) { overlay = PAUSE; sel = 0; audio::sfx(Sfx::Select); break; }
            if (!stage::busy()) playInput();
            else if (rpgame.justPressedMask()) stage::shoo();
            break;
    }
    viewInput();
    stage::update(overlay == NONE);
    if (overlay == NONE && stage::clearedShown()) {
        countResult();
        overlay = RESULT;
        audio::sfx(Sfx::Win);
    }
    if (overlay == NONE && stage::stuckShown()) { overlay = STUCK; sel = 0; }
}

static void panel(int y, int h) {
    fillRound(14, y, 100, h, 3, NAVY);
    roundRect(14, y, 100, h, 3, GOLD);
}

static void playRender(uint32_t frame) {
    uint32_t ui = overlay | (sel << 4);
    if (overlay) ui ^= (frame >> 3) << 12;            // blinking borders
    if (!stage::render(frame, ui)) return;
    if (overlay == PAUSE || overlay == STUCK) {
        const uint8_t *items = overlay == PAUSE ? PAUSE_ITEMS : STUCK_ITEMS;
        panel(32, 64);
        for (uint8_t i = 0; i < 4; i++) {
            int y = 38 + i * 14;
            if (i == sel) fillRound(18, y - 3, 92, 15, 3, INK);
            centred2(y, ACTION[items[i]], !canDo(items[i]) ? SILVER : (i == sel ? FX_B : WHITE));
        }
    } else if (overlay == RESULT) {
        panel(80, 44);
        centred2(84, "CLEARED!", FX_B);
        char buf[28], *p = fmtStr(buf, "TIME ");
        p = fmtClock(p, board::secs());
        p = fmtStr(p, "   WON ");
        fmtMoney(p, board::chips);
        centred35(99, buf, WHITE);
        if (newBest) centred35(106, "A NEW BEST!", (frame & 16) ? GOLD : WHITE);
        else {
            p = fmtStr(buf, "BEST ");
            fmtMoney(p, stats.bestChips[board::layout]);
            centred35(106, buf, SILVER);
        }
        centred35(115, "A NEW DEAL   B MENU", GOLD);
    }
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
enum Opt : uint8_t { O_SOUND, O_FELT, O_FACES, O_VIEW, O_SPEED, O_BACK, OPT_COUNT };
static const char *const OPT_TEXT[OPT_COUNT] = {
    "SOUND|OFF|ON", "TABLE|GREEN|BLUE|RED|PURPLE", "TILES|CLASSIC|EASY", "VIEW|FULL|CLOSE", "PACE|FUN|QUICK", "BACK",
};
// The option's byte in Options.
static uint8_t &optByte(uint8_t i) {
    static const uint8_t AT[OPT_COUNT - 1] = {0, 1, 2, 5, 3};
    return ((uint8_t *)&opt)[AT[i]];
}

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
        char tmp[12];
        uint8_t n = (uint8_t)(optField(OPT_TEXT[sel], 0, tmp) - 1);
        uint8_t &f = optByte(sel);
        f = (uint8_t)((f + n + d) % n);
        applyOptions();
        audio::sfx(Sfx::Coin);
    }
    if ((rpgame.justPressed(A_BUTTON) && sel == O_BACK) || rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        persist(hasGame);
        go(optBack);
    }
}

static void optionsRender(uint32_t frame) {
    feltBackdrop();
    title35("OPTIONS", 7, 3, FX_B, GOLD, WOOD, WINE, 13);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 28 + i * 13;
        char label[12], value[12];
        optField(OPT_TEXT[i], 0, label);
        if (i == sel) {
            fillRound(8, y - 3, 112, 15, 3, NAVY);
            roundRect(8, y - 3, 112, 15, 3, (frame & 16) ? FX_B : GOLD);
        }
        if (i == O_BACK) { centred2(y, label, i == sel ? GOLD : WHITE); continue; }
        text35x2s(15, y, label, i == sel ? GOLD : WHITE);
        optField(OPT_TEXT[i], (uint8_t)(optByte(i) + 1), value);
        text35x2s(114 - text35x2Width(value), y, value, i == sel ? WHITE : SILVER);
    }
    centred35(108, "CHMAHJONG " CHMJ_VERSION, SILVER);
    centred35(117, "FONT: PRESS PLAY ON TAPE", SILVER);
}

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   G <layout> <seed>      deal a table and go to it
//   M <a> <b>              take the pair of tiles a and b
//   A                      take a pair (the first there is); ERR if there is none
//   U / F / I              undo, shuffle, hint
//   J <T|S|O>              jump to title/setup/options
//   H                      STATE left pairs chips cursor picked busy overlay secs streak
//   Y                      render cost by section (table pile hud fx), us
//   O                      (simulator) NEXT a b: the deal's own next pair
//   W                      (simulator) NEXT a b: the pair the hint is showing
//   R <tile>               (simulator) the D-pad route to a tile: ROUTE UDLR..
//   Q                      (simulator) calibration for chdrive's `cal`
static bool debugHook(char cmd, const char *args) {
    char buf[96], *p;
    switch (cmd) {
        case 'Y': {
            uint32_t us[4];
            gfx_wait();
            stage::profile(us);
            p = fmtStr(buf, "RPROF");
            for (int k = 0; k < 4; k++) { *p++ = ' '; p = fmtInt(p, (int32_t)us[k]); }
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'G': {
            opt.layout = (uint8_t)(dbg::parseNum(args, 10) % board::LAYOUTS);
            stage::deal(opt.layout, dbg::parseNum(args, 10));
            overlay = NONE;
            enter(Scr::Play);
            return true;
        }
        case 'M': {
            uint8_t a = (uint8_t)dbg::parseNum(args, 10), b = (uint8_t)dbg::parseNum(args, 10);
            return stage::take(a, b);
        }
        case 'A': {
            uint8_t a, b;
            return board::hint(a, b) && stage::take(a, b);
        }
        case 'U': return stage::undo();
        case 'F': overlay = NONE; return stage::shuffle();
        case 'I': stage::hint(); return true;
        case 'J': {
            static const char K[] = "TSO";
            const char *q = strchr(K, args[0]);
            if (!q) return false;
            static const Scr S[] = {Scr::Title, Scr::Setup, Scr::Options};
            enter(S[q - K]);
            return true;
        }
        case 'H':
            p = fmtInt(fmtStr(buf, "STATE left="), board::left);
            p = fmtInt(fmtStr(p, " pairs="), board::pairs());
            p = fmtInt(fmtStr(p, " chips="), board::chips);
            p = fmtInt(fmtStr(p, " cur="), stage::cursor());
            p = fmtInt(fmtStr(p, " sel="), stage::selected());
            p = fmtInt(fmtStr(p, " busy="), stage::busy());
            p = fmtInt(fmtStr(p, " overlay="), overlay);
            p = fmtInt(fmtStr(p, " secs="), board::secs());
            p = fmtInt(fmtStr(p, " streak="), board::streak);
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
#ifdef CHSIM
        case 'W': {
            uint8_t a, b;
            if (!stage::hinted(a, b)) return false;
            p = fmtInt(fmtStr(buf, "NEXT "), a);
            *p++ = ' ';
            fmtStr(fmtInt(p, b), "\n");
            dbg::print(buf);
            return true;
        }
        case 'O': {
            // The first pair of the deal's own order still on the table.
            for (uint8_t k = 0; k < board::count / 2; k++) {
                uint8_t a = board::dealOrder[2 * k], b = board::dealOrder[2 * k + 1];
                if (!board::present(a) || !board::present(b)) continue;
                p = fmtInt(fmtStr(buf, "NEXT "), a);
                *p++ = ' ';
                fmtStr(fmtInt(p, b), "\n");
                dbg::print(buf);
                return true;
            }
            return false;
        }
        case 'R': {
            // R <tile>: the D-pad presses (U D L R) that take the glove to it,
            // as the cursor hops between the free tiles (fewest presses).
            uint8_t list[board::MAX_FREE], n = board::freeList(list), c = stage::cursor(), to = (uint8_t)dbg::parseNum(args, 10);
            if (c == board::NONE || !board::isFree(to)) return false;
            static const int8_t DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
            static uint8_t from[board::MAX_TILES], how[board::MAX_TILES];
            uint8_t q[board::MAX_FREE], qh = 0, qt = 0;
            memset(from, 0xFF, sizeof from);
            from[c] = c; q[qt++] = c;
            while (qh < qt && from[to] == 0xFF) {
                uint8_t at = q[qh++];
                for (uint8_t d = 0; d < 4; d++) {
                    uint8_t nx = nav::step(list, n, at, DX[d], DY[d]);
                    if (nx != board::NONE && from[nx] == 0xFF) { from[nx] = at; how[nx] = d; q[qt++] = nx; }
                }
            }
            char path[48];
            uint8_t k = 0;
            if (from[to] == 0xFF) path[k++] = '?';
            else for (uint8_t at = to; at != c && k < 40; at = from[at]) path[k++] = "UDLR"[how[at]];
            p = fmtStr(buf, "ROUTE ");
            while (k) *p++ = path[--k];
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
#endif
    }
    return false;
}
#endif

// ---------------------------------------------------------------------------
void begin() {
    stage::begin();
    opt.sound = 1;
    save::load(opt, stats, hasGame);
    if (opt.layout >= board::LAYOUTS) opt.layout = 0;
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
        fx::update();
        return;
    }
    if (fadeIn) { fadeIn--; pal::setFade((uint8_t)(16 - fadeIn * 2)); }
    switch (cur) {
        case Scr::Title:   titleUpdate(); break;
        case Scr::Setup:   setupUpdate(); break;
        case Scr::Play:    playUpdate(); break;
        case Scr::Options: optionsUpdate(); break;
    }
    fx::update();
}

void render(uint32_t frame) {
    switch (cur) {
        case Scr::Title:   titleRender(frame); break;
        case Scr::Setup:   setupRender(frame); break;
        case Scr::Play:    playRender(frame); break;
        case Scr::Options: optionsRender(frame); break;
    }
}

}  // namespace screens
