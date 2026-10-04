/*
 * StlRender.cpp - see StlRender.h for the why. This file is plain C++ with
 * no Arduino dependencies apart from the framebuffer symbol, so the same
 * code also builds on a PC: in the simulator (on RPGfx's host framebuffer)
 * and in the accuracy test (tools/tests/accuracy.cpp, -DCHTEST, on its own).
 */
// The renderer's own speed setting: the sketch is built "Smallest + LTO",
// but this file is the frame time (its hottest loops also run from SRAM).
#pragma GCC optimize("O2")
#include "StlRender.h"
#include <string.h>
#include <rpgame/RamFunc.h>

#ifndef CHTEST
  #include <RPGfx.h>
  #define STL_FB gfx_fb
#else
  extern uint8_t stl_host_fb[];
  #define STL_FB stl_host_fb
#endif
/* Hot code runs from SRAM (the library's RAMFUNC): flash is 3 wait states
 * at 48 MHz and measured 2.2x slower on RPGfx's own inner loops. */

/* Host-side accuracy test (tools/sim): see every projected vertex. */
#ifdef STL_TEST_HOOK
void stlTestVertex(const uint8_t *p, int32_t xq4, int32_t yq4, int32_t z);
#endif

/* Camera distance, in the units where the bounding sphere has radius 2^14.
 * 3 radii is a moderate perspective: the near side of the model is drawn
 * about twice the size of the far side. */
#define CAM_D (3 << 14)

/* Screen radius of the bounding sphere at zoom 1, in pixels. The sphere's
 * silhouette under perspective is R*f/sqrt(D^2-R^2) = f/sqrt(8) for D = 3R,
 * so f = 60 * sqrt(8) ~= 170 keeps it inside a 128 px screen. */
#define FIT_PX      60
#define FOCAL_PX    170

/* ------------------------------------------------------------------------ */
/* Little helpers                                                            */
/* ------------------------------------------------------------------------ */
/* Every float in a record sits at an even offset (84 + 50k + 12 + 4j, and
 * blocks are 512-aligned), but only every other record is 4-byte aligned.
 * Two halfword loads are always legal. */
static inline uint32_t rd32(const uint8_t *p)
{
    const uint16_t *h = (const uint16_t *)p;
    return (uint32_t)h[0] | ((uint32_t)h[1] << 16);
}

/* A vertex's identity for edge matching: its raw bits, with -0.0 folded
 * into +0.0 so the two spellings of zero compare equal. */
static inline uint32_t vkey(uint32_t bits) { return bits == 0x80000000u ? 0 : bits; }

static inline bool keyLess(const uint32_t *a, const uint32_t *b)
{
    if (a[0] != b[0]) return a[0] < b[0];
    if (a[1] != b[1]) return a[1] < b[1];
    return a[2] < b[2];
}

/* Float bits -> int32 that sorts the same way the floats do. Its own inverse. */
static inline int32_t sortable(uint32_t bits)
{
    int32_t s = (int32_t)bits;
    return s ^ ((s >> 31) & 0x7FFFFFFF);
}

/* IEEE-754 single -> fixed point: value * 2^(150 + sh0), truncated towards
 * zero. The float is m * 2^(e - 150) with m the 24-bit mantissa, so this is
 * one shift. sh0 is chosen per model so the largest coordinate stays under
 * 2^29; a larger exponent can only be NaN/infinity and becomes 0. */
static inline int32_t conv(uint32_t bits, int sh0)
{
    const int e  = (int)((bits >> 23) & 0xFF);
    const int sh = e + sh0;
    if (e == 0 || sh <= -24 || sh > 6) return 0;
    const int32_t m = (int32_t)((bits & 0x7FFFFFu) | 0x800000u);
    const int32_t v = sh >= 0 ? (m << sh) : (m >> -sh);
    return (bits & 0x80000000u) ? -v : v;
}

static uint32_t isqrt32(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else              { r >>= 1; }
        bit >>= 2;
    }
    return r;
}

/* ------------------------------------------------------------------------ */
/* Sine                                                                      */
/* ------------------------------------------------------------------------ */
/* Quarter wave, 64 steps, Q14; linear interpolation between entries. */
static const int16_t kSinQ[65] = {
        0,   402,   804,  1205,  1606,  2006,  2404,  2801,  3196,  3590,  3981,  4370,  4756,
     5139,  5520,  5897,  6270,  6639,  7005,  7366,  7723,  8076,  8423,  8765,  9102,  9434,
     9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406, 12665, 12916, 13160,
    13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384,
};

