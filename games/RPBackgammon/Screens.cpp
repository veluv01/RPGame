// The screens and the moves between them: title, setup, play (the glove,
// hints, the cube, pause and the result), options; and the debug
// protocol's commands. The play screen's motion is Stage.cpp.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (hot pixel loops live in Draw/Mask/Table)
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "config.h"
#include "Screens.h"
#include "Font.h"
#include "Fx.h"
#include "Sounds.h"
#include "Table.h"
#include "Ai.h"
#include "Net.h"
#include "Cube.h"
#include "Match.h"
#include "Notation.h"
#include "Stage.h"
#include "Save.h"
#include "src/assets/Assets.h"
#ifdef CHSIM
#include <sim.h>
#endif

namespace screens {

using bg::BAR;
using bg::OFF;

enum class Scr : uint8_t { Title, Setup, Play, Options };
static Scr cur = Scr::Title, pending = Scr::Title;
static uint16_t t;                   // frames on this screen
static uint8_t fadeOut, fadeIn;
static uint8_t sel;                  // menu cursor
static bool twoPlayers;              // the setup screen is for a game between two people
#if !CHBG_LEAN
static Scr optBack = Scr::Title;
#endif

static Options opt;
static Stats stats;
static bool hasGame;                 // a saved game is waiting

// Play-screen overlays.
enum Overlay : uint8_t { NONE, PAUSE, ANSWER, RESULT };
static Overlay overlay;
static bool statsCounted;
static bg::Target held[4];           // where the picked-up checker may go
static uint8_t nHeld;
static bool resettle;                // a checker was just moved: the glove finds one that can move next

// Asking the CPU for a hint: it thinks a slice per tick, as for its own moves.
static bool asking;
static bg::Rng steady;               // (the expert has no use for it)

static const char *const OPPONENT[match::LEVELS] = {"BEGINNER", "EXPERT", "GRANDMASTER"};
static const char *const OPP_LINE[match::LEVELS] = {
    "STILL LEARNING", "ITS BEST PLAY, EVERY ROLL", "LOOKS A ROLL AHEAD"};
static const uint8_t LENGTHS[4] = {1, 3, 5, 7};

#if CHGAME_DEBUG
// The CPU's last think (debug W): how long, and its longest slice of a tick.
static uint32_t thinkAt, thinkMs, sliceUs;
static bool wasThinking;
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
    stage::invalidate();
    t = 0;
    sel = 0;
    fadeIn = 8;
    fx::clear();
    pal::setMode(pal::CASINO);
    if (s == Scr::Title) {
        // The board behind the title: set up to play, close up, the camera
        // drifting over it.
        match::Setup demo = {match::TWO_PLAYER, 0, 1, 1};
        match::start(demo);
        stage::update();
        audio::sfx(Sfx::Title);
    }
    if (s == Scr::Setup) sel = twoPlayers ? 1 : 2;
    if (s == Scr::Play) stage::opponentName = OPPONENT[match::setup.level];
}

static void persist(bool withGame) {
    gfx_wait();                      // save builds its page in the chunk scratch
    save::store(opt, stats, withGame);
    hasGame = withGame;
}

static void applyOptions() {
    audio::setOn(opt.sound != 0);
    pal::setTheme(opt.felt);
    stage::setFast(opt.speed != 0);
    table::mirror = opt.mirror != 0;
    match::autoPlay = !(opt.rules & RULE_MANUAL);
    match::beavers = (opt.rules & RULE_BEAVERS) != 0;
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

// The display font's lettering with a gradient and an outline: the top
// rows FX_B, so the palette makes them shimmer.
static void ramp(uint8_t *r) {
    for (int i = 0; i < FONT_H + 2; i++) r[i] = i < 3 ? FX_B : (i < 8 ? GOLD : WOOD);
}

static void heading(const char *text, int y) {
    int w = fontWidth(text);
    Mask m = maskBegin(w, FONT_H);
    maskFont(m, 0, 0, text);
    uint8_t r[FONT_H + 2];
    ramp(r);
    maskDraw(m, 64 - w / 2, y, 0, INK, -1, r);
}

static void centred35(int y, const char *s, uint8_t c) { text35(64 - text35Width(s) / 2, y, s, c); }
// The display font, plain: menus, choices, the panels' first lines.
static void centred2(int y, const char *s, uint8_t c) { fontText(64 - fontWidth(s) / 2, y, s, c); }

// A menu line in the 3x5 font at twice the size, boxed while chosen with
// two clear pixels between the frame and the lettering.
static void menuItem(int y, const char *s, bool on, uint32_t frame) {
    int w = text35x2Width(s), x = 64 - w / 2;
    if (on) {
        fillRound(x - 7, y - 4, w + 14, 17, 3, NAVY);
        roundRect(x - 7, y - 4, w + 14, 17, 3, (frame & 16) ? FX_B : GOLD);
    }
    text35x2(x, y, s, on ? GOLD : WHITE);
}

static bool menuNav(uint8_t n) {
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel + 1 < n) { sel++; audio::sfx(Sfx::Cursor); }
    return rpgame.justPressed(A_BUTTON);
}

