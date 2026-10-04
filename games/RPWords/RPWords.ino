// CHWords - a casino crossword tile game for the RPGame handheld (CH32X035,
// 128x128 ST7735, piezo, microSD), in the look of CHBlackjack and its tables.
//
// The files, by role:
//   rules       Words (where tiles may go, what a play scores), Game (racks,
//               bag, turns, the end)
//   dictionary  FlashDict (the built-in list's decoder; the list itself is
//               generated, src/dict/DictData.*), Dict (the full list on the SD card)
//   CPU         Ai
//   screens     Screens (title, setup, play, options), Stage (the play screen:
//               board, rack, camera and the show a play puts on)
//   drawing     Font (the display face), Tiles (the tiles' serif letters),
//               Fx (particles and banners); the art is generated, src/assets/
//   sound       Sounds
//   saving      Save
//   main loop   Frame; config.h holds the build switches
#include <RPGame.h>
#include "config.h"

// Built for Tools > USB > Upload only: with USB Serial compiled in, the game
// is about 100 B over the flash. A debug build keeps Serial for the debug
// protocol and makes room with CHWD_LEAN instead.
#if defined(USE_CHGAME_USB_CDC) && !CHGAME_DEBUG
#error "CHWords needs Tools > USB > Upload only to fit in the flash (and Tools > Optimize > Smallest + LTO, the default)"
#endif
#include "Frame.h"

void setup() {
    rpgame.boot();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    rpgame.setFrameRate(CHWD_FPS);
}

void loop() {
    frame::run();
}
