// The screens (Screens.h): title, setup, play with its overlays, the
// results, options; and the game's debug commands.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library)
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "config.h"
#include "Screens.h"
#include "Fx.h"
#include "Sounds.h"
#include "BoardView.h"
#include "Game.h"
#include "Stage.h"
#include "src/assets/Assets.h"
#include "Save.h"
#ifdef CHSIM
#include <sim.h>
#endif

namespace screens {

using namespace game;

enum class Scr : uint8_t { Title, Setup, Play, Options };
static Scr cur = Scr::Title, pending = Scr::Title;
static uint16_t t;                   // frames on this screen
static uint8_t fadeOut, fadeIn;
static uint8_t sel;                  // menu cursor
#if !CHSN_LEAN
static Scr optBack = Scr::Title;
#endif

static Options opt;
static Stats stats;
static bool hasGame;                 // a saved game is waiting

// Play-screen overlays.
enum Overlay : uint8_t { NONE, PAUSE, RESULT };
static Overlay overlay;
static bool statsCounted;

// Where everyone stood as each turn began, for the result's graph: the
// story of the game. When it fills up, every other entry goes and it is
// kept half as often. (A continued game's story starts there.)
static const uint8_t HIST = 64;
static uint8_t hist[SEATS][HIST];
static uint8_t nHist, histEvery;
static uint16_t histTurn;

static void record() {
    if (nHist == HIST) {
        for (uint8_t p = 0; p < SEATS; p++)
            for (uint8_t k = 0; k < HIST / 2; k++) hist[p][k] = hist[p][2 * k];
        nHist = HIST / 2;
        histEvery = (uint8_t)(histEvery * 2);
    }
    for (uint8_t p = 0; p < st.players; p++) hist[p][nHist] = st.pos[p];
    nHist++;
}

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------
static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

static uint32_t seedNow() { return micros() * 2654435761u ^ rpgame.frameCount; }

// The board behind the title: four CPUs playing ARCADE, close up.
static void startDemo() {
    Setup demo = {{CPU + 1, CPU + 2, CPU, CPU + 1}, ARCADE, 7u + t};
    start(demo);
}

static void enter(Scr s) {
    cur = s;
    stage::invalidate();
    stage::setDemo(s == Scr::Title);
    t = 0;
    sel = 0;
    fadeIn = 8;
    fx::clear();
    if (s == Scr::Title) {
        startDemo();
        audio::sfx(Sfx::Title);
    }
    if (s == Scr::Setup) sel = 5;                // on BEGIN: the table is set, A starts
    if (s == Scr::Play) {
        overlay = NONE;
        statsCounted = false;
        nHist = 0;
        histEvery = 1;
        histTurn = 0;
    }
}

// Options and records are saved as they change; the game only when you
// SAVE + QUIT (a game already saved stays until it is continued).
static void persist(bool thisGame = false) {
    gfx_wait();                      // save builds its page in the chunk scratch
    save::store(opt, stats, thisGame ? save::THIS_GAME : hasGame ? save::SAVED_GAME : save::NO_GAME);
    hasGame |= thisGame;
}

static void applyOptions() {
    audio::setOn(opt.sound != 0);
    stage::setFast(opt.pace != 0);
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

// Big lettering in PPOT's font (3x) with a gradient, outline and shadow. The
// top rows are FX_B, so the palette makes it shimmer.
static void title35(const char *text, int y) {
    int w = text35WidthScaled(text, 3);
    Mask m = maskBegin(w, 18);                   // just the lettering's bytes
    maskText35(m, 0, 0, text, 3);
    uint8_t ramp[20];
    for (int i = 0; i < 20; i++) ramp[i] = i < 3 ? FX_B : (i < 13 ? GOLD : WOOD);
    maskDraw(m, 64 - w / 2, y, 0, INK, WINE, ramp);
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

static void menuPanel(int y, int h) { panel(14, y, 100, h, 3, NAVY, GOLD); }

// ---------------------------------------------------------------------------
// Title: the game playing itself behind the lettering.
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_ONE, I_TWO, I_CONTINUE, I_OPTIONS };
static const char *const ITEM[4] = {"1 PLAYER", "2 PLAYERS", "CONTINUE", "OPTIONS"};

static uint8_t titleItems(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) items[n++] = I_CONTINUE;
    items[n++] = I_ONE;
    items[n++] = I_TWO;
#if !CHSN_LEAN
    items[n++] = I_OPTIONS;
#endif
    return n;
}

static void titleUpdate() {
    uint8_t items[4], n = titleItems(items);
    if (sel >= n) sel = 0;
    if (menuNav(n)) {
        audio::sfx(Sfx::Select);
        switch (items[sel]) {
            case I_ONE: case I_TWO:
                // You against the CPU, or the two of you; the table keeps
                // any CPUs it had beyond that, and can be changed.
                opt.seat[0] = HUMAN;
                if (items[sel] == I_TWO) opt.seat[1] = HUMAN;
                else if (opt.seat[1] < CPU) opt.seat[1] = CPU + 1;
                for (uint8_t i = 2; i < SEATS; i++) if (opt.seat[i] == HUMAN) opt.seat[i] = OFF;
                go(Scr::Setup);
                return;
            case I_CONTINUE:
                // Continuing takes the game out of the save.
                if (!save::loadGame()) { audio::sfx(Sfx::Deny); startDemo(); }
                else go(Scr::Play);
                hasGame = false;
                persist();
                return;
#if !CHSN_LEAN
            case I_OPTIONS: optBack = Scr::Title; go(Scr::Options); return;
#endif
        }
    }
    game::update(stage::busy());
    stage::update();
    if (phase() == P_OVER && stage::overShown()) startDemo();
}

// The game's own lettering (tools/art: LOGO_*), after CHBlackjack's: gold
// over wood, outlined, with a shadow. Its top rows are FX_B, so the palette
// makes it shimmer.
static void logo(const uint8_t *bits, uint8_t w, uint8_t h, int y) {
    Mask m = maskBegin(w, h);
    maskBlit1(m, bits, w, h);
    uint8_t ramp[LOGO_SNAKES_H];
    for (int i = 0; i < h; i++) ramp[i] = i * 14 < 3 * h ? FX_B : (i * 14 < 10 * h ? GOLD : WOOD);
    maskDraw(m, 64 - w / 2, y, 0, INK, WINE, ramp);
}

static void titleRender(uint32_t frame) {
    uint8_t items[4], n = titleItems(items);
    stage::render(frame, frame);
    dither(0, 0, 128, 50, INK, 0);
    logo(LOGO_SNAKES, LOGO_SNAKES_W, LOGO_SNAKES_H, 2);
    logo(LOGO_LADDERS, LOGO_LADDERS_W, LOGO_LADDERS_H, 33);
    int y0 = 128 - n * 13 - 1;
    dither(0, y0 - 5, 128, 128 - y0 + 5, INK, 1);
    for (uint8_t i = 0; i < n; i++) menuItem(y0 + i * 13, ITEM[items[i]], i == sel, frame);
}

// ---------------------------------------------------------------------------
// Setup: who sits at the table, and which game.
// ---------------------------------------------------------------------------
static const char *const SEAT[5] = {"EMPTY", "PLAYER", "CPU EASY", "CPU FAIR", "CPU SHARK"};

static void newGame() {
    Setup s;
    memcpy(s.kind, opt.seat, SEATS);
    s.mode = opt.mode;
    s.seed = seedNow();
    start(s);
    go(Scr::Play);
}

static void setupUpdate() {
    int d = rpgame.repeat(RIGHT_BUTTON) ? 1 : (rpgame.repeat(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel < 5) { sel++; audio::sfx(Sfx::Cursor); }
    if (d && sel < 4) {
        // The first two seats are always taken. In CLASSIC a CPU has nothing
        // to decide, so there is one kind of it.
        uint8_t &k = opt.seat[sel];
        do k = (uint8_t)((k + 5 + d) % 5);
        while ((!k && sel < 2) || (opt.mode == CLASSIC && k > HUMAN && k != CPU + 1));
        audio::sfx(Sfx::Coin);
    }
    if (d && sel == 4) { opt.mode ^= 1; audio::sfx(Sfx::Coin); }
    if (rpgame.justPressed(A_BUTTON)) {
        if (sel < 5) { sel++; audio::sfx(Sfx::Cursor); }
        else { audio::sfx(Sfx::Select); persist(); newGame(); }
    }
    if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void arrows(int y, int x0, int x1, uint32_t frame) {
    int bob = (frame >> 3) & 1;
    text35(x0 - 6 - bob, y, "<", GOLD);
    text35(x1 + 3 + bob, y, ">", GOLD);
}

static void setupRender(uint32_t frame) {
    feltBackdrop();
    title35("THE TABLE", 8);
    for (uint8_t i = 0; i < 5; i++) {
        int y = 31 + i * 13;
        bool on = sel == i;
        const char *s;
        if (on) fillRound(10, y - 3, 108, 15, 3, NAVY);
        if (i < 4) {
            if (opt.seat[i]) sprite4(TOKEN[i], 14, y - 1, RM_ID);
            s = opt.mode == CLASSIC && opt.seat[i] >= CPU ? "CPU" : SEAT[opt.seat[i]];
        } else s = opt.mode == ARCADE ? "ARCADE" : "CLASSIC";
        if (i == 4 && !on) text35(15, y + 3, "GAME", FELT_LT);
        text35x2(37, y, s, on ? GOLD : (i < 4 && !opt.seat[i]) ? FELT_LT : WHITE);
        if (on) arrows(y + 3, 37, 37 + text35x2Width(s), frame);
    }
    centred35(96, opt.mode == ARCADE ? "PICK A DIE. BUMP YOUR RIVALS." : "ONE DIE. A SIX ROLLS AGAIN.", CYAN);
    menuItem(107, "BEGIN", sel == 5, frame);
}

// ---------------------------------------------------------------------------
// Play: input
// ---------------------------------------------------------------------------
enum Action : uint8_t { A_RESUME, A_OPTIONS, A_QUIT };
static const char *const PAUSE_ITEM[3] = {"RESUME", "OPTIONS", "SAVE + QUIT"};

static void pauseInput() {
    bool startKey = rpgame.justPressed(START_BUTTON);
    if (menuNav(3) || startKey) {
        uint8_t a = startKey ? (uint8_t)A_RESUME : sel;
        overlay = NONE;
        audio::sfx(Sfx::Select);
#if !CHSN_LEAN
        if (a == A_OPTIONS) { optBack = Scr::Play; go(Scr::Options); }
#endif
        if (a == A_QUIT) { hasGame = false; persist(active()); go(Scr::Title); }
        return;
    }
    if (rpgame.justPressed(B_BUTTON)) overlay = NONE;
}

static uint16_t rounds() { return (uint16_t)((st.turn + st.players - 1) / st.players); }

static void countResult() {
    if (statsCounted) return;
    statsCounted = true;
    stats.games++;
    if (isHuman(st.winner)) {
        stats.humanWins++;
        if (!stats.fastest || rounds() < stats.fastest) stats.fastest = rounds();
    } else stats.cpuWins++;
    for (uint8_t p = 0; p < st.players; p++)
        if (isHuman(p)) { stats.ladders = (uint16_t)(stats.ladders + st.ladders[p]); stats.snakes = (uint16_t)(stats.snakes + st.snakes[p]); }
    persist();
}

static void playUpdate() {
    switch (overlay) {
        case PAUSE: pauseInput(); return;            // the game stands still
        case RESULT:
            if (rpgame.justPressed(A_BUTTON)) { audio::sfx(Sfx::Select); newGame(); }
            if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
            break;
        default:
            if (rpgame.justPressed(START_BUTTON)) { overlay = PAUSE; sel = 0; audio::sfx(Sfx::Select); return; }
            // SELECT: the whole board, and back.
            if (rpgame.justPressed(SELECT_BUTTON)) { stage::setOverview(!stage::overview()); audio::sfx(Sfx::Whoosh); }
            if (!humanToAct() || stage::busy()) break;
            if (phase() == P_ROLL) {
                if (rpgame.justPressed(A_BUTTON)) { stage::setOverview(false); roll(); }
            } else if (stage::picking()) {
                if (rpgame.justPressedMask() & (LEFT_BUTTON | RIGHT_BUTTON)) {
                    stage::setPick(rpgame.justPressed(RIGHT_BUTTON));
                    audio::sfx(Sfx::Cursor);
                }
                if (rpgame.justPressed(A_BUTTON)) { stage::setOverview(false); pick(stage::pickSel()); audio::sfx(Sfx::Select); }
            }
            break;
    }
    if (st.turn != histTurn && active()) {
        histTurn = st.turn;
        if (st.turn % histEvery == 0) record();
    }
    game::update(stage::busy());
    stage::update();
    if (phase() == P_OVER && stage::overShown() && overlay != RESULT) {
        countResult();
        record();
        overlay = RESULT;
        t = 0;
    }
}

// ---------------------------------------------------------------------------
// Play: what is drawn over the stage
// ---------------------------------------------------------------------------

// Who finished where: a bar for each growing as the count goes, what each
// climbed and slid down, and the story of the game.
static void drawResult(uint32_t frame) {
    char buf[16];
    uint8_t order[SEATS], n = st.players;
    for (uint8_t p = 0; p < n; p++) order[p] = p;
    for (uint8_t i = 0; i < n; i++)
        for (uint8_t j = (uint8_t)(i + 1); j < n; j++)
            if (st.pos[order[j]] > st.pos[order[i]]) { uint8_t s = order[i]; order[i] = order[j]; order[j] = s; }
    menuPanel(14, 102);
    buf[0] = 'P'; buf[1] = (char)('1' + st.winner); fmtStr(buf + 2, " WINS!");
    centred2(18, buf, FX_B);
    int grow = fx::ease(fx::OUT_CUBIC, t > 60 ? 60 : t, 60);
    for (uint8_t i = 0; i < n; i++) {
        uint8_t p = order[i];
        int y = 32 + i * 9;
        sprite4(TOKEN[p], 17, y - 3, RM_ID);
        gfx_fillRect(31, y, (st.pos[p] * 34 / 100 * grow) >> 8, 5, stage::SEAT_COLOUR[p]);
        fmtInt(buf, (st.pos[p] * grow) >> 8);
        text35(79 - text35Width(buf), y, buf, i ? SILVER : GOLD);
        // Ladders up, snakes down.
        char *q = fmtStr(fmtInt(buf, st.ladders[p]), "L ");
        fmtStr(fmtInt(q, st.snakes[p]), "S");
        text35(110 - text35Width(buf), y, buf, i ? SILVER : WHITE);
    }
    // Everyone's square, turn by turn, drawn out left to right (the
    // winner's line last, on top).
    int y0 = 35 + n * 9, y1 = 104;
    gfx_hline(20, y1, 89, FELT_DK);
    if (nHist > 1) {
        int h = y1 - 1 - y0, shown = (nHist - 1) * grow >> 8;
        for (uint8_t i = n; i--;) {
            uint8_t p = order[i];
            for (int k = 0; k < shown; k++)
                gfx_line(20 + k * 88 / (nHist - 1), y1 - 1 - hist[p][k] * h / 100,
                         20 + (k + 1) * 88 / (nHist - 1), y1 - 1 - hist[p][k + 1] * h / 100, stage::SEAT_COLOUR[p]);
        }
    }
    if (frame & 32) centred35(108, "A AGAIN   B MENU", WHITE);
}

static void playRender(uint32_t frame) {
    uint32_t ui = overlay | (sel << 4);
    if (overlay) ui ^= (frame >> 3) << 8;        // blinking borders
    if (overlay == RESULT) ui = frame;
    if (!stage::render(frame, ui)) return;
    if (overlay == PAUSE) {
        char buf[20];
        fmtInt(fmtStr(fmtStr(buf, st.mode == ARCADE ? "ARCADE" : "CLASSIC"), "  ROUND "), rounds());
        menuPanel(30, 62);
        centred35(34, buf, GOLD);
        for (uint8_t i = 0; i < 3; i++) {
            int y = 46 + i * 14;
            if (i == sel) fillRound(18, y - 3, 92, 15, 3, INK);
            centred2(y, PAUSE_ITEM[i], i == sel ? FX_B : WHITE);
        }
    } else if (overlay == RESULT) {
        drawResult(frame);
    }
}

// ---------------------------------------------------------------------------
// Options (device debug builds leave the screen out to fit the protocol)
// ---------------------------------------------------------------------------
#if !CHSN_LEAN
enum Opt : uint8_t { O_SOUND, O_PACE, O_BACK, OPT_COUNT };
static const char *const OPT_TEXT[OPT_COUNT] = {"SOUND|OFF|ON", "PACE|FUN|QUICK", "BACK"};

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
        uint8_t &f = ((uint8_t *)&opt)[sel];
        f = (uint8_t)((f + n + d) % n);
        applyOptions();
        audio::sfx(Sfx::Coin);
    }
    if ((rpgame.justPressed(A_BUTTON) && sel == O_BACK) || rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        persist();
        go(optBack);
    }
}

static void optionsRender(uint32_t frame) {
    feltBackdrop();
    title35("OPTIONS", 7);
    char buf[40];
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 32 + i * 14;
        char label[12], value[12];
        optField(OPT_TEXT[i], 0, label);
        if (i == sel) {
            fillRound(8, y - 3, 112, 15, 3, NAVY);
            roundRect(8, y - 3, 112, 15, 3, (frame & 16) ? FX_B : GOLD);
        }
        if (i == O_BACK) { centred2(y, label, i == sel ? GOLD : WHITE); continue; }
        text35x2(15, y, label, i == sel ? GOLD : WHITE);
        optField(OPT_TEXT[i], (uint8_t)(((uint8_t *)&opt)[i] + 1), value);
        text35x2(114 - text35x2Width(value), y, value, i == sel ? WHITE : FELT_LT);
    }
    // The house's records.
    char *p = fmtInt(fmtStr(buf, "GAMES "), stats.games);
    p = fmtInt(fmtStr(p, "  YOU "), stats.humanWins);
    fmtInt(fmtStr(p, "  CPU "), stats.cpuWins);
    centred35(80, buf, GOLD);
    p = fmtInt(fmtStr(buf, "LADDERS "), stats.ladders);
    fmtInt(fmtStr(p, "  SNAKES "), stats.snakes);
    centred35(88, buf, GOLD);
    if (stats.fastest) {
        fmtStr(fmtInt(fmtStr(buf, "FASTEST WIN "), stats.fastest), " ROUNDS");
        centred35(96, buf, GOLD);
    }
    centred35(108, "SNAKES AND LADDERS " CHSN_VERSION, SILVER);
    centred35(115, "FONT: PRESS PLAY ON TAPE", SILVER);
}
#endif

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   G <k0> <k1> <k2> <k3> <mode> <seed>     start a game (kinds: 0 empty, 1 human, 2-4 CPU; mode 0 CLASSIC, 1 ARCADE)
//   D <d1> <d2>                             the next dice (CLASSIC throws one: D 4 0)
//   M <player> <square>                     a token
//   J <0|1|2>                               jump to title/setup/options
//   H                                       BOARD <phase> <at the turn> <waiting for you 0|1> 0
//   O <0|1>                                 the overview
//   V <square> <zoom>                       (simulator) look at a square
//   X <light> <dark> <numbers>              (simulator) the squares' tones; numbers at the plain size
static bool debugHook(char cmd, const char *args) {
    char buf[48], *p;
    uint32_t a = dbg::parseNum(args, 10), b = dbg::parseNum(args, 10), c = dbg::parseNum(args, 10);
    switch (cmd) {
        case 'G': {
            Setup s = {{(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)dbg::parseNum(args, 10)}, 0, 0};
            s.mode = (uint8_t)dbg::parseNum(args, 10);
            s.seed = dbg::parseNum(args, 10);
            start(s);
            enter(Scr::Play);
            fadeIn = fadeOut = 0;
            pal::setFade(16);
            return true;
        }
        case 'D': forceDice((uint8_t)a, (uint8_t)b); return true;
        case 'M': st.pos[a & 3] = (uint8_t)b; stage::reset(); return true;
        case 'O': stage::setOverview(a != 0); return true;
        case 'H':
            p = fmtInt(fmtStr(buf, "BOARD "), phase());
            p = fmtInt(fmtStr(p, " "), st.cur);
            p = fmtStr(p, humanToAct() && !stage::busy() && overlay == NONE ? " 1" : " 0");
            fmtStr(p, " 0\n");
            dbg::print(buf);
            return true;
#ifdef CHSIM
        case 'J': {
            static const Scr S[] = {Scr::Title, Scr::Setup, Scr::Options};
            if (a > 2) return false;
            enter(S[a]);
            return true;
        }
        case 'V': stage::lookAt((uint8_t)a, (uint8_t)(b ? b : 5)); return true;
        case 'X':
            board::light = (uint8_t)a;
            board::dark = (uint8_t)b;
            board::numbers = c != 0;
            stage::invalidate();
            return true;
#endif
    }
    return false;
}

// A scripted change to the game waits until the stage has caught up with it.
static bool settling(char cmd) {
    if (cmd != 'M' && cmd != 'O') return false;
    return cur == Scr::Play && stage::busy();
}
#endif

// ---------------------------------------------------------------------------
void begin() {
    stage::begin();
    opt.sound = 1;
    opt.seat[0] = HUMAN; opt.seat[1] = CPU + 1;
    opt.mode = ARCADE;
    save::load(opt, stats, hasGame);
    if (opt.mode > ARCADE) opt.mode = ARCADE;
    applyOptions();
#if CHGAME_DEBUG
    dbg::hook = debugHook;
    dbg::holdWhile(settling);
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
#if !CHSN_LEAN
        case Scr::Options: optionsUpdate(); break;
#endif
        default: break;
    }
    fx::update();
}

void render(uint32_t frame) {
    switch (cur) {
        case Scr::Title:   titleRender(frame); break;
        case Scr::Setup:   setupRender(frame); break;
        case Scr::Play:    playRender(frame); break;
#if !CHSN_LEAN
        case Scr::Options: optionsRender(frame); break;
#endif
        default: break;
    }
}

}  // namespace screens
