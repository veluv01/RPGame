#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// Screens after CHBlackjack's (its Screens.cpp) by way of CHCraps:
// fades between them, the options list, statistics, the pause menu and the
// endings.
#include <RPGame.h>
#include <string.h>
#include "config.h"
#include "Screens.h"
#include "Yacht.h"
#include "Cam.h"
#include "Dice3D.h"
#include "Presenter.h"
#include "Fx.h"
#include "Bar.h"
#include "Chips.h"
#include "Layout.h"
#include "Wall.h"
#include "Sounds.h"
#include "Save.h"
#include "src/assets/Assets.h"

#if CHGAME_DEBUG && defined(CHSIM)
uint64_t sim_hostNanos();
#endif

namespace screens {

using present::F_CARD;
using present::F_DICE;
using present::F_ROLL;

enum class Scr : uint8_t { Title, Play, Options, Stats, Result, Lose };

static Yacht game;
static Scr cur = Scr::Title, pending = Scr::Title;
static uint8_t fadeOut = 0, fadeIn = 0;      // palette fade transitions
static uint16_t t = 0;                       // frames on this screen
static bool hasGame = false, paused = false, resumePlay = false;
static uint8_t menuSel = 0, pauseSel = 0, optSel = 0;
static Scr optBack = Scr::Title;
static uint8_t toastT = 0;
static const char *toastText = "";
static uint32_t staticSig = 0;               // last drawn state of a still screen

static void toast(const char *s) { toastText = s; toastT = 70; }

static void redrawAll() { present::invalidate(); staticSig = 0; }

static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

static void persist(bool keepGame) {
    gfx_wait();
    save::store(game, keepGame);
}

// Play-screen state.
static uint8_t focus = F_ROLL, dieSel = 0, catSel = 0, confirm = 0xFF, thrown = 0;
static bool armed = false, threw = false, wasBusy = false, wasCam = false, autoPlay = false;
// The house's turn.
static ai::Think think;
static uint8_t cpuT = 0, shakeT = 0;
static bool thought = false, picked = false;

static bool houseTurn() { return autoPlay || game.cpuTurn(); }

// The open box these dice score most in.
static uint8_t bestBox() {
    uint8_t best = 0;
    int bp = -1;
    for (uint8_t c = 0; c < CAT_COUNT; c++)
        if (game.legal(c) && game.points(c) > bp) { bp = game.points(c); best = c; }
    return best;
}

static void showCursor() { present::cursor(focus, focus == F_DICE ? dieSel : catSel); }

static void focusDefault() {
    confirm = 0xFF;
    if (houseTurn()) focus = present::F_NONE;
    else if (!game.rolled()) focus = F_ROLL;
    else if (game.rollsLeft) focus = F_DICE;
    else { focus = F_CARD; catSel = bestBox(); }
    showCursor();
}

static void enter(Scr s) {
    cur = s; t = 0; fadeIn = 8;
    staticSig = 0;
    fx::clear();
    pal::setDesaturate(0);
    pal::setMode(pal::CASINO);
    switch (s) {
        case Scr::Title: paused = false; break;
        case Scr::Play:
            if (!resumePlay) {
                present::reset(game);
                thought = picked = false; cpuT = 30;
                focusDefault();
            }
            redrawAll();
            resumePlay = false;
            paused = false;
            armed = false;
            break;
        case Scr::Result:
            if (!game.staked() || game.endPay > game.ante) { audio::sfx(Sfx::BigWin); audio::led(audio::LED_PARTY); }
            else audio::sfx(game.endPay ? Sfx::Point : Sfx::Lose);
            break;
        case Scr::Lose: audio::sfx(Sfx::Broke); break;
        default: break;
    }
}

static void titleDiceInit();

static void applyOptions() {
    pal::setTheme(game.opt.theme);
    audio::setOn(!game.opt.sound);
}

void begin() {
    if (!save::load(game, hasGame)) game.newPurse();
    game.mix(micros() * 2654435761u);
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, !game.opt.sound);
    applyOptions();
    titleDiceInit();
    enter(Scr::Title);
}

// ---------------------------------------------------------------------------
// Shared drawing
// ---------------------------------------------------------------------------
static void feltBackdrop() {
    gfx_clear(FELT);
    dither(0, 0, 128, 6, FELT_DK, 0);
    dither(0, 122, 128, 6, FELT_DK, 1);
    dither(0, 0, 6, 128, FELT_DK, 0);
    dither(122, 0, 6, 128, FELT_DK, 1);
    gfx_rect(2, 2, 124, 124, GOLD);
}

// Big lettering in PPOT's font, scale 3, with a gradient, outline and shadow:
// top colour for 3 rows, mid down to row lowFrom, low below.
static void title35(const char *text, int y, uint8_t top, uint8_t mid, uint8_t low, uint8_t shadow,
                    uint8_t lowFrom = 9) {
    Mask m = maskBegin(124, 18);
    maskText35(m, 0, 0, text, 3);
    uint8_t ramp[18];
    for (int i = 0; i < 18; i++) ramp[i] = i < 3 ? top : (i < lowFrom ? mid : low);
    maskDraw(m, 64 - text35WidthScaled(text, 3) / 2, y, mid, INK, shadow, ramp);
}

