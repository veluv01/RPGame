// Sprite colour remaps (art colour -> screen colour) for sprite4. One
// sprite gives several looks; RM_ID (as drawn) is the RPGame library's.
#pragma once
#include <stdint.h>

extern const uint8_t RM_CPU[16];     // the croupier's glove: red cuff (CHChess's CPU glove)
extern const uint8_t RM_ALERT[16];   // the glove flashing red: refused
extern const uint8_t RM_HOT[16];     // a piece with a cycling outline (the winning line, the next to vanish)
extern const uint8_t RM_BLUE[16];    // GOBBLE: the dealer's chips
extern const uint8_t RM_BLUEHOT[16];
extern const uint8_t RM_SHADE[16];   // a piece in the shadow of one held over it
extern const uint8_t RM_BLUESHADE[16];
