// The screens (Screens.h) and the game's flow: menus, the turn (yours or
// the CPU's), the overlays, and the debug protocol's commands at the end.
#pragma GCC optimize("Os", "no-ipa-sra", "no-caller-saves")   // cold code: size over speed (hot pixel loops live in the library's Draw/Mask)
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "config.h"
#include "Screens.h"
#include "Font.h"
#include "Tiles.h"
#include "Fx.h"
#include "Sounds.h"
#include "Ai.h"
#include "Dict.h"
#include "src/dict/DictData.h"
#include "Game.h"
#include "Stage.h"
#include "Save.h"
#ifdef CHSIM
#include <sim.h>
#endif

namespace screens {

using stage::tent;

enum class Scr : uint8_t { Title, Setup, Play, Options };
static Scr cur = Scr::Title, pending = Scr::Title;
static uint16_t t;                   // frames on this screen
static uint8_t fadeOut, fadeIn;
static uint8_t sel;                  // menu cursor

static Options opt;
static Stats stats;
static bool hasGame;                 // a saved game is waiting

// Play-screen overlays.
enum Overlay : uint8_t { NONE, PAUSE, RESULT, HANDOVER, ALLOW };
static Overlay overlay;
static bool statsCounted;
static bool handover;                // two players: the game changes hands after the show
static uint16_t thinkT;              // the CPU's turn: ticks it has been thinking (0: not yet)
static bool hinting;
static wd::Result pendingPlay;       // the play ALLOW is asking about

// A slice of the CPU's search, each tick. On the board: 12 ms of it (while
// it thinks nothing moves but the palette, so the frame rate can sag). The
// simulator's clock does not run while the game computes, so there it is a
// fixed amount of work: the same frames every run.
static bool think() {
#ifdef CHSIM
    return ai::step(1500);
#else
    uint32_t t0 = micros();
    while (!ai::step(24))
        if (micros() - t0 > 12000) return false;
    return true;
#endif
}

static const char *const OPPONENT[game::LEVELS] = {"TOURIST", "REGULAR", "HIGH ROLLER"};
static const char *const OPP_LINE[game::LEVELS] = {"SHORT WORDS, AND FEW", "KNOWS MOST WORDS", "EVERY WORD, BEST SCORE"};

#if CHGAME_DEBUG
static uint32_t thinkAt, thinkMs, sliceUs;
#endif

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------
static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

// The board behind the title: a few words laid out.
static void demoBoard() {
    game::Setup demo = {game::TWO_PLAYER, 0, 1};
    game::start(demo);
    // Each: row and column as letters from 'a', h (across) or v (down), the word.
    for (const char *p = "hfhWORDS" "cgvJACKPOT" "fdhLUCKY" "iivICE" "iehBET"; *p;) {
        uint8_t cell = (uint8_t)((p[0] - 'a') * wd::SIZE + p[1] - 'a'), step = p[2] == 'v' ? wd::SIZE : 1;
        for (p += 3; *p >= 'A' && *p <= 'Z'; p++, cell = (uint8_t)(cell + step)) game::board[cell] = (uint8_t)(*p - 'A' + 1);
    }
    game::last.kind = game::NOTHING;
}

static void enter(Scr s) {
    cur = s;
    stage::invalidate();
    t = 0;
    sel = 0;
    fadeIn = 8;
    fx::clear();
    if (s == Scr::Title) {
        demoBoard();
        if (!dict::card()) { gfx_wait(); dict::begin(); }       // a card put in since?
        audio::sfx(Sfx::Title);
    }
    if (s == Scr::Setup) sel = 1;
}

static void persist(bool withGame) {
    gfx_wait();                      // save builds its page in the chunk scratch
    save::store(opt, stats, withGame);
    hasGame = withGame;
}

static void applyOptions() {
    audio::setOn(opt.sound != 0);
    pal::setTheme(opt.felt);
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
static void heading(const char *text, int y) {
    int w = fontWidth(text);
    Mask m = maskBegin(w, FONT_H);
    maskFont(m, 0, 0, text);
    uint8_t r[FONT_H + 2];
    for (int i = 0; i < FONT_H + 2; i++) r[i] = i < 3 ? FX_B : (i < 8 ? GOLD : WOOD);
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
        panel(x - 7, y - 4, w + 14, 17, 3, NAVY, (frame & 16) ? FX_B : GOLD);
    }
    text35x2(x, y, s, on ? GOLD : WHITE);
}

static bool menuNav(uint8_t n) {
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel + 1 < n) { sel++; audio::sfx(Sfx::Cursor); }
    return rpgame.justPressed(A_BUTTON);
}

static void panel(int y, int h) { panel(10, y, 108, h, 3, NAVY, GOLD); }

