// The watch's beeps: short, high and clean, as a spy's gadget should sound.
#pragma once
#include <RPGame.h>

enum class Sfx : uint8_t { Move, Open, Back, Mode, Ready, Deny, Spin, COUNT };

void soundsBegin();
