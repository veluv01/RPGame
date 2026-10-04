// The dice cam (Cam.h): the phases of a throw, the tray and its back wall
// drawn in perspective, the power bar, and the plate of kept dice.
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
static d3::Dice dice;
static d3::Cam view;
static int32_t holdX[d3::N], holdY[d3::N];
static uint8_t power, keptMask, keptVal[d3::N], rollNo, who, rolled[d3::N];
static bool pending, isHuman;

// Into the cam and back out: a quick fade through black (the zoom-in eases on
// while the cam fades up).
static const uint8_t WHIP_T = 6, ZOOM_T = 12, OUT_T = 6, ENTER_T = 6;
static const int32_t RAIL_TOP = 40 << 8, SIDE_H = 22 << 8, SIDE_W = 12 << 8;

Phase phase() { return ph; }
bool active() { return ph != OFF; }
uint16_t restT() { return ph == RESULT ? t : 0; }
static bool live(uint8_t i) { return dice.d[i].state != d3::KEPT; }

void begin(uint8_t kept, const uint8_t *v, uint8_t roll, uint8_t player, bool human) {
    ph = WHIP_IN; t = 0; pending = false; power = 0;
    keptMask = kept; rollNo = roll; who = player; isHuman = human;
    memcpy(keptVal, v, sizeof keptVal);
    d3::hold(dice, kept);
    for (uint8_t i = 0; i < d3::N; i++) {
        d3::Die &d = dice.d[i];
        d.yaw = (uint16_t)fx::rnd(); d.pitch = (uint16_t)fx::rnd(); d.roll = (uint16_t)fx::rnd();
        holdX[i] = d.x; holdY[i] = d.y;
    }
    d3::defaultCam(view);
    view.focal = 40;
    audio::sfx(Sfx::Whoosh);
}

static void throwNow() {
    d3::release(dice, power, fx::rnd());
    d3::fix(dice, rolled);
    ph = TUMBLE; t = 0;
    audio::sfx(Sfx::Throw);
}