// The bag is shuffled by when you pressed the button: your timing, not ours.
static uint32_t seedNow() { return micros() * 2654435761u ^ rpgame.frameCount; }

// ---------------------------------------------------------------------------
// Title
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_ONE, I_TWO, I_CONTINUE, I_OPTIONS };
static const char *const ITEM[4] = {"1 PLAYER", "2 PLAYERS", "CONTINUE", "OPTIONS"};

static uint8_t titleItems(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) items[n++] = I_CONTINUE;
    items[n++] = I_ONE;
    items[n++] = I_TWO;
#if !CHWD_LEAN
    items[n++] = I_OPTIONS;
#endif
    return n;
}

static void beginPlay() {
    overlay = NONE;
    statsCounted = false;
    handover = false;
    thinkT = 0;
    hinting = false;
    stage::newGame();
}

static void newGame(uint8_t mode) {
    game::Setup s;
    s.mode = mode;
    s.level = opt.level;
    s.seed = seedNow();
    game::start(s);
    beginPlay();
    go(Scr::Play);
}

static void titleUpdate() {
    uint8_t items[5], n = titleItems(items);
    if (sel >= n) sel = 0;
    if (menuNav(n)) {
        audio::sfx(Sfx::Select);
        switch (items[sel]) {
#if CHWD_LEAN
            case I_ONE: newGame(game::VS_CPU); break;
#else
            case I_ONE: go(Scr::Setup); break;
#endif
            case I_TWO: newGame(game::TWO_PLAYER); break;
            case I_CONTINUE:
                if (save::loadGame()) { beginPlay(); go(Scr::Play); }
                else { hasGame = false; demoBoard(); audio::sfx(Sfx::Deny); }
                break;
#if !CHWD_LEAN
            case I_OPTIONS: go(Scr::Options); break;
#endif
        }
    }
}

static void titleRender(uint32_t frame) {
    stage::renderTitle(frame);
    // The sign: a navy plaque framed in gold.
    panel(14, 3, 100, 24, 4, NAVY, GOLD);
    heading("WORDS", 9);
    uint8_t items[5], n = titleItems(items);
    int y0 = 128 - n * 15;
    gfx_fillRect(0, y0 - 14, 128, 128 - y0 + 14, INK);
    gfx_hline(0, y0 - 15, 128, GOLD);
    // Which list is in play.
    char buf[32], *p = fmtInt(buf, (int32_t)dict::count());
    fmtStr(p, dict::card() ? " WORDS ON THE CARD" : " WORDS  NO CARD");
    centred35(y0 - 12, buf, dict::card() ? CYAN : SILVER);
    for (uint8_t i = 0; i < n; i++) menuItem(y0 + i * 15, ITEM[items[i]], i == sel, frame);
}

