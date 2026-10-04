#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in Draw/Mask/Iso)
// The screens (Screens.h): title, setup, play with its pause menu, options
// and house rules; and the debug protocol's game commands.
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "config.h"
#include "Screens.h"
#include "Fx.h"
#include "Sounds.h"
#include "Iso.h"
#include "Engine.h"
#include "Match.h"
#include "Stage.h"
#include "Save.h"
#ifdef CHSIM
#include <sim.h>
#endif

namespace screens {

enum class Scr : uint8_t { Title, Setup, Play, Options, Rules };
static Scr cur = Scr::Title, pending = Scr::Title;
static uint16_t t;                   // frames on this screen
static uint8_t fadeOut, fadeIn;
static uint8_t sel;                  // menu cursor
#if !CHCK_LEAN
static Scr optBack = Scr::Title;
#endif

static Options opt;
static Stats stats;
static bool hasGame;                 // a saved game is waiting
static uint8_t setupMode;            // the game the setup screen is for (match::Mode)

// Play-screen overlays.
enum Overlay : uint8_t { NONE, PAUSE, RESULT };
static Overlay overlay;
static uint8_t pendingAction;        // chosen from the pause menu while the CPU was thinking
static bool statsCounted;
#if CHGAME_DEBUG
static uint32_t thinkMs;            // the CPU's last search, wall time (debug W)
#endif

static const char *const OPPONENT[match::LEVELS] = {"TOURIST", "DEALER", "THE HOUSE"};
static const char *const OPP_LINE[match::LEVELS] = {
    "JUST HERE FOR THE BUFFET", "KNOWS EVERY TRICK", "THE HOUSE ALWAYS WINS"};

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------
static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

// The title's board: the camera drifting over it, close up.
static void titleDrift() {
    int a = (int)t / 3;
    stage::drift((fx::isin(a) * 110) >> 8, 86 + ((fx::isin(a * 2 + 64) * 44) >> 8));
}

// A fresh game on it, the chips dropping in.
static void titleBoard() {
    match::Setup demo = {match::TWO_PLAYER, 0, 0, eng::R_FORCED, 1};
    match::start(demo);
    stage::demo(true);
    titleDrift();
    stage::update();
}

static uint16_t logoAt;              // when the title's lettering came down (0: not yet)
static uint8_t demoStep;
static uint16_t demoAt;

// The title's tune, looping on every screen but Play. tune(true) starts it
// from the top unless it is already playing; nothing plays with MUSIC off.
static bool tuneOn;
static void tune(bool play) {
    if (play && tuneOn) return;
    tuneOn = play && opt.music;
    playSong(tuneOn ? Song::TITLE : Song::NONE);
}

static void enter(Scr s) {
    cur = s;
    stage::invalidate();
    t = 0;
    sel = 0;
    fadeIn = 8;
    fx::clear();
    pal::setMode(pal::CASINO);
    tune(s != Scr::Play);
    if (s == Scr::Title) {
        stage::setView(stage::NORMAL);
        titleBoard();
        logoAt = 0;
        demoStep = 0;
    }
    if (s == Scr::Setup) sel = 3;
    if (s == Scr::Play) {
        stage::demo(false);
        stage::opponentName = OPPONENT[match::setup.level];
        stage::setView(stage::NORMAL);
    }
}

static void persist(bool withGame) {
    gfx_wait();                      // save builds its page in the chunk scratch
    save::store(opt, stats, withGame);
    hasGame = withGame;
}

static void applyOptions() {
    audio::setOn(opt.sound != 0);
    audio::setMusic(opt.music ? audio::LEAD : audio::MUSIC_OFF);
    if (!opt.music) tune(false);
    pal::setTheme(opt.felt);
    stage::setFast(opt.speed != 0);
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

static uint32_t seedNow() { return micros() * 2654435761u ^ rpgame.frameCount; }

// ---------------------------------------------------------------------------
// Title: the chips drop onto the board, the name slams down, and a game
// plays itself out underneath while nobody presses anything.
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_ONE, I_TWO, I_CONTINUE, I_OPTIONS };
static const char *const ITEM[4] = {"1 PLAYER", "2 PLAYERS", "CONTINUE", "OPTIONS"};

static uint8_t titleItems(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) items[n++] = I_CONTINUE;
    items[n++] = I_ONE;
    items[n++] = I_TWO;
#if !CHCK_LEAN
    items[n++] = I_OPTIONS;
#endif
    return n;
}

