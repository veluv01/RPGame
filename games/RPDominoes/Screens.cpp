// The screens (Screens.h): title, setup, play with its pause and result,
// options; saving at the right moments; the debug protocol's game hooks.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (hot pixel loops live in the library's Draw/Mask)
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
#include "Match.h"
#include "Stage.h"
#include "Save.h"
#include "src/assets/Assets.h"
#ifdef CHSIM
#include <sim.h>
#endif

namespace screens {

using dom::NONE;

enum class Scr : uint8_t { Title, Setup, Play, Options };
static Scr cur = Scr::Title, pending = Scr::Title;
static uint16_t t;                   // frames on this screen
static uint8_t fadeOut, fadeIn;
static uint8_t sel;                  // menu cursor
static bool twoPlayers;              // the setup screen is for a game between two people
#if !CHDM_LEAN
static Scr optBack = Scr::Title;
#endif

static Options opt;
static Stats stats;
static bool hasGame;                 // a saved game is waiting

// Play-screen overlays.
enum Overlay : uint8_t { NONE_, PAUSE, RESULT };
static Overlay overlay;
static bool statsCounted;

static const char *const OPPONENT[match::LEVELS] = {"ROOKIE", "REGULAR", "SHARK"};
static const char *const OPP_LINE[match::LEVELS] = {
    "PLAYS WHATEVER FITS", "TAKES EVERY POINT GOING", "COUNTS WHAT YOU HOLD"};
static const char *const GAME[2] = {"DRAW", "ALL FIVES"};
static const char *const GAME_LINE[2] = {"FIRST OUT SCORES THE PIPS LEFT", "ENDS ADDING TO FIVES SCORE"};
// What a match is played to, in fives: by game, short to long.
static const uint8_t TARGETS[2][3] = {{10, 20, 30}, {20, 30, 40}};

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
    fx::clear();
    pal::setMode(pal::CASINO);
    table::useSet(s == Scr::Title ? table::SET_WHITE : opt.tiles);
    if (s == Scr::Title) {
        titleTiles();
        audio::sfx(Sfx::Title);
    }
    if (s == Scr::Setup) sel = 3;
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
    fontHalf(64 - w / 2, y, text, nullptr, 1, WOOD);
}

static void centred35(int y, const char *s, uint8_t c) { text35(64 - text35Width(s) / 2, y, s, c); }
// The display font, plain: choices, the panels' first lines.
static void centred2(int y, const char *s, uint8_t c) {
    uint8_t gap = fontWidth(s) > 102 ? 0 : 1;           // (a long line: the letters closer)
    fontText(64 - fontWidth(s, gap) / 2, y, s, c, gap);
}

// A menu line in the serif, boxed while chosen with two clear pixels between
// the frame and the lettering above and below.
static void menuItem(int y, const char *s, bool on, uint32_t frame) {
    int w = fontWidth(s), x = 64 - w / 2;
    if (on) {
        // (Radius 2: its corners one clean diagonal step; 3's steps, 2 then
        // 1 then 1, read as lumps at this size.)
        fillRound(x - 7, y - 3, w + 14, 15, 2, NAVY);
        roundRect(x - 7, y - 3, w + 14, 15, 2, (frame & 16) ? FX_B : GOLD);
    }
    fontText(x, y, s, on ? GOLD : WHITE);
}

static bool menuNav(uint8_t n) {
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel + 1 < n) { sel++; audio::sfx(Sfx::Cursor); }
    return rpgame.justPressed(A_BUTTON);
}

// The tiles are shuffled from when you pressed the button: your timing, not ours.
static uint32_t seedNow() { return micros() * 2654435761u ^ rpgame.frameCount; }

// ---------------------------------------------------------------------------
// Title: dominoes tumbling down the felt behind the name (CHMahjong's and
// CHSolitaire's title, with tiles).
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_ONE, I_TWO, I_CONTINUE, I_OPTIONS };
static const char *const ITEM[4] = {"1 PLAYER", "2 PLAYERS", "CONTINUE", "OPTIONS"};