static int32_t sinQuarter(uint32_t p)          /* p in [0, 0x4000] */
{
    const uint32_t i = p >> 8, f = p & 0xFF;
    if (i >= 64) return kSinQ[64];
    return kSinQ[i] + (((kSinQ[i + 1] - kSinQ[i]) * (int32_t)f) >> 8);
}

int32_t stlSin(uint16_t a)
{
    const uint32_t q = a >> 14, p = a & 0x3FFFu;
    const int32_t v = (q & 1) ? sinQuarter(0x4000u - p) : sinQuarter(p);
    return (q & 2) ? -v : v;
}

int32_t stlCos(uint16_t a) { return stlSin((uint16_t)(a + 0x4000u)); }

/* ------------------------------------------------------------------------ */
/* Record assembler                                                          */
/* ------------------------------------------------------------------------ */
void stlFeedBegin(StlFeed &f, uint32_t tris)
{
    f.pos  = 0;
    f.end  = STL_HEADER_BYTES + tris * STL_RECORD_BYTES;
    f.have = 0;
}

RAMFUNC(stlFeedBytes)
void stlFeedBytes(StlFeed &f, uint32_t off, const uint8_t *p, uint32_t n, StlRecordFn fn)
{
    if (off != f.pos) f.have = 0;                /* jumped: partial is stale */
    f.pos = off + n;

    uint32_t end = off + n;
    if (end > f.end) end = f.end;
    if (off < STL_HEADER_BYTES) {
        const uint32_t skip = STL_HEADER_BYTES - off;
        p += skip; off += skip;
    }
    if (off >= end) return;

    /* Where in a record this chunk starts. Continuing in order, that is
     * exactly how much of the record is already in rec[]. Anything else
     * means we arrived mid-record: skip to the next one. */
    const uint32_t within = (off - STL_HEADER_BYTES) % STL_RECORD_BYTES;
    if (within != f.have) {
        f.have = 0;
        const uint32_t skip = STL_RECORD_BYTES - within;
        if (off + skip >= end) return;
        p += skip; off += skip;
    }

    if (f.have) {
        const uint32_t need = STL_RECORD_BYTES - f.have;
        if (off + need > end) {
            memcpy(f.rec + f.have, p, end - off);
            f.have = (uint8_t)(f.have + (end - off));
            return;
        }
        memcpy(f.rec + f.have, p, need);
        fn(f.rec);
        f.have = 0;
        p += need; off += need;
    }

    while (off + STL_RECORD_BYTES <= end) {
        fn(p);
        p += STL_RECORD_BYTES; off += STL_RECORD_BYTES;
    }
    if (off < end) {
        memcpy(f.rec, p, end - off);
        f.have = (uint8_t)(end - off);
    }
}

/* ------------------------------------------------------------------------ */
/* Pass 1: scan                                                              */
/* ------------------------------------------------------------------------ */
static struct {
    uint32_t tris, bad;
    int32_t  kmin[3], kmax[3];      /* sortable() of the bounding box      */
    uint32_t maxExp;                /* largest biased exponent seen        */
    uint32_t edgeSum;
    uint32_t expect;                /* triangle count from the header      */
    uint32_t *sample;               /* raw float bits, 3 words per point   */
    uint16_t sampleMax, samples;
    uint32_t sampleAcc;
    uint8_t  sampleVert;
} S;

/* Depth range of the previous frame, for shading. Reset per model. */
static int32_t zLo, zHi;
static bool    zValid;

void stlScanBegin(uint32_t tris, uint32_t *sampleBuf, uint16_t maxSamples)
{
    memset(&S, 0, sizeof S);
    for (int i = 0; i < 3; i++) { S.kmin[i] = 0x7FFFFFFF; S.kmax[i] = (int32_t)0x80000000; }
    S.expect    = tris ? tris : 1;
    S.sample    = sampleBuf;
    S.sampleMax = sampleBuf ? maxSamples : 0;
    zValid = false;
}

/* FNV-1a over the six words, then a finaliser. It only has to make an
 * accidental zero sum unlikely, not be cryptographic. */
