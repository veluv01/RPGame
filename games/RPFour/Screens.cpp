// The screens and the moves between them: title, setup, play (with pause and
// the result), options; and the debug protocol's commands. The play screen's
// motion is Stage.cpp.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (hot pixel loops live in Draw/Mask)
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "Font.h"
#include "config.h"
#include "Screens.h"
#include "Fx.h"
#include "Sounds.h"
#include "Table.h"
#include "Ai.h"
#include "Game.h"
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
#if !CHF4_LEAN
static Scr optBack = Scr::Title;
#endif

static Options opt;
static Stats stats;
static bool hasGame;                 // a saved game is waiting
static uint8_t tally[2];             // two players: games each has won since switching on
static uint8_t games;                // games begun (who moves first, when they take turns)

// Play-screen overlays.
enum Overlay : uint8_t { NONE, PAUSE, RESULT };
static Overlay overlay;
static bool statsCounted;
// What entering the play screen starts: a new game, or the one just loaded.
static game::Setup next;
static bool fresh;

static const char *const OPPONENT[game::LEVELS] = {"ROOKIE", "SHARK", "THE BOSS"};
// What he says for himself on the setup screen.
static const char *const OPP_LINE[game::LEVELS] = {
    "A GENTLE\nGAME TO\nWARM UP.", "A PROPER\nCHALLENGE.\nGOOD LUCK!", "MY VERY\nBEST PLAY.\nGOOD LUCK!"};
static const uint8_t OPP_FACE[game::LEVELS] = {table::E_SMILE, table::E_NORMAL, table::E_RAISED};

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

static void setPlaque() {
    if (game::setup.mode == game::VS_CPU) {
        uint8_t lv = game::setup.level;
        stage::plaque = {OPPONENT[lv], "WON", "LOST", stats.won[lv], stats.lost[lv]};
    } else stage::plaque = {"2 PLAYERS", "RED", "GOLD", tally[0], tally[1]};
}

static void enter(Scr s) {
    cur = s;
    stage::invalidate();
    t = 0;
    sel = 0;
    fadeIn = 8;
    fx::clear();
    if (s == Scr::Title) {
        // The board behind the title: a game in full swing, close up, the
        // camera drifting over it.
        stage::demo();
        audio::sfx(Sfx::Title);
    }
    if (s == Scr::Setup) sel = 2;
    if (s == Scr::Play) {
        // (Here, not when it was chosen: the screen being left fades out as it was.)
        stage::reset();
        if (fresh) game::start(next);
        overlay = NONE;
        statsCounted = false;
        setPlaque();
    }
}

static void persist(bool withGame) {
    gfx_wait();                      // save builds its page in the chunk scratch
    save::store(opt, stats, withGame);
    hasGame = withGame;
}

static void applyOptions() {
    audio::setOn(opt.sound != 0);
    stage::setFast(opt.speed != 0);
}

// ---------------------------------------------------------------------------
// Shared drawing (CHBlackjack's look)
// ---------------------------------------------------------------------------
static void feltBackdrop(int y0 = 0) {
    gfx_fillRect(0, y0, 128, 128 - y0, FELT);
    if (!y0) dither(0, 0, 128, 6, FELT_DK, 0);
    dither(0, 122, 128, 6, FELT_DK, 1);
    dither(0, y0, 6, 128 - y0, FELT_DK, 0);
    dither(122, y0, 6, 128 - y0, FELT_DK, 1);
    if (!y0) gfx_rect(2, 2, 124, 124, GOLD);
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

// The dealer's mood is seeded from when you pressed the button: your timing, not ours.
static uint32_t seedNow() { return micros() * 2654435761u ^ rpgame.frameCount; }

// ---------------------------------------------------------------------------
// Title: a board mid-game, the camera drifting over it.
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_ONE, I_TWO, I_CONTINUE, I_OPTIONS };
static const char *const ITEM[4] = {"1 PLAYER", "2 PLAYERS", "CONTINUE", "OPTIONS"};

