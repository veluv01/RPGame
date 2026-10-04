/*
 * stlsim - run CHStlView's renderer (src/StlRender.cpp, unchanged) on a PC.
 *
 * Feeds an STL file through the same scan and draw passes the device uses,
 * 512 bytes at a time the way the card delivers it, and writes each frame's
 * raw 4 bpp framebuffer (8192 bytes) to an output file. tools/preview.py
 * turns that into a PNG / animated GIF with the device palette.
 *
 *   stlsim model.stl out.fb [frames] [mode] [pitch_deg] [zoomQ8] [draft]
 *
 * draft = 1 draws what the device shows while turning a big model: the
 * bounding box and the points sampled during the scan.
 *
 * Prints the model summary and per-frame counts (edges, pixels) - the
 * numbers that set the frame time on the device.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../../StlRender.h"

uint8_t stl_host_fb[STL_STRIDE * STL_H];

/* As in CHStlView.ino. */
#define DRAFT_POINTS 256

static void feedFile(const std::vector<uint8_t> &file, uint32_t tris, StlRecordFn fn)
{
    StlFeed feed;
    stlFeedBegin(feed, tris);
    uint8_t block[512];
    const uint32_t blocks = (uint32_t)((file.size() + 511) / 512);
    for (uint32_t b = 0; b < blocks; b++) {
        const size_t off = (size_t)b * 512;
        const size_t n = file.size() - off < 512 ? file.size() - off : 512;
        memset(block, 0xA5, sizeof block);       /* past EOF: garbage, as on a card */
        memcpy(block, &file[off], n);
        stlFeedBytes(feed, (uint32_t)off, block, 512, fn);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: stlsim model.stl out.fb [frames] [mode] [pitch_deg] [zoomQ8] [draft]\n");
        return 2;
    }
    const int frames = argc > 3 ? atoi(argv[3]) : 1;
    const int mode   = argc > 4 ? atoi(argv[4]) : STL_XRAY;
    const int pitchD = argc > 5 ? atoi(argv[5]) : 25;
    const int zoomQ8 = argc > 6 ? atoi(argv[6]) : 256;
    const bool draft = argc > 7 && atoi(argv[7]) != 0;

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    std::vector<uint8_t> file;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) file.insert(file.end(), buf, buf + n);
    fclose(f);

    if (file.size() < STL_HEADER_BYTES) { fprintf(stderr, "too short\n"); return 1; }
    uint32_t tris;
    memcpy(&tris, &file[80], 4);
    if (STL_HEADER_BYTES + (uint64_t)tris * STL_RECORD_BYTES > file.size()) {
        fprintf(stderr, "not a binary STL (count %u, size %zu)\n", tris, file.size());
        return 1;
    }

    /* The device samples into the framebuffer's lower rows during the scan;
     * any scratch will do here. */
    static uint32_t sampleRaw[DRAFT_POINTS * 3];
    static int8_t pts[DRAFT_POINTS][3];
    uint16_t nPts = 0;
    stlScanBegin(tris, sampleRaw, DRAFT_POINTS);
    feedFile(file, tris, stlScanRecord);
    StlModel m;
    if (!stlScanEnd(m, pts, &nPts)) { fprintf(stderr, "nothing drawable\n"); return 1; }
    printf("tris %u  bad %u  closed %d  size %.3f x %.3f x %.3f  box %d %d %d  points %u\n",
           m.tris, m.badTris, m.closed,
           m.sizeMilli[0] / 1000.0, m.sizeMilli[1] / 1000.0, m.sizeMilli[2] / 1000.0,
           m.box[0], m.box[1], m.box[2], nPts);

    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); return 1; }

    StlView v;
    memset(&v, 0, sizeof v);
    v.pitch  = (uint16_t)(pitchD * 65536 / 360);
    v.zoomQ8 = (uint16_t)zoomQ8;
    v.mode   = (uint8_t)mode;
    v.dedup  = m.closed;
    v.cx = STL_W / 2; v.cy = STL_H / 2;

    unsigned long long totEdges = 0, totPx = 0;
    for (int i = 0; i < frames + 1; i++) {
        /* Frame 0 is thrown away: it primes the depth range for shading,
         * as the device's first frame does. */
        const int fi = i ? i - 1 : 0;
        v.yaw = (uint16_t)(30 * 65536 / 360 + fi * 65536 / (frames > 0 ? frames : 1));
        memset(stl_host_fb, 0, sizeof stl_host_fb);
        stlFrameBegin(m, v);
        if (draft) stlDrawDraft(m, pts, nPts);
        else       feedFile(file, tris, stlDrawRecord);
        StlFrameInfo info;
        stlFrameEnd(info);
        if (!i) continue;
        totEdges += info.edges; totPx += info.pixels;
        if (i == 1 || frames <= 4)
            printf("frame %d: tris %u edges %u pixels %u box %d,%d..%d,%d\n", fi, info.tris,
                   info.edges, info.pixels, info.x0, info.y0, info.x1, info.y1);
        fwrite(stl_host_fb, 1, sizeof stl_host_fb, out);
    }
    fclose(out);
    if (frames)
        printf("avg edges %llu  avg pixels %llu\n", totEdges / frames, totPx / frames);
    return 0;
}