static uint32_t edgeHash(const uint32_t *a, const uint32_t *b)
{
    uint32_t h = 0x811C9DC5u;
    h = (h ^ a[0]) * 0x01000193u; h = (h ^ a[1]) * 0x01000193u; h = (h ^ a[2]) * 0x01000193u;
    h = (h ^ b[0]) * 0x01000193u; h = (h ^ b[1]) * 0x01000193u; h = (h ^ b[2]) * 0x01000193u;
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13;
    return h;
}

void stlScanRecord(const uint8_t *rec)
{
    uint32_t k[3][3];
    const uint8_t *p = rec + 12;
    S.tris++;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++, p += 4) {
            const uint32_t b = vkey(rd32(p));
            if (((b >> 23) & 0xFF) == 0xFF) { S.bad++; return; }
            k[i][j] = b;
        }
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            const uint32_t e = (k[i][j] >> 23) & 0xFF;
            if (e > S.maxExp) S.maxExp = e;
            const int32_t s = sortable(k[i][j]);
            if (s < S.kmin[j]) S.kmin[j] = s;
            if (s > S.kmax[j]) S.kmax[j] = s;
        }
    }
    /* Closed-mesh test: A->B adds hash(A,B), B->A subtracts it. */
    for (int i = 0; i < 3; i++) {
        const uint32_t *a = k[i], *b = k[i == 2 ? 0 : i + 1];
        if (keyLess(a, b))      S.edgeSum += edgeHash(a, b);
        else if (keyLess(b, a)) S.edgeSum -= edgeHash(b, a);
    }
    /* Draft points: sampleMax of them spread evenly through the file (a
     * Bresenham-style accumulator), taking each triangle's vertices in turn
     * so a small model still gets all of them. */
    S.sampleAcc += S.sampleMax;
    while (S.sampleAcc >= S.expect && S.samples < S.sampleMax) {
        S.sampleAcc -= S.expect;
        memcpy(S.sample + 3 * S.samples++, k[S.sampleVert], 12);
        S.sampleVert = (uint8_t)(S.sampleVert == 2 ? 0 : S.sampleVert + 1);
    }
}

