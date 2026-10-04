#pragma once
#include "rpgame.h"

enum GifResult : uint8_t {
    GIF_CANCELLED,   /* the user pressed something          */
    GIF_FINISHED,    /* the animation ran out its loop count */
    GIF_ERROR        /* could not open or could not parse    */
};

/* Plays `path` full screen, looping, until a button is pressed. Owns the
 * screen, the scratch arena, RPGfx's framebuffer and its chunk buffers for
 * the duration; the caller must redraw whatever was on screen afterwards. */
GifResult gifPlay(const char *path);

/* Why the last gifPlay() returned GIF_ERROR, for the on-screen message. */
extern const char *gifErrorText;