// The dice are seeded from when you pressed the button: your timing, not ours.
static uint32_t seedNow() { return micros() * 2654435761u ^ rpgame.frameCount; }

// ---------------------------------------------------------------------------
// Title: the board set up to play, the camera drifting over it.
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_ONE, I_TWO, I_CONTINUE, I_OPTIONS };
static const char *const ITEM[4] = {"1 PLAYER", "2 PLAYERS", "CONTINUE", "OPTIONS"};

static uint8_t titleItems(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) items[n++] = I_CONTINUE;
    items[n++] = I_ONE;
    items[n++] = I_TWO;
#if !CHBG_LEAN
    items[n++] = I_OPTIONS;
#endif
    return n;
}

static void newGame(uint8_t mode) {
    match::Setup s;
    s.mode = mode;
    s.level = opt.level;
    s.length = LENGTHS[opt.length & 3];
    s.seed = seedNow();
    match::start(s);
    overlay = NONE;
    statsCounted = false;
    go(Scr::Play);
}

static void titleUpdate() {
    uint8_t items[5], n = titleItems(items);
    if (sel >= n) sel = 0;
    if (menuNav(n)) {
        audio::sfx(Sfx::Select);
        switch (items[sel]) {
#if CHBG_LEAN
            case I_ONE: newGame(match::VS_CPU); break;
            case I_TWO: newGame(match::TWO_PLAYER); break;
#else
            case I_ONE: twoPlayers = false; go(Scr::Setup); break;
            case I_TWO: twoPlayers = true; go(Scr::Setup); break;
#endif
            case I_CONTINUE:
                if (save::loadGame()) { overlay = NONE; statsCounted = false; go(Scr::Play); }
                else { hasGame = false; audio::sfx(Sfx::Deny); }
                break;
#if !CHBG_LEAN
            case I_OPTIONS: optBack = Scr::Title; go(Scr::Options); break;
#endif
        }
    }
    // Drift over the board, close up: a slow figure of eight.
    int a = (int)t / 3;
    table::zoom = 10;
    table::setCamera(64 + ((fx::isin(a) * 32) >> 8), 64 + ((fx::isin(a * 2) * 27) >> 8));
}

static void titleRender(uint32_t frame) {
    stage::renderScene(frame);
    // The sign: "Backgammon" on a navy plaque framed in gold.
    fillRound(4, 3, 120, 24, 4, NAVY);
    roundRect(4, 3, 120, 24, 4, GOLD);
    heading("Backgammon", 9);
    uint8_t items[5], n = titleItems(items);
    int y0 = 128 - n * 15;
    dither(0, y0 - 6, 128, 128 - y0 + 6, INK, 1);
    for (uint8_t i = 0; i < n; i++) menuItem(y0 + i * 15, ITEM[items[i]], i == sel, frame);
}

