/*
 * rpgame.h - plumbing shared by the browser, the GIF player and the text
 *            viewer: pin names, SPI arbitration, button polling, and the one
 *            scratch arena the three of them take turns owning.
 */
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <RPGameSD.h>
#include <RPGfx.h>

/* --------------------------------------------------------------------- */
/* Wiring                                                                 */
/* --------------------------------------------------------------------- */
// RPGamePins.h is included by RPGfx.h.

/* --------------------------------------------------------------------- */
/* Palette (RPGfx draws in 4 bpp, so colours are indices 0..15)           */
/* --------------------------------------------------------------------- */
enum : uint8_t {
    C_BG = 0, C_TEXT, C_DIM, C_SEL_BG, C_SEL_TEXT,
    C_HDR_BG, C_HDR_TEXT, C_DIR, C_ERR, C_BAR
};
extern const uint16_t PALETTE[16];

/* Screen furniture, shared so the viewer matches the browser. */
#define HDR_H        11
#define LIST_TOP     13
#define ROW_H        11
#define VISIBLE       9
#define STATUS_Y    115

/* --------------------------------------------------------------------- */
/* Shared SPI arbitration                                                 */
/* --------------------------------------------------------------------- */
/* RPGameSD waits for LCD DMA, selects 8-bit SD format and its own clock,
 * then restores the full LCD configuration. sdBegin adds an explicit
 * wait for the browser's direct streaming calls. */
void spiClaimForLcd(void);
void sdBegin(void);
void sdEnd(void);

/* --------------------------------------------------------------------- */
/* Buttons - active low, internal pull-up, edge + auto-repeat             */
/* --------------------------------------------------------------------- */
#define DEBOUNCE_MS    8
#define REPEAT_DELAY 350
#define REPEAT_RATE   90

struct Btn {
    uint8_t  pin;
    bool     down;
    uint32_t nextRepeat;
    uint32_t settleAt;
};

extern Btn bUp, bDown, bLeft, bRight, bA, bB, bSelect, bStart;

/* True on the press edge, and again on each auto-repeat if `repeat` is set. */
bool btnPressed(Btn &b, bool repeat);

/* Raw state of the whole pad, ignoring all edge/repeat bookkeeping. Used by
 * the player to arm and then trip its "any key cancels". */
bool btnAnyDown(void);

/* Forget any edge state, so releasing a button after a mode change cannot be
 * read as a fresh press by the mode that comes next. */
void btnResetAll(void);

/* --------------------------------------------------------------------- */
/* The scratch arena                                                      */
/* --------------------------------------------------------------------- */
/*
 * Three tenants, never live at the same time:
 *
 *   browser     Entry entries[MAX_ENTRIES]      2560 bytes
 *   GIF player  LZW suffix table + I/O buffer   4224 bytes
 *   text viewer its 256-byte sliding window      256 bytes
 *
 * A union of structs would be tidier, but the player's arms are plain byte
 * arrays carved at fixed offsets and the browser's is an array of structs, so
 * a byte arena with named accessors says what is actually going on. Whoever
 * takes over calls its own reset first; nothing survives a mode change.
 *
 * Sizing note: statics have to fit below the linker's fixed 2 KB stack
 * reservation at 0x20004800, which leaves 18416 bytes for .data + .bss - not
 * the 20480 arduino-cli reports against. This arena is the biggest single
 * thing the sketch owns, so it is what that limit is really spent on.
 *
 * And .bss must stop short of that line, not merely reach it: the gap between
 * _end and the stack is the heap, and the SD library mallocs 40 bytes per
 * open File. scanDir() holds two at once - the directory, plus the child from
 * openNextFile() - so peak live heap is about 96 bytes once nano-malloc's
 * chunk headers are counted. Growing this arena eats that gap, and exhausting
 * it does not look like a memory error: SD.open() simply starts returning
 * files that will not open.
 */
#define SCRATCH_BYTES 4224
extern uint8_t scratch[SCRATCH_BYTES] __attribute__((aligned(4)));

/* --------------------------------------------------------------------- */
/* Small shared helpers                                                   */
/* --------------------------------------------------------------------- */
/* Decimal, without dragging in printf. */
void fmtU32(uint32_t v, char *out);

/* ASCII-only, deliberately: strcasecmp/toupper pull in newlib's locale
 * tables, and __global_locale is 364 bytes of SRAM this part cannot spare. */
bool extEquals(const char *name, const char *ext);

/* Draw a centred one-line message over a cleared screen and flush it. */
void splashMessage(const char *msg, uint8_t colour);