static void newGame(uint8_t mode) {
    match::Setup s;
    s.mode = mode;
    s.level = opt.level;
    s.humanBlack = opt.side == 2 ? (uint8_t)(fx::rnd() & 1) : opt.side;
    s.rules = opt.rules;
    s.seed = seedNow();
    match::start(s);
    overlay = NONE;
    statsCounted = false;
    go(Scr::Play);
}

// The game the title plays: an opening that walks into a triple jump and
// a crowning (squares from, to; found by tools/tests/demo_line.cpp).
static const uint8_t DEMO[] = {
#include "src/states/DemoLine.h"
};

static void titleUpdate() {
    uint8_t items[5], n = titleItems(items);
    if (sel >= n) sel = 0;
    if (menuNav(n)) {
        audio::sfx(Sfx::Select);
        switch (items[sel]) {
            case I_ONE: setupMode = match::VS_CPU; go(Scr::Setup); break;
            case I_TWO: setupMode = match::TWO_PLAYER; go(Scr::Setup); break;
            case I_CONTINUE:
                if (save::loadGame()) { overlay = NONE; statsCounted = false; go(Scr::Play); }
                else { hasGame = false; audio::sfx(Sfx::Deny); }
                return;
#if !CHCK_LEAN
            case I_OPTIONS: optBack = Scr::Title; go(Scr::Options); break;
#endif
        }
        return;
    }
    if (!logoAt && stage::introDone()) {
        // The chips are down: the name lands on them.
        logoAt = t;
        demoAt = (uint16_t)(t + 240);
    }
    if (logoAt && t == logoAt + 6) {
        fx::shake(10, 3);
        fx::burst(fx::STAR, 64, 16, 16, 40, GOLD);
        fx::burst(fx::SPARK, 64, 16, 12, 30, WHITE);
        audio::sfx(Sfx::Crown);
    }
    // Left alone, the board plays a few moves, then sets itself up again.
    if (logoAt && (int16_t)(t - demoAt) >= 0 && !stage::busy()) {
        if (demoStep * 2u >= sizeof DEMO) {
            titleBoard();
            demoStep = 0;
            demoAt = (uint16_t)(t + 300);
        } else if (match::humanToMove()) {
            match::play(DEMO[demoStep * 2], DEMO[demoStep * 2 + 1]);
            demoStep++;
            demoAt = (uint16_t)(t + (demoStep * 2u >= sizeof DEMO ? 200 : 50));
        }
    }
    titleDrift();
    match::update(stage::busy());
    stage::update();
}

static void titleRender(uint32_t frame) {
    stage::renderScene(frame);
    dither(0, 0, 128, 30, INK, 0);
    if (logoAt) {
        // Down it comes, with a bounce. The top rows are FX_B, so the palette
        // makes the lettering shimmer.
        int d = (int)t - logoAt, y = 5 - (((256 - fx::ease(fx::OUT_BOUNCE, d, 14)) * 30) >> 8);
        title35("CHECKERS", y, 3, FX_B, GOLD, WOOD, WINE, 13);
    }
    uint8_t items[5], n = titleItems(items);
    int y0 = 128 - n * 14 - 1;
    dither(0, y0 - 5, 128, 128 - y0 + 5, INK, 1);
    for (uint8_t i = 0; i < n; i++) menuItem(y0 + i * 14, ITEM[items[i]], i == sel, frame);
    fx::applyShake(0, 127);
}

// ---------------------------------------------------------------------------
// Setup: the opponent, your colour and the house rules.
// ---------------------------------------------------------------------------
enum SetupRow : uint8_t { S_LEVEL, S_SIDE, S_RULES, S_BEGIN };

