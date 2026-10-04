// The screens (Screens.h): title, setup, play with its action bar and
// overlays, the results, options; and the game's debug commands.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library)
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "config.h"
#include "Screens.h"
#include "Fx.h"
#include "Sounds.h"
#include "Iso.h"
#include "Game.h"
#include "Stage.h"
#include "MapView.h"
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
#if !CHBW_LEAN
static Scr optBack = Scr::Title;
#endif

static Options opt;
static Stats stats;
static bool hasGame;                 // a saved game is waiting

// Play-screen overlays.
enum Overlay : uint8_t { NONE, PAUSE, MAP, CONFIRM, RESULT };
static Overlay overlay;
static bool statsCounted;
static bool managing;                // the manage view: the glove on your deeds
static uint8_t mTile;
static uint8_t barSel;

// Each player's net worth as each round began ($10s), for the result's
// graph: the story of the game. (A continued game's story starts there.)
static const uint8_t HIST = 64;
static int16_t hist[SEATS][HIST];
static uint8_t nHist;
static uint16_t histRound;

static void record() {
    if (nHist == HIST) return;
    for (uint8_t p = 0; p < st.players; p++) hist[p][nHist] = (int16_t)(netWorth(p) / 10);
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

static void flushEvents() {
    Event e;
    while (popEvent(e)) {}
}

static void enter(Scr s) {
    cur = s;
    stage::invalidate();
    t = 0;
    sel = 0;
    fadeIn = 8;
    fx::clear();
    if (s == Scr::Title) {
        // The board behind the title: a table just dealt, close up, the
        // camera cruising round the ring.
        Setup demo = {{CPU, CPU, CPU, CPU}, 0, 7};
        start(demo);
        flushEvents();
        stage::reset();
        stage::setZoom(10);
        audio::sfx(Sfx::Title);
    }
    if (s == Scr::Setup) sel = 5;                // on BEGIN: the table is set, A deals
    if (s == Scr::Play) {
        overlay = NONE;
        statsCounted = managing = false;
        stage::manage(-1);
        nHist = 0;
        histRound = 0;
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

static void panel(int y, int h) {
    fillRound(14, y, 100, h, 3, NAVY);
    roundRect(14, y, 100, h, 3, GOLD);
}

static uint32_t seedNow() { return micros() * 2654435761u ^ rpgame.frameCount; }

// ---------------------------------------------------------------------------
// Title: a dealt table, the camera cruising round the board.
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_ONE, I_TWO, I_CONTINUE, I_OPTIONS };
static const char *const ITEM[4] = {"1 PLAYER", "2 PLAYERS", "CONTINUE", "OPTIONS"};

static uint8_t titleItems(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) items[n++] = I_CONTINUE;
    items[n++] = I_ONE;
    items[n++] = I_TWO;
#if !CHBW_LEAN
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
                break;
            case I_CONTINUE:
                // Continuing takes the game out of the save.
                if (!save::loadGame()) audio::sfx(Sfx::Deny);
                else go(Scr::Play);
                hasGame = false;
                persist();
                break;
#if !CHBW_LEAN
            case I_OPTIONS: optBack = Scr::Title; go(Scr::Options); break;
#endif
        }
    }
    // Round the ring, a tile every 48 frames, gliding between their centres.
    int ax, ay, bx, by, k = t % 48;
    uint8_t at = (uint8_t)(t / 48 % 40);
    iso::worldOf(at, ax, ay);
    iso::worldOf((uint8_t)((at + 1) % 40), bx, by);
    iso::cam.x = ax + (bx - ax) * k / 48;
    iso::cam.y = ay + (by - ay) * k / 48 - 6;
}

// The game's own lettering (tools/art: LOGO), gold over wood like
// CHBlackjack's. Its top rows are FX_B, so the palette makes it shimmer.
static void logo(int y) {
    Mask m = maskBegin(LOGO_W, LOGO_H);
    maskBlit1(m, LOGO, LOGO_W, LOGO_H);
    uint8_t ramp[LOGO_H];
    for (int i = 0; i < LOGO_H; i++) ramp[i] = i < 2 ? FX_B : (i < 11 ? GOLD : WOOD);
    maskDraw(m, 64 - LOGO_W / 2, y, 0, INK, WINE, ramp);
}