static void centred35(int y, const char *s, uint8_t c) { text35(64 - text35Width(s) / 2, y, s, c); }
static void centred57(int y, const char *s, uint8_t c) { gfx_text(64 - gfx_textWidth(s) / 2, y, s, c); }

// ---------------------------------------------------------------------------
// Title: the lettering, five dice tumbling in the spotlight, the menu.
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_CONTINUE, I_PLAY, I_OPTIONS, I_STATS };
static const char *const MODE_NAME[MODE_COUNT] = {"SOLO", "VS DEALER", "PARTY 2P", "PARTY 3P", "PARTY 4P"};

static uint8_t menu(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) items[n++] = I_CONTINUE;
    items[n++] = I_PLAY;
    items[n++] = I_OPTIONS;
    items[n++] = I_STATS;
    return n;
}

static d3::Die titleDice[d3::N];
static d3::Cam titleCam;
static const int SPOT_X = 8, SPOT_Y = 27, SPOT_W = 112, SPOT_H = 34;

static void titleDiceInit() {
    memset(titleDice, 0, sizeof titleDice);
    for (uint8_t i = 0; i < d3::N; i++) {
        d3::Die &d = titleDice[i];
        d.x = (i - 2) * 33 * 128; d.y = 15 * 128; d.z = 40 * 256;
        d.yaw = (uint16_t)(i * 13000 + 9000); d.pitch = (uint16_t)(i * 23000 + 3000); d.roll = (uint16_t)(i * 30000);
        d3::labelDie(d, 2, 6, i);                        // pips from the first frame
    }
    d3::defaultCam(titleCam);
    titleCam.focal = 141; titleCam.camY = 30 * 256; titleCam.sy0 = 11;      // level, close in
}

static void titleUpdate() {
    uint8_t items[4], n = menu(items);
    if (menuSel >= n) menuSel = 0;
    if (rpgame.repeat(UP_BUTTON) && menuSel > 0) { menuSel--; audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON) && menuSel + 1 < n) { menuSel++; audio::sfx(Sfx::Cursor); }
    if (items[menuSel] == I_PLAY) {
        int d = rpgame.justPressed(RIGHT_BUTTON) ? 1 : (rpgame.justPressed(LEFT_BUTTON) ? MODE_COUNT - 1 : 0);
        if (d) { game.opt.mode = (uint8_t)((game.opt.mode + d) % MODE_COUNT); audio::sfx(Sfx::Chip); }
    }
    if (rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
        game.mix(micros() ^ rpgame.frameCount);
        switch (items[menuSel]) {
            case I_PLAY: game.newGame(game.opt.mode); hasGame = true; go(Scr::Play); break;
            case I_CONTINUE: go(Scr::Play); break;
            case I_OPTIONS: optBack = Scr::Title; optSel = 0; go(Scr::Options); break;
            case I_STATS: go(Scr::Stats); break;
        }
    }
    // The dice tumble slowly, then every few seconds land flat on a yacht:
    // sixes, fives, fours...
    for (uint8_t i = 0; i < d3::N; i++) {
        d3::Die &d = titleDice[i];
        uint16_t k = t % 240;
        int8_t dir = (i & 1) ? 1 : -1;
        if (k == 0) d3::labelDie(d, 2, (uint8_t)(6 - (t / 240) % 6), (uint8_t)(t >> 3));
        if (k < 180) {
            d.y = (int32_t)(15 * 128 + fx::isin(k * 2 + i * 40) * 9 / 2);
            d.yaw = (uint16_t)(d.yaw + dir * 280);
            d.pitch = (uint16_t)(d.pitch + 350 + i * 30);
            d.roll = (uint16_t)(d.roll + 120 + i * 40);
        } else {
            d.y = 15 * 128;
            d.pitch = (uint16_t)(d.pitch - (int16_t)d.pitch / 3);
            d.roll = (uint16_t)(d.roll - (int16_t)d.roll / 3);
            d.yaw = (uint16_t)(d.yaw + (int16_t)(dir * 2000 - (int16_t)d.yaw) / 6);
            if (k == 190 + i * 3) audio::sfx(Sfx::Clack);
        }
    }
}