static void setupUpdate() {
    uint8_t first = setupMode == match::VS_CPU ? S_LEVEL : S_RULES;
    if (sel < first) sel = first;
    int d = rpgame.repeat(RIGHT_BUTTON) ? 1 : (rpgame.repeat(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.repeat(UP_BUTTON) && sel > first) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel < S_BEGIN) { sel++; audio::sfx(Sfx::Cursor); }
    if (d && sel == S_LEVEL) { opt.level = (uint8_t)((opt.level + match::LEVELS + d) % match::LEVELS); audio::sfx(Sfx::Coin); }
    if (d && sel == S_SIDE) { opt.side = (uint8_t)((opt.side + 3 + d) % 3); audio::sfx(Sfx::Coin); }
    // Hold SELECT to wipe your record against this opponent.
    static uint8_t hold;
    hold = rpgame.pressed(SELECT_BUTTON) && setupMode == match::VS_CPU ? (uint8_t)(hold + 1) : 0;
    if (hold == 90) {
        stats.won[opt.level] = stats.lost[opt.level] = stats.drawn[opt.level] = 0;
        persist(hasGame);
        audio::sfx(Sfx::Capture);
    }
    if (rpgame.justPressed(A_BUTTON)) {
        if (sel < S_RULES) { sel++; audio::sfx(Sfx::Cursor); }
        else if (sel == S_RULES) {
            audio::sfx(Sfx::Select);
#if CHCK_LEAN
            opt.rules = (uint8_t)((opt.rules + 1) & eng::R_ALL);
#else
            optBack = Scr::Setup;
            go(Scr::Rules);
#endif
        } else { audio::sfx(Sfx::Select); persist(hasGame); newGame(setupMode); }
    }
    if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void arrows(int y, int w, bool on, uint32_t frame) {
    if (!on) return;
    int bob = (frame >> 3) & 1;
    text35(64 - w / 2 - 9 - bob, y + 1, "<", GOLD);
    text35(64 + w / 2 + 6 + bob, y + 1, ">", GOLD);
}

// A setup choice: boxed while chosen, with arrows to change it.
static void choice(int y, const char *s, bool on, uint32_t frame) {
    int w = text35x2Width(s);
    if (on) fillRound(64 - w / 2 - 5, y - 3, w + 10, 15, 3, NAVY);
    centred2(y, s, on ? GOLD : WHITE);
    arrows(y + 2, w, on, frame);
}

static const char *rulesName() { return opt.rules == eng::R_FORCED ? "AMERICAN RULES" : "HOUSE RULES"; }

static void setupRender(uint32_t frame) {
    feltBackdrop();
    if (setupMode == match::VS_CPU) {
        title35("OPPONENT", 8, 3, FX_B, GOLD, WOOD, WINE, 13);
        // Stars for how hard they play.
        for (int i = 0; i <= opt.level; i++) text35(64 - opt.level * 4 + i * 8, 31, "*", FX_B);
        choice(42, OPPONENT[opt.level], sel == S_LEVEL, frame);
        centred35(56, OPP_LINE[opt.level], FELT_LT);
        // Your record against them.
        char buf[24], *p = fmtStr(buf, "WON ");
        p = fmtInt(p, stats.won[opt.level]); p = fmtStr(p, " LOST ");
        p = fmtInt(p, stats.lost[opt.level]); p = fmtStr(p, " DRAWN ");
        fmtInt(p, stats.drawn[opt.level]);
        centred35(63, buf, GOLD);

        static const char *const SIDE[3] = {"PLAY WHITE", "PLAY BLACK", "RANDOM SIDE"};
        choice(74, SIDE[opt.side], sel == S_SIDE, frame);
    } else {
        title35("2 PLAYERS", 14, 3, FX_B, GOLD, WOOD, WINE, 13);
        centred35(46, "WHITE MOVES FIRST", FELT_LT);
        centred35(54, "PASS THE BOARD EACH TURN", FELT_LT);
    }
    menuItem(91, "RULES", sel == S_RULES, frame);
    menuItem(107, "BEGIN", sel == S_BEGIN, frame);
    if (sel == S_RULES) centred35(116, rulesName(), FELT_LT);
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
static void tryTarget(uint8_t sq) {
    if (match::play(stage::selected(), sq)) stage::deselect();
}

// A tap of B puts a piece back down. Held (or with a direction), it
// inspects the board: the camera zooms right in and the D-pad pushes the
// view to the board's edges and corners until B is let go. Holding a piece,
// a tap is anything up to HOLD_B frames, while a bar grows under the HUD.
static const uint8_t HOLD_B = 32;
static bool bUsed;
static uint8_t bHeld;
static uint8_t holdBar() {
    return bHeld && !bUsed && stage::selected() != 0xFF ? (uint8_t)(bHeld * (128 / HOLD_B)) : 0;
}
static void lookAround() {
    if (rpgame.justPressed(B_BUTTON)) bUsed = false;
    bHeld = rpgame.pressed(B_BUTTON) ? (uint8_t)(bHeld < 255 ? bHeld + 1 : 255) : 0;
    int dx = 0, dy = 0;
    if (bHeld) {
        if (rpgame.pressed(LEFT_BUTTON)) dx = -1;
        if (rpgame.pressed(RIGHT_BUTTON)) dx = 1;
        if (rpgame.pressed(UP_BUTTON)) dy = -1;
        if (rpgame.pressed(DOWN_BUTTON)) dy = 1;
        if (dx || dy || bHeld > (stage::selected() != 0xFF ? HOLD_B : 8)) bUsed = true;
    }
    stage::inspect(bHeld && bUsed, dx, dy);
}

static void cycleView() {
    stage::setView((uint8_t)((stage::view() + 1) % stage::VIEWS));
    audio::sfx(Sfx::Whoosh);
}

// The cursor visits your pieces (the plate says when one is stuck, or has
// to leave the jumping to another) or, once one is picked up, the squares it
// can go to.
static uint8_t spots(uint8_t *list) {
    uint8_t s = stage::selected(), cap[16], n = 0;
    if (s != 0xFF) return match::movesFrom(s, list, cap);
    for (uint8_t sq = 0; sq < 64; sq++) {
        uint8_t p = match::board[sq];
        if (p && ((p & eng::BLACK) != 0) == match::blackToMove()) list[n++] = sq;
    }
    return n;
}

// The spot nearest `from` in screen direction (ux, uy) - diagonals and all,
// whatever the view - preferring ones straight ahead. With none that way it
// wraps round to the farthest the other way, so pressing on steps through
// every spot. (0, 0): simply the nearest.
static uint8_t nearest(const uint8_t *list, uint8_t n, uint8_t from, int ux, int uy) {
    int fx, fy;
    iso::worldOf(from, fx, fy);
    uint8_t best = 0xFF, back = 0xFF;
    int32_t bestS = 0x7FFFFFFF, backS = 0x7FFFFFFF;
    for (uint8_t i = 0; i < n; i++) {
        if (list[i] == from) continue;
        int x, y;
        iso::worldOf(list[i], x, y);
        int dx = x - fx, dy = y - fy;
        int along = dx * ux + dy * uy, side = dx * uy - dy * ux;
        if (side < 0) side = -side;
        if (!ux && !uy) along = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        int32_t sc = along > 0 ? along + 2 * side : 4 * along + side;
        if (along > 0 && sc < bestS) { bestS = sc; best = list[i]; }
        if (along <= 0 && sc < backS) { backS = sc; back = list[i]; }
    }
    return best != 0xFF ? best : back;
}

static uint8_t chainBeat;            // frames a forced next jump has been shown

static void playInput() {
    uint8_t s = stage::selected(), chain = match::chainSq();
    uint8_t to[16], cap[16];
    if (chain != 0xFF) {
        // In the middle of a multiple jump: the piece is in hand again, and
        // goes on by itself when there is only one way on.
        uint8_t k = match::movesFrom(chain, to, cap);
        if (s == 0xFF) {
            stage::select(chain, to, cap, k);
            stage::setCursor(nearest(to, k, chain, 0, 0));
            chainBeat = 0;
            return;
        }
        if (k == 1) {
            if (++chainBeat >= (opt.speed ? 4 : 10)) tryTarget(to[0]);
            return;
        }
    }
    uint8_t list[32], n = spots(list), c = stage::cursor();
    if (!n) return;
    // A new turn (or an undo) may leave the cursor on nothing playable.
    bool on = false;
    for (uint8_t i = 0; i < n; i++) on |= list[i] == c;
    if (!on) stage::setCursor(c = nearest(list, n, c, 0, 0));
    int ux = 0, uy = 0;
    if (!rpgame.pressed(B_BUTTON)) {
        if (rpgame.repeat(UP_BUTTON)) uy = -1;
        if (rpgame.repeat(DOWN_BUTTON)) uy = 1;
        if (rpgame.repeat(LEFT_BUTTON)) ux = -1;
        if (rpgame.repeat(RIGHT_BUTTON)) ux = 1;
    }
    if (ux || uy) {
        uint8_t t = nearest(list, n, c, ux, uy);
        if (t != 0xFF) { stage::setCursor(t); audio::sfx(Sfx::Cursor); }
        else audio::sfx(Sfx::Deny);
    }
    if (rpgame.justReleased(B_BUTTON) && !bUsed && s != 0xFF) {
        if (chain != 0xFF) {
            stage::deny(stage::KEEP_JUMPING);    // a jump once begun is finished
        } else {
            stage::deselect();
            stage::setCursor(s);                 // back onto the piece
            audio::sfx(Sfx::Cursor);
        }
    }
    c = stage::cursor();
    uint8_t k = s == 0xFF ? match::movesFrom(c, to, cap) : 1;
    uint8_t why = k ? stage::FREE : match::mustJump() ? stage::MUST_JUMP : stage::NO_MOVES;
    stage::setBlocked(why);
    if (rpgame.justPressed(A_BUTTON) && !bHeld) {
        if (s != 0xFF) tryTarget(c);
        else if (!k) stage::deny(why);
        else {
            stage::select(c, to, cap, k);
            stage::setCursor(nearest(to, k, c, 0, 0));
        }
    }
}

enum Action : uint8_t { A_NONE, A_RESUME, A_UNDO, A_RESIGN, A_QUIT };
static const char *const PAUSE_ITEM[4] = {"RESUME", "UNDO", "RESIGN", "SAVE + QUIT"};

static void doAction(uint8_t a) {
    switch (a) {
        case A_UNDO:
            if (match::undo()) audio::sfx(Sfx::Whoosh);
            else audio::sfx(Sfx::Deny);
            break;
        case A_RESIGN: match::resign(); break;
        case A_QUIT:
            persist(match::active());
            go(Scr::Title);
            break;
    }
}

static void pauseInput(bool thinking) {
    bool start = rpgame.justPressed(START_BUTTON);
    if (menuNav(4) || start) {
        uint8_t a = start ? (uint8_t)A_RESUME : (uint8_t)(sel + 1);
        overlay = NONE;
        audio::sfx(Sfx::Select);
        if (a == A_RESUME) return;
        if (thinking) { pendingAction = a; match::abortThink(); }
        else doAction(a);
        return;
    }
    if (rpgame.justPressed(B_BUTTON)) overlay = NONE;
}

static void countResult() {
    if (statsCounted || match::setup.mode != match::VS_CPU) return;
    statsCounted = true;
    uint8_t lv = match::setup.level;
    bool white = match::setup.humanBlack == 0;
    switch (match::result) {
        case match::WHITE_WINS: if (white) stats.won[lv]++; else stats.lost[lv]++; break;
        case match::BLACK_WINS: if (white) stats.lost[lv]++; else stats.won[lv]++; break;
        default: stats.drawn[lv]++; break;
    }
}

// What the game waits for: everything on show - or, between the hops of a
// multiple jump, just the hop itself.
static bool showing() { return match::chainSq() != 0xFF ? stage::moving() : stage::busy(); }

static void playUpdate(bool thinking) {
    if (thinking) {
        // Mid-search: the pause menu and the view toggle work; the game waits.
        if (overlay == PAUSE) pauseInput(true);
        else if (rpgame.justPressed(START_BUTTON) && !bHeld) { overlay = PAUSE; sel = 0; audio::sfx(Sfx::Select); }
        if (overlay == NONE) lookAround();
        if (rpgame.justPressed(SELECT_BUTTON) && !bHeld) cycleView();
        stage::update();
        return;
    }
    if (pendingAction) { uint8_t a = pendingAction; pendingAction = 0; doAction(a); }
    switch (overlay) {
        case PAUSE: pauseInput(false); break;
        case RESULT:
            if (rpgame.justPressed(A_BUTTON)) {
                audio::sfx(Sfx::Select);
                persist(false);
                if (match::setup.mode == match::VS_CPU) opt.side = match::setup.humanBlack ? 0 : 1;   // swap sides for the rematch
                newGame(match::setup.mode);
            }
            if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); persist(false); go(Scr::Title); }
            break;
        default:
            lookAround();
            if (stage::waiting()) {
                if (rpgame.justPressedMask()) { stage::acknowledge(); audio::sfx(Sfx::Select); }
                break;
            }
            if (rpgame.justPressed(START_BUTTON) && !bHeld) { overlay = PAUSE; sel = 0; audio::sfx(Sfx::Select); break; }
            if (rpgame.justPressed(SELECT_BUTTON) && !bHeld) cycleView();
            if (match::humanToMove() && !showing()) playInput();
            break;
    }
#if CHGAME_DEBUG
    uint32_t t0 = millis();
#endif
    match::update(showing());              // the CPU's search runs in here
#if CHGAME_DEBUG
    if (millis() - t0 > 100) thinkMs = millis() - t0;
#endif
    stage::update();
    if (!match::active() && stage::overShown() && overlay != RESULT) {
        countResult();
        overlay = RESULT;
    }
}

