// SNAKES & LADDERS - the childhood board game, casino style, for the RPGame
// handheld (CH32X035, 128x128 ST7735, piezo): roll, hop, climb, get eaten.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
//
// The files, by role:
//   rules, no graphics (host-tested)  Layout (the ladders and snakes), Game, Cpu
//   the play screen                   Stage, BoardView (the board drawn), Fx
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
    rpgame.setFrameRate(CHSN_FPS);
}

void loop() {
    frame::run();
}