// ---------------------------------------------------------------------------
// Setup: the opponent.
// ---------------------------------------------------------------------------
#if !CHWD_LEAN
static void setupUpdate() {
    int d = rpgame.repeat(RIGHT_BUTTON) ? 1 : (rpgame.repeat(LEFT_BUTTON) ? -1 : 0);
    menuNav(2);
    if (d) { opt.level = (uint8_t)((opt.level + game::LEVELS + d) % game::LEVELS); audio::sfx(Sfx::Coin); }
    // Hold SELECT to wipe your record against this opponent.
    static uint8_t hold;
    hold = rpgame.pressed(SELECT_BUTTON) ? (uint8_t)(hold + 1) : 0;
    if (hold == 90) {
        stats.won[opt.level] = stats.lost[opt.level] = 0;
        persist(hasGame);
        audio::sfx(Sfx::NoMove);
    }
    if (rpgame.justPressed(A_BUTTON)) { audio::sfx(Sfx::Select); persist(hasGame); newGame(game::VS_CPU); }
    if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void setupRender(uint32_t frame) {
    feltBackdrop();
    heading("OPPONENT", 9);
    // Stars for how hard they play.
    for (int i = 0; i <= opt.level; i++) text35(64 - opt.level * 4 + i * 8, 28, "*", FX_B);
    const char *s = OPPONENT[opt.level];
    int w = fontWidth(s, 0), bob = (frame >> 3) & 1;
    if (sel == 0) fillRound(64 - w / 2 - 5, 37, w + 10, 16, 3, NAVY);
    fontText(64 - w / 2, 40, s, sel == 0 ? GOLD : WHITE, 0);
    text35(64 - w / 2 - 6 - bob, 43, "<", GOLD);
    text35(64 + w / 2 + 3 + bob, 43, ">", GOLD);
    centred35(57, OPP_LINE[opt.level], FELT_LT);
    char buf[40], *p;
    p = fmtInt(fmtStr(buf, "WON "), stats.won[opt.level]);
    fmtInt(fmtStr(p, "  LOST "), stats.lost[opt.level]);
    centred35(68, buf, GOLD);
    p = fmtInt(fmtStr(buf, "BEST GAME "), stats.bestGame);
    fmtInt(fmtStr(p, "  PLAY "), stats.bestPlay);
    centred35(76, buf, SILVER);
    menuItem(96, "BEGIN", sel == 1, frame);
}
#endif

// ---------------------------------------------------------------------------
// Play: laying tiles out
// ---------------------------------------------------------------------------
static bool cellEmpty(uint8_t cell) {
    if (game::board[cell]) return false;
    for (uint8_t i = 0; i < tent.n; i++) if (tent.p[i].cell == cell) return false;
    return true;
}

// The next rack slot with a tile to lay, from `from` going `dir`; 0xFF: none.
static uint8_t freeSlot(uint8_t from, int dir) {
    for (uint8_t k = 0; k < wd::RACK; k++) {
        uint8_t s = (uint8_t)((from + wd::RACK * 2 + k * dir) % wd::RACK);
        if (stage::slotFree(s)) return s;
    }
    return 0xFF;
}

static void takeBack(uint8_t i) {
    stage::cursor = tent.p[i].cell;
    stage::rackSel = stage::tentSlot[i];
    for (; i + 1 < tent.n; i++) { tent.p[i] = tent.p[i + 1]; stage::tentSlot[i] = stage::tentSlot[i + 1]; }
    tent.n--;
    stage::retally();
    audio::sfx(Sfx::Lift);
}

static void recallAll() {
    tent.n = 0;
    stage::retally();
}

static void placeTile(uint8_t tile) {
    tent.p[tent.n] = {stage::cursor, tile};
    stage::tentSlot[tent.n++] = stage::rackSel;
    stage::retally();
    stage::placed(stage::cursor);
    // On to the next empty square the way the word runs, and the next tile.
    uint8_t step = stage::down ? wd::SIZE : 1, c = stage::cursor;
    stage::mode = stage::BOARD;
    for (;;) {
        bool edge = stage::down ? c >= wd::CELLS - wd::SIZE : c % wd::SIZE == wd::SIZE - 1;
        if (edge) break;
        c = (uint8_t)(c + step);
        if (cellEmpty(c)) { stage::cursor = c; stage::mode = stage::RACK; break; }
    }
    uint8_t next = freeSlot(stage::rackSel, 1);
    if (next == 0xFF) stage::mode = stage::BOARD;
    else stage::rackSel = next;
}

static void shuffleRack() {
    uint8_t slot[wd::RACK], n = 0;
    for (uint8_t i = 0; i < wd::RACK; i++) if (stage::slotFree(i)) slot[n++] = i;
    uint8_t *r = game::rack[stage::viewSide];
    for (uint8_t i = n; i > 1; i--) {
        uint8_t k = (uint8_t)(fx::rnd() % i), a = slot[i - 1], b = slot[k], tmp = r[a];
        r[a] = r[b];
        r[b] = tmp;
    }
    audio::sfx(Sfx::Rattle);
    stage::invalidate();
}

static const char *const WHY[6] = {
    "", "", "ONE ROW OR COLUMN", "NO GAPS IN A WORD", "COVER THE STAR", "JOIN A WORD",
};

// The move is made: the show, and for two players the handing over.
static void moved(bool byCpu) {
    stage::show(byCpu);
    if (game::setup.mode == game::TWO_PLAYER) handover = true;
}

static void commitPlay(const wd::Result &r) {
    if (game::setup.mode == game::VS_CPU && r.score > stats.bestPlay) stats.bestPlay = (uint16_t)r.score;
    game::play(tent, r);
    moved(false);
}

static void submit() {
    wd::Result r;
    if (!tent.n) { stage::note("LAY TILES FIRST", SILVER); audio::sfx(Sfx::Deny); return; }
    uint8_t err = wd::check(game::board, tent.p, tent.n, r);
    if (err) { stage::note(WHY[err], RED); stage::deny(nullptr); return; }
    gfx_wait();                      // the card shares the LCD's SPI
    wd::Span bad;
    bool hadCard = dict::card(), ok = game::wordsOk(tent, r, false, bad);
    if (hadCard && !dict::card()) {
        // The card stopped answering part way: nothing is decided on half an answer.
        stage::note("CARD LOST: FLASH WORDS NOW", CYAN, 150);
        audio::sfx(Sfx::NoMove);
        return;
    }
    if (ok) { commitPlay(r); return; }
    char text[32], *p = text;
    uint8_t w[wd::SIZE];
    wd::letters(game::board, tent.p, tent.n, bad, w);
    for (uint8_t i = 0; i < bad.len; i++) *p++ = (char)('A' + w[i] - 1);
    fmtStr(p, bad.len > 11 ? "?" : dict::card() ? " IS NOT A WORD" : " NOT IN MY LIST");
    stage::note(text, RED, 150);
    stage::deny(&bad);
    // Two people with no card to settle it: they may agree to allow it.
    if (game::setup.mode == game::TWO_PLAYER && !dict::card()) { pendingPlay = r; overlay = ALLOW; sel = 1; }
}

static void cpuMove() {
    game::Play pl;
    wd::Result r;
    if (ai::chosen(pl, r)) { tent = pl; game::play(pl, r); }
    else if (uint8_t m = ai::swapMask()) game::swap(m);
    else game::pass();
    moved(true);
}

// The hint: the best play the flash list has, laid out for you.
static void hintReady() {
    game::Play pl;
    wd::Result r;
    hinting = false;
    stage::thinking(nullptr);
    if (!ai::chosen(pl, r, true)) { stage::note("NO PLAY FOUND: SWAP?", SILVER); audio::sfx(Sfx::NoMove); return; }
    tent.n = 0;
    for (uint8_t i = 0; i < pl.n; i++) {
        uint8_t tile = wd::tileOf(pl.p[i].tile);
        for (uint8_t s = 0; s < wd::RACK; s++)
            if (game::rack[stage::viewSide][s] == tile && stage::slotFree(s)) {
                tent.p[tent.n] = pl.p[i];
                stage::tentSlot[tent.n++] = s;
                break;
            }
    }
    stage::retally();
    stage::cursor = pl.p[0].cell;
    stage::mode = stage::BOARD;
    stage::note("START TO PLAY IT", GOLD);
    audio::sfx(Sfx::Coin);
}

// B held this long: the whole board.
static const uint8_t PEEK_AFTER = 12;
static uint8_t bHeld;

static void playInput() {
    using namespace stage;
    bool a = rpgame.justPressed(A_BUTTON), b = rpgame.justPressed(B_BUTTON);
    if (mode != BOARD) { bHeld = 0; peek = false; }
    int dx = rpgame.repeat(RIGHT_BUTTON) ? 1 : (rpgame.repeat(LEFT_BUTTON) ? -1 : 0);
    int dy = rpgame.repeat(DOWN_BUTTON) ? 1 : (rpgame.repeat(UP_BUTTON) ? -1 : 0);
    switch (mode) {
        case BOARD: {
            int col = cursor % wd::SIZE + dx, row = cursor / wd::SIZE + dy;
            if ((dx || dy) && col >= 0 && col < wd::SIZE && row >= 0 && row < wd::SIZE) {
                cursor = (uint8_t)(row * wd::SIZE + col);
                audio::sfx(Sfx::Cursor);
            }
            if (a) {
                uint8_t i = 0;
                while (i < tent.n && tent.p[i].cell != cursor) i++;
                if (i < tent.n) takeBack(i);
                else if (!game::board[cursor] && freeSlot(rackSel, 1) != 0xFF) {
                    rackSel = freeSlot(rackSel, 1);
                    mode = RACK;
                    audio::sfx(Sfx::Select);
                } else audio::sfx(Sfx::Deny);
            }
            // B: a tap takes the last tile back; held, the whole board.
            if (rpgame.pressed(B_BUTTON)) { if (bHeld < 255) bHeld++; }
            else {
                if (bHeld && bHeld < PEEK_AFTER && tent.n) takeBack((uint8_t)(tent.n - 1));
                bHeld = 0;
            }
            peek = bHeld >= PEEK_AFTER;
#if !CHWD_LEAN
            if (rpgame.justPressed(SELECT_BUTTON)) {
                recallAll();
                ai::start(viewSide, ai::HIGH_ROLLER);
                hinting = true;
                thinking("LOOKING...");
            }
#endif
            break;
        }
        case RACK:
            if (dx) {
                uint8_t s = freeSlot((uint8_t)(rackSel + wd::RACK + dx), dx);
                if (s != 0xFF) rackSel = s;
                audio::sfx(Sfx::Cursor);
            }
            if (dy < 0) { down = !down; audio::sfx(Sfx::Cursor); }
            if (dy > 0) shuffleRack();
            if (a) {
                uint8_t tl = game::rack[viewSide][rackSel];
                if (tl == wd::BLANK_TILE) { mode = PICK; audio::sfx(Sfx::Select); }
                else placeTile(tl);
            }
            if (b) { mode = BOARD; audio::sfx(Sfx::Cursor); }
            break;
        case PICK: {
            int k = pickSel + dx + dy * 7;
            if ((dx || dy) && k >= 0 && k < 26) { pickSel = (uint8_t)k; audio::sfx(Sfx::Cursor); }
            if (a) placeTile((uint8_t)((pickSel + 1) | wd::BLANK));
            if (b) mode = RACK;
            break;
        }
        case SWAP:
            if (dx) {
                for (uint8_t k = 0; k < wd::RACK; k++) {
                    rackSel = (uint8_t)((rackSel + wd::RACK + dx) % wd::RACK);
                    if (game::rack[viewSide][rackSel]) break;
                }
                audio::sfx(Sfx::Cursor);
            }
            if (a) { swapMarks ^= (uint8_t)(1u << rackSel); audio::sfx(Sfx::Lift); }
            if (b) { mode = BOARD; swapMarks = 0; note("", SILVER, 1); }
            if (rpgame.justPressed(START_BUTTON)) {
                if (!swapMarks) { audio::sfx(Sfx::Deny); break; }
                game::swap(swapMarks);
                swapMarks = 0;
                moved(false);
            }
            break;
    }
    invalidate();
}

// ---------------------------------------------------------------------------
// Play: the overlays
// ---------------------------------------------------------------------------
static const char *const PAUSE_ITEM[4] = {"PLAY", "SWAP TILES", "PASS", "SAVE+QUIT"};

static void pauseInput() {
    bool start = rpgame.justPressed(START_BUTTON);
    if (menuNav(4)) {
        overlay = NONE;
        audio::sfx(Sfx::Select);
        switch (sel) {
            case 0: submit(); break;
            case 1:
                if (!game::canSwap()) { stage::note("THE BAG IS TOO LOW", SILVER); break; }
                recallAll();
                stage::mode = stage::SWAP;
                stage::swapMarks = 0;
                stage::rackSel = 0;
                stage::note("A MARK   START SWAP   B BACK", SILVER, 255);
                break;
            case 2: recallAll(); game::pass(); moved(false); break;
            default: recallAll(); persist(!game::over); go(Scr::Title); break;
        }
        return;
    }
    if (start || rpgame.justPressed(B_BUTTON)) { overlay = NONE; audio::sfx(Sfx::Cursor); }
}

static void finish() {
    uint8_t w = game::winner();
    bool cpu = game::setup.mode == game::VS_CPU;
    if (cpu && !statsCounted) {
        uint8_t lv = game::setup.level;
        if (w == 0) stats.won[lv]++;
        if (w == 1) stats.lost[lv]++;
        if (game::score[0] > (int16_t)stats.bestGame) stats.bestGame = (uint16_t)game::score[0];
    }
    statsCounted = true;
    if (w == 2) fx::banner("A TIE", fx::B_WHITE, 40, 120);
    else if (cpu && w == 1) { fx::banner("YOU LOSE", fx::B_RED, 40, 120); audio::sfx(Sfx::Lose); }
    else {
        fx::banner(cpu ? "YOU WIN!" : w ? "PLAYER 2!" : "PLAYER 1!", fx::B_RAINBOW, 40, 150);
        fx::fountain(40, 100, 20);
        fx::fountain(88, 100, 20);
        audio::sfx(Sfx::Win);
        audio::led(audio::LED_PARTY);
    }
    overlay = RESULT;
    persist(false);
}

static void playUpdate() {
    switch (overlay) {
        case PAUSE: pauseInput(); return;
        case RESULT:
            if (rpgame.justPressed(A_BUTTON)) { audio::sfx(Sfx::Select); newGame(game::setup.mode); }
            if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
            break;
        case HANDOVER:
            if (rpgame.justPressed(A_BUTTON)) { overlay = NONE; audio::sfx(Sfx::Turn); stage::invalidate(); }
            return;
        case ALLOW:
            if (menuNav(2)) {
                overlay = NONE;
                audio::sfx(Sfx::Select);
                if (sel == 0) commitPlay(pendingPlay);
            } else if (rpgame.justPressed(B_BUTTON)) overlay = NONE;
            stage::invalidate();
            break;
        default:
            break;
    }
    stage::update();
    if (overlay != NONE || stage::busy() || fadeOut) return;
    if (game::over) { finish(); return; }
    if (handover) {
        handover = false;
        stage::viewSide = game::turn;
        overlay = HANDOVER;
        return;
    }
    if (game::cpuTurn()) {
        if (!thinkT) {
            ai::start(1, game::setup.level);
            stage::thinking("CPU THINKING");
#if CHGAME_DEBUG
            thinkAt = millis();
            sliceUs = 0;
#endif
        }
#if CHGAME_DEBUG
        uint32_t t0 = micros();
#endif
        bool done = think();
#if CHGAME_DEBUG
        if (micros() - t0 > sliceUs) sliceUs = micros() - t0;
#endif
        // Long enough: it plays the best it has found (a blank in the rack
        // on a crowded board can take a while to get through the list).
        if (++thinkT == 1200) { ai::stop(); done = true; }
        if (done && thinkT > 40) {
#if CHGAME_DEBUG
            thinkMs = millis() - thinkAt;
#endif
            thinkT = 0;
            stage::thinking(nullptr);
            cpuMove();
        }
        return;
    }
    if (hinting) {
        if (think()) hintReady();
        return;
    }
    if (rpgame.justPressed(START_BUTTON) && stage::mode != stage::SWAP && stage::mode != stage::PICK) {
        overlay = PAUSE;
        sel = tent.n ? 0 : 1;
        audio::sfx(Sfx::Select);
        return;
    }
    playInput();
}

static void scoreLine(char *buf) {
    bool cpu = game::setup.mode == game::VS_CPU;
    char *p = fmtInt(fmtStr(buf, cpu ? "YOU " : "PLAYER 1  "), game::score[0]);
    fmtInt(fmtStr(p, cpu ? "   CPU " : "   PLAYER 2  "), game::score[1]);
}

static void playRender(uint32_t frame) {
    uint32_t ui = overlay | (sel << 4);
    if (overlay == HANDOVER) {
        // The curtain: nobody sees the other rack.
        feltBackdrop();
        heading(game::turn ? "PLAYER 2" : "PLAYER 1", 30);
        char buf[40];
        scoreLine(buf);
        centred35(56, buf, GOLD);
        centred35(72, "PASS THE GAME ALONG", FELT_LT);
        if (frame & 32) centred35(90, "PRESS A", WHITE);
        stage::invalidate();
        return;
    }
    if (!stage::render(frame, ui)) return;
    char buf[40];
    if (overlay == PAUSE) {
        panel(26, 68);
        for (uint8_t i = 0; i < 4; i++) {
            int y = 32 + i * 15;
            if (i == sel) fillRound(14, y - 3, 100, 16, 3, INK);
            if (i == 0 && tent.n && stage::tentScore >= 0) {
                fmtInt(fmtStr(buf, "PLAY +"), stage::tentScore);
                centred2(y, buf, i == sel ? FX_B : WHITE);
            } else centred2(y, PAUSE_ITEM[i], i == sel ? FX_B : (i == 0 && !tent.n) ? SILVER : WHITE);
        }
    } else if (overlay == ALLOW) {
        panel(36, 46);
        centred35(41, "ALLOW IT ANYWAY?", GOLD);
        static const char *const ANS[2] = {"ALLOW", "TAKE BACK"};
        for (uint8_t i = 0; i < 2; i++) {
            int y = 51 + i * 14;
            if (i == sel) fillRound(24, y - 3, 80, 16, 3, INK);
            centred2(y, ANS[i], i == sel ? FX_B : WHITE);
        }
    } else if (overlay == RESULT && !fx::bannerActive()) {
        panel(52, 50);
        uint8_t w = game::winner();
        bool cpu = game::setup.mode == game::VS_CPU;
        centred2(56, w == 2 ? "A TIE" : cpu ? (w ? "YOU LOSE" : "YOU WIN!") : (w ? "PLAYER 2 WINS" : "PLAYER 1 WINS"), FX_B);
        scoreLine(buf);
        centred35(73, buf, WHITE);
        // What the tiles left on the racks did to the scores.
        char *p = fmtStr(buf, "RACKS ");
        for (uint8_t s = 0; s < 2; s++) {
            if (game::rackPenalty[s] > 0) *p++ = '+';
            p = fmtInt(p, game::rackPenalty[s]);
            if (!s) p = fmtStr(p, "  ");
        }
        centred35(81, buf, SILVER);
        centred35(92, "A AGAIN   B MENU", GOLD);
    }
}

// ---------------------------------------------------------------------------
// Options (device debug builds leave the screen out to fit the protocol)
// ---------------------------------------------------------------------------
#if !CHWD_LEAN
enum Opt : uint8_t { O_SOUND, O_FELT, O_WORDS, O_BACK, OPT_COUNT };
static const char *const OPT_TEXT[OPT_COUNT] = {"SOUND|OFF|ON", "FELT|GREEN|BLUE|RED|PURPLE", "WORDS", "BACK"};
static bool wordsInfo;               // the panel on the word lists is up
static char randomWord[16];          // ... and a word off the card for it
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
    if (wordsInfo) {
        if (rpgame.justPressed(A_BUTTON | B_BUTTON)) { wordsInfo = false; audio::sfx(Sfx::Select); }
        return;
    }
    if (rpgame.justPressed(A_BUTTON) && sel == O_WORDS) {
        gfx_wait();
        dict::begin();               // a card put in since?
        wordsInfo = true;
        // One that fits the panel in the display face.
        for (uint8_t k = 0; k < 12; k++)
            if (!dict::randomWord(randomWord, fx::rnd() ^ micros()) || fontWidth(randomWord, 0) <= 104) break;
        audio::sfx(Sfx::Select);
        return;
    }
    if (rpgame.repeat(UP_BUTTON)) { sel = (uint8_t)((sel + OPT_COUNT - 1) % OPT_COUNT); audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON)) { sel = (uint8_t)((sel + 1) % OPT_COUNT); audio::sfx(Sfx::Cursor); }
    int d = rpgame.justPressed(RIGHT_BUTTON) ? 1 : (rpgame.justPressed(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.justPressed(A_BUTTON) && sel < O_WORDS) d = 1;
    if (d && sel < O_WORDS) {
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
        go(Scr::Title);
    }
}