static uint8_t titleItems(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) items[n++] = I_CONTINUE;
    items[n++] = I_ONE;
    items[n++] = I_TWO;
#if !CHDM_LEAN
    items[n++] = I_OPTIONS;
#endif
    return n;
}

static void beginPlay() {
    overlay = NONE_;
    statsCounted = false;
}

static void newGame(uint8_t mode) {
    match::Setup s;
    s.mode = mode;
    s.level = opt.level;
    s.game = opt.game & 1;
    s.target = TARGETS[s.game][opt.target % 3];
    s.seed = seedNow();
    match::start(s);
    beginPlay();
    go(Scr::Play);
}

// Tiles sinking at eight depths, the far ones slower, the white set and the
// black (whatever set you play with), all at their true size (one pixel
// of the art a pixel of the screen), each swaying and tumbling slowly end
// over end as it falls; now and then a
// meteor: one streaks across, spinning hard, trailing rainbow sparks.
enum { RAIL = 30, FALLERS = 8 };
struct Faller {
    int16_t x16, y16;                // its middle, Q4
    int8_t vx, vy, spin;             // Q4 a frame; spin in 1/16ths of 1/256 turn
    uint8_t sway, swaySpd, amp;
    uint16_t ang;
};
static Faller fallers[FALLERS + 1];  // the last: the meteor
static uint8_t fallerImg[FALLERS + 1][table::TILE_IMG];
static bool meteorOn, railKept;
static uint16_t meteorIn;

static int menuTop(uint8_t n) { return 128 - n * 14 - 4; }

// Slot i's tile, new, at height y: its size and pace go with its depth.
static void dropFaller(uint8_t i, int y) {
    Faller &f = fallers[i];
    f.x16 = (int16_t)(fx::rndRange(4, 124) << 4);
    f.y16 = (int16_t)(y << 4);
    f.vx = (int8_t)fx::rndRange(-1, 2);
    f.vy = (int8_t)(3 + i + fx::rndRange(0, 3));
    int spin = fx::rndRange(2, 6);                                 // a turn in 15-40 s
    f.spin = (int8_t)(fx::rnd() & 1 ? spin : -spin);
    f.ang = (uint16_t)fx::rnd();
    f.sway = (uint8_t)fx::rnd();
    f.swaySpd = (uint8_t)fx::rndRange(1, 3);
    f.amp = (uint8_t)fx::rndRange(2, 7);
    uint8_t a = (uint8_t)fx::rndRange(0, 7), b = (uint8_t)fx::rndRange(0, 7);
    table::tileImage(fallerImg[i], a < b ? a : b, a < b ? b : a, (fx::rnd() & 3) == 0);
}


static void titleTiles() {
    for (uint8_t i = 0; i < FALLERS; i++) dropFaller(i, fx::rndRange(RAIL, 120));
    meteorOn = railKept = false;
    meteorIn = 150;
}