static void panel(int y, int h) {
    fillRound(14, y, 100, h, 3, NAVY);
    roundRect(14, y, 100, h, 3, GOLD);
}

static void playRender(uint32_t frame) {
    uint8_t bar = overlay ? 0 : holdBar();
    uint32_t ui = overlay | (sel << 4) | ((uint32_t)bar << 16);
    if (overlay) ui ^= (frame >> 3) << 12;            // blinking arrows and borders
    if (!stage::render(frame, ui)) return;
    gfx_fillRect(0, 10, bar, 2, CYAN);                // B held: how long till it inspects
    if (overlay == PAUSE) {
        panel(32, 64);
        for (uint8_t i = 0; i < 4; i++) {
            bool dim = i == 1 && !match::canUndo() && !match::cpuThinking();
            int y = 38 + i * 14;
            if (i == sel) fillRound(18, y - 3, 92, 15, 3, INK);
            centred2(y, PAUSE_ITEM[i], dim ? SILVER : (i == sel ? FX_B : WHITE));
        }
    } else if (overlay == RESULT) {
        panel(84, 40);
        const char *head = "DRAW";
        static const char *const WHY[4] = {"ALL CAPTURED", "NO MOVES LEFT", "BY RESIGNATION", "REPETITION"};
        const char *why = match::result == match::DRAW_40 ? "40 MOVES, NO PROGRESS" : WHY[match::reason];
        bool vsCpu = match::setup.mode == match::VS_CPU;
        bool whiteWon = match::result == match::WHITE_WINS, blackWon = match::result == match::BLACK_WINS;
        if (whiteWon || blackWon) {
            if (vsCpu) head = whiteWon == (match::setup.humanBlack == 0) ? "YOU WIN!" : "YOU LOSE";
            else head = whiteWon ? "WHITE WINS" : "BLACK WINS";
        }
        centred2(87, head, FX_B);
        centred35(99, why, SILVER);
        centred35(111, vsCpu ? "A REMATCH   B MENU" : "A AGAIN   B MENU", WHITE);
    }
}

