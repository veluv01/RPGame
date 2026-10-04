// The dice cam (Cam.h): its phases, the view down the table to the back
// wall, and the dice (Dice3D) thrown, tumbling and coming to rest in it.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
#include <RPGame.h>
#include <string.h>
#include "Cam.h"
#include "Dice3D.h"
#include "Fx.h"
#include "Sounds.h"
#include "Chips.h"

namespace cam {

static Phase ph = OFF;
static uint16_t t;                      // ticks in this phase
static d3::Pair dice;
static d3::Cam view;
static int32_t holdX[2], holdY[2];
static uint8_t power, pointNum, rollA, rollB, rest;
static bool pending, isHot, showSum;

// Into the cam and back out: a quick fade through black (the zoom-in eases on
// while the cam fades up).
static const uint8_t WHIP_T = 6, ZOOM_T = 12, OUT_T = 6, ENTER_T = 6;
static const int32_t RUBBER_Y = 34 << 8, RAIL_TOP = 40 << 8, SIDE_H = 22 << 8, SIDE_W = 12 << 8;

Phase phase() { return ph; }
bool active() { return ph != OFF; }
uint16_t restT() { return ph == RESULT ? t : 0; }
void hot(bool on) { isHot = on; }
bool burning() { return isHot && ph != OFF && ph != ENTER && ph != WHIP_OUT; }

void begin(uint8_t point) {
    ph = WHIP_IN; t = 0; pointNum = point; pending = false; power = 0; showSum = false;
    d3::hold(dice);
    for (uint8_t i = 0; i < 2; i++) {
        d3::Die &d = dice.d[i];
        d.yaw = (uint16_t)fx::rnd(); d.pitch = (uint16_t)fx::rnd(); d.roll = (uint16_t)fx::rnd();
        d3::labelDie(d, (uint8_t)(fx::rnd() % 6), (uint8_t)(1 + fx::rnd() % 6), (uint8_t)fx::rnd());
        holdX[i] = d.x; holdY[i] = d.y;
    }
    d3::defaultCam(view);
    view.focal = 40;
    audio::sfx(Sfx::Whoosh);
}

static void throwNow() {
    uint32_t spin = fx::rnd();
    d3::release(dice, power, spin);
    d3::fix(dice, rollA, rollB);
    ph = TUMBLE; t = 0;
    audio::sfx(Sfx::Throw);
}

void release(uint8_t a, uint8_t b) {
    rollA = a; rollB = b;
    if (ph == WHIP_IN) pending = true;
    else throwNow();
}

void cancel() { ph = WHIP_OUT; t = 0; audio::sfx(Sfx::Whoosh); }
void leave() { if (ph == RESULT) { ph = WHIP_OUT; t = 0; audio::sfx(Sfx::Whoosh); } }

void skip() {
    if (ph != TUMBLE) return;
    for (int i = 0; i < 400 && !d3::atRest(dice); i++) d3::step(dice);
    dice.hits = 0;
}

// In the hand: the dice rattle (presentation randomness only - the faces
// are painted after the throw anyway).
static void jiggle() {
    for (uint8_t i = 0; i < 2; i++) {
        d3::Die &d = dice.d[i];
        d.x = holdX[i] + fx::rndRange(-200, 201);
        d.y = holdY[i] + fx::rndRange(-200, 201) + (fx::isin((int)t * 20) * 3);
        d.yaw = (uint16_t)(d.yaw + fx::rndRange(-2500, 2500));
        d.pitch = (uint16_t)(d.pitch + fx::rndRange(-4000, 4000));
        d.roll = (uint16_t)(d.roll + fx::rndRange(-3000, 3000));
    }
    if ((t & 3) == 0 && (fx::rnd() & 1)) audio::blip((uint16_t)fx::rndRange(2400, 3800), 4);
}

static void screenOf(const d3::Die &d, int &x, int &y) {
    int16_t sx, sy;
    d3::project(view, d.x, d.y, d.z, sx, sy);
    x = (sx + 8) >> 4; y = (sy + 8) >> 4;
}

static void ease32(int32_t &v, int32_t to) { v += (to - v) / 6; }

// A hot hand: flames lick up off the dice.
static void flames(uint8_t every) {
    if (!isHot || (t % every)) return;
    for (uint8_t i = 0; i < 2; i++) {
        int x, y; screenOf(dice.d[i], x, y);
        fx::spawn(fx::SPARK, x + fx::rndRange(-6, 7), y + fx::rndRange(-2, 4), fx::rndRange(-6, 7),
                  fx::rndRange(-26, -12), (uint8_t)fx::rndRange(10, 20), FX_A);
    }
}
static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

void update() {
    switch (ph) {
        case OFF: case ENTER:
            if (ph == ENTER) {
                ++t;
                pal::setFade((uint8_t)(t * 16 / ENTER_T));
                if (t >= ENTER_T) ph = OFF;
            }
            return;
        case WHIP_IN:
            t++;
            pal::setFade((uint8_t)(t <= WHIP_T ? 16 - t * 16 / WHIP_T : ((t - WHIP_T) * 16 / 6 > 16 ? 16 : (t - WHIP_T) * 16 / 6)));
            if (t > WHIP_T) view.focal = (int16_t)(40 + (70 * fx::ease(fx::OUT_CUBIC, t - WHIP_T, ZOOM_T)) / 256);
            jiggle();
            if (t >= WHIP_T + ZOOM_T) {
                ph = SHAKE; t = 0;
                if (pending) throwNow();
            }
            return;
        case SHAKE: {
            t++;
            jiggle();
            flames(2);
            int k = (t * 6) & 511;                           // the power swings, a second a cycle
            power = (uint8_t)(k < 256 ? k : 511 - k);
            return;
        }
        case TUMBLE: {
            t++;
            d3::step(dice);
            uint8_t h = dice.hits;
            dice.hits = 0;
            if (h & d3::HIT_WALL) {
                const d3::Die &d = dice.d[dice.d[0].z > dice.d[1].z ? 0 : 1];
                int x, y; screenOf(d, x, y);
                fx::burst(fx::SPARK, x, y, 10, 40, GOLD);
                fx::burst(fx::STAR, x, y, 4, 30, WHITE);
                fx::shake(6, 2);
                audio::sfx(Sfx::Wall);
            } else if (h & d3::HIT_DICE) {
                audio::sfx(Sfx::Clack);
            } else if (h & d3::HIT_FLOOR) {
                audio::sfx(Sfx::Bounce);
            }
            if (h & d3::HIT_FLOOR)
                for (uint8_t i = 0; i < 2; i++) {
                    const d3::Die &d = dice.d[i];
                    if (d.y > (d3::HALF + 6) << 8) continue;
                    int16_t sx, sy;
                    d3::project(view, d.x, 0, d.z, sx, sy);
                    fx::burst(fx::DUST, (sx + 8) >> 4, (sy + 8) >> 4, 5, 22, FELT_LT);
                }
            flames(2);
            if (d3::atRest(dice)) { ph = RESULT; t = 0; }
            return;
        }
        case RESULT: {
            t++;
            // Crane up and look down on the dice, so their tops read, both in frame.
            int32_t mx = (dice.d[0].x + dice.d[1].x) / 2, mz = (dice.d[0].z + dice.d[1].z) / 2;
            int32_t spread = iabs32(dice.d[0].x - dice.d[1].x) + iabs32(dice.d[0].z - dice.d[1].z);
            ease32(view.camX, mx);
            ease32(view.camY, 120 << 8);
            ease32(view.camZ, mz - (85 << 8));
            int32_t v = view.pitch, f = view.focal, sx = view.sx0, sy = view.sy0;
            ease32(v, 36);
            int32_t want = spread > (34 << 8) ? 190 * (34 << 8) / spread : 190;      // dice ~22 px
            ease32(f, want);
            ease32(sx, 64);
            ease32(sy, 60);
            if (v < 36 && t < 40) v++;                        // ease32 stalls short of small targets
            view.pitch = (uint8_t)v; view.focal = (int16_t)f; view.sx0 = (int16_t)sx; view.sy0 = (int16_t)sy;
            flames(3);
            return;
        }
        case WHIP_OUT:
            ++t;
            pal::setFade((uint8_t)(16 - t * 16 / OUT_T));
            if (t >= OUT_T) { ph = ENTER; t = 0; pal::setFade(0); }
            return;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static int16_t qx(int32_t x, int32_t y, int32_t z) { int16_t sx, sy; d3::project(view, x, y, z, sx, sy); return sx; }
static int16_t qy(int32_t x, int32_t y, int32_t z) { int16_t sx, sy; d3::project(view, x, y, z, sx, sy); return sy; }
static int px(int16_t q) { return (q + 8) >> 4; }

// The far end: the casino's wall, the rail along the back and its rubber
// pyramids (diamonds, lit from the top left), then the felt and the side
// rails running toward the camera.
// Stretch an edge's near end along its line until it is below the screen.
static void reach(int16_t fx, int16_t fy, int16_t &nx, int16_t &ny) {
    const int32_t BOTTOM = 136 << 4;
    if (ny >= BOTTOM || ny <= fy) return;
    int32_t x = fx + (int32_t)(nx - fx) * (BOTTOM - fy) / (ny - fy);
    nx = (int16_t)(x > 4000 ? 4000 : x < -4000 ? -4000 : x);
    ny = (int16_t)BOTTOM;
}

static void background() {
    const int32_t W = d3::WALL_Z, S = d3::SIDE_X;
    int32_t N = view.camZ + (28 << 8);                      // nothing behind the lens
    if (N < d3::NEAR_Z) N = d3::NEAR_Z;
    int capY = px(qy(0, RAIL_TOP, W)), topY = px(qy(0, RUBBER_Y, W)), botY = px(qy(0, 0, W));
    int xl = px(qx(-S, 0, W)), xr = px(qx(S, 0, W));
    // The room.
    uint8_t row[GFX_FB_STRIDE] __attribute__((aligned(4)));
    memset(row, NAVY | (NAVY << 4), sizeof row);
    for (int x = 3; x < 128; x += 8) row[x >> 1] = (uint8_t)((row[x >> 1] & 0x0F) | (INK << 4));
    // The room down to the foot of the back wall, full width: beside the
    // wall, above where the side rails start, nothing else paints (sparks
    // and the shake would otherwise leave their pixels there).
    int sky = capY < 0 ? 0 : (capY > 128 ? 128 : capY), room = botY < 0 ? 0 : (botY > 128 ? 128 : botY);
    for (int y = 0; y < room; y++) gfx_copyRow(y, row, 0, GFX_W);
    dither(0, 0, 128, sky / 2, INK, 0);
    // The back rail and its rubber.
    gfx_fillRect(xl - 2, capY, xr - xl + 4, topY - capY, WOOD);
    gfx_hline(xl - 2, capY, xr - xl + 4, GOLD);
    int bandH = botY - topY;
    if (bandH > 0) {
        int span = xr - xl;
        int across = span / (bandH > 4 ? bandH / 2 : 2);    // square-ish diamonds
        if (across < 2) across = 2;
        static const uint8_t FACET[4] = {SILVER, NAVY, NAVY, INK};
        for (int y = topY; y < botY; y++) {
            if (y < 0 || y >= GFX_H) continue;
            int v = (y - topY) * 32 / bandH;                // two rows of diamonds, 16 steps each
            int vr = v & 15, odd = (v >> 4) & 1;
            memset(row, INK | (INK << 4), sizeof row);
            // u runs 0..15 across each diamond: 16.16 steps, no division a pixel.
            int32_t du = ((int32_t)across << 20) / span, u16 = (int32_t)(odd ? 8 : 0) << 16;
            int x = xl;
            if (x < 0) { u16 += du * -x; x = 0; }
            int dv = vr < 8 ? 8 - vr : vr - 8, top = vr >= 8;
            for (int xe = xr < GFX_W ? xr : GFX_W; x < xe; x++, u16 += du) {
                int u = (u16 >> 16) & 15;
                int du8 = u < 8 ? 8 - u : u - 8;
                uint8_t c = du8 + dv < 8 ? FACET[(u >= 8) + 2 * top] : INK;
                uint8_t &b = row[x >> 1];
                b = (x & 1) ? (uint8_t)((b & 0x0F) | (c << 4)) : (uint8_t)((b & 0xF0) | c);
            }
            gfx_copyRow(y, row, xl < 0 ? 0 : xl, xr > GFX_W ? GFX_W : xr);
        }
    }
    // The felt, darker in the wall's shadow, with the layout's lines (lines
    // of equal depth are level on screen).
    int fy = botY < 0 ? 0 : botY;
    gfx_fillRect(0, fy, 128, 128 - fy, FELT);
    dither(0, fy, 128, 3, FELT_DK, 0);
    static const int16_t PRINT_Z[3] = {28, 36, 96};
    for (uint8_t i = 0; i < 3; i++) {
        int32_t z = (int32_t)PRINT_Z[i] << 8;
        gfx_hline(px(qx(-S, 0, z)), px(qy(0, 0, z)), px(qx(S, 0, z)) - px(qx(-S, 0, z)), FELT_LT);
    }
    // Side rails: the padded inner face, its gold top edge, the dark beyond.
    // Looking down from far off, the rails' near ends can project above the
    // bottom of the screen: each edge runs on down its own line past it.
    for (int side = -1; side <= 1; side += 2) {
        int32_t x = side * S, xo = side * (S + SIDE_W);
        int16_t face[8] = {qx(x, 0, W), qy(x, 0, W), qx(x, 0, N), qy(x, 0, N),
                           qx(x, SIDE_H, N), qy(x, SIDE_H, N), qx(x, SIDE_H, W), qy(x, SIDE_H, W)};
        int16_t outerN[2] = {qx(xo, SIDE_H, N), qy(xo, SIDE_H, N)};
        reach(face[0], face[1], face[2], face[3]);
        reach(face[6], face[7], face[4], face[5]);
        reach(qx(xo, SIDE_H, W), qy(xo, SIDE_H, W), outerN[0], outerN[1]);
        fillConvex(face, 4, WOOD);
        fillConvex(face, 4, WINE, 1);
        int16_t top[8] = {face[6], face[7], face[4], face[5], outerN[0], outerN[1],
                          qx(xo, SIDE_H, W), qy(xo, SIDE_H, W)};
        fillConvex(top, 4, WOOD);
        int16_t out[8] = {top[6], top[7], top[4], top[5], (int16_t)(side * 4000), top[5], (int16_t)(side * 4000), top[7]};
        fillConvex(out, 4, INK);
        gfx_line(px(face[6]), px(face[7]), px(face[4]), px(face[5]), GOLD);
        gfx_line(px(face[0]), px(face[1]), px(face[2]), px(face[3]), INK);
    }
}

static void plate() {
    // What this roll is for, top left.
    fillRound(2, 2, pointNum ? 50 : 58, 11, 3, INK);
    roundRect(2, 2, pointNum ? 50 : 58, 11, 3, GOLD);
    art::puck(10, 7, pointNum != 0, pointNum);
    text35(19, 5, pointNum ? "POINT" : "COMING OUT", isHot ? FX_A : (pointNum ? GOLD : WHITE));
    if (pointNum) {
        char s[3] = {(char)('0' + (pointNum >= 10 ? 1 : pointNum)), (char)(pointNum >= 10 ? '0' : 0), 0};
        text35(41, 5, s, WHITE);
    }
}

// Under the dice once they stop: "3 + 4 = 7".
void result(uint8_t a, uint8_t b) { rollA = a; rollB = b; showSum = true; }

static void sumPlate() {
    char s[12] = {(char)('0' + rollA), ' ', '+', ' ', (char)('0' + rollB), ' ', '=', ' ', 0};
    char *p = s + 8;
    uint8_t n = (uint8_t)(rollA + rollB);
    if (n >= 10) *p++ = '1';
    *p++ = (char)('0' + n % 10);
    *p = 0;
    int w = gfx_textWidth(s) + 10;
    panel(64 - w / 2, 110, w, 13, 3, INK, GOLD);
    gfx_text(64 - (w - 10) / 2, 113, s, WHITE);
}

static void powerBar(uint32_t frame) {
    int h = 56, x = 119, y = 46;
    fillRound(x - 1, y - 1, 8, h + 2, 2, INK);
    roundRect(x - 1, y - 1, 8, h + 2, 2, GOLD);
    int f = power * (h - 2) / 255;
    uint8_t c = power < 96 ? FELT_LT : (power < 200 ? GOLD : RED);
    gfx_fillRect(x + 1, y + h - 1 - f, 4, f, c);
    if ((frame >> 4) & 1 || t > 120) text35(64 - text35Width("LET GO TO THROW") / 2, 120, "LET GO TO THROW", WHITE);
}

bool render(uint32_t frame) {
    switch (ph) {
        case OFF: case ENTER: return false;
        case WHIP_IN:
            if (t <= WHIP_T) return true;                       // the table fades out as it was
            break;
        default: break;
    }
    background();
    // Shadows first, then the far die, then the near one.
    bool redRed = pal::theme() == pal::RED_FELT;
    d3::Look look = redRed ? d3::Look{BLUE, BLUE, NAVY, WHITE, SILVER, INK}
                           : d3::Look{RED, RED, WINE, WHITE, SILVER, INK};
    if (isHot) look.edge = FX_A;
    for (uint8_t i = 0; i < 2; i++) if (dice.d[i].state != d3::HELD) d3::shadow(dice.d[i], view, FELT_DK);
    uint8_t far = dice.d[0].z > dice.d[1].z ? 0 : 1;
    d3::draw(dice.d[far], view, look);
    d3::draw(dice.d[1 - far], view, look);
    fx::drawParticles();
    fx::drawBanner();
    fx::drawFloats();
    plate();
    if (ph == SHAKE || ph == WHIP_IN) powerBar(frame);
    if (ph == RESULT && showSum && t > 6) sumPlate();
    fx::applyShake(0, 127, INK);                        // uncovered edges black, not smeared
    return true;
}



}  // namespace cam