static void titleUpdate() {
    uint8_t items[5], n = titleItems(items);
    if (sel >= n) sel = 0;
    for (uint8_t i = 0; i < FALLERS + meteorOn; i++) {
        Faller &f = fallers[i];
        f.x16 = (int16_t)(f.x16 + f.vx);
        f.y16 = (int16_t)(f.y16 + f.vy);
        f.ang = (uint16_t)(f.ang + f.spin);
        if (!(t & 3)) f.sway = (uint8_t)(f.sway + f.swaySpd);
        int x = f.x16 >> 4, y = f.y16 >> 4;
        if (i < FALLERS) {
            if (y > menuTop(n) + 16) dropFaller(i, RAIL - 14);       // round again, from behind the rail
            continue;
        }
        // The meteor's trail.
        for (uint8_t k = 0; k < 2; k++)
            fx::spawn(k ? fx::STAR : fx::SPARK, x + fx::rndRange(-5, 6), y + fx::rndRange(-6, 7),
                      fx::rndRange(-8, 9) - f.vx / 4, fx::rndRange(-12, 3), (uint8_t)fx::rndRange(18, 34),
                      fx::RAIN[(t / 2 + k) % 5]);
        if (y > 150 || x < -30 || x > 158) { meteorOn = false; meteorIn = (uint16_t)fx::rndRange(200, 420); }
    }
    if (!meteorOn && !--meteorIn) {
        bool left = fx::rnd() & 1;
        dropFaller(FALLERS, RAIL - 16);
        Faller &m = fallers[FALLERS];
        m.x16 = (int16_t)((left ? fx::rndRange(-10, 40) : fx::rndRange(88, 138)) << 4);
        m.vx = (int8_t)(left ? fx::rndRange(10, 18) : -fx::rndRange(10, 18));
        m.vy = (int8_t)fx::rndRange(28, 40);
        m.spin = (int8_t)(left ? 90 : -90);
        m.amp = 0;
        meteorOn = true;
        audio::sfx(Sfx::Whoosh);
    }
    if (menuNav(n)) {
        audio::sfx(Sfx::Select);
        switch (items[sel]) {
#if CHDM_LEAN
            case I_ONE: newGame(match::VS_CPU); break;
            case I_TWO: newGame(match::TWO_PLAYER); break;
#else
            case I_ONE: twoPlayers = false; go(Scr::Setup); break;
            case I_TWO: twoPlayers = true; go(Scr::Setup); break;
#endif
            case I_CONTINUE:
                if (save::loadGame()) { beginPlay(); go(Scr::Play); }
                else { hasGame = false; railKept = false; audio::sfx(Sfx::Deny); }
                break;
#if !CHDM_LEAN
            case I_OPTIONS: optBack = Scr::Title; go(Scr::Options); break;
#endif
        }
    }
}

static void titleRender(uint32_t frame) {
    // The lettering is the costliest thing on the screen, and nothing passes
    // over the top rail: it is drawn once and left in the framebuffer, and
    // everything else is drawn below it.
    if (!railKept) {
        gfx_fillRect(0, 0, 128, RAIL, INK);
        gfx_hline(0, RAIL, 128, GOLD);
        // The name, drawn at this size (tools/art/logo.txt), straight onto
        // the black: the top rows FX_B, so the palette makes them shimmer.
        Mask m = maskBegin(LOGO_W, LOGO_H);
        maskBlit1(m, LOGO, LOGO_W, LOGO_H);
        uint8_t r[LOGO_H];
        for (int i = 0; i < LOGO_H; i++) r[i] = i < 7 ? FX_B : (i < 12 ? GOLD : WOOD);
        maskDraw(m, 64 - LOGO_W / 2, 7, 0, INK, -1, r);
        railKept = true;
    }
    gfx_setClip(0, RAIL + 1, 128, 127 - RAIL);
    gfx_fillRect(0, RAIL + 1, 128, 127 - RAIL, FELT);
    dither(0, RAIL + 1, 128, 3, FELT_DK, 0);                 // the rail's shadow on the felt
    for (uint8_t i = 0; i < FALLERS + meteorOn; i++) {
        const Faller &f = fallers[i];
        int x = (f.x16 >> 4) + ((fx::isin(f.sway) * f.amp) >> 8);
        table::spinImage(fallerImg[i], x, f.y16 >> 4, (uint8_t)(f.ang >> 4), 256, RM_ID);
    }
    fx::drawParticles(2);
    uint8_t items[5], n = titleItems(items);
    int y0 = menuTop(n);
    gfx_fillRect(0, y0, 128, 128 - y0, NAVY);
    gfx_hline(0, y0, 128, GOLD);
    for (uint8_t i = 0; i < n; i++) menuItem(y0 + 6 + i * 14, ITEM[items[i]], i == sel, frame);
    gfx_resetClip();
}