static void titleStatic(uint32_t frame) {
    feltBackdrop();
    // The lettering: a gradient, an outline and a shadow.
    uint8_t ramp[LOGO_H];
    for (int i = 0; i < LOGO_H; i++) ramp[i] = i < 3 ? FX_B : (i < 11 ? GOLD : WOOD);
    Mask m = maskBegin(LOGO_W, LOGO_H);
    maskBlit1(m, LOGO, (uint8_t)LOGO_W, (uint8_t)LOGO_H);
    maskDraw(m, 64 - LOGO_W / 2, 6, GOLD, INK, WINE, ramp);
    char buf[24];
    fmtMoney(fmtStr(buf, "PURSE "), game.purse);
    text35(7, 116, buf, FELT_LT);
    text35(121 - text35Width("CHGAME 2026"), 116, "CHGAME 2026", FELT_LT);
    uint8_t items[4], n = menu(items);
    int y0 = hasGame ? 66 : 70;
    for (uint8_t i = 0; i < n; i++) {
        static const char *const LABEL[4] = {"CONTINUE", "", "OPTIONS", "STATS"};
        bool sel = i == menuSel, play = items[i] == I_PLAY;
        const char *s = play ? MODE_NAME[game.opt.mode] : LABEL[items[i]];
        int y = y0 + i * 12, w = gfx_textWidth(s);
        if (sel) panel(64 - w / 2 - 6, y - 2, w + 12, 11, 3, NAVY, (frame & 16) ? FX_B : GOLD);
        centred57(y, s, sel ? GOLD : WHITE);
        if (play) {
            uint8_t c = sel ? FX_B : FELT_LT;
            gfx_text(64 - w / 2 - 16, y, "<", c);
            gfx_text(64 + w / 2 + 11, y, ">", c);
        }
    }
}

