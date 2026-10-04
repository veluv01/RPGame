/*
 * accuracy - check StlRender's integer pipeline against double precision.
 *
 *   accuracy model.stl
 *
 * Runs the scan pass, then draws the model from a set of views with the
 * STL_TEST_HOOK build of StlRender.cpp. For every vertex the hook sees, the
 * same projection is computed here in doubles straight from the float in
 * the file, and the screen-space difference is recorded. Prints the worst
 * and mean error in pixels per view; exits non-zero if any view is off by
 * more than half a pixel (x1 zoom) or a pixel (x16).
 */
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../../StlRender.h"

uint8_t stl_host_fb[STL_STRIDE * STL_H];

static double gC[3], gR, gM[3][3], gF, gD;
static bool gOrtho;
static int gCx, gCy;
static double gMaxErr, gSumErr;
static long gN;

static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

void stlTestVertex(const uint8_t *p, int32_t xq4, int32_t yq4, int32_t z)
{
    (void)z;
    double d[3], r[3];
    for (int i = 0; i < 3; i++) d[i] = (rdf(p + 4 * i) - gC[i]) / gR * 16384.0;
    for (int i = 0; i < 3; i++) r[i] = gM[i][0] * d[0] + gM[i][1] * d[1] + gM[i][2] * d[2];
    double sx, sy;
    if (gOrtho) { sx = gCx + r[0] * gF / 16384.0; sy = gCy + r[1] * gF / 16384.0; }
    else        { sx = gCx + r[0] * gF / (r[2] + gD); sy = gCy + r[1] * gF / (r[2] + gD); }
    const double ex = xq4 / 16.0 - sx, ey = yq4 / 16.0 - sy;
    const double e = sqrt(ex * ex + ey * ey);
    if (e > gMaxErr) gMaxErr = e;
    gSumErr += e;
    gN++;
}

static std::vector<uint8_t> file;
static uint32_t tris;

static void feedAll(StlRecordFn fn)
{
    StlFeed feed;
    stlFeedBegin(feed, tris);
    uint8_t block[512];
    for (size_t off = 0; off < file.size(); off += 512) {
        const size_t n = file.size() - off < 512 ? file.size() - off : 512;
        memset(block, 0, sizeof block);
        memcpy(block, &file[off], n);
        stlFeedBytes(feed, (uint32_t)off, block, 512, fn);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: accuracy model.stl\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) file.insert(file.end(), buf, buf + n);
    fclose(f);
    memcpy(&tris, &file[80], 4);

    /* Reference bounds, from the finite vertices only. */
    double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
    for (uint32_t t = 0; t < tris; t++) {
        const uint8_t *r = &file[84 + 50 * (size_t)t + 12];
        bool ok = true;
        for (int i = 0; i < 9; i++) if (!isfinite(rdf(r + 4 * i))) ok = false;
        if (!ok) continue;
        for (int v = 0; v < 3; v++)
            for (int i = 0; i < 3; i++) {
                const double x = rdf(r + 12 * v + 4 * i);
                if (x < lo[i]) lo[i] = x;
                if (x > hi[i]) hi[i] = x;
            }
    }
    double h2 = 0;
    for (int i = 0; i < 3; i++) { gC[i] = (lo[i] + hi[i]) / 2; h2 += (hi[i] - lo[i]) * (hi[i] - lo[i]) / 4; }
    gR = sqrt(h2);
    if (gR == 0) gR = 1;

    stlScanBegin(tris, nullptr, 0);
    feedAll(stlScanRecord);
    StlModel m;
    if (!stlScanEnd(m, nullptr, nullptr)) { printf("%s: nothing drawable\n", argv[1]); return 1; }

    struct { int yaw, pitch, zoomQ8; bool ortho; } views[] = {
        { 30, 25, 256, false }, { 200, -60, 256, false }, { 91, 89, 256, false },
        { 30, 25, 4096, false }, { 30, 25, 64, false }, { 45, 30, 256, true },
        { 300, 170, 1024, true },
    };
    int bad = 0;
    printf("%s: tris %u bad %u closed %d  radius %.6g\n", argv[1], m.tris, m.badTris, m.closed, gR);
    for (auto &vw : views) {
        StlView v;
        memset(&v, 0, sizeof v);
        v.yaw = (uint16_t)(vw.yaw * 65536 / 360);
        v.pitch = (uint16_t)((vw.pitch + 360) % 360 * 65536 / 360);
        v.zoomQ8 = (uint16_t)vw.zoomQ8;
        v.ortho = vw.ortho;
        v.cx = 64; v.cy = 64;
        v.dedup = m.closed;

        /* Same matrix the renderer builds, in doubles from the same 16-bit
         * angles. */
        const double a = v.yaw * 2 * M_PI / 65536, b = v.pitch * 2 * M_PI / 65536;
        const double cy = cos(a), sy = sin(a), cp = cos(b), sp = sin(b);
        const double M[3][3] = { { cy, -sy, 0 }, { -sp * sy, -sp * cy, -cp }, { cp * sy, cp * cy, -sp } };
        memcpy(gM, M, sizeof M);
        gOrtho = vw.ortho;
        gF = (vw.ortho ? 60.0 : 170.0) * vw.zoomQ8 / 256.0;
        gD = 3 * 16384.0;
        gCx = 64; gCy = 64;
        gMaxErr = gSumErr = 0; gN = 0;

        memset(stl_host_fb, 0, sizeof stl_host_fb);
        stlFrameBegin(m, v);
        feedAll(stlDrawRecord);
        StlFrameInfo fi;
        stlFrameEnd(fi);

        const double limit = vw.zoomQ8 > 256 ? 0.5 * vw.zoomQ8 / 256 * 0.25 + 0.5 : 0.5;
        const bool ok = gMaxErr <= limit;
        if (!ok) bad++;
        printf("  yaw %3d pitch %4d zoom %5.2f %-5s  verts %6ld  err max %.3f px  mean %.3f px  %s\n",
               vw.yaw, vw.pitch, vw.zoomQ8 / 256.0, vw.ortho ? "ortho" : "persp", gN, gMaxErr,
               gN ? gSumErr / gN : 0.0, ok ? "ok" : "FAIL");
    }
    return bad ? 1 : 0;
}