// ---------------------------------------------------------------------------
// Setup: the opponent, the game, and what it is played to.
// ---------------------------------------------------------------------------
#if !CHDM_LEAN
static void setupUpdate() {
    // Rows: 0 opponent (not for two players), 1 game, 2 target, 3 begin.
    uint8_t first = twoPlayers ? 1 : 0;
    if (sel < first) sel = first;
    int d = rpgame.repeat(RIGHT_BUTTON) ? 1 : (rpgame.repeat(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.repeat(UP_BUTTON) && sel > first) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel < 3) { sel++; audio::sfx(Sfx::Cursor); }
    if (d && sel < 3) {
        if (sel == 0) opt.level = (uint8_t)((opt.level + match::LEVELS + d) % match::LEVELS);
        if (sel == 1) opt.game ^= 1;
        if (sel == 2) opt.target = (uint8_t)((opt.target + 3 + d) % 3);
        audio::sfx(Sfx::Coin);
    }
    // Hold SELECT to wipe your record against this opponent.
    static uint8_t hold;
    hold = rpgame.pressed(SELECT_BUTTON) ? (uint8_t)(hold + 1) : 0;
    if (hold == 90 && !twoPlayers) {
        stats.won[opt.level] = stats.lost[opt.level] = stats.mwon[opt.level] = stats.mlost[opt.level] = 0;
        persist(hasGame);
        audio::sfx(Sfx::Boom);
    }
    if (rpgame.justPressed(A_BUTTON)) {
        if (sel < 3) { sel++; audio::sfx(Sfx::Cursor); }
        else { audio::sfx(Sfx::Select); persist(hasGame); newGame(twoPlayers ? match::TWO_PLAYER : match::VS_CPU); }
    }
    if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

// A setup choice: boxed while chosen, with bobbing arrows to change it.
static void choice(int y, const char *s, bool on, uint32_t frame) {
    int w = fontWidth(s, 0), bob = (frame >> 3) & 1;
    if (on) fillRound(64 - w / 2 - 5, y - 3, w + 10, 15, 2, NAVY);
    fontText(64 - w / 2, y, s, on ? GOLD : WHITE, 0);
    text35(64 - w / 2 - 6 - bob, y + 3, "<", GOLD);
    text35(64 + w / 2 + 3 + bob, y + 3, ">", GOLD);
}

static void setupRender(uint32_t frame) {
    feltBackdrop();
    char buf[36], *p;
    if (twoPlayers) {
        heading("TWO PLAYERS", 9);
        centred35(30, "PASS THE HANDHELD EACH TURN", FELT_LT);
    } else {
        // Stars for how hard they play.
        for (int i = 0; i <= opt.level; i++) text35(64 - opt.level * 4 + i * 8 - 1, 7, "*", FX_B);
        choice(16, OPPONENT[opt.level], sel == 0, frame);
        centred35(31, OPP_LINE[opt.level], FELT_LT);
        // Your record against them.
        p = fmtStr(buf, "ROUNDS ");
        p = fmtInt(p, stats.won[opt.level]); *p++ = '-';
        p = fmtInt(p, stats.lost[opt.level]); p = fmtStr(p, "  MATCHES ");
        p = fmtInt(p, stats.mwon[opt.level]); *p++ = '-';
        fmtInt(p, stats.mlost[opt.level]);
        centred35(39, buf, GOLD);
    }
    uint8_t g = opt.game & 1;
    choice(52, GAME[g], sel == 1, frame);
    centred35(67, GAME_LINE[g], FELT_LT);
    fmtInt(fmtStr(buf, "PLAY TO "), TARGETS[g][opt.target % 3] * 5);
    choice(80, buf, sel == 2, frame);
    menuItem(105, "BEGIN", sel == 3, frame);
}
#endif

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
// The next tile of the hand to the left or right of `from`, round the ends.
static uint8_t beside(uint32_t hand, uint8_t from, int d) {
    for (uint8_t k = 1; k <= dom::TILES; k++) {
        uint8_t tile = (uint8_t)((from + dom::TILES + d * k) % dom::TILES);
        if ((hand >> tile) & 1) return tile;
    }
    return NONE;
}

static void playInput() {
    bool a = rpgame.justPressed(A_BUTTON), b = rpgame.justPressed(B_BUTTON);
    int d = rpgame.repeat(RIGHT_BUTTON) || rpgame.repeat(DOWN_BUTTON) ? 1
          : rpgame.repeat(LEFT_BUTTON) || rpgame.repeat(UP_BUTTON) ? -1 : 0;
    if (match::humanToDraw()) {
        if (a) match::draw();
        return;
    }
    if (!match::humanToPlay()) return;
    uint32_t hand = match::round.hand[match::round.turn];
    uint8_t c = stage::cursor(), arms[dom::ARMS];
    if (stage::choosing()) {
        // The tile fits more than one end: the glove goes between them.
        if (d) stage::chooseStep(d);
        if (a) match::play(c, stage::chosen());
        else if (b) { stage::unchoose(); audio::sfx(Sfx::Cursor); }
        return;
    }
    if (c == NONE || !((hand >> c) & 1)) {
        // A new turn, or a tile just drawn: the glove goes to the first tile that fits.
        c = dom::nth(hand, 0);
        for (uint8_t tile = 0; tile < dom::TILES; tile++)
            if (((hand >> tile) & 1) && match::options(tile, arms)) { c = tile; break; }
        stage::setCursor(c);
    }
    if (d) {
        stage::setCursor(c = beside(hand, c, d));
        audio::sfx(Sfx::Cursor);
    }
#if !CHDM_LEAN
    if (rpgame.justPressed(SELECT_BUTTON)) {
        ai::Move m;
        if (match::hint(m)) stage::hint(m.tile, m.arm);
    }
#endif
    if (!a) return;
    uint8_t n = match::options(c, arms);
    if (!n) stage::deny();
    else if (n == 1) match::play(c, arms[0]);
    else stage::choose(c, arms, n);
}

static const char *const PAUSE_ITEM[2] = {"RESUME", "SAVE+QUIT"};

static void pauseInput() {
    bool start = rpgame.justPressed(START_BUTTON);
    if (menuNav(2) || start) {
        overlay = NONE_;
        audio::sfx(Sfx::Select);
        if (start || sel == 0) return;
        if (match::between()) match::nextRound();
        persist(match::active());
        go(Scr::Title);
        return;
    }
    if (rpgame.justPressed(B_BUTTON)) overlay = NONE_;
}

static void countResult() {
    if (statsCounted || match::setup.mode != match::VS_CPU) return;
    statsCounted = true;
    uint8_t lv = match::setup.level;
    if (match::winner == 0) stats.won[lv]++;
    else if (match::winner == 1) stats.lost[lv]++;
    if (match::matchOver()) { if (match::score[0] > match::score[1]) stats.mwon[lv]++; else stats.mlost[lv]++; }
}

static void playUpdate() {
    switch (overlay) {
        case PAUSE: pauseInput(); return;                            // the game waits
        case RESULT:
            if (rpgame.justPressed(A_BUTTON)) {
                audio::sfx(Sfx::Select);
                overlay = NONE_;
                statsCounted = false;
                if (match::between()) { match::nextRound(); persist(true); }
                else { persist(false); newGame(match::setup.mode); }
            }
            if (rpgame.justPressed(B_BUTTON)) {
                // Back to the menu; a match that goes on is saved at its next round.
                audio::sfx(Sfx::Select);
                bool on = match::between();
                if (on) match::nextRound();
                persist(on);
                go(Scr::Title);
            }
            break;
        default:
            if (stage::waiting()) {
                if (rpgame.justPressed(A_BUTTON | B_BUTTON | START_BUTTON)) { stage::acknowledge(); audio::sfx(Sfx::Select); }
                break;
            }
            if (rpgame.justPressed(A_BUTTON | B_BUTTON)) stage::hurry();
            // B held (with no end being chosen): the whole table.
            stage::setOverview(rpgame.pressed(B_BUTTON) && !stage::choosing() && match::active());
            if (rpgame.justPressed(START_BUTTON) && match::active()) { overlay = PAUSE; sel = 0; audio::sfx(Sfx::Select); break; }
            if (stage::ready()) playInput();
            break;
    }
    match::update(stage::busy());
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
        panel(42, 38);
        for (uint8_t i = 0; i < 2; i++) {
            int y = 48 + i * 14;
            if (i == sel) fillRound(14, y - 3, 100, 15, 2, INK);
            centred2(y, PAUSE_ITEM[i], i == sel ? FX_B : WHITE);
        }
    } else if (overlay == RESULT) {
        bool done = match::matchOver();
        uint8_t w = done ? (uint8_t)(match::score[1] > match::score[0]) : match::winner;
        const int y = 42;
        panel(y, 46);
        const char *head;
        if (w == 2) head = "NO WINNER";
        else if (vsCpu) head = done ? (w ? "MATCH LOST" : "MATCH WON!") : w ? "CPU'S ROUND" : "YOUR ROUND!";
        else head = done ? (w ? "P2 WINS!" : "P1 WINS!") : w ? "P2'S ROUND" : "P1'S ROUND";
        centred2(y + 4, head, FX_B);
        // How the round ended, and what it was worth.
        p = fmtStr(buf, match::reason == dom::DOMINO ? "DOMINO" : "BLOCKED");
        if (w < 2 || !done) {
            p = fmtStr(p, ": +");
            p = fmtInt(p, match::points);
            if (match::round.game == dom::DRAW) fmtStr(p, match::points == 1 ? " PIP" : " PIPS");
        }
        centred35(y + 20, buf, SILVER);
        p = fmtStr(buf, vsCpu ? "YOU " : "P1 ");
        p = fmtInt(p, match::score[0]);
        p = fmtStr(p, vsCpu ? "  CPU " : "  P2 ");
        p = fmtInt(p, match::score[1]);
        fmtInt(fmtStr(p, "  TO "), match::target());
        centred35(y + 28, buf, WHITE);
        centred35(y + 37, done ? (vsCpu ? "A REMATCH   B MENU" : "A AGAIN   B MENU") : "A NEXT ROUND   B MENU", GOLD);
    }
}

