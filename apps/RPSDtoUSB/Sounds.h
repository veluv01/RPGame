// The watch's beeps, one for each kind of event the screen reports: soft
// ticks for files, clear chirps for the card and the PC.
#pragma once
#include <RPGame.h>

enum class Sfx : uint8_t {
    FileNew, FileGone, FileTouch, CardIn, CardOut, Connect, Eject, Lock, Unlock, Big, Goal, Fail, Menu, COUNT
};

void soundsBegin();