bool stlScanEnd(StlModel &m, int8_t (*pts)[3], uint16_t *nPts)
{
    if (nPts) *nPts = 0;
    memset(&m, 0, sizeof m);
    m.tris    = S.tris;
    m.badTris = S.bad;
    if (S.tris == S.bad) return false;
    m.closed  = (S.edgeSum == 0);

    /* One shift for the whole model: the largest coordinate lands under
     * 2^29, leaving a bit of headroom for subtracting the centre. */
    const int maxExp = S.maxExp ? (int)S.maxExp : 127;
    const int sh = 5 - maxExp;

    int32_t lo[3], hi[3], c[3], h[3];
    for (int i = 0; i < 3; i++) {
        lo[i] = conv((uint32_t)sortable((uint32_t)S.kmin[i]), sh);
        hi[i] = conv((uint32_t)sortable((uint32_t)S.kmax[i]), sh);
        c[i]  = (lo[i] + hi[i]) >> 1;
        h[i]  = ((hi[i] - lo[i]) >> 1) + 1;

        /* Box size for the HUD, in file units x 1000. Fixed point is
         * value * 2^(155 - maxExp). */
        const int p = 155 - maxExp;
        uint64_t d = (uint64_t)(uint32_t)(hi[i] - lo[i]) * 1000u;
        if (p >= 63)     d = 0;
        else if (p >= 0) d >>= p;
        else             d = (-p >= 24) ? 0xFFFFFFFFu : (d << -p);
        m.sizeMilli[i] = d > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)d;
    }

    /* Bounding radius: half the box diagonal, at 15-bit precision. */
    int32_t hm = h[0] > h[1] ? h[0] : h[1];
    if (h[2] > hm) hm = h[2];
    int t = 0;
    while ((hm >> t) >= 32768) t++;
    uint32_t sum = 0;
    for (int i = 0; i < 3; i++) {
        const uint32_t q = (uint32_t)(h[i] >> t) + 1;
        sum += q * q;
    }
    const uint32_t R = (isqrt32(sum) + 1) << t;

    /* Scale down so the radius is 15 bits; fold that into the conversion
     * shift so each vertex is still one shift. */
    int n = 0;
    while ((R >> n) >= 32768) n++;
    uint32_t Rn = R >> n;
    if (Rn < 64) Rn = 64;      /* tiny model far from the origin: keep the
                                  transform's intermediate sums in range   */
    m.sh0 = (int16_t)(sh - n);
    for (int i = 0; i < 3; i++) m.c[i] = c[i] >> n;
    m.k = (int32_t)((1u << 28) / Rn);

    /* The box and the draft points, in the frame the renderer draws in:
     * centred, bounding radius 2^14. k is 2^28 / radius, so d * k fits. */
    for (int i = 0; i < 3; i++) {
        const int32_t b = (int32_t)(((int64_t)(h[i] >> n) * m.k) >> 14);
        m.box[i] = (int16_t)(b > 16384 ? 16384 : b);
    }
    if (pts && nPts) {
        for (uint16_t p = 0; p < S.samples; p++) {
            for (int i = 0; i < 3; i++) {
                const int32_t d = conv(S.sample[3 * p + i], m.sh0) - m.c[i];
                int32_t q = (int32_t)(((int64_t)d * m.k) >> 21);   /* radius -> 128 */
                if (q > 127) q = 127;
                if (q < -127) q = -127;
                pts[p][i] = (int8_t)q;
            }
        }
        *nPts = S.samples;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Pass 2: draw                                                              */
/* ------------------------------------------------------------------------ */
static struct {
    int32_t  M[3][3];          /* rotation * fit scale, Q14                 */
    int32_t  R[3][3];          /* rotation alone, Q14                       */
    int32_t  c0, c1, c2;
    int32_t  fq;               /* projection scale (see xform)              */
    int32_t  cxq, cyq;         /* screen centre, Q4                         */
    int32_t  zBase, zMul;      /* shading: shade = (zBase - z) * zMul >> 16 */
    int32_t  zmin, zmax;       /* this frame's depth range                  */
    int16_t  x0, y0, x1, y1;   /* drawn box                                 */
    uint32_t tris, edges, pixels;
    int16_t  sh0;
    uint8_t  mode;
    uint8_t  shadeMax;         /* brightest shade edge() may use            */
    bool     ortho, dedup, checkBad;
} F;

void stlFrameBegin(const StlModel &m, const StlView &v)
{
    const int32_t cy = stlCos(v.yaw),   sy = stlSin(v.yaw);
    const int32_t cp = stlCos(v.pitch), sp = stlSin(v.pitch);

    /* M = Rx(pitch) * B * Rz(yaw).  Rz spins the model about its own Z
     * (STL's up axis); B maps model (X, Y, Z) to camera (X, -Z, Y) so Z
     * points up the screen and Y into it; Rx tilts about the screen's
     * horizontal. Rows are camera x (right), y (down), z (away). */
    F.R[0][0] = cy;                  F.R[0][1] = -sy;                 F.R[0][2] = 0;
    F.R[1][0] = -((sp * sy) >> 14);  F.R[1][1] = -((sp * cy) >> 14);  F.R[1][2] = -cp;
    F.R[2][0] = (cp * sy) >> 14;     F.R[2][1] = (cp * cy) >> 14;     F.R[2][2] = -sp;

    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            F.M[i][j] = (int32_t)(((int64_t)F.R[i][j] * m.k) >> 14);

    F.c0 = m.c[0]; F.c1 = m.c[1]; F.c2 = m.c[2];
    F.sh0 = m.sh0;
    F.checkBad = m.badTris != 0;

    uint32_t zoom = v.zoomQ8;
    if (zoom < STL_ZOOM_MIN) zoom = STL_ZOOM_MIN;
    if (zoom > STL_ZOOM_MAX) zoom = STL_ZOOM_MAX;
    F.ortho = v.ortho;
    F.fq  = (int32_t)(((v.ortho ? FIT_PX : FOCAL_PX) * 16u * zoom) >> 8);
    F.cxq = (int32_t)v.cx << 4;
    F.cyq = (int32_t)v.cy << 4;

    F.mode  = v.mode;
    F.dedup = v.dedup;

    /* Shade over last frame's depth range, so the full ramp is used however
     * deep the model is from this angle. */
    int32_t lo = -(1 << 14), hi = 1 << 14;
    if (zValid) { lo = zLo; hi = zHi; }
    int32_t span = hi - lo;
    if (span < 256) span = 256;
    F.zBase = hi;
    F.zMul  = (int32_t)(((uint32_t)STL_SHADES << 16) / (uint32_t)span);

    F.shadeMax = STL_SHADES - 1;
    F.zmin = 0x7FFFFFFF; F.zmax = -0x7FFFFFFF;
    F.x0 = STL_W; F.y0 = STL_H; F.x1 = -1; F.y1 = -1;
    F.tris = F.edges = F.pixels = 0;
}

void stlFramePeek(StlFrameInfo &out)
{
    out.x0 = F.x0; out.y0 = F.y0; out.x1 = F.x1; out.y1 = F.y1;
    out.tris = F.tris; out.edges = F.edges; out.pixels = F.pixels;
}

void stlFrameEnd(StlFrameInfo &out)
{
    stlFramePeek(out);
    if (F.zmax >= F.zmin) { zLo = F.zmin; zHi = F.zmax; zValid = true; }
}

void stlRotation(int32_t out[3][3]) { memcpy(out, F.R, sizeof F.R); }

/* ---- lines --------------------------------------------------------------- */
static inline uint8_t outcode(int x, int y)
{
    return (uint8_t)((x < 0) | ((x > STL_W - 1) << 1) | ((y < 0) << 2) | ((y > STL_H - 1) << 3));
}

/* Cohen-Sutherland. Projected coordinates stay within a few thousand
 * pixels even at full zoom, so int maths cannot overflow here. */
static bool clipLine(int &x0, int &y0, int &x1, int &y1)
{
    uint8_t c0 = outcode(x0, y0), c1 = outcode(x1, y1);
    while (c0 | c1) {
        if (c0 & c1) return false;
        const uint8_t c = c0 ? c0 : c1;
        int x, y;
        if (c & 8)      { y = STL_H - 1; x = x0 + (x1 - x0) * (y - y0) / (y1 - y0); }
        else if (c & 4) { y = 0;         x = x0 + (x1 - x0) * (y - y0) / (y1 - y0); }
        else if (c & 2) { x = STL_W - 1; y = y0 + (y1 - y0) * (x - x0) / (x1 - x0); }
        else            { x = 0;         y = y0 + (y1 - y0) * (x - x0) / (x1 - x0); }
        if (c == c0) { x0 = x; y0 = y; c0 = outcode(x0, y0); }
        else         { x1 = x; y1 = y; c1 = outcode(x1, y1); }
    }
    return true;
}

/* Bresenham into the 4 bpp buffer, endpoints already on screen. A pixel is
 * only written if c is brighter than what is there (see header). */
RAMFUNC(stlLineMax)
static void lineMax(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    const uint8_t chi = (uint8_t)(c << 4);

    if (adx >= ady) {
        if (dx < 0) { x0 = x1; y0 = y1; dy = -dy; }       /* walk left to right */
        uint8_t *row = STL_FB + y0 * STL_STRIDE;
        const int step = dy < 0 ? -STL_STRIDE : STL_STRIDE;
        int err = adx >> 1, x = x0;
        for (int n = adx + 1; n; n--, x++) {
            uint8_t *p = row + (x >> 1);
            const uint8_t b = *p;
            if (x & 1) { if ((b >> 4)   < c) *p = (uint8_t)((b & 0x0F) | chi); }
            else       { if ((b & 0x0F) < c) *p = (uint8_t)((b & 0xF0) | c);   }
            err -= ady;
            if (err < 0) { err += adx; row += step; }
        }
    } else {
        if (dy < 0) { x0 = x1; y0 = y1; dx = -dx; }       /* walk top to bottom */
        uint8_t *row = STL_FB + y0 * STL_STRIDE;
        const int step = dx < 0 ? -1 : 1;
        int err = ady >> 1, x = x0;
        for (int n = ady + 1; n; n--, row += STL_STRIDE) {
            uint8_t *p = row + (x >> 1);
            const uint8_t b = *p;
            if (x & 1) { if ((b >> 4)   < c) *p = (uint8_t)((b & 0x0F) | chi); }
            else       { if ((b & 0x0F) < c) *p = (uint8_t)((b & 0xF0) | c);   }
            err -= adx;
            if (err < 0) { err += ady; x += step; }
        }
    }
}

void stlLine(int x0, int y0, int x1, int y1, uint8_t c)
{
    if (!clipLine(x0, y0, x1, y1)) return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        uint8_t *p = STL_FB + y0 * STL_STRIDE + (x0 >> 1);
        *p = (x0 & 1) ? (uint8_t)((*p & 0x0F) | (c << 4)) : (uint8_t)((*p & 0xF0) | (c & 0x0F));
        if (x0 == x1 && y0 == y1) break;
        const int e2 = err << 1;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* ---- vertices ------------------------------------------------------------ */
struct PV {
    int32_t x, y;              /* screen, Q4                                */
    int32_t z;                 /* depth, radius = 2^14, + is away           */
    uint32_t k[3];             /* identity, for matching shared edges       */
};

/* Camera space (bounding radius 2^14) to screen, Q4. */
static inline void project(int32_t rx, int32_t ry, int32_t rz, PV &o)
{
    if (F.ortho) {
        o.x = F.cxq + ((rx * F.fq) >> 14);
        o.y = F.cyq + ((ry * F.fq) >> 14);
    } else {
        /* One divide per vertex: 2^28 / depth, then two multiplies. The
         * depth is always 2..4 radii, so inv is 12-13 bits and every
         * product below stays inside 31 bits even at x16 zoom. */
        const int32_t inv = (int32_t)((1u << 28) / (uint32_t)(rz + CAM_D));
        o.x = F.cxq + ((((rx * inv) >> 12) * F.fq) >> 16);
        o.y = F.cyq + ((((ry * inv) >> 12) * F.fq) >> 16);
    }
    o.z = rz;
    if (rz < F.zmin) F.zmin = rz;
    if (rz > F.zmax) F.zmax = rz;
}

/* xform() and edge() are called from stlDrawRecord() three times each.
 * Left to itself the compiler inlines every call, and since that code runs
 * from SRAM the result was 3.3 KB of the 20 KB. Out of line they cost a
 * call each and about a tenth of the space. */
RAMFUNC(stlXform)
static void xform(const uint8_t *p, PV &o)
{
    const uint32_t bx = vkey(rd32(p)), by = vkey(rd32(p + 4)), bz = vkey(rd32(p + 8));
    o.k[0] = bx; o.k[1] = by; o.k[2] = bz;

    const int32_t dx = conv(bx, F.sh0) - F.c0;
    const int32_t dy = conv(by, F.sh0) - F.c1;
    const int32_t dz = conv(bz, F.sh0) - F.c2;
    const int32_t rx = (F.M[0][0] * dx + F.M[0][1] * dy + F.M[0][2] * dz) >> 14;
    const int32_t ry = (F.M[1][0] * dx + F.M[1][1] * dy + F.M[1][2] * dz) >> 14;
    const int32_t rz = (F.M[2][0] * dx + F.M[2][1] * dy + F.M[2][2] * dz) >> 14;
    project(rx, ry, rz, o);
#ifdef STL_TEST_HOOK
    stlTestVertex(p, o.x, o.y, rz);
#endif
}

static inline bool badRecord(const uint8_t *p)
{
    for (int i = 0; i < 9; i++, p += 4)
        if (((rd32(p) >> 23) & 0xFF) == 0xFF) return true;
    return false;
}

RAMFUNC(stlEdge)
static void edge(const PV &a, const PV &b)
{
    int x0 = (a.x + 8) >> 4, y0 = (a.y + 8) >> 4;
    int x1 = (b.x + 8) >> 4, y1 = (b.y + 8) >> 4;
    /* Almost every line is on screen at normal zoom: test that inline and
     * only call the (flash-resident) clipper for the rest. */
    if ((outcode(x0, y0) | outcode(x1, y1)) && !clipLine(x0, y0, x1, y1)) return;

    int32_t s = ((F.zBase - ((a.z + b.z) >> 1)) * F.zMul) >> 16;
    if (s < 0) s = 0;
    if (s > F.shadeMax) s = F.shadeMax;

    if (x0 < F.x0) F.x0 = (int16_t)x0;
    if (x0 > F.x1) F.x1 = (int16_t)x0;
    if (x1 < F.x0) F.x0 = (int16_t)x1;
    if (x1 > F.x1) F.x1 = (int16_t)x1;
    if (y0 < F.y0) F.y0 = (int16_t)y0;
    if (y0 > F.y1) F.y1 = (int16_t)y0;
    if (y1 < F.y0) F.y0 = (int16_t)y1;
    if (y1 > F.y1) F.y1 = (int16_t)y1;

    const int adx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int ady = y1 > y0 ? y1 - y0 : y0 - y1;
    F.edges++;
    F.pixels += (uint32_t)(adx > ady ? adx : ady) + 1;
    lineMax(x0, y0, x1, y1, (uint8_t)(STL_SHADE0 + s));
}

RAMFUNC(stlDrawRecord)
void stlDrawRecord(const uint8_t *rec)
{
    F.tris++;
    if (F.checkBad && badRecord(rec + 12)) return;

    PV v[3];
    xform(rec + 12, v[0]);
    xform(rec + 24, v[1]);
    xform(rec + 36, v[2]);

    bool all = !F.dedup;
    if (F.mode == STL_FRONT) {
        /* Winding on screen: y points down, so a triangle whose outward
         * normal faces the camera comes out with negative area. */
        const int64_t area = (int64_t)(v[1].x - v[0].x) * (v[2].y - v[0].y)
                           - (int64_t)(v[2].x - v[0].x) * (v[1].y - v[0].y);
        if (area > 0) return;
        all = true;
    }

    /* Closed mesh: each edge also appears reversed in its neighbour, so
     * drawing only the direction with the smaller key draws it once. */
    for (int i = 0; i < 3; i++) {
        const PV &a = v[i], &b = v[i == 2 ? 0 : i + 1];
        if (all || keyLess(a.k, b.k)) edge(a, b);
    }
}

/* ------------------------------------------------------------------------ */
/* Draft: the bounding box and the sampled points, all from RAM              */
/* ------------------------------------------------------------------------ */
void stlDrawDraft(const StlModel &m, const int8_t (*pts)[3], uint16_t n)
{
    /* Box first and dim, so the points read as the model inside it. Its
     * corners stay out of the depth range: that belongs to the model, and
     * the points are shaded across all of it. */
    const int32_t zmin = F.zmin, zmax = F.zmax;
    PV c[8];
    for (int i = 0; i < 8; i++) {
        const int32_t d0 = (i & 1) ? m.box[0] : -m.box[0];
        const int32_t d1 = (i & 2) ? m.box[1] : -m.box[1];
        const int32_t d2 = (i & 4) ? m.box[2] : -m.box[2];
        project((F.R[0][0] * d0 + F.R[0][1] * d1 + F.R[0][2] * d2) >> 14,
                (F.R[1][0] * d0 + F.R[1][1] * d1 + F.R[1][2] * d2) >> 14,
                (F.R[2][0] * d0 + F.R[2][1] * d1 + F.R[2][2] * d2) >> 14, c[i]);
    }
    F.zmin = zmin; F.zmax = zmax;
    static const uint8_t E[12][2] = {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
        { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };
    F.shadeMax = STL_SHADES / 3;
    for (int e = 0; e < 12; e++) edge(c[E[e][0]], c[E[e][1]]);
    F.shadeMax = STL_SHADES - 1;

    /* The points were quantised to radius = 128, so d = q << 7. Each is a
     * 2x2 dot: a single pixel is too faint to read as a surface. */
    for (uint16_t i = 0; i < n; i++) {
        const int32_t q0 = pts[i][0], q1 = pts[i][1], q2 = pts[i][2];
        PV v;
        project((F.R[0][0] * q0 + F.R[0][1] * q1 + F.R[0][2] * q2) >> 7,
                (F.R[1][0] * q0 + F.R[1][1] * q1 + F.R[1][2] * q2) >> 7,
                (F.R[2][0] * q0 + F.R[2][1] * q1 + F.R[2][2] * q2) >> 7, v);
        const int x = v.x >> 4, y = v.y >> 4;          /* top-left of the dot */
        if (x < 0 || y < 0 || x >= STL_W - 1 || y >= STL_H - 1) continue;
        int32_t s = ((F.zBase - v.z) * F.zMul) >> 16;
        if (s < 0) s = 0;
        if (s > STL_SHADES - 1) s = STL_SHADES - 1;
        const uint8_t col = (uint8_t)(STL_SHADE0 + s);
        lineMax(x, y, x + 1, y, col);
        lineMax(x, y + 1, x + 1, y + 1, col);
        if (x < F.x0) F.x0 = (int16_t)x;
        if (x + 1 > F.x1) F.x1 = (int16_t)(x + 1);
        if (y < F.y0) F.y0 = (int16_t)y;
        if (y + 1 > F.y1) F.y1 = (int16_t)(y + 1);
        F.pixels += 4;
    }
}