// ---------------------------------------------------------------------------
// Options, and the house rules (the same screen with another table). Device
// debug builds leave both out to fit the protocol: their tests start games
// directly.
// ---------------------------------------------------------------------------
#if !CHCK_LEAN
static const char *const OPT_TEXT[] = {
    "SOUND|OFF|ON", "BOARD|GREEN|BLUE|RED|PURPLE", "MUSIC|OFF|ON", "PACE|FUN|QUICK", "BACK",
};
static const uint8_t OPT_BYTE[4] = {0, 1, 2, 4};       // each row's byte in Options
static const char *const RULE_TEXT[] = {
    "JUMPS|FREE|FORCED", "KINGS|SHORT|FLYING", "MEN|AHEAD|ANY WAY", "BACK",
};
// What the chosen row's setting means, [row][value].
static const char *const RULE_HELP[3][2] = {
    {"JUMPING IS YOUR CHOICE", "A JUMP MUST BE TAKEN"},
    {"KINGS STEP ONE SQUARE", "KINGS SLIDE ANY DISTANCE"},
    {"MEN ONLY JUMP FORWARD", "MEN JUMP BACKWARD TOO"},
};

static bool rulesPage() { return cur == Scr::Rules; }
static uint8_t optRows() { return rulesPage() ? 4 : 5; }
static const char *optText(uint8_t i) { return rulesPage() ? RULE_TEXT[i] : OPT_TEXT[i]; }
static uint8_t optGet(uint8_t i) {
    return rulesPage() ? (uint8_t)((opt.rules >> i) & 1) : ((uint8_t *)&opt)[OPT_BYTE[i]];
}
static void optSet(uint8_t i, uint8_t v) {
    if (rulesPage()) opt.rules = (uint8_t)((opt.rules & ~(1 << i)) | (v << i));
    else ((uint8_t *)&opt)[OPT_BYTE[i]] = v;
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
    uint8_t rows = optRows(), back = (uint8_t)(rows - 1);
    if (rpgame.repeat(UP_BUTTON)) { sel = (uint8_t)((sel + rows - 1) % rows); audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON)) { sel = (uint8_t)((sel + 1) % rows); audio::sfx(Sfx::Cursor); }
    int d = rpgame.justPressed(RIGHT_BUTTON) ? 1 : (rpgame.justPressed(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.justPressed(A_BUTTON) && sel != back) d = 1;
    if (d && sel != back) {
        char tmp[12];
        uint8_t n = (uint8_t)(optField(optText(sel), 0, tmp) - 1);
        optSet(sel, (uint8_t)((optGet(sel) + n + d) % n));
        applyOptions();
        tune(true);
        audio::sfx(Sfx::Coin);
    }
    if ((rpgame.justPressed(A_BUTTON) && sel == back) || rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        persist(hasGame);
        go(optBack);
    }
}

