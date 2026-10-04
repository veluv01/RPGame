// CHStlView - a wireframe STL viewer for the RPGame handheld (CH32X035 at
// 48 MHz, 20 KB of SRAM, a 128x128 ST7735 and a microSD card on one SPI bus).
//
// Pick a .STL file on the card and it spins in 3D as a depth-cued
// wireframe: near edges bright, far ones dim, the nearer wire winning where
// two cross. Nothing about the mesh is kept in RAM: every frame streams the
// file off the card and draws each triangle as it goes past (StlRender.h).
//
// The art style is "secret agent": a spy's wristwatch LCD, the instrument
// panel CHSDtoUSB has (Agent.h, shared), in CHStlView's own blue-teal: the
// chrome is cut from the same colours as the wires.
//
//   Card list   UP / DOWN      move (hold to repeat)
//               LEFT / RIGHT   a page up / down
//               A              open a model or folder
//               B              up a folder
//               START          read the card again
//   Viewer      D-pad          turn (stops the spin)
//               A / B (hold)   zoom in / out
//               A + B          reset the view
//               SELECT         X-RAY (every edge) / FRONT (edges of the faces towards you)
//               START          spin on / off
//               START (hold)   back to the list (held 3 s: the SD game menu)
//
// Binary STL, any size, in any folder. The card is read with RPGameSD, which
// sees 8.3 names, so "my_bracket.stl" is listed as "MY_BRA~1.STL".
//
// The files:
//   CHStlView.ino   setup, the frame loop, the palette, the debug commands
//   Browser.cpp     the intro, the card's folders, the refusals
//   Viewer.cpp      opening a model, drawing it, the HUD and the controls
//   StlRender.*     the renderer: scan, transform, clip, lines, draft
//   Card.*          the card through RPGameSD (DMA streaming of a file's blocks)
//   Agent.*         the secret agent chrome (shared with CHSDtoUSB)
//   Fx.*, Sounds.*  Sizzle (banners and shake, no particles) and the beeps
#include <RPGame.h>
#include "config.h"
#include "App.h"

// The palette (RGB444). 0-7: the secret agent chrome (Agent.h's roles),
// the glass first, then teals from the wires' own hue (CHSDtoUSB fills the
// same roles with greens); 8-15: the wire ramp, dim to bright. pal::init
// takes it as it is; FX_A/FX_B cycling is off, as those slots are wires.
const uint16_t PALETTE[16] = {
    0x001,          // BG     the glass: CHStlView's near-black blue
    0xCFF,          // PALE   the brightest text (near the nearest wire)
    0x024,          // GRID   rules, off segments, empty gauges
    0x379,          // MID    the selected tab, a gauge's fill
    0x5DE,          // LIVE   live readings: names, digits, brackets
    0x267,          // DIM    labels
    0xF32,          // ALERT  trouble (and the gizmo's X)
    0x013,          // PANEL  the status bar, the boxes
    // The wires, far to near: CHStlView's original blue ramp, in 8 steps.
    0x135, 0x156, 0x278, 0x389, 0x5AB, 0x7CC, 0xAEE, 0xDFF,
};

Scr screen = Scr::Intro;

#if CHGAME_DEBUG
// H: the state, for scripts. On the board (a debug build) it is also the
// way to read a model's frame time and card figures.
static bool debugHook(char cmd, const char *) {
    if (cmd != 'H') return false;
    char buf[112], *p = fmtStr(buf, "STATE ");
    if (screen == Scr::View) p = viewer::status(p);
    else p = fmtInt(fmtStr(p, "SCREEN "), (int32_t)screen);
    fmtStr(p, "\n");
    dbg::print(buf);
    return true;
}
#endif

void setup() {
    rpgame.boot();
    dbg::begin("STLV " STLV_VERSION);
    // 24 MHz SPI, 12 bpp: a quarter less panel traffic than 16 bpp, and the
    // palette has only 16 colours anyway.
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init(PALETTE);
    pal::setCycling(false);
    soundsBegin();
    rpgame.setFrameRate(STLV_FPS);
#if CHGAME_DEBUG
    dbg::hook = debugHook;
#endif
    browser::begin();
}

void loop() {
    dbg::poll();
    if (!rpgame.nextFrame()) return;
    rpgame.pollButtons();
    if (screen == Scr::View) viewer::frame();
    else browser::frame();
}