static void optionsRender(uint32_t frame) {
    feltBackdrop();
    heading("OPTIONS", 8);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 30 + i * 18;
        char label[12], value[12];
        optField(OPT_TEXT[i], 0, label);
        if (i == sel && !wordsInfo) {               // two clear pixels or more round the lettering
            panel(8, y - 4, 112, 18, 3, NAVY, (frame & 16) ? FX_B : GOLD);
        }
        // In the tiles' anti-aliased serif.
        bool on = i == sel && !wordsInfo;
        uint8_t ink = on ? GOLD : WHITE, mid = on ? WOOD : FELT_LT;
        if (i >= O_WORDS) { tileText(64 - tileText(0, 0, label, 0, 0, 0, false) / 2, y + 1, label, ink, mid, INK); continue; }
        tileText(14, y + 1, label, ink, mid, INK);
        optField(OPT_TEXT[i], (uint8_t)(optByte(i) + 1), value);
        tileText(114 - tileText(0, 0, value, 0, 0, 0, false), y + 1, value, on ? WHITE : FELT_LT, on ? SILVER : FELT, INK);
    }
    centred35(101, "SELECT IN A GAME: A HINT", FELT_LT);
    if (wordsInfo) {
        // How many words, and how to get the rest.
        panel(18, 92);
        centred2(22, "WORDS", FX_B);
        char buf[32];
        fmtStr(fmtInt(fmtStr(buf, "BUILT IN: "), DICT_WORDS), " WORDS");
        centred35(40, buf, WHITE);
        if (dict::card()) {
            fmtStr(fmtInt(buf, (int32_t)dict::count()), " WORDS ON CARD");
            centred35(52, buf, FELT_LT);
            centred35(66, "RANDOM WORD:", SILVER);
            // As the banners say BINGO!: the display face in the rainbow,
            // the letters dancing.
            uint8_t gap = fontWidth(randomWord) > 100 ? 0 : 1;
            int w = fontWidth(randomWord, gap), t = (int)frame;
            int8_t dy[16];
            for (uint8_t k = 0; k < 16; k++) dy[k] = (int8_t)(2 + ((fx::isin(t * 10 + k * 36) * 2) >> 8));
            Mask m = maskBegin(w + 1, FONT_H + 6);
            maskFont(m, 0, 0, randomWord, dy, gap);
            uint8_t ramp[FONT_H + 6];
            for (int r = 0; r < FONT_H + 6; r++) ramp[r] = fx::RAIN[(((r + 8) / 2) + t / 3) % 5];
            maskDraw(m, 64 - w / 2, 74, 0, FX_A, INK, ramp);
        } else {
            centred35(52, "168551 WORDS MISSING", RED);
            centred35(66, "PLACE FILE WORDS.DIC IN", SILVER);
            centred35(74, "ROOT FOLDER OF FAT32", SILVER);
            centred35(82, "SD CARD", SILVER);
        }
        centred35(100, "A OR B: BACK", GOLD);
    }
    centred35(109, "WORDS: THE ENABLE LIST", SILVER);
    centred35(116, "3X5 FONT: PRESS PLAY ON TAPE", SILVER);
}
#endif

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   G <mode> <level> <seed>    start a game (mode 0 vs CPU, 1 two players)
//   R <letters>                the rack of the side to move (? a blank)
//   W <row> <col> <H|V> <word> lay the word's tiles out from there (squares already taken are
//                              skipped; a small letter is played with a blank)
//   A                          (simulator) play for the human: the best play found, else swap or pass
//   H                          STATE <turn> <O over, T the CPU's turn, W waiting for A, M yours> <ready>
//                              <score 0> <score 1> <bag>
//   C                          the CPU's last think: THINK tried=<placements> ms=<n> slice_us=<longest tick>
//   M                          DICT card=<0|1> words=<n>
//   D <word>                   WORD <in the list in use> core=<in flash> us=<time taken>
//   J <T|S|O>                  jump to title/setup/options
//   X <0|1>                    (simulator) pull the card out / put it back
static bool debugHook(char cmd, const char *args) {
    char buf[96], *p;
    switch (cmd) {
        case 'G': {
            game::Setup s;
            s.mode = (uint8_t)dbg::parseNum(args, 10);
            s.level = (uint8_t)dbg::parseNum(args, 10);
            s.seed = dbg::parseNum(args, 10);
            game::start(s);
            beginPlay();
            enter(Scr::Play);
            return true;
        }
        case 'R': {
            uint8_t *r = game::rack[game::turn];
            memset(r, 0, wd::RACK);
            for (uint8_t i = 0; i < wd::RACK && args[i] > ' '; i++)
                r[i] = args[i] == '?' ? wd::BLANK_TILE : (uint8_t)((args[i] & 31));
            recallAll();
            return true;
        }
        case 'W': {
            uint8_t cell = (uint8_t)(dbg::parseNum(args, 10) * wd::SIZE);
            cell = (uint8_t)(cell + dbg::parseNum(args, 10));
            while (*args == ' ') args++;
            uint8_t step = *args++ == 'V' ? wd::SIZE : 1;
            while (*args == ' ') args++;
            recallAll();
            for (; *args > ' '; args++, cell = (uint8_t)(cell + step)) {
                if (game::board[cell]) continue;
                bool blank = *args >= 'a';
                uint8_t want = blank ? wd::BLANK_TILE : (uint8_t)(*args & 31), s = 0;
                while (s < wd::RACK && !(game::rack[stage::viewSide][s] == want && stage::slotFree(s))) s++;
                if (s == wd::RACK || tent.n == wd::RACK) return false;
                tent.p[tent.n] = {cell, (uint8_t)((*args & 31) | (blank ? wd::BLANK : 0))};
                stage::tentSlot[tent.n++] = s;
            }
            stage::retally();
            return true;
        }
        case 'H':
            p = fmtStr(buf, "STATE ");
            *p++ = (char)('0' + game::turn);
            *p++ = ' ';
            *p++ = game::over ? 'O' : (overlay == HANDOVER || overlay == ALLOW) ? 'W' : game::cpuTurn() ? 'T' : 'M';
            *p++ = ' ';
            *p++ = cur == Scr::Play && overlay == NONE && !stage::busy() && !hinting && !fadeOut && !fadeIn ? '1' : '0';
            p = fmtInt(fmtStr(p, " "), game::score[0]);
            p = fmtInt(fmtStr(p, " "), game::score[1]);
            p = fmtInt(fmtStr(p, " "), game::bagLeft);
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        case 'C':
            p = fmtInt(fmtStr(buf, "THINK tried="), (int32_t)ai::tried());
            p = fmtInt(fmtStr(p, " ms="), (int32_t)thinkMs);
            p = fmtInt(fmtStr(p, " slice_us="), (int32_t)sliceUs);
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        case 'M':
            p = fmtInt(fmtStr(buf, "DICT card="), dict::card());
            p = fmtInt(fmtStr(p, " words="), (int32_t)dict::count());
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        case 'D': {
            uint8_t w[16], n = 0;
            while (args[n] > ' ' && n < 15) { w[n] = (uint8_t)(args[n] & 31); n++; }
            gfx_wait();
            uint32_t t0 = micros();
            bool full = dict::has(w, n);
            uint32_t us = micros() - t0;
            p = fmtInt(fmtStr(buf, "WORD "), full);
            p = fmtInt(fmtStr(p, " core="), dict::hasCore(w, n));
            p = fmtInt(fmtStr(p, " us="), (int32_t)us);
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'J': {
            static const char K[] = "TSO";
            const char *q = strchr(K, args[0]);
            if (!q) return false;
            static const Scr S[] = {Scr::Title, Scr::Setup, Scr::Options};
            enter(S[q - K]);
            return true;
        }
#ifdef CHSIM
        case 'X':
            sim_cardEject(args[0] == '0');
            if (args[0] != '0') { gfx_wait(); dict::begin(); }
            return true;
        case 'A': {
            if (cur != Scr::Play || game::over || game::cpuTurn()) return false;
            recallAll();
            ai::start(stage::viewSide, ai::HIGH_ROLLER);
            while (!ai::step(1000)) {}
            game::Play pl;
            wd::Result r;
            if (ai::chosen(pl, r)) { tent = pl; commitPlay(r); }
            else if (uint8_t m = ai::swapMask()) { game::swap(m); moved(false); }
            else { game::pass(); moved(false); }
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
    applyOptions();
    dict::begin();
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
#if !CHWD_LEAN
        case Scr::Setup:   setupUpdate(); break;
#endif
        case Scr::Play:    playUpdate(); break;
#if !CHWD_LEAN
        case Scr::Options: optionsUpdate(); break;
#endif
        default: break;
    }
    fx::update();
}

void render(uint32_t frame) {
    switch (cur) {
        case Scr::Title:   titleRender(frame); break;
#if !CHWD_LEAN
        case Scr::Setup:   setupRender(frame); break;
#endif
        case Scr::Play:    playRender(frame); break;
#if !CHWD_LEAN
        case Scr::Options: optionsRender(frame); break;
#endif
        default: break;
    }
}

}  // namespace screens
