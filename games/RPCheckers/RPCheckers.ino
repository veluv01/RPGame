// CHCheckers - isometric casino checkers for the RPGame handheld (CH32X035,
// 128x128 ST7735, piezo): CHChess's board, camera and glove, with chips.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
// The same frame runs from inside the CPU's search (see Frame.h), so the
// game keeps moving while the engine thinks.
//
// The files, by role:
//   rules        Engine (moves, house rules, the CPU's search), Match (turns, undo,
//                events): no drawing or sound, host-tested
//   frame        Frame (input, logic, drawing, flush; also run mid-search)
//   screens      Screens: title, setup, play, options, house rules
//   presenting   Stage (camera, gloves, chips in motion, HUD), Fx (sparkle)
//   drawing      Iso (the isometric board, the flat map, the table)
//   sound        Sounds (the effects and the title's tune)
//   saving       Save
//   generated    src/assets (art: do not edit), src/states/DemoLine.h (the title's demo game)
//   switches     config.h
#include <RPGame.h>
#include "config.h"
#include "Frame.h"

void setup() {
    rpgame.boot();
    dbg::begin("CHCK " CHCK_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    rpgame.setFrameRate(CHCK_FPS);
}

void loop() {
    frame::run(false);
}