static uint8_t titleItems(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) items[n++] = I_CONTINUE;
    items[n++] = I_ONE;
    items[n++] = I_TWO;
#if !CHF4_LEAN
    items[n++] = I_OPTIONS;
#endif
    return n;
}

static void newGame(uint8_t mode) {
    game::Setup &s = next;
    s.mode = mode;
    s.level = opt.level;
    // Against the dealer: as the setup screen says. Two players take turns.
    uint8_t who = mode == game::VS_CPU ? opt.first : 2;
    s.first = who < 2 ? who : (uint8_t)(games & 1);
    s.quick = opt.speed;
    s.seed = seedNow();
    games++;
    fresh = true;
    go(Scr::Play);
}

static void titleUpdate() {
    uint8_t items[5], n = titleItems(items);
    if (sel >= n) sel = 0;
    if (menuNav(n)) {
        audio::sfx(Sfx::Select);
        switch (items[sel]) {
#if CHF4_LEAN
            case I_ONE: newGame(game::VS_CPU); break;
#else
            case I_ONE: go(Scr::Setup); break;
#endif
            case I_TWO: newGame(game::TWO_PLAYER); break;
            case I_CONTINUE:
                if (save::loadGame()) { fresh = false; go(Scr::Play); }
                else { hasGame = false; audio::sfx(Sfx::Deny); }
                break;
#if !CHF4_LEAN
            case I_OPTIONS: optBack = Scr::Title; go(Scr::Options); break;
#endif
        }
    }
    // Drift over the board, close up: a slow figure of eight.
    int a = (int)t / 3;
    table::zoom = 10;
    table::setCamera(64 + ((fx::isin(a) * 30) >> 8), 86 + ((fx::isin(a * 2) * 9) >> 8));
}

static void titleRender(uint32_t frame) {
    stage::renderScene(frame);
    // The sign: a navy plaque framed in gold, two of each side's discs
    // either side of the first word.
    fillRound(4, 3, 120, 37, 4, NAVY);
    roundRect(4, 3, 120, 37, 4, GOLD);
    heading("FOUR", 7);
    for (int i = 0; i < 2; i++) {
        sprite4(DISC, 12 + i * 12, 9, DISC_REMAP[c4::RED]);
        sprite4(DISC, 94 + i * 12, 9, DISC_REMAP[c4::GOLD]);
    }
    heading("IN A ROW", 23);
    uint8_t items[5], n = titleItems(items);
    int y0 = 128 - n * 15;
    dither(0, y0 - 6, 128, 128 - y0 + 6, INK, 1);
    for (uint8_t i = 0; i < n; i++) menuItem(y0 + i * 15, ITEM[items[i]], i == sel, frame);
}

// ---------------------------------------------------------------------------
// Setup: which dealer, and who moves first.
// ---------------------------------------------------------------------------
#if !CHF4_LEAN
static const char *const FIRST[3] = {"YOU", "DEALER", "TAKE TURNS"};