// ---------------------------------------------------------------------------
// Options (device debug builds leave the screen out to fit the protocol:
// their tests start games directly)
// ---------------------------------------------------------------------------
#if !CHDM_LEAN
enum Opt : uint8_t { O_SOUND, O_FELT, O_SPEED, O_TILES, O_BACK, OPT_COUNT };
static const char *const OPT_TEXT[OPT_COUNT] = {
    "SOUND|OFF|ON", "FELT|GREEN|BLUE|RED|PURPLE", "PACE|FUN|QUICK",
    "TILES|WHITE|BLACK|IVORY|RED|BLUE|JADE|GRAPE|PINK", "BACK",
};
// Each row's byte in Options.
static const uint8_t OPT_AT[OPT_COUNT - 1] = {0, 1, 2, 6};
static uint8_t &optByte(uint8_t i) { return ((uint8_t *)&opt)[OPT_AT[i]]; }

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
        table::useSet(opt.tiles);
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
        int y = 28 + i * 14;
        char label[12], value[12];
        optField(OPT_TEXT[i], 0, label);
        if (i == sel) {
            fillRound(8, y - 3, 112, 15, 2, NAVY);
            roundRect(8, y - 3, 112, 15, 2, (frame & 16) ? FX_B : GOLD);
        }
        if (i == O_BACK) { centred2(y, label, i == sel ? GOLD : WHITE); continue; }
        fontText(13, y, label, i == sel ? GOLD : WHITE);
        optField(OPT_TEXT[i], (uint8_t)(optByte(i) + 1), value);
        fontText(115 - fontWidth(value, 0), y, value, i == sel ? WHITE : FELT_LT, 0);
    }
    // The set of tiles, as it plays.
    table::drawTile(36, 98, 6, 5, false, 3, 5, table::tileFace(), INK, 3);
    table::drawTile(67, 98, 1, 3, false, 3, 5, table::tileFace(), INK, 3);
    centred35(118, "3X5 FONT: PRESS PLAY ON TAPE", SILVER);
}
#endif

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   G <mode> <level> <seed> <game> <target>   start a match (mode 0 vs CPU, 1 two players; game 0 DRAW,
//                                    1 ALL FIVES; target in points)
//   D <tiles>                        the next deal: "66 65 ..", seven for side 0, seven for side 1,
//                                    then the boneyard in the order it is drawn
//   C <a> <b>                        the match's score
//   W <side>                         end the round now, as if the side had played its last tile
//   Y                                render cost by section on the board
//   J <T|S|O>                        jump to title/setup/options
//   A                                play for the human: the SHARK's choice, or a draw
//   H                                the game: BOARD <tiles down> <score> <score> <state> <ready> <side>
//                                    <the last round's winner>
static bool debugHook(char cmd, const char *args) {
    char buf[96], *p;
    switch (cmd) {
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
        case 'G': {
            match::Setup s;
            s.mode = (uint8_t)dbg::parseNum(args, 10);
            s.level = (uint8_t)dbg::parseNum(args, 10);
            s.seed = dbg::parseNum(args, 10);
            s.game = (uint8_t)dbg::parseNum(args, 10);
            s.target = (uint8_t)(dbg::parseNum(args, 10) / 5);
            match::start(s);
            beginPlay();
            enter(Scr::Play);
            return true;
        }
        case 'C': {
            uint16_t a = (uint16_t)dbg::parseNum(args, 10);
            match::setScore(a, (uint16_t)dbg::parseNum(args, 10));
            stage::snap();
            return true;
        }
        case 'H': {
            p = fmtStr(buf, "BOARD ");
            p = fmtInt(p, match::round.n); *p++ = ' ';
            p = fmtInt(p, match::score[0]); *p++ = ' ';
            p = fmtInt(p, match::score[1]); *p++ = ' ';
            *p++ = overlay == RESULT ? (match::matchOver() ? 'O' : 'B') : stage::waiting() ? 'W'
                   : match::humanToDraw() ? 'R' : match::humanToPlay() ? 'M' : 'T';
            *p++ = ' ';
            *p++ = cur == Scr::Play && overlay == NONE_ && stage::ready() ? '1' : '0';
            *p++ = ' ';
            *p++ = (char)('0' + match::round.turn);
            *p++ = ' ';
            *p++ = (char)('0' + match::winner);
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'D':
            match::stackDeal(args);
            return true;
        case 'W':
            match::endRound((uint8_t)dbg::parseNum(args, 10));
            return true;
        case 'J': {
            static const char K[] = "TSO";
            const char *q = strchr(K, args[0]);
            if (!q) return false;
            static const Scr S[] = {Scr::Title, Scr::Setup, Scr::Options};
            enter(S[q - K]);
            return true;
        }
        case 'A': {
            if (match::humanToDraw()) { match::draw(); return true; }
            ai::Move m;
            if (!match::hint(m)) return false;
            stage::unchoose();
            return match::play(m.tile, m.arm);
        }
    }
    return false;
}
#endif

// ---------------------------------------------------------------------------
void begin() {
    stage::begin();
    opt.sound = 1;
    opt.level = 1;
    opt.game = dom::FIVES;
    opt.target = 0;
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
#if !CHDM_LEAN
        case Scr::Setup:   setupUpdate(); break;
#endif
        case Scr::Play:    playUpdate(); break;
#if !CHDM_LEAN
        case Scr::Options: optionsUpdate(); break;
#endif
    }
    fx::update();
}

void render(uint32_t frame) {
    switch (cur) {
        case Scr::Title:   titleRender(frame); break;
#if !CHDM_LEAN
        case Scr::Setup:   setupRender(frame); break;
#endif
        case Scr::Play:    playRender(frame); break;
#if !CHDM_LEAN
        case Scr::Options: optionsRender(frame); break;
#endif
    }
}

}  // namespace screens
