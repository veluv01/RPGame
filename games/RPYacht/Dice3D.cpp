// The 3D dice (Dice3D.h): throwing and the physics step, repainting the pips
// so the rolled numbers land on top (fix()), the camera, and drawing a die
// as lit and shaded faces.
#pragma GCC optimize("Os")   // cold code: size over speed (the fills it calls are the hot part)
#include "Dice3D.h"
#ifndef CHTEST
#include <RPGame.h>
#include "Fx.h"
#endif

namespace d3 {

// The game's sine, fx::isin (a in 1/256 turn -> -255..255). The host tests
// build this file alone, so they carry a copy of its table.
#ifdef CHTEST
static const uint8_t SIN[65] = {
    0, 6, 13, 19, 25, 31, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92, 98, 104, 109, 115, 121, 126,
    132, 137, 142, 147, 152, 157, 162, 167, 172, 177, 181, 185, 190, 194, 198, 202, 206, 209,
    213, 216, 220, 223, 226, 229, 231, 234, 237, 239, 241, 243, 245, 247, 248, 250, 251, 252,
    253, 254, 255, 255, 255, 255, 255};
static int isin(int a) {
    uint8_t i = (uint8_t)a, q = i >> 6, k = i & 63;
    int v = (q & 1) ? SIN[64 - k] : SIN[k];
    return q & 2 ? -v : v;
}
#else
static int isin(int a) { return fx::isin(a); }
#endif

static int sin8(uint16_t a) { return isin(a >> 8); }
static int cos8(uint16_t a) { return sin8((uint16_t)(a + 16384)); }
static int mul(int a, int b) { return (a * b) >> 8; }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

Mat matrix(const Die &d) {
    int sy = sin8(d.yaw), cy = cos8(d.yaw), sp = sin8(d.pitch), cp = cos8(d.pitch);
    int sr = sin8(d.roll), cr = cos8(d.roll);
    int sysp = mul(sy, sp), cysp = mul(cy, sp);
    Mat m;
    m.m[0][0] = (int16_t)(mul(cy, cr) + mul(sysp, sr));
    m.m[0][1] = (int16_t)(mul(sysp, cr) - mul(cy, sr));
    m.m[0][2] = (int16_t)mul(sy, cp);
    m.m[1][0] = (int16_t)mul(cp, sr);
    m.m[1][1] = (int16_t)mul(cp, cr);
    m.m[1][2] = (int16_t)-sp;
    m.m[2][0] = (int16_t)(mul(cysp, sr) - mul(sy, cr));
    m.m[2][1] = (int16_t)(mul(sy, sr) + mul(cysp, cr));
    m.m[2][2] = (int16_t)mul(cy, cp);
    return m;
}

uint8_t upFace(const Die &d) {
    Mat m = matrix(d);
    uint8_t best = 0;
    int bv = 0;
    for (uint8_t a = 0; a < 3; a++) {
        int v = m.m[1][a];
        if (iabs(v) > iabs(bv)) { bv = v; best = a; }
    }
    return (uint8_t)(best * 2 + (bv < 0));
}

// How far the die reaches from its centre along world axis r (Q8).
static int32_t extent(const Mat &m, int r) {
    return (int32_t)HALF * (iabs(m.m[r][0]) + iabs(m.m[r][1]) + iabs(m.m[r][2]));
}

static uint32_t noise(Die &d) {
    d.seed ^= d.seed << 13; d.seed ^= d.seed >> 17; d.seed ^= d.seed << 5;
    return d.seed;
}
static int16_t jitter(Die &d, int range) { return (int16_t)((int)(noise(d) % (uint32_t)(2 * range + 1)) - range); }

// ---------------------------------------------------------------------------
// Faces
// ---------------------------------------------------------------------------
// Base die: +Y 1, +X 2, +Z 3 (opposite faces add to 7). In this left-handed
// world (Z into the screen) 1-2-3 run anticlockwise round their corner: a
// Western die. Its 24 orientations are visited by one walk of quarter turns
// (X: +Y -> +Z -> -Y -> -Z, Y: +Z -> +X -> -Z -> -X; bit k of WALK says Y),
// which meets each value on each face exactly four times: spin picks one.
void labelDie(Die &d, uint8_t up, uint8_t v, uint8_t spin) {
    static const uint8_t CYC[2][4] = {{2, 5, 3, 4}, {4, 1, 5, 0}};
    static const uint32_t WALK = 0x288A28u;
    uint8_t L[6] = {2, 5, 1, 6, 3, 4};
    spin &= 3;
    for (uint8_t k = 0; k < 24; k++) {
        if (L[up] == v && !spin--) break;
        const uint8_t *c = CYC[(WALK >> k) & 1];
        uint8_t t = L[c[0]];
        L[c[0]] = L[c[1]]; L[c[1]] = L[c[2]]; L[c[2]] = L[c[3]]; L[c[3]] = t;
    }
    for (uint8_t i = 0; i < 6; i++) d.label[i] = L[i];
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------
static const int32_t G = 30;             // gravity, Q8 units per tick^2
static const int32_t R2 = (EDGE + 2) << 8;      // two dice closer than this (per axis) touch

void hold(Dice &p, uint8_t kept) {
    uint8_t n = 0, j = 0;
    for (uint8_t i = 0; i < N; i++) n += !(kept >> i & 1);
    uint8_t low = n > 3 ? 3 : n;                    // a row of up to three, the rest on top
    for (uint8_t i = 0; i < N; i++) {
        Die &d = p.d[i];
        d.vx = d.vy = d.vz = 0;
        d.wyaw = d.wpitch = d.wroll = 0;
        d.settleT = 0;
        if (kept >> i & 1) { d.state = KEPT; continue; }
        d.state = HELD;
        bool top = j >= low;
        int row = top ? n - low : low, k = top ? j - low : j;
        d.x = (2 * k - (row - 1)) * 10 * 256;       // 20 apart: clear of each other (R2)
        d.y = (top ? 35 : 15) * 256;
        d.z = ((j & 1) ? -8 : -11) * 256;
        labelDie(d, (uint8_t)(i % 6), (uint8_t)(1 + i), (uint8_t)i);    // pips from the start
        j++;
    }
    p.t = 0;
    p.hits = 0;
}

void release(Dice &p, uint8_t power, uint32_t spin) {
    for (uint8_t i = 0; i < N; i++) {
        Die &d = p.d[i];
        if (d.state == KEPT) continue;
        d.seed = (spin ^ (0x9E3779B9u * (i + 1))) | 1;
        d.state = AIR;
        d.vz = 1010 + power * 3 + jitter(d, 40);
        d.vy = 260 + jitter(d, 60);
        d.vx = (d.x >> 8) * 3 + jitter(d, 50);      // the handful fans out
        d.wpitch = (int16_t)(2600 + power * 6 + jitter(d, 700));
        d.wroll = jitter(d, 1800);
        d.wyaw = jitter(d, 900);
        d.settleT = 0;
    }
    p.t = 0;
    p.hits = 0;
    p.painting = false;
}

static uint16_t quarter(uint16_t a) { return (uint16_t)((a + 8192) & 0xC000); }

static void stepDie(Die &d, uint8_t &hits) {
    if (d.state == REST || d.state == HELD || d.state == KEPT) return;
    d.x += d.vx; d.y += d.vy; d.z += d.vz;
    d.yaw = (uint16_t)(d.yaw + d.wyaw);
    if (d.state == SETTLE) {
        // Fall flat onto the nearest face and stop.
        int16_t ep = (int16_t)(quarter(d.pitch) - d.pitch), er = (int16_t)(quarter(d.roll) - d.roll);
        d.pitch = (uint16_t)(d.pitch + ep / 3);
        d.roll = (uint16_t)(d.roll + er / 3);
        d.vx = d.vx * 3 / 4; d.vz = d.vz * 3 / 4; d.vy = 0;
        d.wyaw = (int16_t)(d.wyaw * 3 / 4);
        Mat m = matrix(d);
        d.y = extent(m, 1);
        if (++d.settleT > 6 && iabs(ep) < 96 && iabs(er) < 96) {
            d.pitch = quarter(d.pitch); d.roll = quarter(d.roll);
            d.y = HALF << 8;
            d.vx = d.vz = 0; d.wyaw = 0;
            d.state = REST;
        }
        return;
    }
    d.vy -= G;
    d.pitch = (uint16_t)(d.pitch + d.wpitch);
    d.roll = (uint16_t)(d.roll + d.wroll);
    Mat m = matrix(d);
    // The felt.
    int32_t ey = extent(m, 1);
    if (d.y - ey < 0) {
        d.y = ey;
        if (d.vy < 0) {
            if (d.vy < -90) hits |= HIT_FLOOR;
            d.vy = -d.vy / 2;
            if (d.vy < 50) d.vy = 0;
        }
        d.vx = d.vx * 13 / 16; d.vz = d.vz * 13 / 16;
        // Rolling: forward speed turns into a tumble about X, sideways about Z.
        d.wpitch = (int16_t)(d.wpitch + ((int32_t)d.vz * 5 - d.wpitch) / 2);
        d.wroll = (int16_t)(d.wroll + ((int32_t)-d.vx * 5 - d.wroll) / 2);
        d.wyaw = (int16_t)(d.wyaw * 3 / 4);
        if (d.vy == 0 && iabs(d.vx) + iabs(d.vz) < 70) { d.state = SETTLE; d.settleT = 0; }
    }
    // The back wall's pyramids: back it comes, with a kick and a hop.
    int32_t ez = extent(m, 2);
    if (d.z + ez > WALL_Z && d.vz > 0) {
        d.z = WALL_Z - ez;
        d.vz = -d.vz / 4;
        d.vy += iabs(d.vz) / 3;
        d.wyaw = (int16_t)(d.wyaw + jitter(d, 2400));
        d.wroll = (int16_t)(d.wroll + jitter(d, 2400));
        d.wpitch = (int16_t)(-d.wpitch / 2 + jitter(d, 1200));
        d.vx += jitter(d, 120);
        hits |= HIT_WALL;
    }
    if (d.z - ez < NEAR_Z && d.vz < 0) { d.z = NEAR_Z + ez; d.vz = -d.vz / 2; hits |= HIT_SIDE; }
    int32_t ex = extent(m, 0);
    if (d.x + ex > SIDE_X && d.vx > 0) { d.x = SIDE_X - ex; d.vx = -d.vx * 5 / 8; hits |= HIT_SIDE; }
    if (d.x - ex < -SIDE_X && d.vx < 0) { d.x = -SIDE_X + ex; d.vx = -d.vx * 5 / 8; hits |= HIT_SIDE; }
}

// Two dice that meet push apart along the axis they overlap least on and
// trade that part of their speed (no square roots: boxes, not spheres).
static void collide(Die &a, Die &b, uint8_t &hits) {
    if (a.state == HELD || b.state == HELD || a.state == KEPT || b.state == KEPT) return;
    int32_t dx = b.x - a.x, dz = b.z - a.z, dy = b.y - a.y;
    if (iabs(dx) >= R2 || iabs(dz) >= R2 || iabs(dy) >= R2) return;
    int32_t px = R2 - iabs(dx), pz = R2 - iabs(dz);
    if (px < pz) {
        int32_t s = dx >= 0 ? px / 2 : -px / 2;
        a.x -= s; b.x += s;
        if ((b.vx - a.vx) * dx < 0) { int32_t t = a.vx; a.vx = b.vx * 4 / 5; b.vx = t * 4 / 5; hits |= HIT_DICE; }
    } else {
        int32_t s = dz >= 0 ? pz / 2 : -pz / 2;
        a.z -= s; b.z += s;
        if ((b.vz - a.vz) * dz < 0) { int32_t t = a.vz; a.vz = b.vz * 4 / 5; b.vz = t * 4 / 5; hits |= HIT_DICE; }
    }
    // A die knocked while settling tumbles again.
    if (a.state == REST || a.state == SETTLE) { a.state = AIR; a.vy = 60; }
    if (b.state == REST || b.state == SETTLE) { b.state = AIR; b.vy = 60; }
}

void step(Dice &p) {
    p.t++;
    for (uint8_t i = 0; i < N; i++) {
        Die &d = p.d[i];
        // However it lands, it is still within four seconds.
        if (p.t > 200 && d.state == AIR) { d.state = SETTLE; d.settleT = 0; }
        stepDie(d, p.hits);
    }
    if (p.t <= 200)
        for (uint8_t i = 0; i < N; i++)
            for (uint8_t j = (uint8_t)(i + 1); j < N; j++) collide(p.d[i], p.d[j], p.hits);
    if (p.painting && ((p.hits & HIT_WALL) || p.t >= 45)) {
        for (uint8_t i = 0; i < N; i++)
            if (p.d[i].state != KEPT)
                for (uint8_t f = 0; f < 6; f++) p.d[i].label[f] = p.paint[i][f];
        p.painting = false;
    }
}

bool atRest(const Dice &p) {
    for (uint8_t i = 0; i < N; i++) if (p.d[i].state != REST && p.d[i].state != KEPT) return false;
    return true;
}

// Of the four ways to paint the result on, keep the one that changes the
// fewest faces the die already shows (it has had pips since it was picked up).
static void relabel(Die &d, uint8_t up, uint8_t v) {
    Die best = d;
    int bestSame = -1;
    for (uint8_t spin = 0; spin < 4; spin++) {
        Die t = d;
        labelDie(t, up, v, spin);
        int same = 0;
        for (uint8_t f = 0; f < 6; f++) same += t.label[f] == d.label[f];
        if (same > bestSame) { bestSame = same; best = t; }
    }
    for (uint8_t f = 0; f < 6; f++) d.label[f] = best.label[f];
}

void fix(Dice &p, const uint8_t *v) {
    Dice q = p;
    q.painting = false;
    for (int i = 0; i < 400 && !atRest(q); i++) step(q);
    for (uint8_t i = 0; i < N; i++) {
        if (p.d[i].state == KEPT) continue;
        Die d = p.d[i];
        relabel(d, upFace(q.d[i]), v[i]);
        for (uint8_t f = 0; f < 6; f++) p.paint[i][f] = d.label[f];
    }
    p.painting = true;
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
void defaultCam(Cam &c) {
    c.camX = 0; c.camY = 70 << 8; c.camZ = -80 * 256;
    c.pitch = 0; c.focal = 110;
    c.sx0 = 64; c.sy0 = -5;
}

static int16_t clampQ4(int32_t v) { return (int16_t)(v > 4000 ? 4000 : v < -4000 ? -4000 : v); }

// Depth along the view and height across it, Q8.
static void view(const Cam &c, int32_t y, int32_t z, int32_t &depth, int32_t &up) {
    int sp = sin8((uint16_t)(c.pitch << 8)), cp = cos8((uint16_t)(c.pitch << 8));
    int32_t dy = y - c.camY, dz = z - c.camZ;
    depth = (dz * cp - dy * sp) >> 8;
    up = (dy * cp + dz * sp) >> 8;
}

bool project(const Cam &c, int32_t x, int32_t y, int32_t z, int16_t &sx, int16_t &sy) {
    int32_t depth, up;
    view(c, y, z, depth, up);
    if (depth < 256) return false;
    int32_t d4 = depth >> 4;
    sx = clampQ4(((int32_t)c.sx0 << 4) + (x - c.camX) * c.focal / d4);
    sy = clampQ4(((int32_t)c.sy0 << 4) - up * c.focal / d4);
    return true;
}

int16_t scaleAt(const Cam &c, int32_t y, int32_t z) {
    int32_t depth, up;
    view(c, y, z, depth, up);
    if (depth < 256) depth = 256;
    return (int16_t)(((int32_t)c.focal << 12) / depth);
}

#ifndef CHTEST
// 3x3 pip grids (bit r*3+c) for 1..6.
static const uint16_t PIPS[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};
// The light comes from above, behind the shooter's left shoulder (Q8 unit).
static const int LX = -90, LY = 205, LZ = -123;
static const int PIP = 4;                // pip pitch across a face, world units

void draw(const Die &d, const Cam &c, const Look &l) {
    Mat m = matrix(d);
    int16_t sx[8], sy[8];
    for (uint8_t k = 0; k < 8; k++) {
        int32_t w[3];
        for (uint8_t r = 0; r < 3; r++) {
            int32_t v = 0;
            for (uint8_t a = 0; a < 3; a++) v += (k >> a & 1) ? m.m[r][a] : -m.m[r][a];
            w[r] = v * HALF;
        }
        if (!project(c, d.x + w[0], d.y + w[1], d.z + w[2], sx[k], sy[k])) return;   // behind the lens
    }
    int16_t edgePx = (int16_t)((scaleAt(c, d.y, d.z) * EDGE) >> 4);  // the die's size on screen
    int32_t camZ = c.camZ;
    for (uint8_t f = 0; f < 6; f++) {
        uint8_t a = f >> 1;
        int s = (f & 1) ? -1 : 1;
        int nx = s * m.m[0][a], ny = s * m.m[1][a], nz = s * m.m[2][a];
        int32_t px = d.x + HALF * nx, py = d.y + HALF * ny, pz = d.z + HALF * nz;
        if ((px - c.camX) / 16 * nx + (py - c.camY) / 16 * ny + (pz - camZ) / 16 * nz >= 0) continue;
        uint8_t t1 = (uint8_t)((a + 1) % 3), t2 = (uint8_t)((a + 2) % 3);
        uint8_t base = (uint8_t)(s > 0 ? 1 << a : 0);
        static const uint8_t CYC[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        int16_t q[8];
        uint8_t ks[4];
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t k = (uint8_t)(base | (CYC[i][0] << t1) | (CYC[i][1] << t2));
            ks[i] = k; q[2 * i] = sx[k]; q[2 * i + 1] = sy[k];
        }
        int lit = (nx * LX + ny * LY + nz * LZ) >> 8;
        if (lit > 110) fillConvex(q, 4, l.light);
        else if (lit > -20) { fillConvex(q, 4, l.dark); fillConvex(q, 4, l.light, 0); }
        else fillConvex(q, 4, l.dark);
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t k0 = ks[i], k1 = ks[(i + 1) & 3];
            gfx_line((sx[k0] + 8) >> 4, (sy[k0] + 8) >> 4, (sx[k1] + 8) >> 4, (sy[k1] + 8) >> 4, l.edge);
        }
        // Pips, if the face shows enough of itself: its area against a square edge.
        int32_t area = 0;
        for (uint8_t i = 0; i < 4; i++) {
            int j = (i + 1) & 3;
            area += (int32_t)q[2 * i] * q[2 * j + 1] - (int32_t)q[2 * j] * q[2 * i + 1];
        }
        if (area < 0) area = -area;
        area >>= 9;                                              // 1/256 px^2 x2 -> px^2
        int32_t full = (int32_t)edgePx * edgePx;
        if (area * 6 < full) continue;
        uint16_t mask = PIPS[d.label[f] <= 6 ? d.label[f] : 0];
        uint8_t pc = lit > -20 ? l.pip : l.pipDark;
        int r = (area * 3 < full || edgePx < 10) ? 0 : (edgePx >= 18 ? 2 : 1);
        for (uint8_t b = 0; b < 9; b++) {
            if (!(mask >> b & 1)) continue;
            int ca = b % 3 - 1, cb = b / 3 - 1;
            int32_t ox = (ca * m.m[0][t1] + cb * m.m[0][t2]) * PIP, oy = (ca * m.m[1][t1] + cb * m.m[1][t2]) * PIP,
                    oz = (ca * m.m[2][t1] + cb * m.m[2][t2]) * PIP;
            int16_t qx, qy;
            project(c, px + ox, py + oy, pz + oz, qx, qy);
            int x = (qx + 8) >> 4, y = (qy + 8) >> 4;
            if (r == 2) { gfx_hline(x - 1, y, 3, pc); gfx_vline(x, y - 1, 3, pc); }
            else if (r == 1) gfx_fillRect(x, y, 2, 2, pc);
            else gfx_pixel(x, y, pc);
        }
    }
}

void shadow(const Die &d, const Cam &c, uint8_t colour) {
    // The felt under the die, as wide as the die and shrinking as it rises:
    // project the ends of two floor diameters to get the ellipse.
    int32_t lift = d.y - (HALF << 8);
    if (lift < 0) lift = 0;
    int32_t r = (HALF * 3 / 2) * (256 - (lift >> 8 > 160 ? 160 : lift >> 8)) ;  // Q8
    int16_t x0, y0, x1, y1, x2, y2, x3, y3;
    if (!project(c, d.x - r, 0, d.z, x0, y0) || !project(c, d.x + r, 0, d.z, x1, y1) ||
        !project(c, d.x, 0, d.z - r, x2, y2) || !project(c, d.x, 0, d.z + r, x3, y3)) return;
    int rx = (x1 - x0) >> 5, ry = (y2 - y3) >> 5;
    if (ry < 1) ry = 1;
    gfx_fillEllipse((x0 + x1 + 16) >> 5, ((y2 + y3 + 16) >> 5) + 1, rx, ry, colour);
}
#endif

}  // namespace d3