static void setupUpdate() {
    int d = rpgame.repeat(RIGHT_BUTTON) ? 1 : (rpgame.repeat(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel < 2) { sel++; audio::sfx(Sfx::Cursor); }
    if (d && sel == 0) { opt.level = (uint8_t)((opt.level + game::LEVELS + d) % game::LEVELS); audio::sfx(Sfx::Coin); }
    if (d && sel == 1) { opt.first = (uint8_t)((opt.first + 3 + d) % 3); audio::sfx(Sfx::Coin); }
    // Hold SELECT to wipe your record against this opponent.
    static uint8_t hold;
    hold = rpgame.pressed(SELECT_BUTTON) ? (uint8_t)(hold + 1) : 0;
    if (hold == 90) {
        stats.won[opt.level] = stats.lost[opt.level] = stats.drawn[opt.level] = 0;
        persist(hasGame);
        audio::sfx(Sfx::Deny);
    }
    if (rpgame.justPressed(A_BUTTON)) {
        if (sel < 2) { sel++; audio::sfx(Sfx::Cursor); }
        else { audio::sfx(Sfx::Select); persist(hasGame); newGame(game::VS_CPU); }
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
    feltBackdrop(table::RAIL_Y + 2);
    // The dealer introduces himself.
    stage::renderWall(frame, OPP_LINE[opt.level], OPP_FACE[opt.level]);
    // Stars for how hard he plays.
    for (int i = 0; i <= opt.level; i++) text35(64 - opt.level * 4 + i * 8 - 1, 47, "*", FX_B);
    choice(56, OPPONENT[opt.level], sel == 0, frame);
    // Your record against him.
    char buf[32], *p;
    p = fmtInt(fmtStr(buf, "WON "), stats.won[opt.level]);
    p = fmtInt(fmtStr(p, "  LOST "), stats.lost[opt.level]);
    fmtInt(fmtStr(p, "  DRAWN "), stats.drawn[opt.level]);
    centred35(72, buf, GOLD);
    centred35(80, "FIRST MOVE", FELT_LT);
    choice(89, FIRST[opt.first], sel == 1, frame);
    menuItem(110, "BEGIN", sel == 2, frame);
}
#endif

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
static void playInput() {
    uint8_t c = stage::cursor();
    if (rpgame.repeat(LEFT_BUTTON, 14, 4) && c > 0) { stage::setCursor(--c); audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(RIGHT_BUTTON, 14, 4) && c + 1 < c4::COLS) { stage::setCursor(++c); audio::sfx(Sfx::Cursor); }
    if (rpgame.justPressed(A_BUTTON | DOWN_BUTTON) && !game::drop(c)) stage::deny();
}

static const char *const PAUSE_ITEM[3] = {"RESUME", "RESIGN", "SAVE+QUIT"};

static void pauseInput() {
    bool start = rpgame.justPressed(START_BUTTON);
    if (menuNav(3) || start) {
        overlay = NONE;
        audio::sfx(Sfx::Select);
        if (start || sel == 0) return;
        if (sel == 1) game::resign();
        else { persist(game::active()); go(Scr::Title); }
        return;
    }
    if (rpgame.justPressed(B_BUTTON)) overlay = NONE;
}

static void countResult() {
    if (statsCounted) return;
    statsCounted = true;
    uint8_t w = game::winner;
    if (game::setup.mode != game::VS_CPU) {
        if (w < 2 && tally[w] < 255) tally[w]++;
    } else {
        uint16_t &n = (w == game::YOU ? stats.won : w == game::DEALER ? stats.lost : stats.drawn)[game::setup.level];
        if (n < 9999) n++;
    }
    setPlaque();
    persist(false);                                 // the record, and no game to go back to
}

static void playUpdate() {
    switch (overlay) {
        case PAUSE: pauseInput(); return;                            // the game waits
        case RESULT:
            if (rpgame.justPressed(A_BUTTON)) {
                audio::sfx(Sfx::Select);
                newGame(game::setup.mode);
            }
            if (rpgame.justPressed(B_BUTTON)) {
                audio::sfx(Sfx::Select);
                go(Scr::Title);
            }
            break;
        default:
            if (!game::active()) {
                // The ending plays: a button hurries it along.
                if (rpgame.justPressed(A_BUTTON | B_BUTTON | START_BUTTON)) stage::hurry();
                break;
            }
            if (rpgame.justPressed(START_BUTTON) && game::humanToMove()) { overlay = PAUSE; sel = 0; audio::sfx(Sfx::Select); break; }
            if (stage::ready() && game::humanToMove()) playInput();
            break;
    }
#if CHGAME_DEBUG
    bool th = game::cpuThinking();
    uint32_t t0 = micros();
    if (th && !wasThinking) { thinkAt = millis(); sliceUs = 0; }
#endif
    game::update(stage::busy());            // the CPU thinks a little in here
#if CHGAME_DEBUG
    if (th && micros() - t0 > sliceUs) sliceUs = micros() - t0;
    if (wasThinking && !game::cpuThinking()) thinkMs = millis() - thinkAt;
    wasThinking = game::cpuThinking();
#endif
    stage::update();
    if (!game::active() && !overlay && stage::overShown()) {
        countResult();
        overlay = RESULT;
    }
}

static void playRender(uint32_t frame) {
    uint32_t ui = overlay | (sel << 4);
    if (!stage::render(frame, ui)) return;
    char buf[40], *p;
    bool vsCpu = game::setup.mode == game::VS_CPU;
    if (overlay == PAUSE) {
        panel(10, 60, 108, 50, 3, NAVY, GOLD);
        for (uint8_t i = 0; i < 3; i++) {
            int y = 66 + i * 14;
            if (i == sel) fillRound(14, y - 3, 100, 16, 3, INK);
            centred2(y, PAUSE_ITEM[i], i == sel ? FX_B : WHITE);
        }
    } else if (overlay == RESULT) {
        panel(10, 95, 108, 31, 3, NAVY, GOLD);
        uint8_t w = game::winner, lv = game::setup.level;
        const char *head = w == c4::NOBODY ? "A DRAW" : vsCpu ? (w == game::YOU ? "YOU WIN!" : "YOU LOSE")
                         : w == c4::RED ? "RED WINS" : "GOLD WINS";
        centred2(98, head, FX_B);
        if (vsCpu) {
            p = fmtInt(fmtStr(buf, "WON "), stats.won[lv]);
            p = fmtInt(fmtStr(p, "  LOST "), stats.lost[lv]);
            fmtInt(fmtStr(p, "  DRAWN "), stats.drawn[lv]);
        } else {
            p = fmtInt(fmtStr(buf, "RED "), tally[0]);
            fmtInt(fmtStr(p, "   GOLD "), tally[1]);
        }
        centred35(112, buf, GOLD);
        centred35(119, vsCpu ? "A REMATCH   B MENU" : "A AGAIN   B MENU", WHITE);
    }
}

// ---------------------------------------------------------------------------
// Options (left out of a CHF4_LEAN build, config.h: its tests start games
// directly)
// ---------------------------------------------------------------------------
#if !CHF4_LEAN
enum Opt : uint8_t { O_SOUND, O_SPEED, O_BACK, OPT_COUNT };
static const char *const OPT_TEXT[OPT_COUNT] = {"SOUND|OFF|ON", "PACE|FUN|QUICK", "BACK"};
// Each row's byte in Options.
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
    heading("OPTIONS", 10);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 36 + i * 16;
        char label[12], value[12];
        optField(OPT_TEXT[i], 0, label);
        if (i == sel) {
            fillRound(8, y - 3, 112, 15, 3, NAVY);
            roundRect(8, y - 3, 112, 15, 3, (frame & 16) ? FX_B : GOLD);
        }
        if (i == O_BACK) { centred2(y, label, i == sel ? GOLD : WHITE); continue; }
        fontText(13, y, label, i == sel ? GOLD : WHITE);
        optField(OPT_TEXT[i], (uint8_t)(optByte(i) + 1), value);
        fontText(115 - fontWidth(value, 0), y, value, i == sel ? WHITE : FELT_LT, 0);
    }
    centred35(95, "FOUR IN A ROW " CHF4_VERSION, SILVER);
    centred35(105, "THE DEALER AND THE 3X5 FONT:", SILVER);
    centred35(112, "PRESS PLAY ON TAPE", SILVER);
}
#endif

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   G <mode> <level> <first> <seed>   start a game (mode 0 against the dealer, 1 two players)
//   M <mode> <level> <first> <moves>  ... from a position: the columns played so far, 1..7 (M 0 2 0 4435)
//   D <column>                        the person to move drops a disc in column 1..7
//   A                                 ... or lets the SHARK choose for them (simulator)
//   W                                 the CPU's last choice: positions visited, depth, its score, how long
//                                     it took (the stage's show included) and its longest slice of a tick
//   E <winner>                        straight to an ending: 0 you won, 1 the dealer did
//   J <T|S|O>                         jump to title/setup/options
//   H                                 the game: BOARD <RED's cells> <GOLD's> <side> <state> <ready>
//                                     (cells: hex; state M a person to move, T the CPU, W the ending
//                                     playing, O over)
static void hex64(char *&p, uint64_t v) {
    for (int i = 44; i >= 0; i -= 4) *p++ = "0123456789ABCDEF"[(v >> i) & 15];
}