// ---------------------------------------------------------------------------
// Setup: the opponent, and how long a match.
// ---------------------------------------------------------------------------
#if !CHBG_LEAN
static void setupUpdate() {
    uint8_t rows = twoPlayers ? 2 : 3, row = (uint8_t)(sel + (twoPlayers ? 1 : 0));     // 0 opponent, 1 match, 2 begin
    int d = rpgame.repeat(RIGHT_BUTTON) ? 1 : (rpgame.repeat(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel + 1 < rows) { sel++; audio::sfx(Sfx::Cursor); }
    if (d && row == 0) { opt.level = (uint8_t)((opt.level + match::LEVELS + d) % match::LEVELS); audio::sfx(Sfx::Coin); }
    if (d && row == 1) { opt.length = (uint8_t)((opt.length + 4 + d) & 3); audio::sfx(Sfx::Coin); }
    // Hold SELECT to wipe your record against this opponent.
    static uint8_t hold;
    hold = rpgame.pressed(SELECT_BUTTON) ? (uint8_t)(hold + 1) : 0;
    if (hold == 90 && !twoPlayers) {
        stats.won[opt.level] = stats.lost[opt.level] = stats.mwon[opt.level] = stats.mlost[opt.level] = 0;
        persist(hasGame);
        audio::sfx(Sfx::Hit);
    }
    if (rpgame.justPressed(A_BUTTON)) {
        if (row < 2) { sel++; audio::sfx(Sfx::Cursor); }
        else { audio::sfx(Sfx::Select); persist(hasGame); newGame(twoPlayers ? match::TWO_PLAYER : match::VS_CPU); }
    }
    if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

// A setup choice: boxed while chosen, with bobbing arrows to change it.
static void choice(int y, const char *s, bool on, uint32_t frame) {
    int w = fontWidth(s, 0), bob = (frame >> 3) & 1;
    if (on) fillRound(64 - w / 2 - 5, y - 3, w + 10, 16, 3, NAVY);
    fontText(64 - w / 2, y, s, on ? GOLD : WHITE, 0);
    text35(64 - w / 2 - 6 - bob, y + 3, "<", GOLD);
    text35(64 + w / 2 + 3 + bob, y + 3, ">", GOLD);
}

static void setupRender(uint32_t frame) {
    feltBackdrop();
    uint8_t row = (uint8_t)(sel + (twoPlayers ? 1 : 0));
    char buf[32], *p;
    if (twoPlayers) {
        heading("TWO PLAYERS", 9);
        centred35(34, "WHITE AND RED TAKE TURNS", FELT_LT);
    } else {
        heading("OPPONENT", 7);
        // Stars for how hard they play.
        for (int i = 0; i <= opt.level; i++) text35(64 - opt.level * 4 + i * 8, 25, "*", FX_B);
        choice(35, OPPONENT[opt.level], row == 0, frame);
        centred35(50, OPP_LINE[opt.level], FELT_LT);
        // Your record against them.
        p = fmtStr(buf, "GAMES ");
        p = fmtInt(p, stats.won[opt.level]); *p++ = '-';
        p = fmtInt(p, stats.lost[opt.level]); p = fmtStr(p, "   MATCHES ");
        p = fmtInt(p, stats.mwon[opt.level]); *p++ = '-';
        fmtInt(p, stats.mlost[opt.level]);
        centred35(58, buf, GOLD);
    }
    uint8_t len = LENGTHS[opt.length & 3];
    if (len == 1) fmtStr(buf, "SINGLE GAME");
    else fmtInt(fmtStr(buf, "MATCH TO "), len);
    choice(72, buf, row == 1, frame);
    centred35(88, len == 1 ? "NO DOUBLING CUBE" : "WITH THE DOUBLING CUBE", FELT_LT);
    menuItem(103, "BEGIN", row == 2, frame);
}
#endif

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
// Where the glove may go: your points with checkers on them (the bar alone
// while anything is on it) - or, once a checker is picked up, the places it
// can be set down.
static uint8_t spots(uint8_t *list) {
    uint8_t n = 0;
    if (stage::selected() != 0xFF) {
        for (uint8_t i = 0; i < nHeld; i++) list[n++] = held[i].to;
        return n;
    }
    const uint8_t *mine = match::board.n[match::side()];
    if (mine[BAR]) { list[0] = BAR; return 1; }
    for (uint8_t p = 1; p <= 24; p++) if (mine[p]) list[n++] = p;
    return n;
}

// Where the D-pad takes the glove from `from`. LEFT/RIGHT (ux): the next
// spot that way on the same half of the board (top or bottom), wrapping to
// the far end of that half. UP/DOWN (uy): the spot on the other half
// nearest across. (0, 0): simply the nearest spot.
static uint8_t nearest(const uint8_t *list, uint8_t n, uint8_t from, int ux, int uy) {
    int fx, fy;
    stage::spotXY(from, fx, fy);
    bool top = fy < table::CY;
    uint8_t best = 0xFF, wrap = 0xFF;
    int bestS = 0x7FFF, wrapS = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (list[i] == from) continue;
        int x, y;
        stage::spotXY(list[i], x, y);
        int dx = x - fx, dy = y - fy, ax = dx < 0 ? -dx : dx;
        bool same = (y < table::CY) == top;
        int sc;
        if (uy) sc = same ? 0x7FFF : ax;                         // across: the nearest over there
        else if (ux) {
            if (!same) continue;
            int along = dx * ux;
            if (along <= 0) { if (-along >= wrapS) { wrapS = -along; wrap = list[i]; } continue; }
            sc = along;                                          // that way: the next one
        } else sc = ax + (dy < 0 ? -dy : dy);
        if (sc < bestS) { bestS = sc; best = list[i]; }
    }
    return best != 0xFF ? best : wrap;
}

#if !CHBG_LEAN
// Ask the CPU for the best play of the roll from the turn's start.
static void ask() {
    ai::start(match::turnStart(), match::side(), match::die(0), match::die(1), ai::EXPERT, steady);
    asking = true;
}

static int32_t chancePct(int32_t score) { return (int32_t)((net::chance(score) * 100u + 32767u) >> 16); }

// The answer has come in.
static void asked() {
    ai::Play pl;
    ai::chosen(pl);
    uint8_t hit[4], to[4];
    bg::Board b = match::turnStart();
    for (uint8_t i = 0; i < pl.n; i++) {
        to[i] = bg::landing(pl.from[i], pl.die[i]);
        hit[i] = bg::doStep(b, match::side(), pl.from[i], pl.die[i]);
    }
    char text[48], *p;
    notate(p = fmtStr(text, "BEST "), pl.from, pl.die, hit, pl.n);
    p += strlen(p);
    if (!ai::racingNow() && p - text < 24) { p = fmtStr(p, "  "); p = fmtInt(p, chancePct(ai::bestScore())); fmtStr(p, "%"); }
    stage::advise(text, GOLD, to, pl.n);
    audio::sfx(Sfx::Coin);
    asking = false;
}
#endif

static void cubeInput() {
    // The glove goes between the dice and the cube.
    uint8_t c = stage::cursor();
    if (c != stage::AT_DICE && c != stage::AT_CUBE) stage::setCursor(c = stage::AT_DICE);
    if (match::canDouble() && (rpgame.repeat(LEFT_BUTTON) || rpgame.repeat(RIGHT_BUTTON) ||
                               rpgame.repeat(UP_BUTTON) || rpgame.repeat(DOWN_BUTTON))) {
        stage::setCursor(c = c == stage::AT_DICE ? stage::AT_CUBE : stage::AT_DICE);
        audio::sfx(Sfx::Cursor);
    }
    if (!match::canDouble() && c == stage::AT_CUBE) stage::setCursor(c = stage::AT_DICE);
    if (!rpgame.justPressed(A_BUTTON)) return;
    if (c == stage::AT_CUBE) { match::offerDouble(); stage::setCursor(stage::AT_DICE); }
    else match::roll();
}

static void playInput() {
    bool a = rpgame.justPressed(A_BUTTON), b = rpgame.justPressed(B_BUTTON);
    bool hintKey = rpgame.justPressed(SELECT_BUTTON);
    if (match::humanToRoll()) { cubeInput(); return; }
    if (match::humanToAnswer()) { overlay = ANSWER; sel = 0; return; }
#if !CHBG_LEAN
    if (hintKey && (match::humanToMove() || match::humanToConfirm()) && !asking) {
        stage::advise("THINKING...", SILVER, nullptr, 0);
        ask();
    }
#else
    (void)hintKey;
#endif
    if (match::humanToConfirm()) {
        // The dice are played: pick them up, or take the last checker back.
        stage::setCursor(stage::AT_DICE);
        if (a) { asking = false; stage::quiet(); match::confirm(); }
        else if (b) { asking = false; stage::quiet(); match::takeBack(); }
        return;
    }
    if (!match::humanToMove()) return;
    uint8_t list[26], n = spots(list), c = stage::cursor(), s = stage::selected();
    if (!n) return;
    // A new turn, or a checker just moved, may leave the glove on nothing
    // playable: it goes to the nearest checker that can move. (You can still
    // take it to one that cannot, to be told so.)
    bool on = false;
    bg::Target tg[4];
    for (uint8_t i = 0; i < n; i++) on |= list[i] == c;
    if (s == 0xFF && (!on || (resettle && !match::targetsFrom(c, tg)))) {
        uint8_t can[26], m = 0;
        for (uint8_t i = 0; i < n; i++) if (match::targetsFrom(list[i], tg)) can[m++] = list[i];
        stage::setCursor(c = m ? nearest(can, m, c, 0, 0) : on ? c : nearest(list, n, c, 0, 0));
    } else if (!on) stage::setCursor(c = nearest(list, n, c, 0, 0));
    resettle = false;
    int ux = 0, uy = 0;
    if (rpgame.repeat(UP_BUTTON)) uy = -1;
    if (rpgame.repeat(DOWN_BUTTON)) uy = 1;
    if (rpgame.repeat(LEFT_BUTTON)) ux = -1;
    if (rpgame.repeat(RIGHT_BUTTON)) ux = 1;
    if (ux || uy) {
        uint8_t to = nearest(list, n, c, ux, uy);
        if (to != 0xFF) { stage::setCursor(c = to); audio::sfx(Sfx::Cursor); }
    }
    if (b) {
        if (s != 0xFF) {
            stage::deselect();
            stage::setCursor(s);                 // back onto the checker
            audio::sfx(Sfx::Cursor);
        } else if (!match::takeBack()) audio::sfx(Sfx::Deny);
        resettle = true;
        return;
    }
    if (s != 0xFF) {
        if (!a) return;
        for (uint8_t i = 0; i < nHeld; i++)
            if (held[i].to == c) { match::play(s, held[i]); stage::deselect(); resettle = true; return; }
        return;
    }
    uint8_t k = match::targetsFrom(c, tg);
    stage::setBlocked(!k);
    if (!a) return;
    if (!k) { stage::deny(); return; }
    memcpy(held, tg, sizeof held);
    nHeld = k;
    stage::select(c, tg, k);
    uint8_t to[4];
    for (uint8_t i = 0; i < k; i++) to[i] = tg[i].to;
    stage::setCursor(nearest(to, k, c, 0, 0));
}

#ifdef CHSIM
// The cube's verdict on a double, for the debug protocol's player: 0 pass,
// 1 take, 2 beaver. Beavered, the doubler's: 1 take, 2 raccoon.
static uint8_t cubeSays() {
    uint8_t s = match::side() ^ (match::beavered() ? 0 : 1);     // the side answering
    int a = match::setup.length - match::score[s], b = match::setup.length - match::score[s ^ 1];
    uint32_t p = net::chance(net::eval(match::board, match::side() ^ 1));     // the side on roll's opponent, after its move
    if (s == match::side()) p = 65535u - p;
    if (!match::beavered() && !cube::wantsTake(a, b, match::cubeValue(), match::postCrawford(), p)) return 0;
    return match::canBeaver() && p >= cube::BEAVER_AT ? 2 : 1;
}
#endif

// The answer panel's choices: TAKE, PASS, BEAVER; beavered, TAKE, RACCOON.
static uint8_t answers() { return match::beavered() ? 2 : match::canBeaver() ? 3 : 2; }

static void answerInput() {
    if (menuNav(answers())) {
        overlay = NONE;
        audio::sfx(Sfx::Select);
        if (sel == 0) match::take();
        else if (match::beavered()) match::raccoon();
        else if (sel == 1) match::pass();
        else match::beaver();
    }
}

static const char *const PAUSE_ITEM[3] = {"RESUME", "RESIGN", "SAVE+QUIT"};

static void pauseInput() {
    bool start = rpgame.justPressed(START_BUTTON);
    if (menuNav(3) || start) {
        overlay = NONE;
        audio::sfx(Sfx::Select);
        if (start || sel == 0) return;
        if (sel == 1) match::resign();
        else { persist(match::active() || match::between()); go(Scr::Title); }
        return;
    }
    if (rpgame.justPressed(B_BUTTON)) overlay = NONE;
}

static void countResult() {
    if (statsCounted || match::setup.mode != match::VS_CPU) return;
    statsCounted = true;
    uint8_t lv = match::setup.level;
    bool won = match::winner == bg::WHITE;
    if (won) stats.won[lv]++; else stats.lost[lv]++;
    if (match::matchOver() && match::cubeLive()) { if (won) stats.mwon[lv]++; else stats.mlost[lv]++; }
}

static void playUpdate() {
    switch (overlay) {
        case PAUSE: pauseInput(); return;                            // the game waits
        case ANSWER: answerInput(); break;
        case RESULT:
            if (rpgame.justPressed(A_BUTTON)) {
                audio::sfx(Sfx::Select);
                overlay = NONE;
                statsCounted = false;
                if (match::between()) { match::nextGame(); persist(true); }
                else { persist(false); newGame(match::setup.mode); }
            }
            if (rpgame.justPressed(B_BUTTON)) {
                // Back to the menu; a match that goes on is saved at its next game.
                audio::sfx(Sfx::Select);
                bool on = match::between();
                if (on) match::nextGame();
                persist(on);
                go(Scr::Title);
            }
            break;
        default:
            if (stage::waiting()) {
                if (rpgame.justPressed(A_BUTTON | B_BUTTON | START_BUTTON)) { stage::acknowledge(); audio::sfx(Sfx::Select); }
                break;
            }
            if (rpgame.justPressed(START_BUTTON) && match::active()) { overlay = PAUSE; sel = 0; audio::sfx(Sfx::Select); break; }
            if (stage::ready()) playInput();
            break;
    }
#if !CHBG_LEAN
    if (asking && (match::humanToMove() || match::humanToConfirm()) && ai::step(64)) asked();
#endif
#if CHGAME_DEBUG
    bool th = match::cpuThinking();
    uint32_t t0 = micros();
    if (th && !wasThinking) { thinkAt = millis(); sliceUs = 0; }
#endif
    match::update(stage::busy());          // the CPU thinks a little in here
#if CHGAME_DEBUG
    if (th && micros() - t0 > sliceUs) sliceUs = micros() - t0;
    if (wasThinking && !match::cpuThinking()) thinkMs = millis() - thinkAt;
    wasThinking = match::cpuThinking();
#endif
    stage::update();
    if (!match::active() && !overlay && stage::overShown() && (match::between() || match::matchOver())) {
        countResult();
        overlay = RESULT;
    }
}

static void panel(int y, int h) {
    fillRound(10, y, 108, h, 3, NAVY);
    roundRect(10, y, 108, h, 3, GOLD);
}

static void playRender(uint32_t frame) {
    uint32_t ui = overlay | (sel << 4);
    if (!stage::render(frame, ui)) return;
    char buf[40], *p;
    bool vsCpu = match::setup.mode == match::VS_CPU;
    if (overlay == PAUSE) {
        panel(38, 50);
        for (uint8_t i = 0; i < 3; i++) {
            int y = 44 + i * 14;
            if (i == sel) fillRound(14, y - 3, 100, 16, 3, INK);
            centred2(y, PAUSE_ITEM[i], i == sel ? FX_B : WHITE);
        }
    } else if (overlay == ANSWER) {
        // Doubled: take, pass and lose what the cube says now, or beaver.
        // Beavered: take it, or raccoon.
        bool bv = match::beavered();
        uint8_t n = answers();
        int y0 = 72 - (n - 2) * 14;
        panel(y0, 41 + (n - 2) * 14);
        fmtInt(fmtStr(buf, bv ? "BEAVERED TO " : "DOUBLED TO "), match::cubeValue() * (bv ? 4 : 2));
        centred35(y0 + 4, buf, GOLD);
        static const char *const ANS[5] = {"TAKE", "PASS", "BEAVER", "TAKE", "RACCOON"};
        for (uint8_t i = 0; i < n; i++) {
            int y = y0 + 14 + i * 14;
            if (i == sel) fillRound(20, y - 3, 88, 16, 3, INK);
            centred2(y, ANS[i + (bv ? 3 : 0)], i == sel ? FX_B : WHITE);
        }
    } else if (overlay == RESULT) {
        bool white = match::winner == bg::WHITE, done = match::matchOver(), live = match::cubeLive();
        int y = 84 - (live ? 8 : 0);
        panel(y, 124 - y);
        const char *head;
        if (done && live) head = vsCpu ? (white ? "MATCH WON!" : "MATCH LOST") : white ? "WHITE WINS" : "RED WINS";
        else head = vsCpu ? (white ? "YOU WIN!" : "YOU LOSE") : white ? "WHITE WINS" : "RED WINS";
        centred2(y + 3, head, FX_B);
        static const char *const HOW[4] = {"", "", "A GAMMON", "A BACKGAMMON"};
        const char *why = match::reason == match::BY_RESIGNATION ? "BY RESIGNATION"
                        : match::reason == match::BY_PASS ? "THE DOUBLE PASSED" : HOW[match::how];
        int ly = y + 16;
        if (live) {
            p = fmtStr(buf, why);
            if (*why) p = fmtStr(p, ": ");
            p = fmtInt(p, match::points);
            fmtStr(p, match::points == 1 ? " POINT" : " POINTS");
            centred35(ly, buf, SILVER);
            p = fmtStr(buf, vsCpu ? "YOU " : "WHITE ");
            p = fmtInt(p, match::score[0]);
            p = fmtStr(p, vsCpu ? "  CPU " : "  RED ");
            p = fmtInt(p, match::score[1]);
            fmtInt(fmtStr(p, "  OF "), match::setup.length);
            centred35(ly += 8, buf, WHITE);
        } else centred35(ly, why, SILVER);
        centred35(114, done ? (vsCpu ? "A REMATCH   B MENU" : "A AGAIN   B MENU") : "A NEXT GAME   B MENU", WHITE);
    }
}

// ---------------------------------------------------------------------------
// Options (device debug builds leave the screen out to fit the protocol:
// their tests start games directly)
// ---------------------------------------------------------------------------
#if !CHBG_LEAN
enum Opt : uint8_t { O_SOUND, O_FELT, O_HOME, O_SPEED, O_AUTO, O_BEAVERS, O_BACK, OPT_COUNT };
static const char *const OPT_TEXT[OPT_COUNT] = {
    "SOUND|OFF|ON", "FELT|GREEN|BLUE|RED|PURPLE", "HOME|RIGHT|LEFT", "PACE|FUN|QUICK",
    "AUTO|ON|OFF", "BEAVERS|OFF|ON", "BACK",
};
// Each row's place in Options: a byte, or (high nibble) a bit of `rules`.
static const uint8_t OPT_AT[OPT_COUNT - 1] = {0, 1, 4, 2, 0x10, 0x20};
static uint8_t optGet(uint8_t i) {
    uint8_t a = OPT_AT[i];
    return a >> 4 ? (opt.rules >> ((a >> 4) - 1)) & 1 : ((uint8_t *)&opt)[a];
}
static void optSet(uint8_t i, uint8_t v) {
    uint8_t a = OPT_AT[i];
    if (a >> 4) opt.rules = (uint8_t)((opt.rules & ~(1 << ((a >> 4) - 1))) | v << ((a >> 4) - 1));
    else ((uint8_t *)&opt)[a] = v;
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
        optSet(sel, (uint8_t)((optGet(sel) + n + d) % n));
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
    heading("OPTIONS", 8);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 27 + i * 13;
        char label[12], value[12];
        optField(OPT_TEXT[i], 0, label);
        if (i == sel) {
            fillRound(6, y - 3, 116, 15, 3, NAVY);
            roundRect(6, y - 3, 116, 15, 3, (frame & 16) ? FX_B : GOLD);
        }
        if (i == O_BACK) { centred2(y, label, i == sel ? GOLD : WHITE); continue; }
        fontText(10, y, label, i == sel ? GOLD : WHITE);
        optField(OPT_TEXT[i], (uint8_t)(optGet(i) + 1), value);
        fontText(118 - fontWidth(value, 0), y, value, i == sel ? WHITE : FELT_LT, 0);
    }
    centred35(119, "3X5 FONT: PRESS PLAY ON TAPE", SILVER);
}
#endif

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   G <mode> <level> <seed> [length] start a match (mode 0 vs CPU, 1 two players; length 1 by default)
//   D <digits>                       the next rolls, a die a digit (D 6431)
//   C <length> <white> <red> <cube> <owner> <crawford>   the match: its length and score; the cube
//                                    (log2) and its owner (2: middle); whether this is the Crawford game
//   W                                the CPU's last choice: positions weighed, how long it took
//                                    (the stage's show included) and its longest slice of a tick
//   Y                                render cost by section on the board
//   E                                the network's opinion of the opening position (a check that
//                                    the device, the simulator and the trainer agree)
//   J <T|S|O>                        jump to title/setup/options
//   U <bits>                         the rules options (RULE_*: 1 manual, no auto play; 2 beavers)
//   X <side> <position>              two players from a position, <side> to roll
//   V <side> <level> <position>      ... you against the CPU
//   R <spot>                         the D-pad route to a spot: ROUTE UDLR..
//   A                                play for the human: the EXPERT's choice for the whole roll, and
//                                    its cube; throw, pick the dice up, answer a double
//   H                                the game: BOARD <White's 26 counts> <Red's> <side> <state> <ready>
static bool debugHook(char cmd, const char *args) {
    char buf[96], *p;
    switch (cmd) {
        case 'W':
            p = fmtInt(fmtStr(buf, "THINK positions="), (int32_t)ai::positions());
            p = fmtInt(fmtStr(p, " ms="), (int32_t)thinkMs);
            p = fmtInt(fmtStr(p, " slice_us="), (int32_t)sliceUs);
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        case 'Y': {
            uint32_t us[5];
            gfx_wait();
            stage::profile(us);
            p = fmtStr(buf, "RPROF");
            for (int k = 0; k < 5; k++) { *p++ = ' '; p = fmtInt(p, (int32_t)us[k]); }
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'E': {
            bg::Board b;
            bg::reset(b);
            net::forget();
            int32_t e = net::eval(b, bg::WHITE);
            p = fmtInt(fmtStr(buf, "NET "), e);
            p = fmtInt(fmtStr(p, " "), net::chance(e));
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'G': {
            match::Setup s;
            s.mode = (uint8_t)dbg::parseNum(args, 10);
            s.level = (uint8_t)dbg::parseNum(args, 10);
            s.seed = dbg::parseNum(args, 10);
            s.length = (uint8_t)dbg::parseNum(args, 10);
            match::start(s);
            overlay = NONE; statsCounted = false;
            enter(Scr::Play);
            return true;
        }
        case 'C': {
            uint8_t n = (uint8_t)dbg::parseNum(args, 10), w = (uint8_t)dbg::parseNum(args, 10);
            uint8_t r = (uint8_t)dbg::parseNum(args, 10), c = (uint8_t)dbg::parseNum(args, 10);
            uint8_t o = (uint8_t)dbg::parseNum(args, 10);
            match::setScore(n, w, r, c, o, dbg::parseNum(args, 10) != 0);
            return true;
        }
        case 'H': {
            p = fmtStr(buf, "BOARD ");
            for (uint8_t s = 0; s < 2; s++) {
                for (uint8_t i = 0; i < 26; i++) *p++ = "0123456789ABCDEF"[match::board.n[s][i]];
                *p++ = ' ';
            }
            *p++ = (char)('0' + match::side());
            *p++ = ' ';
            *p++ = match::matchOver() ? 'O' : match::between() ? 'B' : stage::waiting() ? 'W' : match::humanToRoll() ? 'R' :
                   match::humanToMove() ? 'M' : match::humanToConfirm() ? 'C' : match::humanToAnswer() ? 'D' : 'T';
            *p++ = ' ';
            *p++ = cur == Scr::Play && overlay == NONE && stage::ready() ? '1' : '0';
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'D':
            match::stackDice(args);
            return true;
        case 'U':
            opt.rules = (uint8_t)dbg::parseNum(args, 10);
            applyOptions();
            return true;
        case 'J': {
            static const char K[] = "TSO";
            const char *q = strchr(K, args[0]);
            if (!q) return false;
            static const Scr S[] = {Scr::Title, Scr::Setup, Scr::Options};
            enter(S[q - K]);
            return true;
        }
#ifdef CHSIM
        case 'A': {
            uint8_t s = match::side();
            int need = match::setup.length - match::score[s], other = match::setup.length - match::score[s ^ 1];
            if (match::humanToAnswer()) {
                overlay = NONE;
                uint8_t c = cubeSays();
                if (match::beavered()) { if (c == 2) match::raccoon(); else match::take(); }
                else if (c == 2) match::beaver();
                else if (c) match::take();
                else match::pass();
                return true;
            }
            if (match::humanToRoll()) {
                uint32_t pc = 65535u - net::chance(net::eval(match::board, s ^ 1));
                if (match::canDouble() && cube::wantsDouble(need, other, match::cubeValue(), match::cubeOwner == s,
                                                            match::postCrawford(), pc, cube::gammonish(match::board, s)))
                    match::offerDouble();
                else match::roll();
                return true;
            }
            if (match::humanToConfirm()) { match::confirm(); return true; }
            if (!match::humanToMove()) return false;
            ai::Play pl;
            ai::start(match::board, s, match::die(0), match::die(1), ai::EXPERT, steady);
            while (!ai::step(1000)) {}
            ai::chosen(pl);
            for (uint8_t i = 0; i < pl.n; i++) {
                bg::Target one = {bg::landing(pl.from[i], pl.die[i]), 1, {pl.die[i]}, 0};
                if (!match::play(pl.from[i], one)) return false;
            }
            return true;
        }
#endif
        case 'V':
        case 'X': {
            uint8_t side = (uint8_t)dbg::parseNum(args, 10);
            match::Setup s = {cmd == 'V' ? (uint8_t)match::VS_CPU : (uint8_t)match::TWO_PLAYER, 0, 1, 1};
            if (cmd == 'V') s.level = (uint8_t)dbg::parseNum(args, 10);
            if (!match::startPosition(s, args, side)) return false;
            overlay = NONE; statsCounted = false;
            enter(Scr::Play);
            return true;
        }
        case 'R': {
            // R <spot>: the D-pad presses (U D L R) that take the glove to the
            // spot, as the cursor steps between its spots (fewest presses).
            uint8_t list[26], n = spots(list), c = stage::cursor(), to = (uint8_t)dbg::parseNum(args, 10);
            bool on = false;
            for (uint8_t i = 0; i < n; i++) on |= list[i] == c;
            if (!on && n) c = nearest(list, n, c, 0, 0);
            static const int8_t DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
            uint8_t from[28], how[28], q[28], qh = 0, qt = 0;
            memset(from, 0xFF, sizeof from);
            if (to > 27 || c > 27) return false;
            from[c] = c; q[qt++] = c;
            while (qh < qt && from[to] == 0xFF) {
                uint8_t sq = q[qh++];
                for (uint8_t d = 0; d < 4; d++) {
                    uint8_t nx = nearest(list, n, sq, DX[d], DY[d]);
                    if (nx != 0xFF && from[nx] == 0xFF) { from[nx] = sq; how[nx] = d; q[qt++] = nx; }
                }
            }
            char path[32];
            uint8_t k = 0;
            if (from[to] == 0xFF) path[k++] = '?';
            else for (uint8_t sq = to; sq != c && k < 30; sq = from[sq]) path[k++] = "UDLR"[how[sq]];
            p = fmtStr(buf, "ROUTE ");
            while (k) *p++ = path[--k];
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
    }
    return false;
}
#endif

// ---------------------------------------------------------------------------
void begin() {
    stage::begin();
    opt.sound = 1;
    save::load(opt, stats, hasGame);
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
#if !CHBG_LEAN
        case Scr::Setup:   setupUpdate(); break;
#endif
        case Scr::Play:    playUpdate(); break;
#if !CHBG_LEAN
        case Scr::Options: optionsUpdate(); break;
#endif
    }
    fx::update();
}

void render(uint32_t frame) {
    switch (cur) {
        case Scr::Title:   titleRender(frame); break;
#if !CHBG_LEAN
        case Scr::Setup:   setupRender(frame); break;
#endif
        case Scr::Play:    playRender(frame); break;
#if !CHBG_LEAN
        case Scr::Options: optionsRender(frame); break;
#endif
    }
}

}  // namespace screens