static void titleRender(uint32_t frame) {
    uint32_t sig = (uint32_t)menuSel << 1 | (uint32_t)hasGame << 4 | ((frame >> 4) & 1) << 5 |
                   (uint32_t)game.opt.mode << 6 | 1;
    if (sig != staticSig) { staticSig = sig; titleStatic(frame); }
    // The spotlight and its dice, every frame.
    gfx_fillRect(SPOT_X, SPOT_Y, SPOT_W, SPOT_H, FELT);
    dither(SPOT_X + 4, SPOT_Y + 2, SPOT_W - 8, SPOT_H - 4, FELT_LT, 0);
    gfx_setClip(SPOT_X, SPOT_Y, SPOT_W, SPOT_H);
    d3::Look look{RED, RED, WINE, WHITE, SILVER, INK};
    art::dieColours(0, look.light, look.dark, look.pip);
    look.mid = look.light;
    for (uint8_t i = 0; i < d3::N; i++) d3::shadow(titleDice[i], titleCam, FELT_DK);
    for (uint8_t i = 0; i < d3::N; i++) d3::draw(titleDice[i], titleCam, look);
    gfx_resetClip();
    if (t == 30) fx::burst(fx::STAR, 64, 44, 14, 60, FX_A);
    fx::update();
    fx::drawParticles();
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
static void startRoll() {
    cam::begin(game.rolled() ? game.held : 0, game.dice, (uint8_t)(4 - game.rollsLeft), game.cur, !houseTurn());
    armed = true; threw = false; shakeT = 0;
    present::setRollHeld(true);
}

static void throwNow() {
    armed = false; threw = true;
    present::setRollHeld(false);
    game.mix(micros() ^ (rpgame.frameCount << 16));
    thrown = (uint8_t)(~(game.rolled() ? game.held : 0) & 31);
    game.roll();
    cam::release(game.dice);
}

static void doScore(uint8_t cat) {
    uint8_t ev = game.score(cat);
    present::onScore(game, ev);
    confirm = 0xFF;
    focus = present::F_NONE;
    showCursor();
    thought = picked = false;
    if (ev & EV_OVER) { hasGame = false; persist(false); }
}

// The cursor on the card: two columns, six boxes on the left, seven on the right.
static void moveCard(int ux, int uy) {
    int col = catSel >= KIND3, row = col ? catSel - KIND3 : catSel;
    if (ux) col = ux > 0;
    row += uy;
    if (row < 0) row = 0;
    if (row > (col ? 6 : 5)) {
        if (uy > 0) { focus = F_DICE; return; }             // off the bottom: down to the dice
        row = 5;
    }
    catSel = (uint8_t)(col ? KIND3 + row : row);
}

static void humanInput() {
    int ux = 0, uy = 0;
    if (rpgame.repeat(UP_BUTTON)) uy = -1;
    if (rpgame.repeat(DOWN_BUTTON)) uy = 1;
    if (rpgame.repeat(LEFT_BUTTON)) ux = -1;
    if (rpgame.repeat(RIGHT_BUTTON)) ux = 1;
    bool a = rpgame.justPressed(A_BUTTON), b = rpgame.justPressed(B_BUTTON);
    uint8_t was = focus, wasSel = focus == F_DICE ? dieSel : catSel;
    if (!game.rolled()) focus = F_ROLL;
    if (rpgame.justPressed(SELECT_BUTTON) && game.players > 1) { present::viewNext(game); audio::sfx(Sfx::Cursor); }
    switch (focus) {
        case F_ROLL:
            if (a) {
                if (game.canRoll()) { startRoll(); return; }
                toast(game.rollsLeft ? "ALL DICE HELD" : "PICK A BOX");
                present::deny();
            }
            if ((uy < 0 || b) && game.rolled()) focus = F_DICE;
            break;
        case F_DICE:
            if (ux) dieSel = (uint8_t)(dieSel + ux < 0 ? 0 : (dieSel + ux > 4 ? 4 : dieSel + ux));
            if (a) {
                if (game.rollsLeft) {
                    game.held ^= (uint8_t)(1 << dieSel);
                    audio::sfx(game.held >> dieSel & 1 ? Sfx::Chip : Sfx::ChipTake);
                } else { toast("NO ROLLS LEFT"); present::deny(); }
            }
            if (uy > 0) {
                if (game.rollsLeft) focus = F_ROLL;
                else present::deny();
            }
            if (uy < 0 || b) { focus = F_CARD; catSel = bestBox(); }      // straight to the best box
            break;
        case F_CARD:
            if (ux || uy) { moveCard(ux, uy); confirm = 0xFF; }
            if (b) focus = F_DICE;
            if (a) {
                if (!game.legal(catSel)) {
                    toast(game.card[game.cur].open(catSel) ? "JOKER GOES ELSEWHERE" : "BOX IS FULL");
                    present::deny();
                } else if (!game.points(catSel) && confirm != catSel) {
                    confirm = catSel;
                    toast("A AGAIN: SCORE 0");
                    audio::sfx(Sfx::Cursor);
                } else { doScore(catSel); return; }
            }
            break;
    }
    if (focus != was || wasSel != (focus == F_DICE ? dieSel : catSel)) audio::sfx(Sfx::Cursor);
    showCursor();
}

// The house plays through the same moves a player makes, a beat apart.
static void houseInput(bool hurry) {
    if (cpuT) { cpuT = (uint8_t)(hurry ? 0 : cpuT - 1); return; }
    if (!game.rolled()) { startRoll(); return; }
    if (game.rollsLeft) {
        if (!thought) {
            if (!think.mask) ai::begin(think);
            thought = ai::step(game, think);
            return;
        }
        uint8_t diff = (uint8_t)(game.held ^ think.best);
        if (diff) {
            uint8_t i = 0;
            while (!(diff >> i & 1)) i++;
            game.held ^= (uint8_t)(1 << i);
            present::cursor(F_DICE, i);
            audio::sfx(Sfx::Chip);
            cpuT = 9;
            return;
        }
        if (think.best != 31) { thought = false; startRoll(); return; }
    }
    if (!picked) {
        catSel = ai::bestCat(game.card[game.cur], game.dice);
        present::cursor(F_CARD, catSel);
        audio::sfx(Sfx::Cursor);
        picked = true;
        cpuT = 26;
        return;
    }
    doScore(catSel);
}

static void camInput(bool house) {
    cam::Phase p = cam::phase();
    uint8_t ab = rpgame.justPressed(A_BUTTON | B_BUTTON);
    if (armed && (p == cam::WHIP_IN || p == cam::SHAKE)) {
        if (house) { if (p == cam::SHAKE && ++shakeT > 34) throwNow(); return; }
        if (rpgame.justPressed(B_BUTTON)) { armed = false; present::setRollHeld(false); cam::cancel(); return; }
        if (!rpgame.pressed(A_BUTTON)) throwNow();
        return;
    }
    if (p == cam::TUMBLE && (house ? ab : rpgame.justPressed(B_BUTTON))) cam::skip();
    uint16_t r = cam::restT();
    if (r == 1) present::onRest(game);
    if (r > (game.opt.speed ? 40u : 75u) || (r > 20 && ab)) cam::leave();
}

static void playUpdate() {
    uint8_t pressed = rpgame.justPressedMask();
    if (paused) {
        if (pressed & (UP_BUTTON | DOWN_BUTTON)) { pauseSel = (uint8_t)((pauseSel + ((pressed & UP_BUTTON) ? 3 : 1)) % 4); audio::sfx(Sfx::Cursor); }
        if (pressed & (START_BUTTON | B_BUTTON)) { paused = false; audio::sfx(Sfx::Select); }
        if (pressed & A_BUTTON) {
            audio::sfx(Sfx::Select);
            switch (pauseSel) {
                case 0: paused = false; break;
                case 1: optBack = Scr::Play; optSel = 0; resumePlay = true; go(Scr::Options); break;
                case 2: game.opt.sound = !game.opt.sound; applyOptions(); toast(game.opt.sound ? "SOUND OFF" : "SOUND ON"); break;
                default: hasGame = true; persist(true); go(Scr::Title); break;
            }
        }
        return;
    }
    bool house = houseTurn();
    bool showing = present::busy() || cam::active();
    if (!showing && (pressed & START_BUTTON)) { paused = true; pauseSel = 0; audio::sfx(Sfx::Select); return; }
    if (cam::active()) camInput(house);
    else if (present::busy()) { if (pressed & (A_BUTTON | B_BUTTON)) present::speedUp(); }
    else if (game.over) go(Scr::Result);
    else if (house) houseInput(pressed & (A_BUTTON | B_BUTTON));
    else humanInput();

    // Palette: the cursor pulses while choosing.
    pal::setMode(cam::active() || present::busy() ? pal::CASINO : pal::HOVER);
    present::update(game);
    // Back from the cam: the dice drop into the tray.
    bool camOn = cam::active();
    if (wasCam && !camOn) {
        if (threw) { present::onLanded(game, thrown); thought = picked = false; think.mask = 0; cpuT = 24; }
        threw = false;
        focusDefault();
    }
    wasCam = camOn;
    // A score has been shown: the next seat's turn.
    bool busy = present::busy();
    if (wasBusy && !busy && !game.over) { cpuT = 20; focusDefault(); }
    wasBusy = busy;
}

static void playRender(uint32_t frame) {
    if (paused || toastT) redrawAll();
    present::render(game, frame);
    if (paused) {
        dither(0, 0, 128, 128, INK, 0);
        panel(20, 30, 88, 62, 4, NAVY, GOLD);
        centred57(35, "PAUSED", GOLD);
        static const char *const P[4] = {"RESUME", "OPTIONS", "SOUND", "SAVE & QUIT"};
        for (int i = 0; i < 4; i++) {
            int y = 48 + i * 11;
            char buf[16];
            if (i == 2) fmtStr(fmtStr(buf, "SOUND "), game.opt.sound ? "OFF" : "ON");
            else fmtStr(buf, P[i]);
            if (i == pauseSel) fillRound(26, y - 2, 76, 11, 3, INK);
            centred57(y, buf, i == pauseSel ? FX_B : WHITE);
        }
    }
}

// The solo paytable, two lines of small print.
static void payLines(int y, uint8_t c) {
    char buf[40], *p = buf;
    for (uint8_t i = 0; i < Yacht::TIERS; i++) {
        p = fmtInt(p, Yacht::TIER_AT[i]);
        p = fmtStr(fmtInt(fmtStr(p, "+~"), Yacht::TIER_PAYS[i]), "X");
        if (i == 2) { centred35(y, buf, c); p = buf; }
        else if (i < Yacht::TIERS - 1) p = fmtStr(p, "~~~");
    }
    centred35(y + 7, buf, c);
}

#if !CHYD_LEAN
// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
enum Opt : uint8_t { O_ANTE, O_SPEED, O_FELT, O_SOUND, O_BACK, OPT_COUNT };

// Options is eight bytes in this order; each entry is "LABEL|value|value...".
static const char *const OPT_TEXT[OPT_COUNT] = {
    "ANTE|$5|$25|$100", "SPEED|NORMAL|FAST", "FELT|GREEN|BLUE|RED|PURPLE", "SOUND|ON|OFF", "BACK",
};
static_assert(sizeof(Options) == 8, "options menu indexes Options as bytes");

// Copy field k of an "a|b|c" string into buf; returns the number of fields.
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
    if (rpgame.repeat(UP_BUTTON)) { optSel = (uint8_t)((optSel + OPT_COUNT - 1) % OPT_COUNT); audio::sfx(Sfx::Cursor); }
    if (rpgame.repeat(DOWN_BUTTON)) { optSel = (uint8_t)((optSel + 1) % OPT_COUNT); audio::sfx(Sfx::Cursor); }
    int d = rpgame.justPressed(RIGHT_BUTTON) ? 1 : (rpgame.justPressed(LEFT_BUTTON) ? -1 : 0);
    if (rpgame.justPressed(A_BUTTON) && optSel != O_BACK) d = 1;
    if (d && optSel != O_BACK) {
        char tmp[12];
        uint8_t n = (uint8_t)(optField(OPT_TEXT[optSel], 0, tmp) - 1);
        uint8_t *f = (uint8_t *)&game.opt + optSel;
        *f = (uint8_t)((*f + n + d) % n);
        applyOptions();
        audio::sfx(Sfx::Chip);
    }
    if ((rpgame.justPressed(A_BUTTON) && optSel == O_BACK) || rpgame.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        persist(hasGame);
        go(optBack);
    }
}

