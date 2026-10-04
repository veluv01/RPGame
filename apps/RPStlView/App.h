// What the parts of CHStlView share: the screens, the palette's layout and
// the hand-offs between the file browser and the viewer.
#pragma once
#include <RPGame.h>
#include "config.h"
#include "StlRender.h"
#include "Agent.h"
#include "Fx.h"
#include "Sounds.h"

// The palette. 0-7 are the secret agent chrome (Agent.h's roles, in the
// house slots the Sizzle banners use), in teals taken from the wires; 8-15
// are the wire ramp from far (dim) to near (bright), in index order, which
// StlRender's "nearest wins" max-blend relies on. The glass, the wires and
// so the whole screen are CHStlView's original blue-teal.
static_assert(STL_SHADE0 == 8 && STL_SHADES == 8, "the wire ramp is slots 8-15");
enum : uint8_t {
    WIRE_CHIP = STL_SHADE0 + 3,         // a model's tag in the list: the wires' own blue
    WIRE_TEXT = STL_SHADE0 + 6,
    AXIS_X = agent::ALERT, AXIS_Y = agent::LIVE, AXIS_Z = agent::PALE,
};
extern const uint16_t PALETTE[16];      // RGB444, for pal::init()

enum class Scr : uint8_t { Intro, List, View, Msg };
extern Scr screen;

// The file list and the viewer's draft points are never needed at the same
// time, and RAM is the scarcest thing on this chip, so they share. Leaving
// the viewer reads the folder again.
static const uint8_t MAX_ENTRIES = 40;
static const uint16_t DRAFT_POINTS = 256;  // 768 bytes
struct Entry {
    uint32_t cluster, size;
    char name[13];                      // 8.3 with its dot, and a NUL
    bool isDir;
};
union Arena {
    Entry entries[MAX_ENTRIES];
    int8_t points[DRAFT_POINTS][3];
};
extern Arena arena;

namespace browser {
void begin();                   // the intro, then the card
void frame();                   // Intro, List and Msg screens: one frame
void back();                    // from the viewer: the folder again, the highlight where it was
// A full-screen refusal ("NOT A BINARY STL"), shown until a button.
void message(const char *title, const char *l1, const char *l2);
}

namespace viewer {
// Scans the file (blocking, with a progress screen) and enters the viewer;
// false (and a message up) if the file will not do.
bool open(const char *name, uint32_t cluster, uint32_t size);
void frame();
char *status(char *p);          // the H debug command's line
}

// A cube, 7 x 9 at (x, y): the status bar's icon, as the card is the
// reader's.
void cubeIcon(int x, int y, uint8_t edge, uint8_t face);

// Formatting the apps share with the HUD: 12800 -> "12800", 225154 -> "225K".
char *putCount(char *p, uint32_t v);