void release(const uint8_t *v) {
    memcpy(rolled, v, sizeof rolled);
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
    for (uint8_t i = 0; i < d3::N; i++) {
        if (!live(i)) continue;
        d3::Die &d = dice.d[i];
        d.x = holdX[i] + fx::rndRange(-200, 201);
        d.y = holdY[i] + fx::rndRange(-200, 201) + (fx::isin((int)t * 20 + i * 50) * 3);
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
                uint8_t far = 0xFF;
                for (uint8_t i = 0; i < d3::N; i++)
                    if (live(i) && (far == 0xFF || dice.d[i].z > dice.d[far].z)) far = i;
                int x, y; screenOf(dice.d[far], x, y);
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
                for (uint8_t i = 0; i < d3::N; i++) {
                    const d3::Die &d = dice.d[i];
                    if (!live(i) || d.state != d3::AIR || d.y > (d3::HALF + 6) << 8) continue;
                    int16_t sx, sy;
                    d3::project(view, d.x, 0, d.z, sx, sy);
                    fx::burst(fx::DUST, (sx + 8) >> 4, (sy + 8) >> 4, 3, 22, FELT_LT);
                }
            if (d3::atRest(dice)) { ph = RESULT; t = 0; }
            return;
        }
        case RESULT: {
            t++;
            // Crane up and look down on the dice, so their tops read, all in frame.
            int32_t x0 = 0x7FFFFFFF, x1 = -x0, z0 = x0, z1 = -x0;
            for (uint8_t i = 0; i < d3::N; i++) {
                if (!live(i)) continue;
                const d3::Die &d = dice.d[i];
                if (d.x < x0) x0 = d.x;
                if (d.x > x1) x1 = d.x;
                if (d.z < z0) z0 = d.z;
                if (d.z > z1) z1 = d.z;
            }
            ease32(view.camX, (x0 + x1) / 2);
            ease32(view.camY, 120 << 8);
            ease32(view.camZ, (z0 + z1) / 2 - (85 << 8));
            int32_t v = view.pitch, f = view.focal, sx = view.sx0, sy = view.sy0;
            ease32(v, 36);
            // As close as keeps the widest and the deepest of them on screen.
            int32_t want = 7000 / (((x1 - x0) >> 9) + 12), deep = 5600 / (((z1 - z0) * 3 >> 11) + 14);
            if (deep < want) want = deep;
            if (want > 190) want = 190;
            ease32(f, want);
            ease32(sx, 64);
            ease32(sy, 62);
            if (v < 36 && t < 40) v++;                        // ease32 stalls short of small targets
            view.pitch = (uint8_t)v; view.focal = (int16_t)f; view.sx0 = (int16_t)sx; view.sy0 = (int16_t)sy;
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

// Stretch an edge's near end along its line until it is below the screen.
static void reach(int16_t fx, int16_t fy, int16_t &nx, int16_t &ny) {
    const int32_t BOTTOM = 136 << 4;
    if (ny >= BOTTOM || ny <= fy) return;
    int32_t x = fx + (int32_t)(nx - fx) * (BOTTOM - fy) / (ny - fy);
    nx = (int16_t)(x > 4000 ? 4000 : x < -4000 ? -4000 : x);
    ny = (int16_t)BOTTOM;
}

// A line on the baize between two floor points, if both are in front of the lens.
static void floorLine(int32_t xa, int32_t za, int32_t xb, int32_t zb) {
    int16_t ax, ay, bx, by;
    if (!d3::project(view, xa, 0, za, ax, ay) || !d3::project(view, xb, 0, zb, bx, by)) return;
    gfx_line(px(ax), px(ay), px(bx), px(by), FELT_LT);
}

// A dice tray: the far end is a padded leather wall, buttoned to a wooden
// frame; the baize runs toward the camera between two wooden rails, an
// inlaid line round it.
static void background() {
    const int32_t W = d3::WALL_Z, S = d3::SIDE_X;
    int32_t N = view.camZ + (28 << 8);                      // nothing behind the lens
    if (N < d3::NEAR_Z) N = d3::NEAR_Z;
    int capY = px(qy(0, RAIL_TOP, W)), botY = px(qy(0, 0, W));
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
    // The back wall: a wooden cap, then leather panels with a sheen along
    // the top, a seam between each and a brass stud at both ends of a seam.
    int h = botY - capY, span = xr - xl, cap = h / 6 + 1;
    if (h > 0 && span > 0) {
        gfx_fillRect(xl - 2, capY, span + 4, cap, WOOD);
        gfx_hline(xl - 2, capY, span + 4, GOLD);
        gfx_fillRect(xl, capY + cap, span, h - cap, WINE);
        dither(xl, capY + cap, span, (h - cap) / 3, RED, 0);
        gfx_hline(xl, botY - 1, span, INK);
        for (int i = 0; i <= 6; i++) {
            int x = xl + span * i / 6 - (i == 6);
            gfx_vline(x, capY + cap, h - cap, INK);
            if (h > 12) {
                gfx_fillRect(x - (i == 6), capY + cap + 2, 2, 2, GOLD);
                gfx_fillRect(x - (i == 6), botY - 4, 2, 2, GOLD);
            }
        }
    }
    // The baize, darker in the wall's shadow, and its inlaid line.
    int fy = botY < 0 ? 0 : botY;
    gfx_fillRect(0, fy, 128, 128 - fy, FELT);
    dither(0, fy, 128, 3, FELT_DK, 0);
    const int32_t IX = S - (9 << 8), ZF = W - (10 << 8), ZN = 6 << 8;
    floorLine(-IX, ZF, IX, ZF);
    floorLine(-IX, ZN, IX, ZN);
    floorLine(-IX, ZN, -IX, ZF);
    floorLine(IX, ZN, IX, ZF);
    // Side rails: polished wood, a gold top edge, the dark beyond.
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
        fillConvex(face, 4, INK, 1);
        int16_t top[8] = {face[6], face[7], face[4], face[5], outerN[0], outerN[1],
                          qx(xo, SIDE_H, W), qy(xo, SIDE_H, W)};
        fillConvex(top, 4, WOOD);
        int16_t out[8] = {top[6], top[7], top[4], top[5], (int16_t)(side * 4000), top[5], (int16_t)(side * 4000), top[7]};
        fillConvex(out, 4, INK);
        gfx_line(px(face[6]), px(face[7]), px(face[4]), px(face[5]), GOLD);
        gfx_line(px(face[0]), px(face[1]), px(face[2]), px(face[3]), INK);
    }
}

// Which roll this is, and the dice that were kept back, top left.
static void plate() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < d3::N; i++) n += keptMask >> i & 1;
    int w = 42, h = n ? 22 : 11;
    if (n * 9 + 5 > w) w = n * 9 + 5;
    fillRound(2, 2, w, h, 3, INK);
    roundRect(2, 2, w, h, 3, GOLD);
    char s[10] = "ROLL 1/3";
    s[5] = (char)('0' + rollNo);
    text35(6, 5, s, rollNo == 3 ? RED : GOLD);
    int x = 5;
    for (uint8_t i = 0; i < d3::N; i++)
        if (keptMask >> i & 1) { art::dieFace(x, 13, 7, keptVal[i], who); x += 9; }
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
    // Shadows first, then the dice from the far end forward.
    d3::Look look;
    art::dieColours(who, look.light, look.dark, look.pip);
    look.mid = look.light; look.pipDark = look.pip == WHITE ? SILVER : look.pip; look.edge = INK;
    uint8_t order[d3::N], n = 0;
    for (uint8_t i = 0; i < d3::N; i++) {
        if (!live(i)) continue;
        if (dice.d[i].state != d3::HELD) d3::shadow(dice.d[i], view, FELT_DK);
        uint8_t k = n++;
        for (; k && dice.d[order[k - 1]].z < dice.d[i].z; k--) order[k] = order[k - 1];
        order[k] = i;
    }
    for (uint8_t k = 0; k < n; k++) d3::draw(dice.d[order[k]], view, look);
    fx::drawParticles();
    fx::drawBanner();
    fx::drawFloats();
    plate();
    if (isHuman && (ph == SHAKE || ph == WHIP_IN)) powerBar(frame);
    fx::applyShake(0, 127, INK);                        // uncovered edges black, not smeared
    return true;
}

}  // namespace cam