static void optionsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("OPTIONS", 6, FX_B, GOLD, WOOD, WINE, 13);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 28 + i * 11;
        bool sel = i == optSel;
        char buf[12];
        if (sel) fillRound(6, y - 2, 116, 10, 3, NAVY);
        optField(OPT_TEXT[i], 0, buf);
        if (i == O_BACK) { centred35(y, buf, sel ? WHITE : FELT_LT); continue; }
        text35(10, y, buf, sel ? WHITE : FELT_LT);
        optField(OPT_TEXT[i], (uint8_t)(1 + ((uint8_t *)&game.opt)[i]), buf);
        int w = text35Width(buf);
        text35(118 - w, y, buf, sel ? FX_B : GOLD);
        if (sel) { text35(112 - w, y, "<", SILVER); text35(120, y, ">", SILVER); }
    }
    centred35(85, optSel == O_ANTE ? "THE NEXT GAME'S STAKE" : "A SOLO SCORE PAYS THE ANTE:", SILVER);
    payLines(93, WHITE);
    centred35(111, "FONT AND DEALER BY", FELT_LT);
    centred35(118, "PRESS PLAY ON TAPE", FELT_LT);
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
static const uint8_t STAT_RESET_FRAMES = 90;
static uint8_t statHold = 0;

static void statsUpdate() {
    if (rpgame.pressed(SELECT_BUTTON)) {
        if (statHold < 255) statHold++;
        if (statHold == STAT_RESET_FRAMES) {
            memset(&game.stats, 0, sizeof game.stats);
            persist(hasGame);
            audio::sfx(Sfx::SevenOut);
        }
        return;
    }
    statHold = 0;
    if (rpgame.justPressed(A_BUTTON | B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

static void statsRender(uint32_t frame) {
    (void)frame;
    feltBackdrop();
    title35("STATISTICS", 6, WHITE, CYAN, BLUE, NAVY);
    const Stats &s = game.stats;
    static const char *const NAME[9] = {"GAMES PLAYED", "BEST SCORE", "YACHTS", "UPPER BONUSES", "BEAT THE DEALER",
                                        "LOST TO DEALER", "BEST PURSE", "BIGGEST WIN", "TIMES BROKE"};
    const int32_t val[9] = {s.games, s.best, s.yachts, s.bonuses, s.cpuWins, s.cpuLosses,
                            s.bestPurse, s.biggestWin, s.timesBroke};
    for (int i = 0; i < 9; i++) {
        char buf[16];
        int y = 26 + i * 9;
        text35(10, y, NAME[i], FELT_LT);
        if (i == 6 || i == 7) fmtMoney(buf, val[i]); else fmtInt(buf, val[i]);
        text35(118 - text35Width(buf), y, buf, i >= 6 ? GOLD : WHITE);
    }
    if (statHold >= STAT_RESET_FRAMES) centred35(110, "STATS RESET", GOLD);
    else if (statHold) {
        gfx_rect(24, 110, 80, 5, SILVER);
        gfx_fillRect(25, 111, 78 * statHold / STAT_RESET_FRAMES, 3, RED);
    } else centred35(110, "HOLD SELECT TO RESET", FELT_LT);
    centred35(118, save::available() ? "SAVED IN FLASH" : "SAVING UNAVAILABLE", SILVER);
}

#else
// Device debug builds leave the options and stats pages out to fit the
// serial protocol (CHYD_LEAN, config.h): the tests drive the game directly.
static void optionsUpdate() { go(Scr::Title); }
static void optionsRender(uint32_t) { gfx_clear(FELT); }
static void statsUpdate() { go(Scr::Title); }
static void statsRender(uint32_t) { gfx_clear(FELT); }
static uint8_t statHold = 0;
#endif

// ---------------------------------------------------------------------------
// The game's end, and going broke (CHBlackjack's, after PPOT's GameWinState /
// GameLoseState)
// ---------------------------------------------------------------------------
static void endUpdate() {
    if (t > 60 && rpgame.justPressed(A_BUTTON | START_BUTTON)) {
        audio::sfx(Sfx::Select);
        if (cur == Scr::Result && game.staked() && game.broke()) { go(Scr::Lose); return; }
        if (cur == Scr::Lose) { game.newPurse(); persist(false); }
        go(Scr::Title);
    }
}

static void ray(int x1, int y1, uint8_t c) {
    int x = 64, y = 60;
    int dx = x1 > x ? x1 - x : x - x1, dy = y1 > y ? y1 - y : y - y1;
    int sx = x < x1 ? 1 : -1, sy = y < y1 ? 1 : -1, err = dx - dy;
    while ((unsigned)x < GFX_W && (unsigned)y < GFX_H) {
        gfx_pixel(x, y, c);
        if (x == x1 && y == y1) break;
        int e2 = err << 1;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
    }
}

// Final scores; a win (a paid solo score, the dealer beaten, any party
// game) gets the rays and the confetti.
static void resultRender(uint32_t frame) {
    bool staked = game.staked();
    uint8_t w = game.winner();
    bool win = staked ? game.endPay > game.ante : true;
    gfx_clear(NAVY);
    if (win)
        for (int i = 0; i < 16; i++) {
            int a = i * 16 + (int)(frame & 255);
            int x1 = 64 + ((fx::isin(a + 64) * 120) >> 8), y1 = 60 + ((fx::isin(a) * 120) >> 8);
            ray(x1, y1, (i & 1) ? FX_A : WINE);
        }
    else for (int x = 3; x < 128; x += 8) gfx_vline(x, 0, 128, INK);
    static const uint8_t R[5] = {RED, GOLD, FELT_LT, CYAN, BLUE};
    uint8_t c = R[(frame / 4) % 5], d = R[(frame / 4 + 2) % 5];
    char buf[20];
    const char *head = "GAME OVER";
    if (game.mode == M_CPU) head = w == 0 ? "YOU WIN!" : (w == 1 ? "DEALER WINS" : "A TIE");
    else if (game.mode != M_SOLO) { fmtStr(fmtStr(buf, present::seatName(game, w)), " WINS!"); head = buf; }
    else if (win) head = "WINNER!";
    if (win) title35(head, 8, WHITE, c, d, WINE, 10);
    else title35(head, 8, WHITE, SILVER, BLUE, INK, 10);
    // The seats and their totals.
    int y = 34;
    for (uint8_t p = 0; p < game.players; p++, y += 11) {
        bool top = p == w || game.players == 1;
        if (top) fillRound(20, y - 2, 88, 11, 3, INK);
        char name[12];
        if (game.mode == M_SOLO) fmtStr(name, "SCORE");
        else if (game.mode == M_CPU) fmtStr(name, present::seatName(game, p));
        else fmtStr(fmtStr(name, "PLAYER "), present::seatName(game, p) + 1);
        gfx_text(24, y, name, top ? GOLD : WHITE);
        char num[8];
        fmtInt(num, game.card[p].total());
        gfx_text(104 - gfx_textWidth(num), y, num, top ? FX_B : WHITE);
    }
    y += 4;
    if (staked) {
        if (game.endPay) {
            fmtMoney(fmtStr(buf, game.endPay > game.ante ? "PAYS " : "PUSH "), game.endPay);
            title35(buf, y, FX_B, GOLD, WOOD, WINE, 13);
        } else centred57(y + 4, "ANTE LOST", RED);
        if (game.mode == M_SOLO) {
            if (game.card[0].total() >= game.stats.best && (frame & 16)) centred35(y + 22, "NEW BEST SCORE!", FX_B);
            payLines(102, SILVER);
        }
    }
    if (win) {
        if ((frame % 6) == 0 && staked) fx::fountain(fx::COIN, fx::rndRange(20, 108), 120, 2);
        if ((frame % 24) == 0) fx::burst(fx::STAR, fx::rndRange(16, 112), fx::rndRange(10, 50), 16, 60, FX_A);
        if ((frame % 30) == 0) fx::fountain(fx::CONFETTI, fx::rndRange(20, 108), 100, 10);
    }
    fx::update();
    fx::drawParticles();
    if (t > 60 && (frame & 16)) centred35(120, "PRESS A", WHITE);
}

static void loseRender(uint32_t frame) {
    gfx_clear(INK);
    pal::setDesaturate((uint8_t)(t / 12 > 12 ? 12 : t / 12));
    for (int x = 3; x < 128; x += 8) gfx_vline(x, 0, 128, NAVY);
    int drop = t < 40 ? (40 - t) : 0;
    title35("YOU ARE", 14 - drop, WHITE, RED, WINE, NAVY, 10);
    if (t > 20) title35("BROKE", 40 - (t < 60 ? 60 - t : 0), WHITE, RED, WINE, NAVY, 10);
    for (int i = 0; i < 2; i++) fx::spawn(fx::RAIN, fx::rndRange(0, 128), -4, -6, 64, 40, CYAN);
    fx::update();
    fx::drawParticles();
    wall::dealer(wall::E_SMILE, 1, 40, 70);
    gfx_fillRect(0, 112, 128, 16, INK);
    if (t > 60 && (frame & 16)) centred35(116, "PRESS A", WHITE);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------
void update() {
    t++;
    if (toastT && !--toastT) redrawAll();
    static bool wasPaused = false;
    if (paused != wasPaused) { wasPaused = paused; redrawAll(); }
    audio::update();
    if (fadeOut) {
        pal::setFade((uint8_t)(fadeOut * 2));
        if (--fadeOut == 0) {
            if (pending == Scr::Title) titleDiceInit();
            enter(pending);
        }
        return;
    }
    if (fadeIn) { fadeIn--; pal::setFade((uint8_t)(16 - fadeIn * 2)); }
    switch (cur) {
        case Scr::Title: titleUpdate(); break;
        case Scr::Play: playUpdate(); break;
        case Scr::Options: optionsUpdate(); break;
        case Scr::Stats: statsUpdate(); break;
        case Scr::Result: case Scr::Lose: endUpdate(); break;
    }
}

static bool unchanged(uint32_t sig) {
    if (sig == staticSig && !fx::particlesAlive() && !toastT) return true;
    staticSig = sig;
    return false;
}

void render(uint32_t frame) {
    if (cur == Scr::Options || cur == Scr::Stats) {
        uint32_t sig = (uint32_t)cur * 2654435761u ^ ((uint32_t)optSel << 12) ^ ((uint32_t)statHold << 20);
        for (uint8_t i = 0; i < 8; i++) sig = sig * 31u + ((uint8_t *)&game.opt)[i];
        if (unchanged(sig | 1)) return;
    }
    switch (cur) {
        case Scr::Title: titleRender(frame); break;
        case Scr::Play: playRender(frame); break;
        case Scr::Options: optionsRender(frame); break;
        case Scr::Stats: statsRender(frame); break;
        case Scr::Result: resultRender(frame); break;
        case Scr::Lose: loseRender(frame); break;
    }
    if (toastT) {
        int w = gfx_textWidth(toastText) + 8;
        panel(64 - w / 2, 2, w, 11, 3, INK, GOLD);
        centred57(4, toastText, WHITE);
    }
}

// ---------------------------------------------------------------------------
// Debug protocol (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHGAME_DEBUG
//   R <seed>             the dice from a fixed seed (timing not mixed in)
//   F <a> <b> <c> <d> <e>   force the next roll's five dice (up to 4 queued)
//   J <T|P|C|2|3|4|O|S|E|L>   jump: title, play solo, vs dealer, party of 2..4,
//                        options, stats, the result screen, broke
//   M <purse>            set the purse
//   V                    reboot the game (reload from the save, back to the title)
//   A <0|1>              the house plays every seat (a whole game for a script)
//   C <focus> <index>    put the cursor: 1 a die, 2 ROLL, 3 a box
//   G <box>              score the dice in a box now
//   H                    state: STATE purse cur round rolls busy cam focus sel | dice held | totals over
//   Q                    (simulator) host-time calibration for perf estimates
bool debugCommand(char cmd, const char *args) {
    char buf[120], *p;
    switch (cmd) {
        case 'R': game.seed(dbg::parseNum(args, 10)); return true;
        case 'F': {
            uint8_t v[5];
            for (int i = 0; i < 5; i++) v[i] = (uint8_t)dbg::parseNum(args, 10);
            game.force(v);
            return true;
        }
        case 'J': {
            rpgame.frameCount = 0;
            pal::resetClock();
            fx::reseed();
            fadeOut = fadeIn = 0;
            pal::setFade(16);
            uint8_t m = M_SOLO;
            switch (args[0]) {
                case 'T': titleDiceInit(); enter(Scr::Title); break;
                case '4': m++; // fallthrough
                case '3': m++; // fallthrough
                case '2': m++; // fallthrough
                case 'C': m++; // fallthrough
                case 'P': game.newGame(m); hasGame = true; resumePlay = false; enter(Scr::Play); break;
                case 'O': optBack = Scr::Title; enter(Scr::Options); break;
                case 'S': enter(Scr::Stats); break;
                case 'E': enter(Scr::Result); break;
                case 'L': enter(Scr::Lose); break;
                default: return false;
            }
            return true;
        }
        case 'M': game.purse = (int32_t)dbg::parseNum(args, 10); return true;
        case 'V': memset(&game, 0, sizeof game); autoPlay = false; begin(); return true;     // a reboot: everything from flash
        case 'A': autoPlay = dbg::parseNum(args, 10) != 0; thought = picked = false; think.mask = 0; focusDefault(); return true;
        case 'C': {
            uint8_t f = (uint8_t)dbg::parseNum(args, 10), i = (uint8_t)dbg::parseNum(args, 10);
            if (f < F_DICE || f > F_CARD || i >= (f == F_DICE ? 5 : CAT_COUNT)) return false;
            focus = f;
            if (f == F_DICE) dieSel = i; else catSel = i;
            showCursor();
            return true;
        }
        case 'G': {
            uint8_t c = (uint8_t)dbg::parseNum(args, 10);
            if (c >= CAT_COUNT || !game.legal(c)) return false;
            doScore(c);
            return true;
        }
        case 'H': {
            p = fmtStr(buf, "STATE ");
            p = fmtInt(p, game.purse); *p++ = ' ';
            p = fmtInt(p, game.cur); *p++ = ' ';
            p = fmtInt(p, game.round); *p++ = ' ';
            p = fmtInt(p, game.rollsLeft); *p++ = ' ';
            p = fmtInt(p, present::busy() || (cur == Scr::Play && houseTurn() && !game.over) || fadeOut); *p++ = ' ';
            p = fmtInt(p, cam::phase()); *p++ = ' ';
            p = fmtInt(p, focus); *p++ = ' ';
            p = fmtInt(p, focus == F_DICE ? dieSel : catSel);
            p = fmtStr(p, " |");
            for (uint8_t i = 0; i < 5; i++) { *p++ = ' '; p = fmtInt(p, game.dice[i]); }
            *p++ = ' '; p = fmtInt(p, game.held);
            p = fmtStr(p, " |");
            for (uint8_t i = 0; i < game.players; i++) { *p++ = ' '; p = fmtInt(p, game.card[i].total()); }
            *p++ = ' '; p = fmtInt(p, game.over);
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
    }
    return false;
}
#else
bool debugCommand(char, const char *) { return false; }
#endif

}  // namespace screens
