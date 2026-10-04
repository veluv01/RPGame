#include "RPGame.h"
#ifdef CHSIM
#include <stdlib.h>
#else
#include "pico/bootrom.h"
#endif
extern "C" void rpgame_enter_bootloader() {
#ifdef CHSIM
    exit(0);
#else
    audio::setOn(false);
    gfx_wait();
    reset_usb_boot(0, 0);
    for (;;) tight_loop_contents();
#endif
}
