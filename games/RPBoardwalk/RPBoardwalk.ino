// BOARDWALK - a casino property game for the RPGame handheld (CH32X035,
// 128x128 ST7735, piezo): roll, buy, build, and tap to outbid the table.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
//
// The files, by role:
//   rules, no graphics (host-tested)  Tiles (the board's data), Game, Cpu, Text
//   the play screen                   Stage, Iso (the board), Cards, MapView, Fx
//   the screens and the frame loop    Screens, Frame
//   sound, saving                     Sounds, Save
//   generated art (tools/assets.py)   src/assets/Assets.*
//   build switches                    config.h
#include <RPGame.h>
#include "config.h"
#include "Frame.h"

void setup() {
    rpgame.boot();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    rpgame.setFrameRate(CHBW_FPS);
}

void loop() {
    frame::run();
}
