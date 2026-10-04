// The viewer: opening a model (the scan) and drawing it, every frame
// streamed off the card (StlRender.h has the why and the maths). Small
// models spin at tens of fps. Big ones would be a slideshow, so while you
// turn them the viewer shows their bounding box and a cloud of points
// sampled when the file was opened, drawn from RAM, and streams the full
// model again once they come to rest.
//
// The HUD is CHSDtoUSB's instrument panel (Agent.h): a status bar with the
// model's name and the frame rate (or the full render's time) in
// seven-segment digits, a gauge under it for how much of the model is on
// the glass, brackets locked onto the model, the modes as tabs along the
// bottom and an XYZ gizmo; a Sizzle banner pops up for a mode change.
#pragma GCC optimize("Os")
#include "App.h"
#include "Card.h"
#include <string.h>

using namespace agent;

namespace viewer {

// ---- Tuning ---------------------------------------------------------------------
static const uint32_t SPIN_DEG_PER_S = 40, TURN_DEG_PER_S = 120;
static const uint32_t ZOOM_MS_PER_2X = 1000;           // A/B: time to double / halve the zoom
static const uint32_t PITCH_DEFAULT_DEG = 25, YAW_DEFAULT_DEG = 30;
static const uint32_t HUD_MS = 3000, START_HOLD_MS = 500;
// Card reads come in pieces so a long render can check the buttons and show
// progress in between. Every piece costs one CMD18 (~0.6 ms of card
// latency), so full renders use big ones: 128 blocks is 64 KB, ~1300
// triangles.
static const uint32_t FULL_PIECE_BLOCKS = 128;
// A model whose full frame takes longer than SLOW_MS is "slow": while it is
// turned the viewer shows a draft instead (its bounding box and the
// DRAFT_POINTS points sampled by the scan, drawn from RAM at full frame
// rate) and streams the real thing when it comes to rest.
//
// Why not draw every Nth piece of the file instead? Tried: files are stored
// in whatever order the exporter walked the surface, so the pieces drawn
// are clumps (whole faces, rings of a tube) rather than a sketch of the
// shape, and each piece still costs ~0.6 ms of card latency.
static const uint32_t SLOW_MS = 100, PROGRESS_MS = 150;

// ---- The open model ---------------------------------------------------------------
static char name[13];
static uint32_t fileBlocks;
static StlModel model;
static StlView view;
static uint16_t pointCount;
static uint32_t zoomQ12;                // finer than view.zoomQ8, for a smooth zoom
static bool spin;

// Render scheduling.
static uint32_t fullMs;                 // what a full frame costs (estimated, then measured)
static bool needFull;                   // a full render is owed
static bool hudShown, bannerShown;      // what the last frame had on it
static uint32_t hudUntil, hintUntil, last;
// The viewer's clock, in ms: real time, but at least a frame period a
// frame (at most 100 ms, so a slow frame does not lurch). Turning, zooming,
// the spin and the HUD's timeouts run on it, so they behave alike on the
// board, where frames are paced at 60 fps, and in the simulator's
// lockstep, where a frame that draws little takes almost no time.
static uint32_t clock;
static int16_t shownX0, shownY0, shownX1, shownY1;   // the model's pixels on the glass

// Stats, over a second.
static uint32_t statStart, statFrames, statRenderUs;
static uint32_t shownFps;
static bool lastWasDraft, frameMoving;

static uint32_t startDownAt;
static bool startHoldFired;
static bool armed;                      // the buttons have all been let go since opening

static bool slowModel() { return fullMs > SLOW_MS; }

// ---- Streaming the file -------------------------------------------------------------
static struct {
    StlFeed feed;
    uint32_t off;                       // file offset of the next block
    StlRecordFn fn;
} stream;

// card::stream() calls this while the NEXT block is already arriving by
// DMA, so the drawing costs no time as long as it keeps up with the card
// (~170 us a block at 24 MHz).
static void onBlock(const uint8_t *blk, void *) {
    stlFeedBytes(stream.feed, stream.off, blk, 512, stream.fn);
    stream.off += 512;
}

enum : int8_t { STREAM_OK = 0, STREAM_IO = -1, STREAM_ABORT = -2 };

// The whole file through fn, FULL_PIECE_BLOCKS at a time. `between` runs
// after each piece, with the card idle and the bus free; false aborts.
static int8_t streamFile(StlRecordFn fn, bool (*between)(uint32_t done, uint32_t total)) {
    // Card data lands in RPGfx's chunk buffers and fn draws into the
    // framebuffer: both need the previous flush finished (card::stream waits).
    stlFeedBegin(stream.feed, model.tris);
    stream.fn = fn;
    stream.off = 0;
    for (uint32_t b = 0; b < fileBlocks; b += FULL_PIECE_BLOCKS) {
        uint32_t n = fileBlocks - b < FULL_PIECE_BLOCKS ? fileBlocks - b : FULL_PIECE_BLOCKS;
        if (!card::stream(b, n, onBlock, nullptr)) return STREAM_IO;
        if (between && !between(b + n, fileBlocks)) return STREAM_ABORT;
    }
    return STREAM_OK;
}

// ---- Opening a model -----------------------------------------------------------------
static uint32_t rd32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// The scan's status bar: ANALYSIS, the percentage done, and the gauge.
static void scanBar(uint32_t done, uint32_t total) {
    statusBar("ANALYSIS", LIVE);
    cubeIcon(2, 1, LIVE, PANEL);
    readout(total ? (uint32_t)((uint64_t)done * 100 / total) : 0, LIVE, "%", "");
    gauge(0, 12, 128, 2, done, total, LIVE, GRID);
}

static uint32_t scanShownAt;
static bool scanProgress(uint32_t done, uint32_t total) {
    uint32_t now = millis();
    if (now - scanShownAt >= 100 || done == total) {
        scanShownAt = now;
        scanBar(done, total);
        gfx_flushRect(0, 0, GFX_W, 14);
    }
    return true;
}

static void resetView() {
    view.yaw = (uint16_t)(YAW_DEFAULT_DEG * 65536 / 360);
    view.pitch = (uint16_t)(PITCH_DEFAULT_DEG * 65536 / 360);
    zoomQ12 = 256u << 4;
    view.zoomQ8 = 256;
    view.cx = GFX_W / 2;
    view.cy = GFX_H / 2;
}

static void enter() {
    screen = Scr::View;
    uint32_t now = millis();
    clock = 0;
    hudUntil = hintUntil = HUD_MS;
    hudShown = true;                    // the first frame repaints everything
    shownX0 = 0; shownY0 = 0; shownX1 = GFX_W - 1; shownY1 = GFX_H - 1;
    needFull = true;
    shownFps = 0;
    statStart = last = now;
    statFrames = statRenderUs = 0;
    startDownAt = 0;
    startHoldFired = false;
    armed = false;                      // the A that opened the model is not a zoom
    audio::sfx(Sfx::Open);
}

bool open(const char *fname, uint32_t cluster, uint32_t size) {
    fat::File f = {cluster, size};
    int8_t runs = card::openFile(f);
    if (runs == fat::E_FRAG) { browser::message("FRAGMENTED", "Copy it onto a", "fresh card"); return false; }
    if (runs < 0) { browser::message("CARD ERROR", fname, "broken FAT chain"); return false; }
    if (size < STL_HEADER_BYTES) { browser::message("NOT AN STL", fname, "file too short"); return false; }
    const uint8_t *hdr = card::block(0);
    if (!hdr) { browser::message("CARD ERROR", fname, "while reading"); return false; }
    // Binary if the count in the header accounts for the file. Some
    // exporters start a binary header with "solid" too, so the ASCII test
    // only applies when the count does not fit.
    uint32_t tris = rd32le(hdr + 80);
    uint64_t expect = STL_HEADER_BYTES + (uint64_t)tris * STL_RECORD_BYTES;
    if (expect > size) {
        if (memcmp(hdr, "solid", 5) == 0) browser::message("ASCII STL", "Re-export it as", "binary STL");
        else browser::message("NOT AN STL", fname, "size doesn't match");
        return false;
    }
    if (!tris) { browser::message("EMPTY STL", fname, "0 triangles"); return false; }
    fileBlocks = (uint32_t)((expect + 511) >> 9);

    // ---- pass 1: bounds, closed-mesh test, draft points --------------------
    // The screen below SAMPLE_ROW is not flushed during the scan, so that
    // part of the framebuffer holds the sampled points' raw floats (12 bytes
    // each) until stlScanEnd() packs them into the arena.
    const int SAMPLE_ROW = GFX_H - (DRAFT_POINTS * 12) / GFX_FB_STRIDE;
    gfx_wait();
    pal::commit();
    gfx_clear(BG);
    scanBar(0, 1);
    gfx_fillRect(6, 22, 116, 52, PANEL);
    brackets(6, 22, 116, 52, 5, LIVE);
    bool big = gfx_textWidthScaled(fname, 2) <= 108;
    if (big) outlined(28, fname, LIVE);
    else centred(32, fname, LIVE);
    char line[24];
    fmtStr(putCount(line, tris), " TRIANGLES");
    centred(48, line, PALE);
    centred(60, "SCANNING", DIM);
    gfx_flush();

    model.tris = tris;                  // streamFile() sizes the feed from it
    scanShownAt = millis();
    uint32_t t0 = millis();
    stlScanBegin(tris, (uint32_t *)(gfx_fb + SAMPLE_ROW * GFX_FB_STRIDE), DRAFT_POINTS);
    if (streamFile(stlScanRecord, scanProgress) != STREAM_OK) {
        browser::message("CARD ERROR", fname, "while reading");
        return false;
    }
    uint32_t scanMs = millis() - t0;
    // From here on the file list is gone: the arena holds the points.
    if (!stlScanEnd(model, arena.points, &pointCount)) {
        browser::message("NO SHAPE", fname, "all NaN or infinite");
        return false;
    }
    strncpy(name, fname, sizeof name - 1);

    // A full frame costs about what the scan did (both are card-bound),
    // plus the panel transfer; refined from every full frame from here on.
    fullMs = scanMs + 8;
    resetView();
    view.mode = STL_XRAY;
    view.ortho = false;
    view.dedup = model.closed;
    // Small models open spinning. A big one opens still, so its first,
    // full render is what you see; turn it and it drops to the draft.
    spin = !slowModel();
    fx::clear();
    enter();
    return true;
}

// ---- Drawing -------------------------------------------------------------------------
// XYZ gizmo: the model's axes under the current rotation, far one first.
static void drawGizmo(int cx, int cy, int len) {
    int32_t R[3][3];
    stlRotation(R);
    static const uint8_t col[3] = {AXIS_X, AXIS_Y, AXIS_Z};
    uint8_t order[3] = {0, 1, 2};
    for (int i = 0; i < 2; i++)                  // sort by depth, far first
        for (int j = 0; j < 2 - i; j++)
            if (R[2][order[j]] < R[2][order[j + 1]]) {
                uint8_t t = order[j]; order[j] = order[j + 1]; order[j + 1] = t;
            }
    for (int i = 0; i < 3; i++) {
        int a = order[i];
        stlLine(cx, cy, cx + ((R[0][a] * len) >> 14), cy + ((R[1][a] * len) >> 14), col[a]);
    }
}

// Thousandths -> "49.9" below 100, "123" above.
static char *putMilli(char *p, uint32_t m) {
    if (m >= 100000) return fmtInt(p, (int32_t)((m + 500) / 1000));
    uint32_t t = (m + 50) / 100;
    p = fmtInt(p, (int32_t)(t / 10));
    *p++ = '.';
    *p++ = (char)('0' + t % 10);
    *p = 0;
    return p;
}

static void drawHud(const StlFrameInfo &fi) {
    char line[24], *p;
    // Brackets locked onto the model.
    if (fi.x1 >= fi.x0) {
        int x0 = fi.x0 - 3, y0 = fi.y0 - 3, x1 = fi.x1 + 3, y1 = fi.y1 + 3;
        if (x0 < 1) x0 = 1;
        if (y0 < 20) y0 = 20;
        if (x1 > 126) x1 = 126;
        if (y1 > 116) y1 = 116;
        if (x1 - x0 > 10 && y1 - y0 > 10) brackets(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 4, MID);
    }
    // The status bar: the name, and the frame rate while it moves; at rest
    // the time the full render on screen took says more. Under it the
    // gauge (how much of the model is drawn: none for the draft), then the
    // size of the box and the triangles.
    statusBar(name, LIVE);
    cubeIcon(2, 1, LIVE, frameMoving ? MID : PANEL);
    if (frameMoving && shownFps >= 3) readout(shownFps, LIVE, "FR", "/S");
    else readout(fullMs, LIVE, "MS", "");
    gauge(0, 12, 128, 2, lastWasDraft ? 0 : 1, 1, MID);
    gfx_fillRect(0, 14, GFX_W, 8, BG);
    p = putMilli(line, model.sizeMilli[0]); *p++ = 'x';
    p = putMilli(p, model.sizeMilli[1]); *p++ = 'x';
    putMilli(p, model.sizeMilli[2]);
    tiny(0, 16, line, DIM);
    if (!model.closed) tinyR(127, 16, "OPEN", ALERT);
    else {
        fmtStr(putCount(line, model.tris), " TRI");
        tinyR(127, 16, line, DIM);
    }

    // Bottom: the modes as tabs (DRAFT while a slow model turns) and the
    // zoom, or the controls just after opening.
    gfx_fillRect(0, GFX_H - 12, GFX_W, 12, BG);
    gfx_hline(0, GFX_H - 12, GFX_W, GRID);
    if ((int32_t)(hintUntil - clock) > 0) {
        keys(GFX_H - 8, "START:SPIN HOLD:LIST");
    } else {
        static const char *const MODES[3] = {"X-RAY", "FRONT", "DRAFT"};
        tabs(GFX_H - 7, MODES, lastWasDraft ? 3 : 2, lastWasDraft ? 2 : view.mode);
        p = fmtInt(line, view.zoomQ8 * 10 / 256 / 10); *p++ = '.';
        p = fmtInt(p, view.zoomQ8 * 10 / 256 % 10);
        fmtStr(p, "X");
        gfx_fillRect(127 - tinyW(line) - 2, GFX_H - 8, tinyW(line) + 3, 7, BG);
        tinyR(127, GFX_H - 7, line, PALE);
    }
    drawGizmo(GFX_W - 9, GFX_H - 23, 7);
}

// Slow full render: between pieces, give up if a button went down (the
// loop starts over with the new view) and every PROGRESS_MS put what is
// drawn so far on the glass, so a big model visibly builds up.
static bool flushed;                    // this frame put something on the glass
static uint32_t progressAt;
static bool renderProgress(uint32_t done, uint32_t total) {
    if (armed && (rpgame_readButtons() | rpgame.injected)) return false;
    uint32_t now = millis();
    if (now - progressAt < PROGRESS_MS) return true;
    progressAt = now;
    StlFrameInfo fi;
    stlFramePeek(fi);
    // The gauge under the status bar: how much of the model is drawn.
    gauge(0, 12, 128, 2, done, total, LIVE, GRID);
    int16_t x0 = shownX0 < fi.x0 ? shownX0 : fi.x0, y0 = shownY0 < fi.y0 ? shownY0 : fi.y0;
    int16_t x1 = shownX1 > fi.x1 ? shownX1 : fi.x1, y1 = shownY1 > fi.y1 ? shownY1 : fi.y1;
    if (x1 >= x0) gfx_flushRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    gfx_flushRect(0, 12, GFX_W, 2);
    shownX0 = x0; shownY0 = y0; shownX1 = x1; shownY1 = y1;
    return true;
}

// One frame. False if a full render was cut short by input.
static bool render(bool draft, bool hud) {
    gfx_wait();                         // the previous frame off the bus
    pal::commit();
    uint32_t t0 = micros();
    gfx_clear(BG);
    stlFrameBegin(model, view);
    int8_t r = STREAM_OK;
    if (draft) {
        stlDrawDraft(model, arena.points, pointCount);
    } else {
        progressAt = millis();
        r = streamFile(stlDrawRecord, slowModel() ? renderProgress : nullptr);
    }
    StlFrameInfo fi;
    stlFrameEnd(fi);
    uint32_t us = micros() - t0;

    if (r == STREAM_IO) {
        browser::message("CARD ERROR", name, "while reading");
        return true;
    }
    if (r == STREAM_ABORT) return false;
    // Learn what a full frame really costs (+ a typical flush): that decides
    // between streaming and the draft while moving.
    if (!draft) fullMs = us / 1000 + 4;
    lastWasDraft = draft;

    if (hud) drawHud(fi);
    bool banner = fx::bannerActive();
    fx::drawBanner();

    // Flush what changed: the whole screen while the HUD or a banner is up
    // or has just gone, otherwise the model's box now and where it was.
    if (hud || hudShown || banner || bannerShown) {
        gfx_flushAsync();
        flushed = true;
    } else {
        int16_t x0 = shownX0 < fi.x0 ? shownX0 : fi.x0, y0 = shownY0 < fi.y0 ? shownY0 : fi.y0;
        int16_t x1 = shownX1 > fi.x1 ? shownX1 : fi.x1, y1 = shownY1 > fi.y1 ? shownY1 : fi.y1;
        if (x1 >= x0) { gfx_flushRectAsync(x0, y0, x1 - x0 + 1, y1 - y0 + 1); flushed = true; }
    }
    shownX0 = fi.x0; shownY0 = fi.y0; shownX1 = fi.x1; shownY1 = fi.y1;
    hudShown = hud;
    bannerShown = banner;
    statFrames++;
    statRenderUs += us;
    return true;
}

// ---- Input and scheduling ------------------------------------------------------------
static void stats(uint32_t now) {
    if (now - statStart < 1000) return;
    uint32_t dt = now - statStart;
    shownFps = (statFrames * 1000 + dt / 2) / dt;
    if (shownFps > STLV_FPS) shownFps = STLV_FPS;   // (the simulator's lockstep is not paced)
    statStart = now;
    statFrames = statRenderUs = 0;
}

static void modeBanner(const char *s) {
    fx::banner(s, fx::B_GREEN, 64, 50);
    audio::sfx(Sfx::Mode);
}

// The buttons. False: back to the list.
static bool input(uint32_t now, uint32_t dt, bool &moving) {
    // START: a tap toggles the spin, a hold goes back to the list.
    if (rpgame.justPressed(START_BUTTON)) { startDownAt = now; startHoldFired = false; }
    if (startDownAt && rpgame.pressed(START_BUTTON) && !startHoldFired && now - startDownAt >= START_HOLD_MS) {
        startHoldFired = true;
        fx::clear();
        audio::sfx(Sfx::Back);
        browser::back();
        return false;
    }
    if (rpgame.justReleased(START_BUTTON)) {
        if (!startHoldFired && startDownAt) {
            spin = !spin;
            needFull = true;
            audio::sfx(Sfx::Spin);
        }
        startDownAt = 0;
    }
    if (rpgame.justPressed(SELECT_BUTTON)) {
        view.mode = (uint8_t)((view.mode + 1) % STL_MODES);
        modeBanner(view.mode == STL_FRONT ? "FRONT" : "X-RAY");
        needFull = true;
    }

    // D-pad: rotate.
    int32_t turn = (int32_t)(TURN_DEG_PER_S * 65536u / 360u * dt / 1000u);
    bool l = rpgame.pressed(LEFT_BUTTON), r = rpgame.pressed(RIGHT_BUTTON);
    bool u = rpgame.pressed(UP_BUTTON), d = rpgame.pressed(DOWN_BUTTON);
    if (l || r || u || d) {
        spin = false;
        moving = true;
        if (l) view.yaw = (uint16_t)(view.yaw - turn);
        if (r) view.yaw = (uint16_t)(view.yaw + turn);
        if (u) view.pitch = (uint16_t)(view.pitch + turn);
        if (d) view.pitch = (uint16_t)(view.pitch - turn);
    }

    // A/B: zoom; both: reset.
    bool a = rpgame.pressed(A_BUTTON), b = rpgame.pressed(B_BUTTON);
    if (a && b) {
        if (rpgame.justPressed(A_BUTTON | B_BUTTON)) {
            resetView();
            modeBanner("RESET");
            needFull = true;
        }
    } else if (a || b) {
        // zoom *= 2^(dt / ZOOM_MS_PER_2X), linearised per step: ln 2 =
        // 710/1024. Divide before the multiply: zoomQ12 * dt * 710 would
        // pass 2^32.
        uint32_t step = (zoomQ12 * dt / ZOOM_MS_PER_2X * 710u) >> 10;
        if (a) zoomQ12 += step ? step : 1;
        else zoomQ12 -= step ? step : 1;
        if (zoomQ12 < (uint32_t)STL_ZOOM_MIN << 4) zoomQ12 = (uint32_t)STL_ZOOM_MIN << 4;
        if (zoomQ12 > (uint32_t)STL_ZOOM_MAX << 4) zoomQ12 = (uint32_t)STL_ZOOM_MAX << 4;
        view.zoomQ8 = (uint16_t)(zoomQ12 >> 4);
        moving = true;
    }
    return true;
}

void frame() {
    uint32_t real = millis(), dt = real - last;
    last = real;
    if (dt < 1000 / STLV_FPS) dt = 1000 / STLV_FPS;
    if (dt > 100) dt = 100;             // after a slow frame, don't lurch
    uint32_t now = clock += dt;

    if (rpgame.buttons()) hudUntil = now + HUD_MS;
    bool moving = false;

    if (!armed) armed = !rpgame.buttons();
    if (armed && !input(now, dt, moving)) return;
    if (spin) {
        view.yaw = (uint16_t)(view.yaw + SPIN_DEG_PER_S * 65536u / 360u * dt / 1000u);
        moving = true;
    }

    fx::update();
    pal::tick();
    audio::update();

    // A banner moves, so it is drawn over a moving model (the draft, for a
    // slow one) and a full render follows it. The HUD stays up on a slow
    // model at rest: taking it down would mean streaming the whole model
    // again just to erase some text.
    bool banner = fx::bannerActive();
    if (banner) moving = true;
    if (bannerShown && !banner) needFull = true;
    bool hud = (int32_t)(hudUntil - now) > 0 || (slowModel() && !moving);
    if (hud != hudShown) needFull = true;

    frameMoving = moving;
    flushed = false;
    if (moving) {
        bool draft = slowModel();
        if (render(draft, hud)) needFull = draft;
    } else if (needFull) {
        if (render(false, hud)) needFull = false;
    }
    // Otherwise nothing changed: the glass is left alone.
#ifdef CHSIM
    // (The simulator's lockstep waits for its driver once a few frames go
    // by without a flush; a model at rest has none, nor a render cut short,
    // so give it one.)
    if (!flushed && screen == Scr::View) { gfx_wait(); gfx_flushRectAsync(0, 0, 2, 1); }
#endif
    stats(millis());
}

// The debug protocol's H command: what the viewer is showing.
char *status(char *p) {
    p = fmtStr(p, "VIEW ");
    p = fmtStr(p, name);
    p = fmtInt(fmtStr(p, " tris="), (int32_t)model.tris);
    p = fmtInt(fmtStr(p, " closed="), model.closed);
    p = fmtInt(fmtStr(p, " runs="), card::runCount());
    p = fmtInt(fmtStr(p, " blocks="), (int32_t)fileBlocks);
    p = fmtInt(fmtStr(p, " full_ms="), (int32_t)fullMs);
    p = fmtInt(fmtStr(p, " fps="), (int32_t)shownFps);
    p = fmtInt(fmtStr(p, " mode="), view.mode);
    p = fmtInt(fmtStr(p, " draft="), lastWasDraft);
    return fmtInt(fmtStr(p, " zoom="), view.zoomQ8);
}

}  // namespace viewer
