// The play screen (Stage.h): the camera, the show a play puts on, and
// drawing the board, the tiles, the rack and the HUD.
#pragma GCC optimize("Os", "no-ipa-sra", "no-caller-saves")
#include <Arduino.h>
#include <string.h>
#include <RPGame.h>
#include "config.h"
#include "Stage.h"
#include "Font.h"
#include "Tiles.h"
#include "Fx.h"
#include "Sounds.h"
#include "Ai.h"
#include "src/assets/Assets.h"

namespace stage {

using wd::SIZE;

game::Play tent;
uint8_t tentSlot[wd::RACK];
int16_t tentScore;
uint8_t cursor, rackSel, pickSel, swapMarks, mode, viewSide;
bool down, peek;

// --- Layout -------------------------------------------------------------------
static const int HUD_H = 8;             // the scores: rows 0..7
static const int RACK_H = 24;           // the rack, when up: rows 104..127
static const int FRAME = 4;             // the board's wooden frame
static const uint8_t FAR = 8, NEAR = 16;    // square pitch: the whole board, close up
static const int TILE_PITCH = 16;       // in the rack

// The camera: squares zs pixels apart (FAR, NEAR, or the sizes between
// while it whips from one to the other), the board's corner drawn at
// (-camX, -camY).
static uint8_t zs = FAR;
static int camX, camY;
static uint8_t rackT;                   // the rack: 0 away .. 8 up
static bool title;

// --- The show -------------------------------------------------------------------
enum Anim : uint8_t { A_NONE, A_DROP, A_SWEEP, A_FLOAT, A_NOTE };
static uint8_t anim, animT;
static uint8_t dropped;                 // of the CPU's tiles, how many have landed
static int16_t shown[2];                // the scores on the HUD, counting up
static uint8_t slamCell = 0xFF, slamT;  // your last tile, on its way down

// Floating text: the score off the word (in the display face), and what
// the premium squares did (small).
struct Float { int16_t x, y; uint8_t t, colour; char text[8]; };
static Float floats[4];

static char noteText[32];
static uint8_t noteCol, noteT;
static const char *cpuThinking;
static uint8_t denyT;
static wd::Span denySpan;
static bool denyWord;

static bool dirty = true;

// Notes for a word lighting up: up a major pentatonic scale.
static const uint16_t SCALE[8] = {1319, 1568, 1760, 2093, 2349, 2637, 3136, 3520};

// ---------------------------------------------------------------------------
static int rackTop() { return 128 - (RACK_H * rackT) / 8; }
static int cellX(uint8_t c) { return (c % SIZE) * zs - camX; }
static int cellY(uint8_t c) { return (c / SIZE) * zs - camY; }
static uint8_t middle(const wd::Span &s) { return (uint8_t)(s.start + (s.len / 2) * s.step); }

static bool wantClose() {
    return title || (!game::over && !peek && !cpuThinking);
}

// Where the camera should be: the square it follows in the middle of the
// window; the board's frame kept to the window's edges, or the board
// centred if it fits.
static void camTarget(int &tx, int &ty) {
    uint8_t c = anim != A_NONE && game::last.kind == game::PLAYED ? middle(game::last.main) : cursor;
    // While a word is being laid out, its first tile stays in view too.
    uint8_t a = tent.n && anim == A_NONE ? tent.p[0].cell : c;
    int size = SIZE * zs, w = 128, h = rackTop() - HUD_H;
    tx = (a % SIZE + c % SIZE) * zs / 2 + zs / 2 - w / 2;
    ty = (a / SIZE + c / SIZE) * zs / 2 + zs / 2 - h / 2 - HUD_H;
    if (tx > size + FRAME - w) tx = size + FRAME - w;
    if (ty > size + FRAME - h - HUD_H) ty = size + FRAME - h - HUD_H;
    if (tx < -FRAME) tx = -FRAME;
    if (ty < -FRAME - HUD_H) ty = -FRAME - HUD_H;
    if (size <= w) tx = -(w - size) / 2;
    if (size <= h) ty = -(h - size) / 2 - HUD_H;
}

static bool settled() {
    int tx, ty;
    camTarget(tx, ty);
    return zs == (wantClose() ? NEAR : FAR) && tx == camX && ty == camY;
}

bool slotFree(uint8_t slot) {
    if (!game::rack[viewSide][slot]) return false;
    for (uint8_t i = 0; i < tent.n; i++) if (tentSlot[i] == slot) return false;
    return true;
}

void retally() {
    wd::Result r;
    tentScore = tent.n && wd::check(game::board, tent.p, tent.n, r) == wd::OK ? r.score : -1;
    dirty = true;
}

void begin() {}

void newGame() {
    tent.n = 0;
    tentScore = -1;
    cursor = wd::CENTRE;
    rackSel = 0;
    swapMarks = 0;
    mode = BOARD;
    down = peek = title = false;
    viewSide = game::setup.mode == game::VS_CPU ? 0 : game::turn;
    anim = A_NONE;
    noteT = denyT = slamT = 0;
    memset(floats, 0, sizeof floats);
    cpuThinking = nullptr;
    shown[0] = game::score[0];
    shown[1] = game::score[1];
    // The whole board to start with: the camera whips in.
    zs = FAR;
    rackT = 0;
    camTarget(camX, camY);
    dirty = true;
}

void note(const char *text, uint8_t colour, uint8_t frames) {
    fmtStr(noteText, text);          // (the longest note is 28 characters)
    noteCol = colour;
    noteT = frames;
    dirty = true;
}

void thinking(const char *text) {
    cpuThinking = text;
    dirty = true;
}

void deny(const wd::Span *word) {
    denyT = 36;
    denyWord = word != nullptr;
    if (word) denySpan = *word;
    fx::shake(8, 2);
    audio::sfx(Sfx::Deny);
}

void placed(uint8_t cell) {
    slamCell = cell;
    slamT = 6;
    audio::sfx(Sfx::Lift);
}

// ---------------------------------------------------------------------------
// The show
// ---------------------------------------------------------------------------
static void sparkle(uint8_t cell, fx::Kind k, uint8_t n, uint8_t colour) {
    fx::burst(k, cellX(cell) + zs / 2, cellY(cell) + zs / 2, n, 22, colour);
}

static void addFloat(int x, int y, const char *text, uint8_t colour) {
    for (auto &f : floats) {
        if (f.t) continue;
        f.x = (int16_t)x;
        f.y = (int16_t)y;
        f.t = 60;
        f.colour = colour;
        fmtStr(f.text, text);         // "+123", "3X WORD": 7 characters at most
        return;
    }
}

// A tile lands: dust, a jolt, a knock.
static void landed(uint8_t cell) {
    sparkle(cell, fx::DUST, 6, FELT_LT);
    fx::shake(3, 1);
    audio::sfx(Sfx::Land);
}

void show(bool byCpu) {
    const game::Last &l = game::last;
    tent.n = 0;
    tentScore = -1;
    mode = BOARD;
    animT = 0;
    dirty = true;
    if (l.kind == game::PLAYED) {
        dropped = byCpu ? 0 : l.n;
        anim = byCpu ? A_DROP : A_SWEEP;
        return;
    }
    char *p = fmtStr(noteText, game::setup.mode == game::VS_CPU ? (l.side ? "CPU " : "YOU ") : (l.side ? "PLAYER 2 " : "PLAYER 1 "));
    if (l.kind == game::SWAPPED) {
        p = fmtInt(fmtStr(p, game::setup.mode == game::VS_CPU && !l.side ? "SWAP " : "SWAPS "), l.n);
        fmtStr(p, l.n == 1 ? " TILE" : " TILES");
        audio::sfx(Sfx::Whoosh);
    } else {
        fmtStr(p, game::setup.mode == game::VS_CPU && !l.side ? "PASS" : "PASSES");
        audio::sfx(Sfx::NoMove);
    }
    noteCol = SILVER;
    noteT = 80;
    anim = A_NOTE;
}

// The word has lit up: it pays, and the premium squares under the new tiles
// say what they did.
static void payout() {
    const game::Last &l = game::last;
    uint8_t mid = middle(l.main), best = 0;
    char buf[8];
    buf[0] = '+';
    fmtInt(buf + 1, l.score);
    addFloat(cellX(mid) + zs / 2, cellY(mid) - 2, buf, FX_B);
    for (uint8_t i = 0; i < l.n; i++) {
        uint8_t pr = wd::premium(l.cell[i]);
        if (!pr) continue;
        static const char *const SAYS[5] = {"", "2X", "3X", "2X WORD", "3X WORD"};
        static const uint8_t SAY_COL[5] = {0, CYAN, CYAN, SKIN, RED};
        addFloat(cellX(l.cell[i]) + zs / 2, cellY(l.cell[i]) + zs, SAYS[pr], SAY_COL[pr]);
        if (pr > best) best = pr;
    }
    if (l.n == wd::RACK) {
        fx::banner("BINGO!", fx::B_RAINBOW, 82, 90);
        fx::fountain(40, 100, 18);
        fx::fountain(88, 100, 18);
        audio::sfx(Sfx::Doubles);
        audio::led(audio::LED_PARTY);
    } else if (best == wd::TW) {
        fx::banner("TRIPLE!", fx::B_RED, 82, 70);
        fx::shake(10, 2);
        for (uint8_t i = 0; i < l.n; i++) sparkle(l.cell[i], fx::STAR, 4, GOLD);
        audio::sfx(Sfx::Pickup);
        audio::led(audio::LED_TRIPLE);
    } else if (l.score >= 30) {
        for (uint8_t i = 0; i < l.n; i++) sparkle(l.cell[i], fx::STAR, 4, GOLD);
        audio::sfx(Sfx::Pickup);
        audio::led(audio::LED_BLINK);
    } else {
        sparkle(mid, fx::SPARK, 8, GOLD);
        audio::sfx(Sfx::Coin);
    }
}

bool busy() { return anim != A_NONE || denyT; }

void update() {
    // The camera: it whips between the whole board and the close-up in four
    // ticks, and pans after what it follows. The rack slides in and out.
    uint8_t want = wantClose() ? NEAR : FAR;
    bool rackWanted = want == NEAR && !title;
    if (rackWanted && rackT < 8) { rackT++; dirty = true; }
    if (!rackWanted && rackT) { rackT--; dirty = true; }
    int tx, ty;
    if (zs != want) {
        if (zs == (want == NEAR ? FAR : NEAR)) audio::sfx(Sfx::Whoosh);
        zs = (uint8_t)(zs < want ? zs + 2 : zs - 2);
        camTarget(camX, camY);
        dirty = true;
    } else {
        camTarget(tx, ty);
        int dx = tx - camX, dy = ty - camY;
        if (dx || dy) {
            camX += dx / 3 + (dx > 0) - (dx < 0);
            camY += dy / 3 + (dy > 0) - (dy < 0);
            dirty = true;
        }
    }
    if (noteT && !--noteT) dirty = true;
    if (denyT && !--denyT) dirty = true;
    if (slamT && !--slamT && slamCell != 0xFF) landed(slamCell);
    for (auto &f : floats) if (f.t) { f.t--; if (f.t & 1) f.y--; dirty = true; }
    for (uint8_t s = 0; s < 2; s++) {
        int16_t d = (int16_t)(game::score[s] - shown[s]);
        if (!d || anim == A_DROP || anim == A_SWEEP) continue;
        int16_t stepBy = (int16_t)(d > 24 || d < -24 ? 3 : 1);
        shown[s] = (int16_t)(shown[s] + (d > 0 ? stepBy : -stepBy));
        if (!(shown[s] & 3)) audio::sfx(Sfx::Tick);
        dirty = true;
    }
    const game::Last &l = game::last;
    switch (anim) {
        case A_DROP:
            // The CPU's tiles come down one after another, once the camera
            // is there to watch.
            if (!settled()) break;
            dirty = true;
            if (++animT >= 8) {
                animT = 0;
                landed(l.cell[dropped]);
                if (++dropped >= l.n) anim = A_SWEEP;
            }
            break;
        case A_SWEEP:
            // The word lights up a tile at a time, each a note up the scale.
            dirty = true;
            if (animT % 4 == 0 && animT / 4 < l.main.len) {
                uint8_t k = (uint8_t)(animT / 4);
                audio::note(SCALE[k > 7 ? 7 : k], 50, 2);
                sparkle((uint8_t)(l.main.start + k * l.main.step), fx::SPARK, 3, FX_B);
            }
            if (++animT >= l.main.len * 4 + 6) {
                payout();
                anim = A_FLOAT;
            }
            break;
        case A_FLOAT: {
            bool floating = false;
            for (auto &f : floats) floating |= f.t != 0;
            if (!floating && !fx::bannerActive() && shown[0] == game::score[0] && shown[1] == game::score[1]) anim = A_NONE;
            break;
        }
        case A_NOTE:
            if (!noteT) anim = A_NONE;
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// The premium squares are set into the board, dark against the felt; the
// tiles stand up off it, light. Letter premiums blue, word premiums red,
// each with what it does on it.
static const uint8_t PREMIUM_COL[5] = {FELT, BLUE, NAVY, WINE, RED};
static const uint8_t PREMIUM_INK[5] = {0, CYAN, CYAN, SKIN, WHITE};
static const char *const PREMIUM_TEXT[5] = {"", "DL", "TL", "DW", "TW"};

// M and W five columns wide in the small letters: in three they are an H
// with its bar a row out of place.
static const uint8_t WIDE_M[5] = {0x1F, 0x02, 0x04, 0x02, 0x1F};
static const uint8_t WIDE_W[5] = {0x1F, 0x08, 0x04, 0x08, 0x1F};
static const uint8_t STAR[5] = {0x04, 0x1E, 0x0F, 0x1E, 0x04};

// A column glyph at twice the size.
static void glyph2(int x, int y, const uint8_t *cols, uint8_t n, uint8_t c) {
    for (uint8_t i = 0; i < n; i++)
        for (uint8_t r = 0; r < 8; r++)
            if (cols[i] >> r & 1) gfx_fillRect(x + 2 * i, y + 2 * r, 2, 2, c);
}

static uint8_t shadeOf(uint8_t face) {
    switch (face) {
        case WHITE: return SILVER;
        case SILVER: return BLUE;
        case FX_B:  return GOLD;
        case RED:   return WINE;
        case CYAN:  return BLUE;
        default:    return WOOD;
    }
}

// A tile on the board, its square's corner at (x, y), squares p apart.
// lift: on its way down (it casts its shadow where it will land, and looms
// larger the higher it is).
static void boardTile(int x, int y, int p, uint8_t v, uint8_t face, uint8_t ink, int lift) {
    int t = p >= 14 ? 2 : 1, f = p - 1 - t;
    uint8_t r = p >= 12 ? 1 : 0;
    if (lift) {
        fillRound(x + t, y + t, f, f, r, FELT_DK);
        int g = lift / 4;
        x -= g;
        y -= lift + g;
        f += 2 * g;
    }
    fillRound(x + t, y + t, f, f, r, face == SILVER ? NAVY : WOOD);     // its thickness
    fillRound(x, y, f, f, r, face);
    uint8_t l = v & wd::LETTER;
    if (p >= 12) {
        uint8_t sh = shadeOf(face);
        gfx_hline(x + 1, y + f - 1, f - 2, sh);
        gfx_vline(x + f - 1, y + 1, f - 2, sh);
    }
    if (p >= 14) {
        tileLetter(x, y + (f - 9) / 2, f, l, ink, face == SILVER ? NAVY : ink == WHITE ? SKIN : shadeOf(face), shadeOf(face));
    }
    else if (l == 13 || l == 23) glyph(x + (f - 5) / 2, y + (f - 5) / 2, l == 13 ? WIDE_M : WIDE_W, 5, ink);
    else glyph(x + (f - 3) / 2, y + (f - 5) / 2, glyph35((char)('A' + l - 1)), 3, ink);
}

// A square with no tile: the felt, or a premium square set into it.
static void square(int x, int y, uint8_t cell) {
    uint8_t pr = wd::premium(cell), s = (uint8_t)(zs - 1);
    if (!pr) {
        // A plain square: a dimple, so the board reads as squares without
        // ruling it into a carpet.
        if (zs >= 12) {
            gfx_hline(x + 1, y, s - 2, FELT_DK);
            gfx_vline(x, y + 1, s - 2, FELT_DK);
        } else gfx_pixel(x + s / 2, y + s / 2, FELT_DK);
        return;
    }
    fillRound(x, y, s, s, zs >= 12 ? 2 : 0, PREMIUM_COL[pr]);
    if (cell == wd::CENTRE) {
        if (zs >= 12) glyph2(x + (s - 10) / 2, y + (s - 10) / 2, STAR, 5, GOLD);
        else glyph(x + 1, y + 1, STAR, 5, GOLD);
    } else if (zs >= 12) {
        text35(x + (s - 7) / 2, y + (s - 5) / 2, PREMIUM_TEXT[pr], PREMIUM_INK[pr]);
    } else {
        // Far off: just how many times over.
        glyph(x + (s - 3) / 2, y + (s - 5) / 2, glyph35(pr & 1 ? '2' : '3'), 3, PREMIUM_INK[pr]);
    }
}

static bool inSpan(const wd::Span &s, uint8_t cell, uint8_t upTo) {
    for (uint8_t i = 0; i < s.len && i < upTo; i++) if ((uint8_t)(s.start + i * s.step) == cell) return true;
    return false;
}

static void drawBoard(bool play) {
    int size = SIZE * zs, x0 = -camX, y0 = -camY;
    // The table, the board's wooden frame on it, the felt in the frame.
    gfx_fillRect(0, HUD_H, 128, 128 - HUD_H, FELT_DK);
    dither(0, HUD_H, 128, 128 - HUD_H, INK, 0);
    fillRound(x0 - FRAME + 1, y0 - FRAME + 1, size + 2 * FRAME, size + 2 * FRAME, 3, INK);     // its shadow
    fillRound(x0 - FRAME, y0 - FRAME, size + 2 * FRAME, size + 2 * FRAME, 3, WOOD);
    gfx_rect(x0 - 1, y0 - 1, size + 2, size + 2, GOLD);
    gfx_fillRect(x0, y0, size, size, FELT);
    const game::Last &l = game::last;
    bool lastPlay = play && l.kind == game::PLAYED;
    int r0 = (camY + HUD_H) / zs, r1 = (camY + 127) / zs, c0 = camX / zs, c1 = (camX + 127) / zs;
    if (r0 < 0) r0 = 0;
    if (c0 < 0) c0 = 0;
    if (r1 > SIZE - 1) r1 = SIZE - 1;
    if (c1 > SIZE - 1) c1 = SIZE - 1;
    // The squares, then the tiles over them (a lifted tile overlaps the row above).
    for (int pass = 0; pass < 2; pass++)
        for (int row = r0; row <= r1; row++)
            for (int col = c0; col <= c1; col++) {
                uint8_t cell = (uint8_t)(row * SIZE + col), v = game::board[cell];
                int x = col * zs - camX, y = row * zs - camY;
                uint8_t face = WHITE, ink = INK;
                int lift = 0;
                if (lastPlay && v) {
                    for (uint8_t i = 0; i < l.n; i++) {
                        if (l.cell[i] != cell) continue;
                        if (i > dropped) v = 0;                                         // not down yet
                        else if (i == dropped && anim == A_DROP) lift = 2 * (8 - animT) * (8 - animT) / 4;
                        face = GOLD;                                                    // the last play stays marked
                    }
                    if (anim == A_SWEEP && inSpan(l.main, cell, (uint8_t)(animT / 4 + 1))) {
                        face = FX_B;
                        if (inSpan(l.main, cell, (uint8_t)(animT / 4 + 1)) && !inSpan(l.main, cell, (uint8_t)(animT / 4))) lift = 2;
                    }
                }
                if (play) for (uint8_t i = 0; i < tent.n; i++)
                    if (tent.p[i].cell == cell) {
                        v = tent.p[i].tile;
                        face = SILVER;                                                  // the chess set's dark pieces: grey, blue, navy
                        lift = cell == slamCell && slamT ? slamT * slamT / 2 : 1;      // laid out, not yet played: held up
                    }
                if (v && (v & wd::BLANK)) ink = RED;
                if (denyT && (denyT & 4) && (denyWord ? inSpan(denySpan, cell, 15) : face == SILVER)) { face = RED; ink = WHITE; }
                if (pass == 0) { if (!v || lift) square(x, y, cell); }
                else if (v) boardTile(x, y, zs, v, face, ink, lift);
            }
}

// A tile in the rack: taller than wide, its letter in the serif face, its
// value in the corner.
static void rackTile(int x, int y, uint8_t t, uint8_t face) {
    fillRound(x + 2, y + 2, 13, 17, 1, WOOD);
    fillRound(x, y, 13, 17, 1, face);
    gfx_hline(x + 1, y + 16, 11, shadeOf(face));
    gfx_vline(x + 12, y + 1, 15, shadeOf(face));
    if (t == wd::BLANK_TILE) return;
    tileLetter(x, y + 2, 13, t, INK, shadeOf(face), shadeOf(face));
    char s[3];
    uint8_t val = wd::VALUE[t];
    fmtInt(s, val);
    text35(x + (val >= 10 ? 4 : 8), y + 11, s, WOOD);
}

// The way the word runs, hopping: a sprite's top left at (x, y).
static void arrow(int x, int y, uint32_t frame) {
    static const int8_t HOP[4] = {0, -1, -2, -1};
    sprite4(down ? ARROW_D : ARROW_R, x, y + HOP[(frame >> 3) & 3], RM_ID);
}

static void drawRack(uint32_t frame) {
    int top = rackTop();
    if (top >= 128) return;
    gfx_fillRect(0, top, 128, RACK_H, INK);
    gfx_hline(0, top, 128, GOLD);
    bool mine = !game::cpuTurn() && !game::over;
    for (uint8_t i = 0; i < wd::RACK; i++) {
        uint8_t t = game::rack[viewSide][i];
        int x = i * TILE_PITCH, y = top + 3;
        if (!t || !slotFree(i)) { roundRect(x + 2, y + 2, 11, 15, 2, NAVY); continue; }     // an empty slot
        uint8_t face = WHITE;
        bool sel = mine && (mode == RACK || mode == SWAP) && i == rackSel;
        if (mode == SWAP && (swapMarks >> i & 1)) { y -= 2; face = CYAN; }
        if (sel) y -= 2;
        rackTile(x, y, t, face);
        if (sel) roundRect(x - 1, y - 1, 17, 21, 2, FX_A);          // round the face and its thickness, in the rainbow
    }
    // Beside the rack: what the laid-out tiles would score, and the way the word runs.
    char buf[8];
    if (tent.n) {
        if (tentScore >= 0) { buf[0] = '+'; fmtInt(buf + 1, tentScore); }
        else fmtStr(buf, "--");
        text35(127 - text35Width(buf), top + 4, buf, tentScore >= 0 ? FX_B : RED);
    }
    if (mine && mode != SWAP) arrow(116, top + 11, frame);
}

static void drawHud() {
    gfx_fillRect(0, 0, 128, HUD_H, INK);
    gfx_hline(0, HUD_H - 1, 128, GOLD);
    char buf[16], *p;
    bool cpu = game::setup.mode == game::VS_CPU;
    for (uint8_t s = 0; s < 2; s++) {
        p = fmtStr(buf, cpu ? (s ? "CPU " : "YOU ") : (s ? "P2 " : "P1 "));
        fmtInt(p, shown[s]);
        uint8_t c = !game::over && game::turn == s ? FX_B : SILVER;
        text35(s ? 126 - text35Width(buf) : 2, 1, buf, c);
    }
    fmtInt(fmtStr(buf, "BAG "), game::bagLeft);
    text35(64 - text35Width(buf) / 2, 1, buf, FELT_LT);
}

static void plate(const char *s, uint8_t c, int y) {
    int w = text35Width(s);
    panel(64 - w / 2 - 5, y, w + 10, 11, 3, NAVY, GOLD);
    text35(64 - w / 2, y + 3, s, c);
}

static void drawCursor(uint32_t frame) {
    if (game::cpuTurn() || game::over || anim != A_NONE || mode == SWAP) return;
    int x = cellX(cursor), y = cellY(cursor);
    uint8_t c = (frame & 16) ? FX_B : WHITE;
    gfx_rect(x - 1, y - 1, zs + 1, zs + 1, c);
    if (zs >= 12) gfx_rect(x - 2, y - 2, zs + 3, zs + 3, INK);
    if (mode == RACK || mode == PICK) {
        // Where the next tile goes, and which way the word runs from it.
        if (!game::board[cursor]) arrow(x + (zs - 10) / 2, y + (zs - 8) / 2, frame);
        // The glove, over the chosen tile (unless it would hide the square).
        int top = rackTop();
        if (mode == RACK && y + zs < top - 18) {
            int bob = (frame >> 4) & 1;
            sprite4(HAND, rackSel * TILE_PITCH + 6 - HAND_TIP, top - 15 - bob, RM_ID);
        }
    }
}

static void drawPicker(uint32_t frame) {
    panel(11, 26, 106, 66, 3, NAVY, GOLD);
    text35(64 - text35Width("THE BLANK IS...") / 2, 30, "THE BLANK IS...", GOLD);
    for (uint8_t i = 0; i < 26; i++) {
        int x = 15 + (i % 7) * 14, y = 39 + (i / 7) * 13;
        if (i == pickSel) fillRound(x, y, 13, 13, 1, (frame & 16) ? FX_B : GOLD);
        tileLetter(x, y + 2, 13, (uint8_t)(i + 1), i == pickSel ? INK : WHITE, i == pickSel ? WOOD : BLUE, i == pickSel ? WOOD : INK);
    }
}

// The floats: the score in the display face, gold with an outline; the rest small.
static void drawFloats() {
    for (auto &f : floats) {
        if (!f.t || (f.t < 8 && !(f.t & 1))) continue;
        if (f.text[0] == '+') {
            int w = fontWidth(f.text), x = f.x - w / 2, y = f.y - FONT_H;
            if (x < 2) x = 2;
            if (x > 125 - w) x = 125 - w;
            if (y < HUD_H + 2) y = HUD_H + 2;
            Mask m = maskBegin(w, FONT_H);
            maskFont(m, 0, 0, f.text);
            uint8_t ramp[FONT_H + 2];
            for (int i = 0; i < FONT_H + 2; i++) ramp[i] = i < 4 ? FX_B : (i < 9 ? GOLD : WOOD);
            maskDraw(m, x, y, 0, INK, -1, ramp);
        } else {
            int w = text35Width(f.text), x = f.x - w / 2;
            if (x < 2) x = 2;
            if (x > 126 - w) x = 126 - w;
            fillRound(x - 2, f.y - 2, w + 3, 9, 2, INK);
            text35(x, f.y, f.text, f.colour);
        }
    }
}

void invalidate() { dirty = true; }

static uint32_t lastSig;
static bool wasMoving;

bool render(uint32_t frame, uint32_t ui) {
    int lo, hi;
    bool moving = fx::activeRows(lo, hi) || anim != A_NONE || denyT || slamT;
    if (wasMoving && !moving) dirty = true;         // once more, to clear up after it
    wasMoving = moving;
    // Still: the blinks and the arrow's hop step every 8 frames, and the palette does the rest.
    uint32_t sig = (frame >> 3) ^ (ui << 8) ^ ((uint32_t)cursor << 16) ^ ((uint32_t)mode << 24) ^ ((uint32_t)rackSel << 27) ^
                   ((uint32_t)pickSel * 2654435761u) ^ ((uint32_t)swapMarks << 3) ^ (cpuThinking ? ai::progress() >> 3 : 0) ^
                   ((uint32_t)down << 30);
    if (!moving && !dirty && sig == lastSig) return false;
    lastSig = sig;
    dirty = false;
    drawBoard(true);
    drawCursor(frame);
    fx::drawParticles(zs >= 12 ? 3 : 2);
    drawFloats();
    drawHud();
    drawRack(frame);
    int py = rackTop() - 15;
    if (cpuThinking) {
        plate(cpuThinking, SILVER, py);
        gfx_hline(36, py + 9, (ai::progress() * 56) >> 8, FX_B);
    } else if (noteT) plate(noteText, noteCol, py);
    if (mode == PICK) drawPicker(frame);
    fx::drawBanner();
    fx::applyShake(HUD_H, rackTop() - 1);
    return true;
}

void renderTitle(uint32_t frame) {
    // Close up, drifting over the words laid out on it: a slow figure of eight.
    title = true;
    zs = NEAR;
    rackT = 0;
    int a = (int)(frame / 3);
    camX = 40 + ((fx::isin(a) * 36) >> 8);
    camY = 36 + ((fx::isin(a * 2) * 22) >> 8);
    game::last.kind = game::NOTHING;
    drawBoard(false);
}

}  // namespace stage