static void titleRender(uint32_t frame) {
    stage::renderScene(frame);
    dither(0, 0, 128, 30, INK, 0);
    logo(8);
    uint8_t items[4], n = titleItems(items);
    int y0 = 128 - n * 14 - 1;
    dither(0, y0 - 5, 128, 128 - y0 + 5, INK, 1);
    for (uint8_t i = 0; i < n; i++) menuItem(y0 + i * 14, ITEM[items[i]], i == sel, frame);
}

// ---------------------------------------------------------------------------
// Setup: who sits at the table, and closing time.
// ---------------------------------------------------------------------------
static const char *const SEAT[5] = {"EMPTY", "PLAYER", "CPU EASY", "CPU FAIR", "CPU SHARK"};

static void newGame() {
    Setup s = {};                    // deal 0: the usual number of deeds
    memcpy(s.kind, opt.seat, SEATS);
    s.roundCap = (uint8_t)(opt.rounds * 10);
    s.seed = seedNow();
    start(s);
    go(Scr::Play);
}

static void setupUpdate() {
    int d = rpgame.repeat(RIGHT_BUTTON) ? 1 : (rpgame.repeat(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && sel < 5) { sel++; audio::sfx(Sfx::Cursor); }
    if (d && sel < 4) {
        // The first two seats are always taken.
        uint8_t &k = opt.seat[sel];
        do k = (uint8_t)((k + 5 + d) % 5); while (!k && sel < 2);
        audio::sfx(Sfx::Coin);
    }
    if (d && sel == 4) { opt.rounds = (uint8_t)((opt.rounds + 5 + d) % 6 + 1); audio::sfx(Sfx::Coin); }
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
    char buf[16];
    for (uint8_t i = 0; i < 5; i++) {
        int y = 32 + i * 13;
        bool on = sel == i;
        if (on) fillRound(10, y - 3, 108, 15, 3, NAVY);
        if (i < 4) {
            if (opt.seat[i]) sprite4(TOKEN[i], 14, y - 1, RM_ID);
            fmtStr(buf, SEAT[opt.seat[i]]);
        } else {
            fmtStr(fmtInt(buf, opt.rounds * 10), " ROUNDS");
        }
        text35x2(37, y, buf, on ? GOLD : (i < 4 && !opt.seat[i]) ? FELT_LT : WHITE);
        if (on) arrows(y + 3, 37, 37 + text35x2Width(buf), frame);
    }
    menuItem(104, "BEGIN", sel == 5, frame);
}

// ---------------------------------------------------------------------------
// Play: the action bar
// ---------------------------------------------------------------------------
enum Btn : uint8_t { B_ROLL, B_PAY, B_CARD, B_MANAGE, B_BUY, B_AUCTION, B_BUILD };
static const char *const BTN[7] = {"ROLL", "PAY $50", "CARD", "MANAGE", "BUY", "AUCTION", "BUILD"};
static const uint8_t BTN_COLOUR[7] = {FELT_LT, GOLD, CYAN, BLUE, FELT_LT, RED, GOLD};

// Where the player at the turn could build next (from tile `from`, in
// board order; the same group only, if `g` is one), else -1.
static int buildable(uint8_t from, uint8_t g = 0xFF) {
    for (uint8_t i = 0; i < board::TILES; i++) {
        uint8_t t = (uint8_t)((from + i) % board::TILES);
        if (canBuild(t) && (g == 0xFF || board::group(t) == g)) return t;
    }
    return -1;
}
static uint8_t btn[4], nBtn;

// The buttons the player at the turn has: on an unowned deed, or before
// the roll. `also`: one the CPU has just pressed (it may be gone already).
static void barFor(bool offer, uint8_t also = 0xFF) {
    const Player &me = st.pl[st.cur];
    nBtn = 0;
    if (offer) { btn[nBtn++] = B_BUY; btn[nBtn++] = B_AUCTION; return; }
    btn[nBtn++] = B_ROLL;
    if (me.jail || also == B_PAY) btn[nBtn++] = B_PAY;
    if (me.jail ? (me.flags & (F_CARD | F_CARD << 1)) != 0 : also == B_CARD) btn[nBtn++] = B_CARD;
    // Your deeds: BUILD when there is a house to be had, else MANAGE (the
    // same view, starting on a tile you can build on).
    if (buildable(0) >= 0 || also == B_BUILD) { btn[nBtn++] = B_BUILD; return; }
    for (uint8_t i = 0; i < board::TILES; i++)
        if (owner(i) == st.cur) { btn[nBtn++] = B_MANAGE; break; }
}

static bool showBar;
static uint8_t pressed = 0xFF;       // the CPU's choice, lit

static void drawBar(uint32_t frame) {
    char label[4][12];
    int w[4], total = -3;
    for (uint8_t i = 0; i < nBtn; i++) {
        char *p = fmtStr(label[i], BTN[btn[i]]);
        if (btn[i] == B_BUY) fmtMoney(fmtStr(p, " "), board::price(st.pl[st.cur].pos));
        w[i] = text35Width(label[i]) + 8;
        total += w[i] + 3;
    }
    gfx_fillRect(0, 112, 128, 16, NAVY);
    gfx_hline(0, 111, 128, GOLD);
    int x = 64 - total / 2;
    for (uint8_t i = 0; i < nBtn; i++) {
        bool on = pressed == 0xFF ? i == barSel : btn[i] == pressed;
        uint8_t c = BTN_COLOUR[btn[i]];
        bool dim = btn[i] == B_PAY && st.pl[st.cur].cash < 50;
        int y = on ? (pressed == 0xFF ? 113 : 115) : 114;
        fillRound(x, y, w[i], 11, 2, dim ? SILVER : c);
        if (on) roundRect(x, y, w[i], 11, 2, (frame & 8) ? WHITE : FX_B);
        text35(x + 4, y + 3, label[i], c == BLUE || c == RED ? WHITE : INK);
        x += w[i] + 3;
    }
}

// ---------------------------------------------------------------------------
// Play: input
// ---------------------------------------------------------------------------

// The manage view walks your deeds in board order.
static void manageStep(int d) {
    uint8_t k = mTile;
    for (uint8_t i = 0; i < board::TILES; i++) {
        k = (uint8_t)((k + board::TILES + d) % board::TILES);
        if (owner(k) == st.cur) break;
    }
    mTile = k;
    stage::manage(k);
}

static void manageInput() {
    if (rpgame.repeat(LEFT_BUTTON)) { manageStep(-1); audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(RIGHT_BUTTON)) { manageStep(1); audio::sfx(Sfx::Cursor); }
    if (rpgame.justPressed(UP_BUTTON)) {
        uint8_t g = board::group(mTile);
        if (!build(mTile)) audio::sfx(Sfx::Deny);
        else if (!canBuild(mTile)) {
            // Built evenly, so on to the next of the set: UP, UP, UP builds it up.
            int k = buildable(mTile, g);
            if (k >= 0) { mTile = (uint8_t)k; stage::manage(k); }
        }
    }
    if (rpgame.justPressed(DOWN_BUTTON) && !sell(mTile)) audio::sfx(Sfx::Deny);
    if (rpgame.justPressed(A_BUTTON)) {
        if (canOffer(mTile)) { overlay = CONFIRM; audio::sfx(Sfx::Select); }
        else audio::sfx(Sfx::Deny);
    }
    if (rpgame.justPressed(B_BUTTON)) { managing = false; stage::manage(-1); audio::sfx(Sfx::Cursor); }
}

static void barInput() {
    if (barSel >= nBtn) barSel = 0;
    if (rpgame.repeat(LEFT_BUTTON) && barSel > 0) { barSel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(RIGHT_BUTTON) && barSel + 1 < nBtn) { barSel++; audio::sfx(Sfx::Cursor); }
    if (!rpgame.justPressed(A_BUTTON)) return;
    bool ok = true;
    switch (btn[barSel]) {
        case B_ROLL:    ok = roll(); break;
        case B_PAY:     ok = payJail(); break;
        case B_CARD:    ok = useJailCard(); break;
        case B_BUY:     ok = buy(); break;
        case B_AUCTION: ok = decline(); break;
        case B_MANAGE: case B_BUILD:
            managing = true;
            mTile = st.pl[st.cur].pos;
            if (buildable(mTile) >= 0) mTile = (uint8_t)buildable(mTile);
            if (owner(mTile) != st.cur) manageStep(1); else stage::manage(mTile);
            break;
    }
    audio::sfx(ok ? Sfx::Select : Sfx::Deny);
    barSel = 0;
}

// The auction: every human has a button of their own - A, the D-pad, B,
// SELECT, in seat order - and taps it to take the lead.
static void auctionInput() {
    static const uint8_t TAP[4] = {A_BUTTON, UP_BUTTON | DOWN_BUTTON | LEFT_BUTTON | RIGHT_BUTTON, B_BUTTON,
                                   SELECT_BUTTON};
    uint8_t human = 0, down = rpgame.justPressedMask();
    for (uint8_t p = 0; p < st.players; p++) {
        if (!isHuman(p)) continue;
        if ((down & TAP[human++]) && p != auc.seller && !bid(p)) audio::sfx(Sfx::Deny);
    }
}

enum Action : uint8_t { A_RESUME, A_OPTIONS, A_QUIT };
static const char *const PAUSE_ITEM[3] = {"RESUME", "OPTIONS", "SAVE + QUIT"};

static void pauseInput() {
    bool startKey = rpgame.justPressed(START_BUTTON);
    if (menuNav(3) || startKey) {
        uint8_t a = startKey ? (uint8_t)A_RESUME : sel;
        overlay = NONE;
        audio::sfx(Sfx::Select);
#if !CHBW_LEAN
        if (a == A_OPTIONS) { optBack = Scr::Play; go(Scr::Options); }
#endif
        if (a == A_QUIT) { hasGame = false; persist(active()); go(Scr::Title); }
        return;
    }
    if (rpgame.justPressed(B_BUTTON)) overlay = NONE;
}

static void countResult() {
    if (statsCounted) return;
    statsCounted = true;
    stats.games++;
    if (isHuman(st.winner)) stats.humanWins++; else stats.cpuWins++;
    if (st.over == BY_BANKRUPTCY + 1) stats.busts++;
    int32_t w = netWorth(st.winner);
    if (w > stats.best) stats.best = w;
    persist();
}

static void playUpdate() {
    bool auction = phase() == P_AUCTION;
    switch (overlay) {
        case PAUSE: pauseInput(); return;            // the game (and the auction's clock) stands still
        case MAP:
            if (rpgame.justPressed(SELECT_BUTTON) || rpgame.justPressed(B_BUTTON)) {
                overlay = NONE;
                stage::invalidate();
                audio::sfx(Sfx::Whoosh);
            }
            return;
        case CONFIRM:
            if (rpgame.justPressed(A_BUTTON) && offer(mTile)) { overlay = NONE; managing = false; stage::manage(-1); audio::sfx(Sfx::Select); }
            else if (rpgame.justPressedMask() & (A_BUTTON | B_BUTTON)) { overlay = NONE; audio::sfx(Sfx::Cursor); }
            return;
        case RESULT:
            if (rpgame.justPressed(A_BUTTON)) { audio::sfx(Sfx::Select); newGame(); }
            if (rpgame.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
            break;
        default:
            if (rpgame.justPressed(START_BUTTON)) { overlay = PAUSE; sel = 0; audio::sfx(Sfx::Select); return; }
            if (auction) { if (auc.started) auctionInput(); break; }
            if (rpgame.justPressed(SELECT_BUTTON)) { overlay = MAP; audio::sfx(Sfx::Whoosh); return; }
            if (stage::waiting()) {
                if (rpgame.justPressedMask()) { stage::acknowledge(); audio::sfx(Sfx::Select); }
                break;
            }
            if (humanToAct() && !stage::busy()) {
                if (managing) manageInput();
                else { barFor(phase() == P_OFFER); barInput(); }
            }
            break;
    }
    // What the bar shows: your choices, or the CPU's choice as it makes it.
    uint8_t pick = stage::picked();
    pressed = 0xFF;
    showBar = false;
    if (overlay == NONE || overlay == CONFIRM) {
        if (pick) {
            static const uint8_t ACT_BTN[6] = {B_ROLL, B_PAY, B_CARD, B_BUY, B_AUCTION, B_BUILD};
            pressed = ACT_BTN[pick - 1];
            barFor(pressed == B_BUY || pressed == B_AUCTION, pressed);
            showBar = true;
        } else if (humanToAct() && !stage::busy() && !stage::waiting() && !managing) {
            barFor(phase() == P_OFFER);
            showBar = true;
        }
    }
    stage::setBar(showBar || managing);
    if (st.round != histRound && active()) { histRound = st.round; record(); }
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

// The manage view's keys, beside the deed's card.
static void drawManage() {
    char buf[32];
    const int x = 68;
    fillRound(x, 15, 58, 48, 3, NAVY);
    roundRect(x, 15, 58, 48, 3, GOLD);
    text35(x + 5, 19, "YOUR DEEDS", GOLD);
    text35(x + 5, 28, "< > NEXT", WHITE);
    fmtMoney(fmtStr(buf, "UP BUILD "), board::houseCost(mTile));
    text35(x + 5, 36, buf, canBuild(mTile) ? WHITE : SILVER);
    fmtMoney(fmtStr(buf, "DN SELL "), board::houseCost(mTile) / 2);
    text35(x + 5, 44, buf, canSell(mTile) ? WHITE : SILVER);
    text35(x + 5, 52, "A AUCTION IT", canOffer(mTile) ? WHITE : SILVER);
    // What the others pay to land here as it stands (a set doubles the
    // bare rent, railroads go by how many): it grows as you build.
    char *p = fmtStr(buf, "THEY PAY ");
    if (board::type(mTile) == board::UTIL) fmtStr(fmtInt(p, rent(mTile, 1)), "X DICE");
    else fmtMoney(p, rent(mTile, 0));
    fmtStr(buf + strlen(buf), "   B: BACK");
    int w = text35Width(buf);
    gfx_fillRect(62 - w / 2, 117, w + 4, 9, NAVY);    // (clear of anything standing on the carpet there)
    centred35(119, buf, WHITE);
}

// Who finished where: net worth, a bar for each growing as the count goes.
static void drawResult(uint32_t frame) {
    char buf[16];
    uint8_t order[SEATS], n = st.players;
    int32_t worth[SEATS], top = 1;
    for (uint8_t p = 0; p < n; p++) {
        order[p] = p;
        worth[p] = st.pl[p].flags & F_BUST ? -1 : netWorth(p);
        if (worth[p] > top) top = worth[p];
    }
    for (uint8_t i = 0; i < n; i++)
        for (uint8_t j = (uint8_t)(i + 1); j < n; j++)
            if (order[j] == st.winner || (order[i] != st.winner && worth[order[j]] > worth[order[i]])) {
                uint8_t s = order[i]; order[i] = order[j]; order[j] = s;
            }
    panel(14, 100);
    centred2(18, st.over == BY_CLOSING + 1 ? "CLOSING TIME" : "BANKRUPT!", FX_B);
    int grow = fx::ease(fx::OUT_CUBIC, t > 60 ? 60 : t, 60);
    for (uint8_t i = 0; i < n; i++) {
        uint8_t p = order[i];
        int y = 31 + i * 9;
        gfx_fillRect(20, y, 5, 5, stage::SEAT_COLOUR[p]);
        buf[0] = 'P'; buf[1] = (char)('1' + p); buf[2] = 0;
        text35(28, y, buf, i ? WHITE : FX_B);
        if (worth[p] < 0) { text35(40, y, "BUST", RED); continue; }
        gfx_fillRect(40, y, (int)(worth[p] * 40 / top * grow >> 8), 5, stage::SEAT_COLOUR[p]);
        fmtMoney(buf, worth[p] * grow >> 8);
        text35(108 - text35Width(buf), y, buf, i ? SILVER : GOLD);
    }
    // The story of the game: everyone's worth, round by round, drawn out
    // left to right (the winner's line last, on top).
    int y0 = 34 + n * 9, y1 = 101;
    gfx_hline(20, y1, 89, FELT_DK);
    if (nHist > 1) {
        // Scaled to the range it covers, so the lead changes show.
        int lo = 0x7FFF, hi = 0;
        for (uint8_t p = 0; p < n; p++)
            for (uint8_t k = 0; k < nHist; k++) {
                if (hist[p][k] < lo) lo = hist[p][k];
                if (hist[p][k] > hi) hi = hist[p][k];
            }
        int span = hi > lo ? hi - lo : 1, h = y1 - 1 - y0, shown = (nHist - 1) * grow >> 8;
        for (uint8_t i = n; i--;) {
            uint8_t p = order[i];
            for (int k = 0; k < shown; k++)
                gfx_line(20 + k * 88 / (nHist - 1), y1 - 1 - (hist[p][k] - lo) * h / span,
                         20 + (k + 1) * 88 / (nHist - 1), y1 - 1 - (hist[p][k + 1] - lo) * h / span, stage::SEAT_COLOUR[p]);
        }
    }
    if (frame & 32) centred35(106, "A AGAIN   B MENU", WHITE);
}

static void playRender(uint32_t frame) {
    if (overlay == MAP) {
        stage::hud(frame);
        mapview::draw(frame);
        centred35(119, "SELECT: BACK", SILVER);
        return;
    }
    uint32_t ui = overlay | (sel << 4) | (barSel << 8) | ((uint32_t)showBar << 12) | ((uint32_t)pressed << 16) |
                  ((uint32_t)managing << 24);
    if (overlay || showBar || managing) ui ^= (frame >> 3) << 25;       // blinking borders
    if (overlay == RESULT) ui = frame;
    if (!stage::render(frame, ui)) return;
    if (managing) drawManage();
    else if (showBar) drawBar(frame);
    if (overlay == PAUSE) {
        char buf[20], *p = fmtInt(fmtStr(buf, "ROUND "), st.round);
        if (st.roundCap) fmtInt(fmtStr(p, " OF "), st.roundCap);
        panel(30, 62);
        centred35(34, buf, st.round == st.roundCap ? RED : GOLD);
        for (uint8_t i = 0; i < 3; i++) {
            int y = 46 + i * 14;
            if (i == sel) fillRound(18, y - 3, 92, 15, 3, INK);
            centred2(y, PAUSE_ITEM[i], i == sel ? FX_B : WHITE);
        }
    } else if (overlay == CONFIRM) {
        panel(76, 30);
        centred35(80, "AUCTION IT OFF?", FX_B);
        centred35(88, "THE BANK BIDS HALF PRICE", WHITE);
        centred35(97, "A: YES   B: NO", GOLD);
    } else if (overlay == RESULT) {
        drawResult(frame);
    }
}

// ---------------------------------------------------------------------------
// Options (device debug builds leave the screen out to fit the protocol)
// ---------------------------------------------------------------------------
#if !CHBW_LEAN
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
    char buf[32];
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
    centred35(82, buf, GOLD);
    fmtMoney(fmtStr(buf, "RICHEST WINNER "), stats.best);
    centred35(90, buf, GOLD);
    centred35(108, "BOARDWALK " CHBW_VERSION " FOR CHGAME", SILVER);
    centred35(115, "FONT: PRESS PLAY ON TAPE", SILVER);
}
#endif

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   G <k0> <k1> <k2> <k3> <rounds> <seed>   start a game (kinds: 0 empty, 1 human, 2-4 CPU)
//   D <d1> <d2>                             the next roll
//   A <deck> <card>                         the card on top of a deck (0 Chance, 1 Chest)
//   $ <player> <cash>
//   E <tile> <owner> <level>                a deed (owner 7: the bank)
//   T <player> <tile> [jail]                a token
//   J <0|1|2>                               jump to title/setup/options
//   H                                       BOARD <phase> <at the turn> <waiting for you 0|1> <for a press 0|1>
//   V <tile> <zoom>                         (simulator) look at a tile
//   X <light> <dark> <solid>                (simulator) tile tones, solid pink and orange
//   F <tile>                                (simulator) a made-up mid-game, tokens round a tile
static bool debugHook(char cmd, const char *args) {
    char buf[48], *p;
    uint32_t a = dbg::parseNum(args, 10), b = dbg::parseNum(args, 10), c = dbg::parseNum(args, 10);
    switch (cmd) {
        case 'G': {
            Setup s = {{(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)dbg::parseNum(args, 10)}, 0, 0};
            s.roundCap = (uint8_t)dbg::parseNum(args, 10);
            s.seed = dbg::parseNum(args, 10);
            start(s);
            enter(Scr::Play);
            fadeIn = fadeOut = 0;
            pal::setFade(16);
            return true;
        }
        case 'D': forceDice((uint8_t)a, (uint8_t)b); return true;
        case 'A': st.deck[a & 1][st.top[a & 1]] = (uint8_t)b; return true;
        case '$': st.pl[a & 3].cash = (int32_t)b; stage::reset(); return true;
        case 'E': st.deed[a % board::TILES] = (uint8_t)(b | c << 3); stage::reset(); return true;
        case 'T': st.pl[a & 3].pos = (uint8_t)b; st.pl[a & 3].jail = (uint8_t)c; stage::reset(); return true;
        case 'H':
            p = fmtInt(fmtStr(buf, "BOARD "), phase());
            p = fmtInt(fmtStr(p, " "), st.cur);
            p = fmtStr(p, humanToAct() && !stage::busy() && !stage::waiting() && overlay == NONE ? " 1" : " 0");
            p = fmtStr(p, stage::waiting() ? " 1" : " 0");
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
#ifdef CHSIM
        case 'J': {
            static const Scr S[] = {Scr::Title, Scr::Setup, Scr::Options};
            if (a > 2) return false;
            enter(S[a]);
            return true;
        }
        case 'V':
            stage::setZoom((uint8_t)(b ? b : 5));
            stage::lookAt((uint8_t)a);
            return true;
        case 'X':
            iso::lightTile = (uint8_t)a;
            iso::darkTile = (uint8_t)b;
            iso::solidGroups = c != 0;
            return true;
        case 'F': {
            static const uint8_t OWN[][2] = {
                {1, 0}, {3, 0}, {6, 1}, {8, 1}, {9, 1}, {11, 2}, {13, 2}, {14, 2}, {16, 3}, {18, 3}, {19, 3},
                {21, 0}, {23, 0}, {24, 0}, {26, 1}, {27, 1}, {29, 1}, {31, 2}, {32, 2}, {34, 2}, {37, 3}, {39, 3},
                {5, 0}, {15, 1}, {25, 2}, {35, 3}, {12, 0}, {28, 2},
            };
            for (auto &o : OWN) st.deed[o[0]] = o[1];
            static const uint8_t LV[][2] = {{1, 2}, {3, 1}, {6, 4}, {8, 4}, {9, 5}, {11, 3}, {13, 3}, {14, 3},
                                            {16, 1}, {21, 5}, {23, 4}, {24, 4}, {26, 2}, {31, 5}, {32, 5}, {34, 5},
                                            {37, 3}, {39, 4}};
            for (auto &l : LV) st.deed[l[0]] |= (uint8_t)(l[1] << 3);
            st.pl[0].pos = st.pl[1].pos = (uint8_t)a;
            st.pl[2].pos = (uint8_t)((a + 1) % 40);
            st.pl[3].pos = (uint8_t)((a + 39) % 40);
            stage::reset();
            return true;
        }
#endif
    }
    return false;
}

// A scripted change to the game waits until the stage has caught up with
// it (a held banner or card is answered as a press would).
static bool settling(char cmd) {
    if (cmd == 'H' || cmd == 'G' || cmd == 'D' || cmd == 'J' || cmd == 'Q') return false;
    if (cur != Scr::Play) return false;
    if (stage::waiting()) stage::acknowledge();
    return stage::busy();
}
#endif

// ---------------------------------------------------------------------------
void begin() {
    stage::begin();
    opt.sound = 1;
    opt.seat[0] = HUMAN; opt.seat[1] = CPU + 1;
    opt.rounds = 2;
    save::load(opt, stats, hasGame);
    if (!opt.rounds || opt.rounds > 6) opt.rounds = 2;
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
#if !CHBW_LEAN
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
#if !CHBW_LEAN
        case Scr::Options: optionsRender(frame); break;
#endif
        default: break;
    }
}

}  // namespace screens