static bool debugHook(char cmd, const char *args) {
    char buf[96], *p;
    switch (cmd) {
        case 'W':
            p = fmtInt(fmtStr(buf, "THINK positions="), (int32_t)ai::positions());
            p = fmtInt(fmtStr(p, " depth="), ai::depthReached());
            p = fmtInt(fmtStr(p, " score="), ai::score());
            p = fmtInt(fmtStr(p, " col="), ai::chosen() + 1);
            p = fmtInt(fmtStr(p, " ms="), (int32_t)thinkMs);
            p = fmtInt(fmtStr(p, " slice_us="), (int32_t)sliceUs);
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        case 'G':
        case 'M': {
            game::Setup s;
            s.mode = (uint8_t)dbg::parseNum(args, 10);
            s.level = (uint8_t)dbg::parseNum(args, 10);
            s.first = (uint8_t)dbg::parseNum(args, 10);
            s.quick = opt.speed;
            s.seed = 1;
            if (s.mode > 1 || s.level >= game::LEVELS) return false;
            fresh = cmd == 'G';
            if (fresh) {
                s.seed = dbg::parseNum(args, 10);
                next = s;
            } else {
                while (*args == ' ') args++;
                if (!game::startPosition(s, args)) return false;
            }
            enter(Scr::Play);
            return true;
        }
        case 'D': {
            uint8_t col = (uint8_t)(dbg::parseNum(args, 10) - 1);
            if (cur != Scr::Play || overlay || !stage::ready() || !game::humanToMove()) return false;
            stage::setCursor(col < c4::COLS ? col : 3);
            return game::drop(col);
        }
        case 'H': {
            p = fmtStr(buf, "BOARD ");
            hex64(p, game::board.side[0]);
            *p++ = ' ';
            hex64(p, game::board.side[1]);
            *p++ = ' ';
            *p++ = (char)('0' + game::side());
            *p++ = ' ';
            *p++ = overlay == RESULT ? 'O' : !game::active() ? 'W' : game::humanToMove() ? 'M' : 'T';
            *p++ = ' ';
            *p++ = cur == Scr::Play && overlay == NONE && stage::ready() && !fadeIn && !fadeOut ? '1' : '0';
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'E': {
            uint8_t who = (uint8_t)dbg::parseNum(args, 10);
            if (who > 1 || cur != Scr::Play || !game::humanToMove()) return false;
            game::resign();
            fmtStr(game::said, who ? "GOOD GAME!\nDO TRY\nAGAIN." : "CONGRATU-\nLATIONS!\nWELL PLAYED.");
            game::winner = who;
            stage::showEnding(who);
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
        case 'A': {
            if (cur != Scr::Play || overlay || !stage::ready() || !game::humanToMove()) return false;
            static c4::Rng steady = {12345};
            ai::start(game::board, game::side(), ai::SHARK, steady);
            while (!ai::step(1000)) {}
            stage::setCursor(ai::chosen());
            return game::drop(ai::chosen());
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
    if (opt.level >= game::LEVELS) opt.level = 0;
    if (opt.first > 2) opt.first = 0;
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
#if !CHF4_LEAN
        case Scr::Setup:   setupUpdate(); break;
#endif
        case Scr::Play:    playUpdate(); break;
#if !CHF4_LEAN
        case Scr::Options: optionsUpdate(); break;
#endif
    }
    fx::update();
}

void render(uint32_t frame) {
    switch (cur) {
        case Scr::Title:   titleRender(frame); break;
#if !CHF4_LEAN
        case Scr::Setup:   setupRender(frame); break;
#endif
        case Scr::Play:    playRender(frame); break;
#if !CHF4_LEAN
        case Scr::Options: optionsRender(frame); break;
#endif
    }
}

}  // namespace screens
