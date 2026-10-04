// Buttons and frame pacing: the Arduboy-flavoured front of the RPGame board.
//
// Started from the helper shared by CHSpriteView/CHMultiSprite/CHStlView and
// reworked for the casino games, which all used this copy:
//   * button masks are parenthesised, so ~UP_BUTTON and A|B behave;
//   * every query reads the state captured by pollButtons(), so a frame sees
//     one consistent snapshot and injected input (debug protocol, simulator)
//     behaves exactly like a real press;
//   * nextFrame() uses a microsecond accumulator, so 60 fps is 60.0, not the
//     62.5 that 1000/60 = 16 ms gave;
//   * a lockstep mode lets the debug protocol step the game frame by frame;
//   * auto-repeat for held buttons (menus, bet adjust);
//   * holding START for 3 s leaves for the SD game menu (startExits).
#pragma once
#include <stdint.h>
#include <RPGfx.h>

#define A_BUTTON      (1u << 0)
#define B_BUTTON      (1u << 1)
#define UP_BUTTON     (1u << 2)
#define DOWN_BUTTON   (1u << 3)
#define LEFT_BUTTON   (1u << 4)
#define RIGHT_BUTTON  (1u << 5)
#define START_BUTTON  (1u << 6)
#define SELECT_BUTTON (1u << 7)

// Physical button mask, pressed = 1. Implemented per platform (device: GPIO
// registers; simulator: the scripted input).
uint8_t rpgame_readButtons();

// Reset to the resident SD menu; a standalone sketch restarts itself.
[[noreturn]] void rpgame_exitToMenu();

class RPGame : public RPGfx {
public:
    void boot();
    void setFrameRate(uint8_t fps);
    bool nextFrame();

    void pollButtons();
    uint8_t buttons() const              { return cur; }
    bool pressed(uint8_t b) const        { return (cur & b) == b; }
    bool anyPressed(uint8_t b) const     { return (cur & b) != 0; }
    bool justPressed(uint8_t b) const    { return (cur & ~prev & b) != 0; }
    bool justReleased(uint8_t b) const   { return (prev & ~cur & b) != 0; }
    uint8_t justPressedMask() const      { return (uint8_t)(cur & ~prev); }
    // Press edge, then auto-repeat while held (delay/rate in frames).
    bool repeat(uint8_t b, uint8_t delay = 18, uint8_t rate = 5) const;
    void clearButtonState()              { prev = cur; }

    bool everyXFrames(uint16_t n) const  { return n && (frameCount % n) == 0; }

    [[noreturn]] void exitToMenu()       { rpgame_exitToMenu(); }

    uint32_t frameCount = 0;
    uint8_t  injected = 0;          // ORed into the physical buttons
    bool     startExits = true;     // START held 3 s calls exitToMenu(); a game that
                                    // needs a long START hold clears it in setup()
#ifdef CHSIM
    int32_t  lockstep = 0;          // the simulator starts paused, driven by N
#else
    int32_t  lockstep = -1;         // <0 free-running, else frames still allowed
#endif

    // Compatibility with RPGame 0.2's helper and graphics inheritance.
    uint8_t buttonsState() { return buttons(); }
    bool notPressed(uint8_t b) const { return !(cur & b); }
    uint16_t getFrameCount() const { return uint16_t(frameCount); }
    uint16_t getFrameCount(uint16_t n) const { return n ? frameCount % n : 0; }
    bool getFrameCountHalf(uint8_t n) const { return n && getFrameCount(n) > n / 2; }
    bool isFrameCount(uint16_t n) const { return everyXFrames(n); }
    bool isFrameCount(uint16_t n, uint16_t v) const { return n && frameCount % n == v; }
    void resetFrameCount() { frameCount = 0; }
    void drawHorizontalDottedLine(uint8_t x1, uint8_t x2, uint8_t y, uint8_t color) {
        for (int x = x1; x <= x2; x += 2) drawPixel(x, y, color);
    }
    void drawVerticalDottedLine(uint8_t y1, uint8_t y2, uint8_t x, uint8_t color) {
        for (int y = y1; y <= y2; y += 2) drawPixel(x, y, color);
    }
private:
    uint32_t startHeldAt = 0;
    bool startHeld = false;
    uint8_t  cur = 0, prev = 0;
    uint16_t held[8] = {};          // frames each button has been held
    uint32_t period = 16667, next = 0;
};

extern RPGame rpgame;              // the one instance: rpgame.boot(), rpgame.pressed() ...