static void optionsRender(uint32_t frame) {
    feltBackdrop();
    uint8_t rows = optRows(), back = (uint8_t)(rows - 1);
    title35(rulesPage() ? "RULES" : "OPTIONS", 7, 3, FX_B, GOLD, WOOD, WINE, 13);
    for (uint8_t i = 0; i < rows; i++) {
        int y = 29 + i * 13;
        char label[12], value[12];
        optField(optText(i), 0, label);
        if (i == sel) {
            fillRound(8, y - 3, 112, 15, 3, NAVY);
            roundRect(8, y - 3, 112, 15, 3, (frame & 16) ? FX_B : GOLD);
        }
        if (i == back) { centred2(y, label, i == sel ? GOLD : WHITE); continue; }
        text35x2(15, y, label, i == sel ? GOLD : WHITE);
        optField(optText(i), (uint8_t)(optGet(i) + 1), value);
        text35x2(114 - text35x2Width(value), y, value, i == sel ? WHITE : FELT_LT);
    }
    if (rulesPage()) {
        centred35(88, rulesName(), GOLD);
        if (sel < 3) centred35(100, RULE_HELP[sel][optGet(sel)], WHITE);
        centred35(113, "THEY APPLY FROM THE NEXT GAME", FELT_LT);
    } else {
        centred35(117, "FONT: PRESS PLAY ON TAPE", SILVER);
    }
}
#endif

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   G <mode> <humanBlack> <level> <seed> <rules>   start a game (mode 0 vs CPU, 1 two players)
//   M <from> <to>                           play a hop (squares 0..63)
//   J <T|S|O|R>                             jump to title/setup/options/rules
//   X <32 cells> <w|b> [rules]              (simulator) set up a position, two players:
//                                           a1 c1 e1 g1 b2 ... h8 as . w W b B, who moves, eng::R_* bits (default 1)
//   V <32 cells> <w|b> [rules]              (simulator) ... you, the side to move, against the CPU
//   R <sq>                                  (simulator) the D-pad route to sq: ROUTE UDLR..
//   H                                       (simulator) the board: BOARD <64 chars> <w|b> <your turn 0|1> <waiting on a press 0|1> <over 0|1>
//   A                                       (simulator) a good hop for the side to move: AUTO <from> <to> <held>
static bool debugHook(char cmd, const char *args) {
    char buf[48], *p;
    switch (cmd) {
        case 'W':
            // W: the CPU's last move: wall time (frames drawn meanwhile) and nodes.
            p = fmtInt(fmtStr(buf, "THINK ms="), (int32_t)thinkMs);
            p = fmtInt(fmtStr(p, " nodes="), (int32_t)eng::nodes());
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        case 'Y': {
            // Y: render cost by section on the board (table board overlays pieces hud fx, us).
            uint32_t us[6];
            gfx_wait();
            stage::profile(us);
            p = fmtStr(buf, "RPROF");
            for (int k = 0; k < 6; k++) { *p++ = ' '; p = fmtInt(p, (int32_t)us[k]); }
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'G': {
            match::Setup s;
            s.mode = (uint8_t)dbg::parseNum(args, 10);
            s.humanBlack = (uint8_t)dbg::parseNum(args, 10);
            s.level = (uint8_t)dbg::parseNum(args, 10);
            s.seed = dbg::parseNum(args, 10);
            s.rules = (uint8_t)(dbg::parseNum(args, 10) & eng::R_ALL);
            match::start(s);
            overlay = NONE; statsCounted = false;
            enter(Scr::Play);
            return true;
        }
        case 'M': {
            uint8_t f = (uint8_t)dbg::parseNum(args, 10), to = (uint8_t)dbg::parseNum(args, 10);
            stage::deselect();
            return match::play(f, to);
        }
#ifdef CHSIM
        case 'J': {
            static const char K[] = "TSOR";
            const char *q = strchr(K, args[0]);
            if (!q) return false;
            static const Scr S[] = {Scr::Title, Scr::Setup, Scr::Options, Scr::Rules};
            enter(S[q - K]);
            return true;
        }
        case 'R': {
            // R <sq>: the D-pad presses (U D L R) that take the glove to sq,
            // as the cursor steps between its spots (fewest presses).
            uint8_t list[32], n = spots(list), c = stage::cursor(), to = (uint8_t)dbg::parseNum(args, 10);
            bool on = false;
            for (uint8_t i = 0; i < n; i++) on |= list[i] == c;
            if (!on && n) c = nearest(list, n, c, 0, 0);
            static const int8_t DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
            uint8_t from[64], how[64], q[64], qh = 0, qt = 0;
            memset(from, 0xFF, sizeof from);
            from[c] = c; q[qt++] = c;
            while (qh < qt && from[to] == 0xFF) {
                uint8_t sq = q[qh++];
                for (uint8_t d = 0; d < 4; d++) {
                    uint8_t nx = nearest(list, n, sq, DX[d], DY[d]);
                    if (nx != 0xFF && from[nx] == 0xFF) { from[nx] = sq; how[nx] = d; q[qt++] = nx; }
                }
            }
            char path[48];
            uint8_t k = 0;
            if (from[to] == 0xFF) path[k++] = '?';
            else for (uint8_t sq = to; sq != c && k < 40; sq = from[sq]) path[k++] = "UDLR"[how[sq]];
            p = fmtStr(buf, "ROUTE ");
            while (k) *p++ = path[--k];
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'H': {
            // H: the board (a1..h8: w W white man, king; b B black) and who moves.
            static const char L[] = ".wW";
            char b[80];
            p = fmtStr(b, "BOARD ");
            for (uint8_t sq = 0; sq < 64; sq++) {
                uint8_t pc = match::board[sq];
                char ch = L[pc & eng::TYPE];
                *p++ = (pc & eng::BLACK) ? (char)(ch == 'w' ? 'b' : 'B') : ch;
            }
            *p++ = ' '; *p++ = match::blackToMove() ? 'b' : 'w';
            *p++ = ' '; *p++ = match::humanToMove() && !showing() && overlay == NONE ? '1' : '0';
            *p++ = ' '; *p++ = stage::waiting() ? '1' : '0';
            *p++ = ' '; *p++ = match::active() ? '0' : '1';
            fmtStr(p, "\n");
            dbg::print(b);
            return true;
        }
        case 'A': {
            // A: the hop a strong player would make now: AUTO <from> <to> <held>
            // (held 1: the piece is in hand already, mid-jump; 2: and the game
            // plays the only way on by itself), or AUTO none.
            int16_t i = match::humanToMove() ? eng::benchThink({6000, 0}) : -1;
            eng::Step st;
            if (i < 0 || !eng::stepAt((uint8_t)i, st)) { dbg::print("AUTO none\n"); return true; }
            uint8_t held = match::chainSq() == 0xFF ? 0 : eng::stepCount() == 1 ? 2 : 1;
            p = fmtInt(fmtStr(buf, "AUTO "), st.from);
            *p++ = ' '; p = fmtInt(p, st.to);
            *p++ = ' '; p = fmtInt(p, held);
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'V':
        case 'X': {
            // X: two players from a position; V: you, the side to move,
            // against the CPU (TOURIST).
            while (*args == ' ') args++;
            char cells[33];
            uint8_t n = 0;
            while (n < 32 && *args && *args != ' ') cells[n++] = *args++;
            cells[n] = 0;
            while (*args == ' ') args++;
            bool black = *args == 'b';
            if (*args) args++;
            uint8_t rules = *args ? (uint8_t)(dbg::parseNum(args, 10) & eng::R_ALL) : (uint8_t)eng::R_FORCED;
            match::Setup s = {cmd == 'V' ? (uint8_t)match::VS_CPU : (uint8_t)match::TWO_PLAYER, (uint8_t)black, 0, rules, 1};
            match::startAt(s, cells, black);
            overlay = NONE; statsCounted = false;
            enter(Scr::Play);
            return true;
        }
#endif
    }
    return false;
}
// Game commands wait while the CPU searches or plays (a scripted move
// arrives when it is the human's turn, as a press would); the render
// profile and the think report never touch the game, so they run at once.
static bool searching(char cmd) {
    if (cmd == 'Y' || cmd == 'W' || cmd == 'R' || cmd == 'H') return false;
    if (stage::waiting()) stage::acknowledge();           // a scripted command answers the last banner as a press would
    return match::cpuThinking() || (cur == Scr::Play && match::active() && (!match::humanToMove() || showing()));
}
#endif

// ---------------------------------------------------------------------------
void begin() {
    stage::begin();
    opt.sound = 1;
    opt.music = 1;
    opt.rules = eng::R_FORCED;
    save::load(opt, stats, hasGame);
    applyOptions();
#if CHGAME_DEBUG
    dbg::hook = debugHook;
    dbg::holdWhile(searching);
#endif
    enter(Scr::Title);
}

bool holdFrames() { return cur == Scr::Play && (overlay != NONE || bHeld); }

void update(bool thinking) {
    t++;
    if (thinking) {
        if (cur == Scr::Play) playUpdate(true);
        fx::update();
        return;
    }
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
        case Scr::Play:    playUpdate(false); break;
#if !CHCK_LEAN
        case Scr::Options:
        case Scr::Rules:   optionsUpdate(); break;
#else
        default: break;
#endif
    }
    fx::update();
}

void render(uint32_t frame) {
    switch (cur) {
        case Scr::Title:   titleRender(frame); break;
        case Scr::Setup:   setupRender(frame); break;
        case Scr::Play:    playRender(frame); break;
#if !CHCK_LEAN
        case Scr::Options:
        case Scr::Rules:   optionsRender(frame); break;
#else
        default: break;
#endif
    }
}

}  // namespace screens
