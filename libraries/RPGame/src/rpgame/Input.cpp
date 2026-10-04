#include <Arduino.h>
#include "Input.h"
#include "Audio.h"

RPGame rpgame;

#ifndef CHSIM
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
uint8_t rpgame_readButtons() {
    const int pins[] = {PIN_BTN_A, PIN_BTN_B, PIN_BTN_UP, PIN_BTN_DOWN,
        PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_START, PIN_BTN_SELECT};
    uint8_t value = 0;
    for (unsigned i = 0; i < 8; ++i) if (digitalRead(pins[i]) == LOW) value |= uint8_t(1u << i);
    return value;
}
#endif

void RPGame::boot() {
#ifndef CHSIM
    const int pins[] = {PIN_BTN_A, PIN_BTN_B, PIN_BTN_UP, PIN_BTN_DOWN,
        PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_START, PIN_BTN_SELECT};
    for (int pin : pins) pinMode(pin, INPUT_PULLUP);
#endif
    cur = prev = rpgame_readButtons();
}

void RPGame::setFrameRate(uint8_t fps) {
    period = 1000000u / (fps ? fps : 1);
    next = micros() + period;
}

bool RPGame::nextFrame() {
    if (lockstep >= 0) {
        if (lockstep == 0) return false;
        lockstep--;
        frameCount++;
        return true;
    }
    uint32_t now = micros();
    if ((int32_t)(now - next) < 0) return false;
    next += period;
    // Fell far behind (debug pause, a save): resync instead of sprinting.
    if ((int32_t)(now - next) > (int32_t)(3 * period)) next = now + period;
    frameCount++;
    return true;
}

void RPGame::pollButtons() {
    prev = cur;
    cur = (uint8_t)(rpgame_readButtons() | injected);
    for (uint8_t i = 0; i < 8; i++) {
        if (cur & (1u << i)) { if (held[i] < 0xFFFF) held[i]++; }
        else held[i] = 0;
    }
    if (startExits && (cur & START_BUTTON)) {
        if (!startHeld) { startHeldAt = millis(); startHeld = true; }
        if (uint32_t(millis() - startHeldAt) >= 3000) rpgame_exitToMenu();
    } else startHeld = false;
}

void rpgame_exitToMenu() {
#ifdef CHSIM
    fprintf(stderr, "chsim: START held 3 s: exit to the menu\n");
    exit(0);
#else
    watchdog_hw->scratch[0] = 0;
    audio::setOn(false);
    gfx_wait();
    watchdog_reboot(0, 0, 0);
    for (;;) { }
#endif
}

bool RPGame::repeat(uint8_t b, uint8_t delay, uint8_t rate) const {
    for (uint8_t i = 0; i < 8; i++) {
        if (!(b & (1u << i))) continue;
        uint16_t h = held[i];
        if (h == 1) return true;
        if (h > delay && ((h - delay) % (rate ? rate : 1)) == 0) return true;
    }
    return false;
}
